"""Offline proof of the stock sun direction edit (editzone "sun": {"direction"}, roadmap 11.6).

editzone with {"sun": {"direction": [pitch, yaw]}} on a stock zone turns the sun primary light and bakes the GfxWorld's
compressed sun shadow again (the converter's baker, the world surfaces of the zone and the static models' geometry from
ACTS). Checked here, on the zone the converter wrote, read back by ACTS:
  1. the ComWorld sun light has the new direction; every other light and every other field of the sun is unchanged;
  2. the GfxWorld's compressed sun shadow has the same direction in its parameters, a valid forest (every tile walks
     its quadtree with every word used once) and differs from the stock one; nothing else in the GfxWorld changed;
  3. the new shadow is self-consistent: receivers (points on world surfaces that face the new sun, and points 40 and
     64 units above the ground, in the play space) that a world triangle hides from the new sun (ray cast) are
     shadowed, and a roof point / an open point are shadowed / lit;
  4. re-baking with the stock direction reproduces the stock shadow closely: the share of receivers both call lit or
     shadowed is printed (the stock tiles are read with their own depth ranges and type-2 inherited depths);
  5. the entity string differs only in the worldspawn sundirection, the vision only in fogSunDir (turned by the same
     pitch and yaw), the other raw files not at all, and ACTS tests the zone like the stock zone.

Usage: py -3 -I tools/stock_sun_direction_test.py <work folder> [--zone mp_shipment] [--exe <zonetool.exe>]
       [--direction <pitch> <yaw>]
Nothing is written outside the work folder. The game folder is only read. Needs numpy."""
import glob, json, math, os, re, shutil, struct, sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import stock_edit_test as t

NEW_DIRECTION = (-45.0, 120.0)
TILE = 512
BUDGET = 96 * 1024 * 1024


def toward_sun(pitch, yaw):
    p, y = math.radians(pitch), math.radians(yaw)
    v = np.array([math.cos(p) * math.cos(y), math.cos(p) * math.sin(y), -math.sin(p)])
    return (v / np.linalg.norm(v)).astype(np.float32).astype(np.float64)


def angles(v):
    v = np.asarray(v, dtype=np.float64)
    v = v / np.linalg.norm(v)
    return -math.degrees(math.asin(max(-1.0, min(1.0, v[2])))), math.degrees(math.atan2(v[1], v[0]))


def vec(node):
    if isinstance(node, dict):
        node = node.get('v') or node.get('values')
    return [float(x) for x in node]


def strip(node, drop=()):
    """An export node without addresses (they differ from run to run) and without the named keys."""
    if isinstance(node, dict):
        return {k: strip(v, drop) for k, v in node.items() if k != 'address' and k not in drop}
    if isinstance(node, list):
        return [strip(v, drop) for v in node]
    return node


class Forest:
    """Replay's compressed sun shadow (GfxWorldDraw.compressedSunShadow). Forest: 32-byte header (size, resolution,
    1/resolution, size, 0x00030009, width | height << 16, origin, inverse span) and a word per cell (0: no caster,
    high bit clear: one depth, high bit set: offset of a 512x512 tile). Tile: 80-byte header (size, 512, 1/512, size,
    0x00020000, 0, the tile's own origin and inverse span) and four 12-byte roots (node word, inherited depth, depth)
    for the four 256x256 quadrants; a node word is child offset << 8 | four 2-bit child types: 0 none (the inherited
    depth), 1 node, 2 node with its own inherited depth (8 bytes: node word, depth), 3 one depth for the child.
    A receiver p is lit where (sun.p + origin) * inverse span >= the stored depth, or the stored depth is 0."""

    def __init__(self, data, params):
        self.data = data
        (self.size, self.resolution, _, self.size2, self.magic, wh, self.origin,
         self.inverse_span) = struct.unpack_from('<IIfIIIff', data, 0)
        self.width, self.height = wh & 0xFFFF, wh >> 16
        self.cells = struct.unpack_from('<%dI' % (self.width * self.height), data, 32)
        self.sun = np.array(vec(params['sunDirection']), dtype=np.float64)
        self.sample = params['sampleSize']
        self.start_x = params['centerX'] - self.resolution * self.sample * 0.5
        self.start_y = -params['centerY'] - self.resolution * self.sample * 0.5
        s = self.sun.astype(np.float32)
        right = np.cross(np.array([1, 0, 0] if abs(s[2]) > 0.9 else [0, 0, 1], dtype=np.float32), s)
        right = right / np.sqrt(np.dot(right, right))
        self.right = right.astype(np.float64)
        self.up = np.cross(s, right).astype(np.float64)
        self.cache = {}

    def walk_ok(self, base):
        """Every word of the tile at `base` is used exactly once by its quadtree."""
        size = struct.unpack_from('<I', self.data, base)[0]
        if size < 80 or size % 4 or base + size > len(self.data) or struct.unpack_from('<IfII', self.data, base + 4) != (
                TILE, 1.0 / TILE, size, 0x00020000):
            return False
        used = set()
        walk = [(32 + 12 * r, 256) for r in range(4)]
        while walk:
            off, s = walk.pop()
            packed = struct.unpack_from('<I', self.data, base + off)[0]
            child = off + (packed >> 8)
            for i in range(4):
                kind = (packed >> (2 * i)) & 3
                if not kind:
                    continue
                width = 8 if kind == 2 else 4
                if child + width > size or (kind in (1, 2) and s < 4) or child in used:
                    return False
                used.update(range(child, child + width, 4))
                if kind in (1, 2):
                    walk.append((child, s // 2))
                child += width
        return used == set(range(80, size, 4))

    def valid(self):
        if self.magic != 0x00030009 or self.size != len(self.data) or self.size2 != self.size:
            return 'header'
        if self.resolution < TILE or self.resolution & (self.resolution - 1) or max(self.width, self.height) * TILE > self.resolution:
            return 'resolution'
        for cell in self.cells:
            if cell & 0x80000000 and not self.walk_ok(cell & 0x7FFFFFFF):
                return 'tile'
        return None

    def tile(self, cell):
        if cell in self.cache:
            return self.cache[cell]
        word = self.cells[cell]
        out = np.zeros((TILE, TILE), dtype=np.float32)
        origin, inverse = self.origin, self.inverse_span
        if word & 0x80000000:
            base = word & 0x7FFFFFFF
            origin, inverse = struct.unpack_from('<ff', self.data, base + 24)
            walk = []
            for r in range(4):
                inherited = struct.unpack_from('<f', self.data, base + 32 + 12 * r + 4)[0]
                walk.append((base + 32 + 12 * r, (r & 1) * 256, (r >> 1) * 256, 256, inherited))
            while walk:
                off, x, y, size, inherited = walk.pop()
                packed = struct.unpack_from('<I', self.data, off)[0]
                child = off + (packed >> 8)
                cs = size // 2
                for i in range(4):
                    kind = (packed >> (2 * i)) & 3
                    cx, cy = x + (i & 1) * cs, y + (i >> 1) * cs
                    if kind == 0:
                        if inherited:
                            out[cy:cy + cs, cx:cx + cs] = inherited
                    elif kind == 3:
                        out[cy:cy + cs, cx:cx + cs] = struct.unpack_from('<f', self.data, child)[0]
                        child += 4
                    elif kind == 1:
                        walk.append((child, cx, cy, cs, inherited))
                        child += 4
                    else:
                        walk.append((child, cx, cy, cs, struct.unpack_from('<f', self.data, child + 4)[0]))
                        child += 8
        elif word:
            out[:, :] = struct.unpack('<f', struct.pack('<I', word))[0]
        if len(self.cache) > 96:
            self.cache.clear()
        self.cache[cell] = (out, origin, inverse)
        return self.cache[cell]

    def lit(self, points):
        x = (points @ self.right - self.start_x) / self.sample
        y = (-(points @ self.up) - self.start_y) / self.sample
        depth = points @ self.sun
        tx, ty = np.floor(x).astype(np.int64), np.floor(y).astype(np.int64)
        cx, cy = tx // TILE, ty // TILE
        inside = (tx >= 0) & (ty >= 0) & (cx < self.width) & (cy < self.height)
        out = np.ones(len(points), dtype=bool)
        cells = np.where(inside, cy * self.width + cx, -1)
        for cell in np.unique(cells[inside]):
            sel = np.nonzero(cells == cell)[0]
            stored, origin, inverse = self.tile(int(cell))
            value = stored[ty[sel] % TILE, tx[sel] % TILE]
            out[sel] = (value == 0) | ((depth[sel] + origin) * inverse >= value)
        return out


def gfx_shadow(export_file):
    f = t.fields(export_file)
    draw = f['draw']
    data = bytes.fromhex(draw['compressedSunShadow']['bytes'])
    return f, draw['compressedSunShadowSize'], draw['compressedSunShadowParams'], data


class World:
    """The stock world triangles (lit-opaque surfaces, not the sky) and receivers in the play space."""

    def __init__(self, gfx, trzone_file):
        tz = t.fields(trzone_file)
        pos = bytes.fromhex(tz['drawVerts']['posData']['bytes'])
        idx = np.frombuffer(bytes.fromhex(tz['drawVerts']['indices']['bytes']), dtype=np.uint16)
        tris = []
        for s in gfx['surfaces']['surfaces']['values'][:gfx['surfaces']['litOpaqueSurfsEnd']]:
            if s['transientZone'] != 0 or '/sky_' in s['material']['name']:
                continue
            tr = s['tris']
            verts = np.frombuffer(pos, dtype=np.float32, count=3 * tr['vertexCount'], offset=tr['posOffset']).reshape(-1, 3)
            tris.append(verts[idx[tr['baseIndex']:tr['baseIndex'] + 3 * tr['triCount']].reshape(-1, 3)])
        self.tris = np.concatenate(tris).astype(np.float64)
        a, b, c = self.tris[:, 0], self.tris[:, 1], self.tris[:, 2]
        n = np.cross(c - a, b - a)  # clockwise seen from outside
        self.area = np.linalg.norm(n, axis=1) * 0.5
        self.normal = n / np.maximum(2 * self.area, 1e-12)[:, None]
        hf = gfx['heightfields']['values'][0]['bounds']
        mid, half = np.array(vec(hf['midPoint'])), np.array(vec(hf['halfSize']))
        cent = (a + b + c) / 3
        # The play space (the heightfield box), not the hidden plane below the ground.
        self.box = np.all(np.abs(cent[:, :2] - mid[:2]) <= half[:2], axis=1) & (cent[:, 2] > mid[2] - half[2] + 5)
        self.rng = np.random.default_rng(1)

    def sample(self, mask, count):
        w = self.area * mask
        pick = self.rng.choice(len(self.tris), size=count, p=w / w.sum())
        r1, r2 = self.rng.random(count), self.rng.random(count)
        s = np.sqrt(r1)
        a, b, c = self.tris[pick, 0], self.tris[pick, 1], self.tris[pick, 2]
        return a * (1 - s)[:, None] + b * (s * (1 - r2))[:, None] + c * (s * r2)[:, None]

    def receivers(self, sun, count):
        surface = self.sample(self.box & (self.normal @ sun > 0.2) & (self.area > 1e-3), count) + 6.0 * sun
        ground = self.sample(self.box & (self.normal[:, 2] > 0.7) & (self.area > 1e-3), count // 2)
        return surface, np.concatenate([ground + [0, 0, 40.0], ground + [0, 0, 64.0]])

    def occluded(self, points, direction):
        """Whether a world triangle lies between each point and the sun (ray cast toward `direction`)."""
        d = np.asarray(direction, dtype=np.float64)
        right = np.cross([1.0, 0, 0] if abs(d[2]) > 0.9 else [0, 0, 1.0], d)
        right /= np.linalg.norm(right)
        up = np.cross(d, right)
        sx, sy, sd = self.tris @ right, self.tris @ up, self.tris @ d
        lo_x, hi_x, lo_y, hi_y, hi_d = sx.min(1), sx.max(1), sy.min(1), sy.max(1), sd.max(1)
        v0, e1, e2 = self.tris[:, 0], self.tris[:, 1] - self.tris[:, 0], self.tris[:, 2] - self.tris[:, 0]
        out = np.zeros(len(points), dtype=bool)
        for k, p in enumerate(points):
            px, py, pd = p @ right, p @ up, p @ d
            cand = np.nonzero((lo_x <= px) & (hi_x >= px) & (lo_y <= py) & (hi_y >= py) & (hi_d > pd + 0.5))[0]
            if not len(cand):
                continue
            h = np.cross(d, e2[cand])
            a = np.einsum('ij,ij->i', e1[cand], h)
            ok = np.abs(a) > 1e-9
            f = np.where(ok, 1.0 / np.where(ok, a, 1.0), 0.0)
            s = p - v0[cand]
            u = f * np.einsum('ij,ij->i', s, h)
            q = np.cross(s, e1[cand])
            v = f * (q @ d)
            tt = f * np.einsum('ij,ij->i', e2[cand], q)
            out[k] = bool(np.any(ok & (u >= 0) & (v >= 0) & (u + v <= 1) & (tt > 0.5)))
        return out

    def roofed(self, points, height=400.0):
        """Whether a world triangle lies straight above each point within `height` units."""
        out = np.zeros(len(points), dtype=bool)
        xs, ys = self.tris[:, :, 0], self.tris[:, :, 1]
        lo_x, hi_x, lo_y, hi_y = xs.min(1), xs.max(1), ys.min(1), ys.max(1)
        for k, p in enumerate(points):
            cand = np.nonzero((lo_x <= p[0]) & (hi_x >= p[0]) & (lo_y <= p[1]) & (hi_y >= p[1]))[0]
            for i in cand:
                a, b, c = self.tris[i]
                det = (b[1] - c[1]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[1] - c[1])
                if abs(det) < 1e-9:
                    continue
                l1 = ((b[1] - c[1]) * (p[0] - c[0]) + (c[0] - b[0]) * (p[1] - c[1])) / det
                l2 = ((c[1] - a[1]) * (p[0] - c[0]) + (a[0] - c[0]) * (p[1] - c[1])) / det
                l3 = 1 - l1 - l2
                if min(l1, l2, l3) < 0:
                    continue
                z = l1 * a[2] + l2 * b[2] + l3 * c[2]
                if p[2] + 1 < z < p[2] + height:
                    out[k] = True
                    break
        return out


def raw_files(zone_file, out):
    shutil.rmtree(out, ignore_errors=True)
    t.acts(['-a', 'rawfile', '-o', out, zone_file])
    files = {}
    for f in glob.glob(os.path.join(out, '**', 'rawfile', '**', '*'), recursive=True):
        if os.path.isfile(f):
            rel = os.path.relpath(f, glob.glob(os.path.join(out, '**', 'rawfile'), recursive=True)[0]).replace('\\', '/')
            files[rel.rsplit('.', 1)[0]] = open(f, 'rb').read()
    return files


def main():
    args = sys.argv[1:]
    zone, exe, direction = 'mp_shipment', t.EXE, NEW_DIRECTION
    while '--zone' in args:
        i = args.index('--zone'); zone = args[i + 1]; del args[i:i + 2]
    while '--exe' in args:
        i = args.index('--exe'); exe = args[i + 1]; del args[i:i + 2]
    while '--direction' in args:
        i = args.index('--direction'); direction = (float(args[i + 1]), float(args[i + 2])); del args[i:i + 3]
    work = os.path.abspath(args[0])
    os.makedirs(work, exist_ok=True)
    failures = []

    def check(ok, text):
        print(('  PASS ' if ok else '  FAIL ') + text)
        if not ok:
            failures.append(text)

    stock_file = os.path.join(t.GAME, 'zone', zone + '.ff')
    print('stock %s' % zone)
    stock_com = t.export(stock_file, 'com_map', os.path.join(work, 'stock_com'))
    stock_gfx_files = t.export(stock_file, 'gfx_map', os.path.join(work, 'stock_gfx'))
    stock_tz = t.export(stock_file, 'gfx_map_trzone', os.path.join(work, 'stock_tz'))
    stock_me = t.export(stock_file, 'map_ents', os.path.join(work, 'stock_me'))
    if not (len(stock_com) == len(stock_gfx_files) == len(stock_me) == 1 and stock_tz):
        check(False, 'ACTS exports the stock ComWorld, GfxWorld, transient zone and map entities')
        return 1
    stock_lights = strip(t.fields(stock_com[0])['primaryLights'])
    stock_lights = stock_lights.get('values', stock_lights) if isinstance(stock_lights, dict) else stock_lights
    gfx, stock_size, stock_params, stock_data = gfx_shadow(stock_gfx_files[0])
    stock = Forest(stock_data, stock_params)
    old_pitch, old_yaw = angles(vec(stock_params['sunDirection']))
    print('  stock sun direction %.3f %.3f, sun shadow %d bytes, %dx%d cells at %g unit(s) per texel' % (
        old_pitch, old_yaw, stock_size, stock.width, stock.height, stock.sample))
    stock_rest = json.dumps(strip(gfx, ('compressedSunShadow', 'compressedSunShadowSize', 'compressedSunShadowParams')), sort_keys=True)
    world = World(gfx, stock_tz[0])
    del gfx
    stock_text = t.entity_text(t.fields(stock_me[0]))
    stock_raw = raw_files(stock_file, os.path.join(work, 'stock_raw'))
    stock_test = [l for l in t.acts(['--test', '-o', os.path.join(work, 'stock_test'), stock_file]) if l.startswith('Replay:')]

    # --- 1. the new direction -------------------------------------------------------------------------------------
    pitch, yaw = direction
    sun = toward_sun(pitch, yaw)
    print('1. editzone with sun direction %g %g (toward the sun %s)' % (pitch, yaw, sun.round(4)))
    code, log, rep = t.editzone(exe, zone, {'sun': {'direction': [pitch, yaw], 'acts': t.ACTS}, 'patch': False},
                                os.path.join(work, 'new'))
    shadow_rep = rep.get('sun_shadow', {})
    check(code == 0 and rep.get('sun_direction') == [pitch, yaw], 'editzone finishes: %s' % {
        k: shadow_rep.get(k) for k in ('bytes', 'cells', 'resolution', 'tolerance', 'coverage', 'model_instances', 'lower_lod_instances',
                                      'missing_instances')})
    if code != 0:
        print(log[-3000:])
        return 1
    new_file = os.path.join(work, 'new', zone + '.ff')
    new_com = t.export(new_file, 'com_map', os.path.join(work, 'new_com'))
    lights = strip(t.fields(new_com[0])['primaryLights']) if len(new_com) == 1 else []
    lights = lights.get('values', lights) if isinstance(lights, dict) else lights
    index = rep.get('sun_primary_light', -1)
    check(len(lights) == len(stock_lights) and 0 <= index < len(lights), '%d primary lights, the sun is light %d' % (len(lights), index))
    if len(lights) != len(stock_lights) or not 0 <= index < len(lights):
        return 1
    light_dir = np.array(vec(lights[index]['dir']))
    check(np.abs(light_dir - sun).max() < 1e-6, 'the ComWorld sun has the new direction %s' % light_dir.round(6))
    check(abs(angles(light_dir)[0] - pitch) < 1e-3 and abs(angles(light_dir)[1] - yaw) < 1e-3,
          'it reads back as pitch %.4f yaw %.4f (toward the sun = (cos p cos y, cos p sin y, -sin p), as the stock '
          'sundirection -20.261 -15.4 is the stock light %s)' % (angles(light_dir) + (np.array(vec(stock_params['sunDirection'])).round(3),)))
    others = [i for i in range(len(lights)) if i != index and lights[i] != stock_lights[i]]
    check(not others, 'every other primary light is unchanged %s' % (others[:5] or ''))
    check({k: v for k, v in lights[index].items() if k != 'dir'} == {k: v for k, v in stock_lights[index].items() if k != 'dir'},
          'the sun keeps its colour, intensity and every other field')
    shutil.rmtree(os.path.join(work, 'new_com'), ignore_errors=True)

    # --- 2. the baked sun shadow ----------------------------------------------------------------------------------
    print('2. the GfxWorld sun shadow')
    new_gfx_files = t.export(new_file, 'gfx_map', os.path.join(work, 'new_gfx'))
    check(len(new_gfx_files) == 1, 'ACTS exports the edited GfxWorld')
    if len(new_gfx_files) != 1:
        return 1
    new_gfx, size, params, data = gfx_shadow(new_gfx_files[0])
    new = Forest(data, params)
    check(np.abs(np.array(vec(params['sunDirection'])) - light_dir).max() < 1e-7,
          'the sun shadow parameters hold the light\'s direction (Replay drops a sun shadow whose direction is not the sun\'s)')
    check(size == len(data) == shadow_rep.get('bytes') and size <= BUDGET,
          '%d bytes (stock %d), within the converter\'s %d-byte budget' % (size, stock_size, BUDGET))
    invalid = new.valid()
    check(invalid is None and params['forestSize'] == new.width * new.height and params['reserved'] == new.width << 16,
          'a valid forest: %dx%d cells, resolution %d, %g unit(s) per texel, every tile\'s quadtree uses each of its words once%s' % (
              new.width, new.height, new.resolution, new.sample, '' if invalid is None else ' (bad: %s)' % invalid))
    check(data != stock_data and vec(params['sunDirection']) != vec(stock_params['sunDirection']), 'it differs from the stock sun shadow')
    rest = json.dumps(strip(new_gfx, ('compressedSunShadow', 'compressedSunShadowSize', 'compressedSunShadowParams')), sort_keys=True)
    check(rest == stock_rest, 'every other GfxWorld field is the stock one')
    del new_gfx, rest
    shutil.rmtree(os.path.join(work, 'new_gfx'), ignore_errors=True)

    # --- 3. self-consistency for the new sun --------------------------------------------------------------------
    print('3. the new shadow against the world geometry (ray cast toward the new sun)')
    surface, standing = world.receivers(sun, 6000)
    lit_surface, lit_standing = new.lit(surface), new.lit(standing)
    probe = np.concatenate([surface[:1500], standing[:1500]])
    probe_lit = np.concatenate([lit_surface[:1500], lit_standing[:1500]])
    hidden = world.occluded(probe, sun)
    shadowed = (~probe_lit[hidden]).mean() if hidden.any() else 0.0
    check(hidden.sum() > 100 and shadowed >= 0.99,
          '%d of %d receivers are hidden from the new sun by world geometry; %.2f%% of them are shadowed' % (
              hidden.sum(), len(probe), 100 * shadowed))
    open_points = ~hidden
    print('       %.1f%% of the receivers the world does not hide are lit (static models shadow the rest)' % (100 * probe_lit[open_points].mean()))
    # Roof points: standing points with a world triangle straight above them that also hides them from the new sun.
    roof = np.nonzero(world.roofed(standing))[0]
    roof_hidden = roof[world.occluded(standing[roof], sun)] if len(roof) else roof
    if len(roof_hidden):
        p = standing[roof_hidden[0]]
        under = (~lit_standing[roof_hidden]).mean()
        check(not lit_standing[roof_hidden[0]] and under >= 0.95,
              'roof points: %s under a roof is shadowed; %d of %d standing points under a roof and hidden from the sun are '
              'shadowed' % (p.round(1), int((~lit_standing[roof_hidden]).sum()), len(roof_hidden)))
    else:
        check(False, 'no point under a roof was found')
    open_lit = np.nonzero(~world.roofed(standing[:1500]) & ~hidden[1500:] & lit_standing[:1500])[0]
    if len(open_lit):
        p = standing[open_lit[0]]
        print('       an open point: %s (no world triangle above it or toward the sun) is lit' % p.round(1))
    check(len(open_lit) > 0, 'open points are lit (%d)' % len(open_lit))
    old_on_new = stock.lit(surface)
    print('       the stock (old sun) shadow agrees with the new one on %.1f%% of these surface receivers' % (100 * (old_on_new == lit_surface).mean()))

    # --- 4. entities, vision, raw files, ACTS ------------------------------------------------------------------
    print('4. the rest of the zone')
    new_me = t.export(new_file, 'map_ents', os.path.join(work, 'new_me'))
    text = t.entity_text(t.fields(new_me[0])) if len(new_me) == 1 else ''
    first = stock_text.find('}')
    expected = stock_text
    m = re.search(r'(^|\n)968 "([^"]*)"', stock_text[:first])
    if m:
        expected = stock_text[:m.start(2)] + ('%.6g %.6g 0' % (pitch, yaw)) + stock_text[m.end(2):]
    check(m is not None and text == expected, 'the entity string differs only in the worldspawn sundirection (968 "%s" -> "%.6g %.6g 0")' % (
        m.group(2) if m else '?', pitch, yaw))
    new_raw = raw_files(new_file, os.path.join(work, 'new_raw'))
    visions = [k for k in stock_raw if k.startswith('vision/')]
    same = [k for k in stock_raw if not k.startswith('vision/') and new_raw.get(k) == stock_raw[k]]
    check(set(new_raw) == set(stock_raw) and len(same) == len(stock_raw) - len(visions),
          '%d raw files, all but the vision unchanged' % len(new_raw))
    for k in visions:
        a, b = stock_raw[k].decode('latin-1').splitlines(), new_raw.get(k, b'').decode('latin-1').splitlines()
        diff = [(x, y) for x, y in zip(a, b) if x != y]
        ok = len(a) == len(b) and len(diff) == 1 and diff[0][0].startswith('RLNQONPKS ')
        if ok:
            fog_old = [float(x) for x in diff[0][0].split('"')[1].split()]
            fog_new = [float(x) for x in diff[0][1].split('"')[1].split()]
            (fp, fy), (gp, gy) = angles(fog_old), angles(fog_new)
            dp, dy = pitch - old_pitch, yaw - old_yaw
            ok = abs((gp - fp) - dp) < 0.01 and abs(((gy - fy - dy) + 180) % 360 - 180) < 0.01
        check(ok, '%s: only fogSunDir changed, turned with the sun (%s)' % (k, diff[0] if diff else 'no change'))
    test = [l for l in t.acts(['--test', '-o', os.path.join(work, 'new_test'), new_file]) if l.startswith('Replay:')]
    check(bool(test) and test == stock_test, 'ACTS tests the edited zone like the stock zone: %s (stock %s)' % (test, stock_test))
    for d in ('new_me', 'new_raw', 'new_test'):
        shutil.rmtree(os.path.join(work, d), ignore_errors=True)
    shutil.rmtree(os.path.join(work, 'new'), ignore_errors=True)

    # --- 5. the stock direction baked again -------------------------------------------------------------------
    print('5. the stock direction (%.4f %.4f) baked again against the stock bake' % (old_pitch, old_yaw))
    code, log, rep = t.editzone(exe, zone, {'sun': {'direction': [old_pitch, old_yaw], 'acts': t.ACTS}, 'patch': False},
                                os.path.join(work, 'orig'))
    check(code == 0, 'editzone finishes: %s' % {k: rep.get('sun_shadow', {}).get(k) for k in ('bytes', 'cells', 'tolerance', 'coverage')})
    if code != 0:
        print(log[-3000:])
    else:
        files = t.export(os.path.join(work, 'orig', zone + '.ff'), 'gfx_map', os.path.join(work, 'orig_gfx'))
        shutil.rmtree(os.path.join(work, 'orig'), ignore_errors=True)
        _, osize, oparams, odata = gfx_shadow(files[0])
        shutil.rmtree(os.path.join(work, 'orig_gfx'), ignore_errors=True)
        ours = Forest(odata, oparams)
        stock_sun = np.array(vec(stock_params['sunDirection']))
        surface, standing = world.receivers(stock_sun, 60000)
        for name, points in (('surface', surface), ('standing', standing)):
            a, b = stock.lit(points), ours.lit(points)
            agree = (a == b).mean()
            check(agree >= 0.93, '%s receivers: %.2f%% agree (lit: stock %.1f%%, re-bake %.1f%%; stock lit / re-bake shadowed %.2f%%, '
                  'stock shadowed / re-bake lit %.2f%%)' % (name, 100 * agree, 100 * a.mean(), 100 * b.mean(), 100 * (a & ~b).mean(),
                                                         100 * (~a & b).mean()))
        probe = np.concatenate([surface[:1500], standing[:1500]])
        hidden = world.occluded(probe, stock_sun)
        print('       receivers hidden from the stock sun by world geometry: %d; shadowed by the stock bake %.2f%%, by the '
              're-bake %.2f%%' % (hidden.sum(), 100 * (~stock.lit(probe[hidden])).mean(), 100 * (~ours.lit(probe[hidden])).mean()))
        print('       re-bake: %d bytes (stock %d), %dx%d cells' % (osize, stock_size, ours.width, ours.height))

    for d in ('stock_com', 'stock_gfx', 'stock_tz', 'stock_me', 'stock_raw', 'stock_test'):
        shutil.rmtree(os.path.join(work, d), ignore_errors=True)
    print('RESULT: %s (%d failed)' % ('PASS' if not failures else 'FAIL', len(failures)))
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
