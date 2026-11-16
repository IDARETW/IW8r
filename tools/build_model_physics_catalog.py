"""Builds data/asset_catalog/iw8_model_physics.tsv: which stock models have a physics asset (the game's static model
collision), and that physics asset's convex pieces in model space (rule: src/core/model_physics.h).

The game (Replay 1.20, StaticModels_AddClipmapCollisionShapes) gives a placed static model collision only from the
clipmap's static model collision lists, and every entry there is exactly the XModel's physicsAsset / detailCollision
pair (mp_shipment: 198 of 198). A model without a physics asset (most bushes, grass, flowers) has no collision at all.
The physics asset is a Havok 2018 tagfile (units: inches / 32, Z-up model space). Its convex shapes
(hknpConvexPolytopeShape and the cylinder variant; type ids of the stock compendium 40354c32492e6bc1) are read here:
each shape's vertex array (hkcdVertex: w = 0.5 with the vertex index in the low bits), placed by the shape instances of
a compound (hknpShapeInstance, 112 bytes: rotation columns, translation, scale, shape item), then by the body
(hknpBodyCinfo: position at +48, orientation quaternion x, y, z, w at +64). A physics asset that holds
anything else (a compressed triangle mesh, several bodies, another compendium) or whose decoded pieces leave the
model's bounds is written as `mesh`: the editor then uses the model's render mesh, as before.

For each zone of data/asset_catalog/iw8_models.tsv it exports (ACTS, read-only on the game folder) the xmodel and
physicsasset tables into out/model_physics_study/<zone> (removed after reading unless --keep; the parsed result is kept
as out/model_physics_study/<zone>.json, so a second run reuses it).

Usage: py -3 -I tools/build_model_physics_catalog.py [--zones a,b] [--keep] [--no-export]"""
import collections, glob, json, os, shutil, struct, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
STUDY = os.path.join(ROOT, 'out', 'model_physics_study')
OUT = os.path.join(ROOT, 'data', 'asset_catalog', 'iw8_model_physics.tsv')
ACTS = r'E:\mw124\atian-cod-tools\build\bin\Release\acts.exe'
GAME = r'E:\IW8\Builds\1.20-replay'
HAVOK_INCH = 32.0                     # inches per Havok unit (x64-zt replay_havok.cpp divides by 32)
COMPENDIUM = '40354c32492e6bc1'       # the stock physics assets' type compendium (TCRF)
T_VERTICES, T_INSTANCES = 90, 261     # hkcdVertex array, hknpShapeInstance array
T_BODY = 39                           # hknpBodyCinfo (one per rigid body)
T_CONVEX = {1447, 1448, 1481}         # convex polytope shapes (1481: the cylinder variant, 128 vertices)
T_NOT_SHAPE = {7, 13, 94, 116, 266, 610, 1574}  # system, body, material, compound and other non-shape objects


def zone_file(zone):
    kept = os.path.join(GAME, 'iw8r_stock', zone + '.ff')  # an installed edit keeps the stock file here
    return kept if os.path.exists(kept) else os.path.join(GAME, 'zone', zone + '.ff')


def zones_of_catalog():
    zones = set()
    with open(os.path.join(ROOT, 'data', 'asset_catalog', 'iw8_models.tsv'), encoding='utf-8') as f:
        for line in f:
            if line.startswith('#') or '\t' not in line:
                continue
            for z in line.rstrip('\n').split('\t')[1].split(','):
                if z.strip():
                    zones.add(z.strip())
    return sorted(zones)


def clean(name):
    return (name or '').lstrip(',~')


def tagfile(b):
    sec = {}

    def walk(off, end):
        while off + 8 <= end:
            raw = struct.unpack('>I', b[off:off + 4])[0]
            size = raw & 0x3FFFFFFF
            tag = b[off + 4:off + 8].decode('latin1')
            if size < 8:
                return
            if tag in ('TAG0', 'INDX'):
                walk(off + 8, off + size)
            else:
                sec.setdefault(tag, (off + 8, off + size))
            off += size
    walk(0, len(b))
    return sec


def decode_havok(b):
    """(pieces, reason): pieces = lists of (x, y, z) in inches, model space; reason explains a failure."""
    sec = tagfile(b)
    if 'DATA' not in sec or 'ITEM' not in sec or 'TCRF' not in sec:
        return None, 'not a tagfile'
    if b[sec['TCRF'][0]:sec['TCRF'][1]].hex()[:16] != COMPENDIUM:
        return None, 'other compendium'
    base = sec['DATA'][0]
    items = []
    lo, hi = sec['ITEM']
    for i in range(lo, hi - 11, 12):
        a, o, c = struct.unpack('<3I', b[i:i + 12])
        items.append((a & 0xFFFFFF, a >> 24, base + o, c))
    objects = [(k, t) for k, (t, fl, o, c) in enumerate(items) if fl == 0x10]
    unknown = sorted({t for k, t in objects if t not in T_CONVEX and t not in T_NOT_SHAPE})
    if unknown:
        return None, 'shape type %s' % ','.join(map(str, unknown))
    shapes = [k for k, t in objects if t in T_CONVEX]
    arrays = [k for k, (t, fl, o, c) in enumerate(items) if t == T_VERTICES]
    if not shapes or len(shapes) != len(arrays):
        return None, 'shapes %d, vertex arrays %d' % (len(shapes), len(arrays))
    verts_of = {}
    for shape, arr in zip(shapes, arrays):  # objects come before their arrays, both in item order
        t, fl, o, c = items[arr]
        pts = []
        for k in range(c):
            x, y, z = struct.unpack('<3f', b[o + 16 * k:o + 16 * k + 12])
            w = struct.unpack('<I', b[o + 16 * k + 12:o + 16 * k + 16])[0]
            if (w & 0xFFFF0000) != 0x3F000000:
                return None, 'vertex array layout'
            pts.append((x, y, z))
        if len(pts) < 4:
            return None, 'shape with %d vertices' % len(pts)
        verts_of[shape] = pts
    instances = [(o, c) for (t, fl, o, c) in items if t == T_INSTANCES]
    pieces = []
    if instances:
        used = set()
        for o, c in instances:
            for i in range(c):
                r = o + 112 * i
                rows = [struct.unpack('<4f', b[r + 16 * k:r + 16 * k + 16]) for k in range(5)]
                ref = struct.unpack('<I', b[r + 80:r + 84])[0]
                if ref not in verts_of:
                    return None, 'instance of a non-convex shape'
                used.add(ref)
                c0, c1, c2, t, s = rows[0][:3], rows[1][:3], rows[2][:3], rows[3][:3], rows[4][:3]
                piece = []
                for (x, y, z) in verts_of[ref]:
                    x, y, z = x * s[0], y * s[1], z * s[2]
                    piece.append(tuple((c0[a] * x + c1[a] * y + c2[a] * z + t[a]) * HAVOK_INCH for a in range(3)))
                pieces.append(piece)
        if used != set(verts_of):
            return None, 'convex shape outside the compound'
    else:
        if len(verts_of) != 1:
            return None, '%d convex shapes without a compound' % len(verts_of)
        pieces.append([tuple(v * HAVOK_INCH for v in p) for p in verts_of[shapes[0]]])
    # The body's place in the model: position and orientation of its hknpBodyCinfo.
    bodies = [(o, c) for (t, fl, o, c) in items if t == T_BODY]
    if len(bodies) != 1 or bodies[0][1] != 1:
        return None, 'body records %d' % len(bodies)
    o = bodies[0][0]
    pos = struct.unpack('<3f', b[o + 48:o + 60])
    qx, qy, qz, qw = struct.unpack('<4f', b[o + 64:o + 80])
    n = (qx * qx + qy * qy + qz * qz + qw * qw) ** 0.5
    if not 0.99 < n < 1.01:
        return None, 'body orientation'
    qx, qy, qz, qw = qx / n, qy / n, qz / n, qw / n
    rot = [[1 - 2 * (qy * qy + qz * qz), 2 * (qx * qy - qz * qw), 2 * (qx * qz + qy * qw)],
           [2 * (qx * qy + qz * qw), 1 - 2 * (qx * qx + qz * qz), 2 * (qy * qz - qx * qw)],
           [2 * (qx * qz - qy * qw), 2 * (qy * qz + qx * qw), 1 - 2 * (qx * qx + qy * qy)]]
    if any(abs(v) > 1e-6 for v in pos) or abs(qw) < 0.999999:
        pieces = [[tuple(sum(rot[a][k] * p[k] for k in range(3)) + pos[a] * HAVOK_INCH for a in range(3)) for p in piece]
                  for piece in pieces]
    return pieces, ''


def inside(pieces, mins, maxs):
    for a in range(3):
        pad = 2.0 + 0.05 * (maxs[a] - mins[a])
        for piece in pieces:
            for p in piece:
                if p[a] < mins[a] - pad or p[a] > maxs[a] + pad:
                    return False
    return True


def export(zone, out):
    os.makedirs(out, exist_ok=True)
    args = [ACTS, '--noUpdater', 'fastfile', '-r', 'mw19replay', '-g', os.path.join(GAME, 'game_dx12_ship_replay.exe'),
            '--oodle', os.path.join(GAME, 'oo2core_7_win64.dll'), '-a', 'xmodel,physicsasset', '-l', '-o', out, zone_file(zone)]
    r = subprocess.run(args, capture_output=True, text=True, errors='replace')
    tail = (r.stdout + r.stderr).strip().splitlines()[-2:]
    return any('Parsed 1' in t for t in tail), ' | '.join(tail)


def read_zone(zone, keep, no_export):
    cached = os.path.join(STUDY, zone + '.json')
    if os.path.exists(cached):
        return json.load(open(cached, encoding='utf-8'))
    if no_export:
        return None
    out = os.path.join(STUDY, zone)
    ok, tail = export(zone, out)
    print('  %s: %s' % (zone, tail))
    models, physics = {}, {}
    for path in glob.glob(os.path.join(out, '**', '*.asset.json'), recursive=True):
        norm = path.replace(os.sep, '/')
        try:
            fields = json.load(open(path, encoding='utf-8'))['asset']['fields']
        except Exception:
            continue
        if '/assets/xmodel/' in norm:
            name = clean(fields.get('name', {}).get('string'))
            bd = fields.get('bounds') or {}
            mid, half = (bd.get('midPoint') or {}).get('v'), (bd.get('halfSize') or {}).get('v')
            models[name] = {
                'physics': clean((fields.get('physicsAsset') or {}).get('name')),
                'detail': clean((fields.get('detailCollision') or {}).get('name')),
                'bounds': [[mid[a] - half[a] for a in range(3)], [mid[a] + half[a] for a in range(3)]] if mid and half else None,
            }
        elif '/assets/physicsasset/' in norm:
            name = clean(fields.get('name', {}).get('string'))
            contents = 0
            for v in ((fields.get('bodyContents') or {}).get('values') or []):
                contents |= int(v['value'] if isinstance(v, dict) else v)
            raw = bytes.fromhex(((fields.get('havokData') or {}).get('bytes')) or '')
            bodies = int(fields.get('numRigidBodies') or 0)
            pieces, reason = (None, '%d bodies' % bodies) if bodies != 1 else decode_havok(raw)
            physics[name] = {'contents': contents, 'pieces': pieces, 'reason': reason}
    result = {'ok': ok, 'models': models, 'physics': physics}
    with open(cached, 'w', encoding='utf-8') as f:
        json.dump(result, f)
    if not keep:
        shutil.rmtree(out, ignore_errors=True)
    return result


def number(v):
    s = '%.2f' % v
    s = s.rstrip('0').rstrip('.') if '.' in s else s
    return '0' if s in ('-0', '') else s


def main():
    argv = sys.argv[1:]
    keep, no_export = '--keep' in argv, '--no-export' in argv
    zones = zones_of_catalog()
    if '--zones' in argv:
        zones = argv[argv.index('--zones') + 1].split(',')
    os.makedirs(STUDY, exist_ok=True)
    models, physics, model_bounds = {}, {}, {}
    for zone in zones:
        z = read_zone(zone, keep, no_export)
        if not z:
            continue
        for name, m in z['models'].items():
            if name and name not in models:
                models[name] = (m['physics'], m['detail'])
                model_bounds[name] = m['bounds']
        for name, p in z['physics'].items():
            if name and name not in physics:
                physics[name] = p
    # Validation: a decoded shape must stay within the bounds of a model that uses it.
    users = collections.defaultdict(list)
    for name, (pa, dc) in models.items():
        if pa:
            users[pa].append(name)
    stats = collections.Counter()
    rows_p = []
    for name in sorted(physics):
        p = physics[name]
        pieces, reason = p['pieces'], p['reason']
        if pieces:
            fits = [u for u in users.get(name, []) if model_bounds.get(u) and inside(pieces, *model_bounds[u])]
            if users.get(name) and not fits:
                pieces, reason = None, 'outside the model bounds'
        shape = 'mesh'
        if pieces:
            shape = ';'.join(' '.join(','.join(number(c) for c in v) for v in piece) for piece in pieces)
            stats['convex'] += 1
        else:
            stats['mesh: ' + reason.split(' ')[0] if reason else 'mesh'] += 1
        rows_p.append('P\t%s\t%x\t%s\t%s' % (name, p['contents'], shape, reason or '-'))
    rows_m = ['M\t%s\t%s\t%s' % (name, pa or '-', dc or '-') for name, (pa, dc) in sorted(models.items())]
    with open(OUT, 'w', encoding='utf-8', newline='\n') as f:
        f.write('# Replay 1.20 stock model physics, from ACTS xmodel / physicsasset exports of %d zones '
                '(tools/build_model_physics_catalog.py; rule: src/core/model_physics.h).\n' % len(zones))
        f.write('# M <model> <physics asset or -> <detail collision or ->: the XModel\'s collision (no physics asset: the game '
                'gives the model no collision).\n')
        f.write('# P <physics asset> <body contents, hex> <shape> <why not decoded or ->: shape = convex pieces in model '
                'space (inches, Z-up; pieces split by ";", vertices by " ", x,y,z), or "mesh" (not read: use the render mesh).\n')
        for line in rows_m + rows_p:
            f.write(line + '\n')
    print('models %d (with a physics asset %d), physics assets %d: %s' %
          (len(models), sum(1 for pa, dc in models.values() if pa), len(physics), dict(stats)))
    print('wrote', OUT)


main()
