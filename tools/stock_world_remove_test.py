"""Offline proof that editzone "remove_world_surfaces" takes stock world geometry out (roadmap 11.5,
docs/research/96_stock_world_removal.md).

On mp_shipment a box around a stack of insulation rolls on a pallet frame that is baked into the world (world surfaces
tw/fabric_insulation_fiberglass_01_yellow, tw/metal_aluminum_insulation_panels_01, twc4/47n_48n, its tw/shadowcaster and
the top of its tw/dam_metal_black_dirty_01 frame; x -960..-820, y 1815..2140, z 17.5..125, the ground at z 15..17 stays
outside) is removed with editzone. Read back with ACTS from the written zone:

- render: exactly the world triangles whose three vertices lie inside the box are degenerate (i0, i0, i0); every
  other index, the vertex positions and the whole surface table (counts, offsets, materials, flags) are stock;
- collision: the world's Havok mesh (col_map havokWorldShapeData, decoded by `zonetool havokinfo --mesh`) holds no
  triangle and no live convex shape inside the box; every other triangle and convex shape is there with the same
  coordinates; the data keeps its size and only the changed primitives / tree slots differ;
- a box with "render": false takes only the collision, one with "collision": false only the triangles;
- the stock sun shadow is untouched without "rebake_sun_shadow"; with it (and a control bake of the same stock
  direction that removes nothing) the sun direction stays stock and the bake differs from the control;
- occlusion: the removed surfaces are Umbra occluders (GFX_SURF_IS_UMBRA_OCCLUDER) and the stock Umbra tome's tiles
  around the box were baked with them; the written tome is the converter's conservative tome (one cell holding every
  stock object with its stock user ID, in the stock order: no object can be hidden behind anything), byte-identical to
  a rebuild of it from the stock objects; every other GfxWorld field is stock; "occlusion": "stock" and a box with
  "render": false keep the stock tome;
- ACTS tests every written zone like the stock one.

Usage: py -3 -I tools/stock_world_remove_test.py <work folder> [--zone mp_shipment] [--exe <zonetool.exe>] [--no-sun]
Nothing is written outside the work folder. The game folder is only read. Needs about 1.5 GB in the work folder."""
import array, collections, glob, hashlib, json, os, shutil, struct, subprocess, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import stock_edit_test as t

BOX = {'mins': [-960.0, 1815.0, 17.5], 'maxs': [-820.0, 2140.0, 125.0]}
# A box far outside the map: removes nothing (the control of the sun bake).
NOTHING = {'mins': [200000.0, 200000.0, 200000.0], 'maxs': [200001.0, 200001.0, 200001.0]}


def inside(box, p):
    return all(box['mins'][a] <= p[a] <= box['maxs'][a] for a in range(3))


GFX_SURF_IS_UMBRA_OCCLUDER = 64


def f32(x):
    return struct.unpack('<f', struct.pack('<f', x))[0]


def crc32c(data):
    table = []
    for i in range(256):
        c = i
        for _ in range(8):
            c = (c >> 1) ^ (0x82F63B78 if c & 1 else 0)
        table.append(c)
    crc = 0xFFFFFFFF
    for b in data:
        crc = (crc >> 8) ^ table[(crc ^ b) & 0xFF]
    return crc ^ 0xFFFFFFFF


def tome_header(d):
    """The Umbra 3 ImpTome header fields the checks need (names as in the IW7 structs, Umbra::ImpTome)."""
    u = lambda o: struct.unpack_from('<I', d, o)[0]
    n = u(0x40)
    # 0xD15AB1ED ("disabled"): the stock tomes carry no checksum.
    crc = 'valid' if u(4) == crc32c(d[8:]) else 'disabled' if u(4) == 0xD15AB1ED else 'invalid'
    return {'version': u(0), 'crc': crc, 'size': u(8), 'min': list(struct.unpack_from('<3f', d, 0x14)),
            'max': list(struct.unpack_from('<3f', d, 0x20)), 'objects': n, 'user_id_starts': u(0x4C), 'gates': u(0x68),
            'clusters': u(0x7C), 'leaf_tiles': u(0x8C), 'tiles': u(0x90), 'tomes': u(0xB0),
            'user_ids': list(struct.unpack_from('<%dI' % n, d, u(0x50))), 'tile_table': u(0xA0), 'lod_levels': u(0x9C)}


def tome_tiles_in(d, box):
    """Tiles (leaf: flags 5, LOD: flags 4) whose bounds meet the box, with their cell counts."""
    h = tome_header(d)
    out = []
    for i in range(h['tiles']):
        off = struct.unpack_from('<I', d, h['tile_table'] + 4 * i)[0]
        if not off:
            continue
        mn, mx = struct.unpack_from('<3f', d, off), struct.unpack_from('<3f', d, off + 12)
        if all(mn[a] <= box['maxs'][a] and mx[a] >= box['mins'][a] for a in range(3)):
            flags = struct.unpack_from('<I', d, off + 0x2C)[0] & 0xFF
            cells = struct.unpack_from('<I', d, off + 0x34)[0] & 0xFFFF
            lod = struct.unpack_from('<f', d, h['lod_levels'] + 4 * i)[0]
            out.append({'tile': i, 'leaf': flags == 5, 'lod': lod, 'cells': cells, 'min': list(mn), 'max': list(mx)})
    return out


def conservative_tome(ids, mn, mx):
    """Python port of the converter's replayrender::BuildConservativeUmbraTome (the tome of every converted map; it
    reproduces converted-map tomes written before the function was split out of BuildUmbraTome byte for byte)."""
    n = len(ids)
    width = 1
    while width < 31 and (1 << width) < n:
        width += 1
    packed = bytearray((n * width + 31) // 32 * 4)
    bit = 0
    for o in range(n):
        for b in range(width):
            if o & (1 << b):
                packed[(bit + b) >> 3] |= 1 << ((bit + b) & 7)
        bit += width
    t = bytearray(0x170)
    p32 = lambda o, v: struct.pack_into('<I', t, o, v & 0xFFFFFFFF)
    pf = lambda o, v: struct.pack_into('<f', t, o, v)

    def alloc(size):
        t.extend(b'\0' * ((-len(t)) % 16))
        o = len(t)
        t.extend(b'\0' * size)
        return o
    mn, mx = [f32(v) for v in mn], [f32(v) for v in mx]
    p32(0, 0xD6000014); pf(0x0C, 128.0); p32(0x40, n); p32(0x54, width); p32(0x5C, n); p32(0x8C, 1); p32(0x90, 1)
    for base, v in ((0x14, mn), (0x20, mx), (0x150, mn), (0x15C, mx)):
        struct.pack_into('<3f', t, base, *v)
    top_tree = alloc(4); p32(top_tree, 3)
    top_map = alloc(4); p32(top_map, 0)
    p32(0x2C, 33); p32(0x30, top_tree); p32(0x34, top_map)
    ob, od = alloc(n * 24), alloc(n * 32)
    mid = [f32(f32(mn[a] + mx[a]) * 0.5) for a in range(3)]
    half = [f32(f32(mx[a] - mn[a]) * 0.5) for a in range(3)]
    omin, omax = [f32(mid[a] - half[a]) for a in range(3)], [f32(mid[a] + half[a]) for a in range(3)]
    for o in range(n):
        struct.pack_into('<6f', t, ob + 24 * o, *omin, *omax)
        struct.pack_into('<8f', t, od + 32 * o, *omin, 0.0, *omax, float('inf'))
    p32(0x44, ob); p32(0x48, od)
    uid = alloc(n * 4); struct.pack_into('<%dI' % n, t, uid, *ids); p32(0x50, uid)
    ol = alloc(len(packed)); t[ol:ol + len(packed)] = packed; p32(0x58, ol)
    cs = alloc(8); p32(cs, 0); p32(cs + 4, 1); p32(0x88, cs)
    lod = alloc(4); pf(lod, 1.0); p32(0x9C, lod)
    offs = alloc(4); p32(0xA0, offs)
    pe = alloc(4); pf(pe, 0.0); p32(0x14C, pe)
    tile = alloc(0x60)
    struct.pack_into('<3f', t, tile, *mn); struct.pack_into('<3f', t, tile + 12, *mx)
    p32(tile + 0x18, 33); pf(tile + 0x30, 0.0); p32(tile + 0x34, 1)
    required = max(mx[a] - mn[a] for a in range(3)) / 65536.0
    scale = f32(required)
    if scale < required:
        scale = struct.unpack('<f', struct.pack('<I', struct.unpack('<I', struct.pack('<f', scale))[0] + 1))[0]
    pf(tile + 0x44, scale); pf(tile + 0x48, f32(1.0 / scale)); pf(0x168, scale)
    tt = alloc(4); p32(tt, 3); p32(tile + 0x1C, tt - tile)
    tm = alloc(8); p32(tm, 0); p32(tile + 0x20, tm - tile)
    ci = alloc(8); p32(ci, 0); p32(ci + 4, 0xFFFFFFFF); p32(tile + 0x40, ci - tile)
    cell = alloc(36)
    for off, v in ((8, 0), (0xC, n), (0x10, 0xFFFFFFFF), (0x14, 0x80000000), (0x18, 0), (0x1C, 0xFFFF), (0x20, 0xFFFFFFFF)):
        p32(cell + off, v)
    p32(tile + 0x38, cell - tile)
    t.extend(b'\0' * ((-len(t)) % 16))
    p32(tile + 0x2C, ((len(t) - tile) << 8) | 3)
    p32(offs, tile)
    p32(0x08, len(t))
    p32(0x04, crc32c(bytes(t[8:])))
    return bytes(t)


def without_addresses(v):
    if isinstance(v, dict):
        return {k: without_addresses(x) for k, x in v.items() if k != 'address'}
    if isinstance(v, list):
        return [without_addresses(x) for x in v]
    return v


def export(zone_file, kinds, out):
    shutil.rmtree(out, ignore_errors=True)
    t.acts(['-a', ','.join(kinds), '-o', out, zone_file])
    found = {}
    for kind in kinds:
        files = glob.glob(os.path.join(out, '**', 'assets', kind, '**', '*.json'), recursive=True)
        found[kind] = files[0] if files else None
    return found


def world(zone_file, out):
    """Surface table, positions, indices and the compressed sun shadow of a zone's GfxWorld."""
    files = export(zone_file, ['gfx_map', 'gfx_map_trzone'], out)
    f = t.fields(files['gfx_map'])
    surfs = []
    opaque_end = f['surfaces']['litOpaqueSurfsEnd']
    for s in f['surfaces']['surfaces']['values']:
        tr = s['tris']
        surfs.append(((tr['posOffset'], tr['vertexCount'], tr['triCount'], tr['baseIndex']), s['material']['name'].lstrip(','),
                      s['flags']['value'] if isinstance(s['flags'], dict) else s['flags'], s['transientZone'], s['lightmapIndex']))
    draw = f['draw']
    sun = draw.get('compressedSunShadow') or {}
    sun_bytes = bytes.fromhex(sun.get('bytes') or '')
    params = draw.get('compressedSunShadowParams') or {}
    tome = bytes.fromhex((f.get('umbraTomeData') or {}).get('bytes') or '')
    gates = f.get('numUmbraGates')
    # Every GfxWorld field but the tome (runtime addresses left out): the occlusion edit must leave them stock.
    rest = without_addresses({k: v for k, v in f.items() if k not in ('umbraTomeData', 'umbraTomeSize')})
    rest = hashlib.sha256(json.dumps(rest, sort_keys=True).encode()).hexdigest()
    del f
    tz = t.fields(files['gfx_map_trzone'])['drawVerts']
    pos = bytes.fromhex(tz['posData']['bytes'])
    idx = array.array('H', bytes.fromhex(tz['indices']['bytes']))
    del tz
    shutil.rmtree(out, ignore_errors=True)
    return {'surfaces': surfs, 'opaque_end': opaque_end, 'pos': pos, 'idx': idx, 'sun': hashlib.sha256(sun_bytes).hexdigest(), 'sun_size': len(sun_bytes),
            'sun_dir': [round(v, 6) for v in ((params.get('sunDirection') or {}).get('v') or [])],
            'tome': tome, 'gates': gates, 'rest': rest}


def tome_of(zone_file, out):
    """The Umbra tome bytes of a zone's GfxWorld."""
    files = export(zone_file, ['gfx_map'], out)
    tome = bytes.fromhex((t.fields(files['gfx_map']).get('umbraTomeData') or {}).get('bytes') or '')
    shutil.rmtree(out, ignore_errors=True)
    return tome


def collision(exe, zone_file, out):
    """The world Havok data of a zone's col_map and its meshes as `zonetool havokinfo --mesh` decodes them."""
    files = export(zone_file, ['col_map'], out)
    f = t.fields(files['col_map'])
    blob = bytes.fromhex(f['havokWorldShapeData']['bytes'])
    shutil.rmtree(out, ignore_errors=True)
    os.makedirs(out)
    path = os.path.join(out, 'world.hkx')
    open(path, 'wb').write(blob)
    r = subprocess.run([exe, '-game', 'iw8', '-path', t.GAME, 'havokinfo', path, '--mesh', os.path.join(out, 'mesh.json')],
                       capture_output=True, text=True, errors='replace')
    mesh = json.load(open(os.path.join(out, 'mesh.json'), encoding='utf-8'))
    shutil.rmtree(out, ignore_errors=True)
    tris, convex = collections.Counter(), collections.Counter()
    for shape in mesh['shapes']:
        v = shape.get('triangles') or []
        for i in range(0, len(v), 9):
            tris[tuple(v[i:i + 9])] += 1
        for c in shape.get('convex') or []:
            if c.get('live', True):
                convex[(tuple(c['min']), tuple(c['max']), c['tag'])] += 1
    return blob, tris, convex


def removed_triangles(w, box):
    """Index positions of the triangles whose three vertices lie inside the box (from the stock data)."""
    out = []
    pos, idx = w['pos'], w['idx']
    for (pos_offset, vertex_count, tri_count, base), material, flags, zone, lightmap in w['surfaces']:
        for k in range(tri_count):
            at = base + 3 * k
            corners = idx[at:at + 3]
            if any(v >= vertex_count for v in corners):
                continue
            if all(inside(box, struct.unpack_from('<3f', pos, pos_offset + 12 * v)) for v in corners):
                out.append(at)
    return out


def main():
    args = sys.argv[1:]
    zone, exe, sun = 'mp_shipment', t.EXE, True
    while '--zone' in args:
        i = args.index('--zone'); zone = args[i + 1]; del args[i:i + 2]
    while '--exe' in args:
        i = args.index('--exe'); exe = args[i + 1]; del args[i:i + 2]
    if '--no-sun' in args:
        args.remove('--no-sun'); sun = False
    work = os.path.abspath(args[0])
    os.makedirs(work, exist_ok=True)
    failures = []

    def check(ok, text):
        print(('  PASS ' if ok else '  FAIL ') + text)
        if not ok:
            failures.append(text)

    stock_file = os.path.join(t.GAME, 'zone', zone + '.ff')
    stock = world(stock_file, os.path.join(work, 'stock_world'))
    expected = removed_triangles(stock, BOX)
    by_surface = collections.Counter()
    starts = sorted((s[0][3], i) for i, s in enumerate(stock['surfaces']))
    import bisect
    for at in expected:
        i = starts[bisect.bisect_right(starts, (at, 1 << 30)) - 1][1]
        by_surface[i] += 1
    emptied = sorted(i for i, n in by_surface.items() if n == stock['surfaces'][i][0][2])
    print('stock %s: %d surfaces, %d indices; the box %s..%s holds %d world triangles of %d surfaces:' %
          (zone, len(stock['surfaces']), len(stock['idx']), BOX['mins'], BOX['maxs'], len(expected), len(by_surface)))
    for i, n in sorted(by_surface.items()):
        print('    surface %d %s: %d of %d triangles' % (i, stock['surfaces'][i][1], n, stock['surfaces'][i][0][2]))
    check(len(expected) > 0 and emptied, 'the box holds world triangles and whole surfaces (emptied: %s)' % emptied)
    stock_blob, stock_tris, stock_convex = collision(exe, stock_file, os.path.join(work, 'stock_col'))
    tri_in = collections.Counter({k: n for k, n in stock_tris.items() if all(inside(BOX, k[c:c + 3]) for c in (0, 3, 6))})
    convex_in = collections.Counter({k: n for k, n in stock_convex.items() if inside(BOX, k[0]) and inside(BOX, k[1])})
    print('stock collision: %d mesh triangles, %d convex shapes; inside the box %d triangles, %d convex shapes' %
          (sum(stock_tris.values()), sum(stock_convex.values()), sum(tri_in.values()), sum(convex_in.values())))
    check(sum(tri_in.values()) > 0 and sum(convex_in.values()) > 0, 'the box holds collision triangles and convex shapes')

    # Occlusion in the stock zone: the records that make the box's geometry an occluder.
    stock_tome = stock['tome']
    th = tome_header(stock_tome)
    occluders = sorted(i for i in by_surface if stock['surfaces'][i][2] & GFX_SURF_IS_UMBRA_OCCLUDER)
    surface_objects = {uid: k for k, uid in enumerate(th['user_ids']) if uid >> 28 == 0}
    box_tiles = tome_tiles_in(stock_tome, BOX)
    print('stock Umbra tome: %d bytes, %d objects (%s by class), %d tiles (%d leaf), %d clusters, %d gates; checksum %s' %
          (len(stock_tome), th['objects'], dict(sorted(collections.Counter(u >> 28 for u in th['user_ids']).items())), th['tiles'],
           th['leaf_tiles'], th['clusters'], th['gates'], th['crc']))
    print('  occluder surfaces in the box (flag %d): %s' % (GFX_SURF_IS_UMBRA_OCCLUDER,
          ['%d %s (object %s)' % (i, stock['surfaces'][i][1], surface_objects.get(i)) for i in occluders]))
    for tile in box_tiles:
        print('  tile %d (%s, lod %g): %d cells over %s..%s' % (tile['tile'], 'leaf' if tile['leaf'] else 'LOD', tile['lod'], tile['cells'],
              tile['min'], tile['max']))
    check(th['version'] == 0xD6000014 and th['crc'] in ('valid', 'disabled') and th['gates'] == 0 and th['tomes'] == 0 and th['user_id_starts'] == 0,
          'the stock tome is a Replay 1.20 Umbra tome (0xD6000014, checksum %s, no gates, no tome collection)' % th['crc'])
    check(occluders and all(i in surface_objects for i in occluders) and any(t_['leaf'] and t_['cells'] > 1 for t_ in box_tiles),
          'the box holds %d Umbra occluder surfaces (Umbra objects too), and leaf tiles with several cells cover it' % len(occluders))

    # 1. Removal without the sun.
    code, log, rep = t.editzone(exe, zone, {'remove_world_surfaces': {'boxes': [BOX]}, 'patch': False}, os.path.join(work, 'out'))
    if code != 0:
        print(log[-3000:])
    r = rep.get('remove_world_surfaces') or {}
    c = r.get('collision') or {}
    check(code == 0 and r.get('triangles_removed') == len(expected) and r.get('surfaces_emptied') == emptied,
          'editzone removes %s render triangles (expected %d), emptied %s' % (r.get('triangles_removed'), len(expected), r.get('surfaces_emptied')))
    check(c.get('triangles_removed') == sum(tri_in.values()) and c.get('convex_removed') == sum(convex_in.values()),
          'editzone removes %s collision triangles and %s convex shapes (expected %d, %d)' %
          (c.get('triangles_removed'), c.get('convex_removed'), sum(tri_in.values()), sum(convex_in.values())))
    if code != 0:
        print('RESULT: FAIL (%d failed)' % (len(failures) + 1))
        return 1
    out_file = os.path.join(work, 'out', zone + '.ff')
    edited = world(out_file, os.path.join(work, 'out_world'))
    check(edited['surfaces'] == stock['surfaces'], 'the surface table (offsets, counts, materials, flags) is stock')
    check(edited['pos'] == stock['pos'], 'the vertex positions are stock (%d bytes)' % len(stock['pos']))
    want = array.array('H', stock['idx'])
    for at in expected:
        want[at + 1] = want[at]
        want[at + 2] = want[at]
    check(len(edited['idx']) == len(stock['idx']), 'the index count is stock (%d)' % len(stock['idx']))
    check(edited['idx'] == want, 'exactly the %d triangles in the box are degenerate (i0, i0, i0); every other index is stock' % len(expected))
    drawn = sum(1 for at in expected if not (edited['idx'][at] == edited['idx'][at + 1] == edited['idx'][at + 2]))
    check(drawn == 0, 'no triangle inside the box still has an area (%d)' % drawn)
    check(edited['sun'] == stock['sun'], 'without rebake_sun_shadow the compressed sun shadow is stock (%d bytes)' % stock['sun_size'])

    # Occlusion: the stock tome replaced by the conservative one built from the stock objects.
    o = r.get('occlusion') or {}
    eh = tome_header(edited['tome']) if len(edited['tome']) >= 0x170 else {}
    check(o.get('opened') is True and o.get('occluder_surfaces_touched') == occluders and o.get('objects') == th['objects']
          and o.get('tome_bytes_before') == len(stock_tome) and o.get('tome_bytes_after') == len(edited['tome']),
          'the report opens the occlusion for the occluder surfaces %s (%s objects, tome %s -> %s bytes)' %
          (o.get('occluder_surfaces_touched'), o.get('objects'), o.get('tome_bytes_before'), o.get('tome_bytes_after')))
    check(edited['tome'] == conservative_tome(th['user_ids'], th['min'], th['max']),
          'the written tome is the conservative tome rebuilt from the stock objects and bounds (%d bytes)' % len(edited['tome']))
    check(eh.get('crc') == 'valid' and eh.get('tiles') == 1 and eh.get('leaf_tiles') == 1 and eh.get('clusters') == 0 and eh.get('gates') == 0
          and eh.get('user_ids') == th['user_ids'] and eh.get('min') == th['min'] and eh.get('max') == th['max'],
          'it has one tile and one cell with all %d stock user IDs in the stock order, over the stock bounds; checksum valid' % th['objects'])
    open_tiles = tome_tiles_in(edited['tome'], BOX) if eh else []
    check(open_tiles and all(t_['cells'] == 1 for t_ in open_tiles) and edited['gates'] == stock['gates'] == 0,
          'no cell boundary is left around the box (the one cell spans the whole tome), no gates')
    check(edited['rest'] == stock['rest'], 'every other GfxWorld field is stock (runtime addresses left out)')

    blob, tris, convex = collision(exe, out_file, os.path.join(work, 'out_col'))
    left_in = sum(n for k, n in tris.items() if all(inside(BOX, k[c:c + 3]) for c in (0, 3, 6)))
    convex_left = sum(n for k, n in convex.items() if inside(BOX, k[0]) and inside(BOX, k[1]))
    check(left_in == 0 and convex_left == 0, 'the written collision holds no triangle (%d) and no live convex shape (%d) inside the box' % (left_in, convex_left))
    check(tris == stock_tris - tri_in, 'every other collision triangle is there with stock coordinates (%d = %d - %d)' %
          (sum(tris.values()), sum(stock_tris.values()), sum(tri_in.values())))
    check(convex == stock_convex - convex_in, 'every other convex shape is there, placed as stock (%d = %d - %d)' %
          (sum(convex.values()), sum(stock_convex.values()), sum(convex_in.values())))
    diff = sum(1 for a, b in zip(blob, stock_blob) if a != b)
    check(len(blob) == len(stock_blob) and 0 < diff < 64 * (sum(tri_in.values()) + sum(convex_in.values())),
          'the Havok data keeps its size (%d bytes) and differs in %d bytes only' % (len(blob), diff))

    test = [l for l in t.acts(['--test', '-o', os.path.join(work, 'test'), out_file]) if l.startswith('Replay:')]
    stock_test = [l for l in t.acts(['--test', '-o', os.path.join(work, 'stock_test'), stock_file]) if l.startswith('Replay:')]
    tail = lambda lines: lines[0].split('tested,', 1)[1].strip() if lines and 'tested,' in lines[0] else None
    check(tail(test) is not None and tail(test) == tail(stock_test), 'ACTS tests it like the stock zone: %s (stock %s)' % (test, stock_test))
    shutil.rmtree(os.path.join(work, 'test'), ignore_errors=True)
    shutil.rmtree(os.path.join(work, 'stock_test'), ignore_errors=True)

    # 2. One part only: "render": false takes only the collision, "collision": false only the triangles.
    for flag, other in (('render', 'collision'), ('collision', 'render')):
        box = dict(BOX)
        box[flag] = False
        code, log, rep = t.editzone(exe, zone, {'remove_world_surfaces': {'boxes': [box]}, 'patch': False}, os.path.join(work, 'only'))
        r = rep.get('remove_world_surfaces') or {}
        only_blob = collision(exe, os.path.join(work, 'only', zone + '.ff'), os.path.join(work, 'only_col'))[0] if code == 0 else b''
        only_tome = tome_of(os.path.join(work, 'only', zone + '.ff'), os.path.join(work, 'only_tome')) if code == 0 else b''
        if flag == 'render':
            check(code == 0 and r.get('triangles_removed') == 0 and (r.get('collision') or {}).get('triangles_removed') == sum(tri_in.values())
                  and only_blob == blob, '"render": false removes no triangle and the same collision (%s)' % r.get('collision'))
            check(only_tome == stock_tome and (r.get('occlusion') or {}).get('opened') is False,
                  '"render": false keeps the stock Umbra tome (the geometry still draws and still occludes)')
        else:
            check(code == 0 and r.get('triangles_removed') == len(expected) and 'collision' not in r and only_blob == stock_blob,
                  '"collision": false removes the %s triangles and keeps the stock collision' % r.get('triangles_removed'))
            check(only_tome == edited['tome'], '"collision": false opens the occlusion as the full removal does')
        shutil.rmtree(os.path.join(work, 'only'), ignore_errors=True)

    # "occlusion": "stock" keeps the stock tome; "open" opens it even for a box that removes nothing.
    for mode, box, want in (('stock', BOX, stock_tome), ('open', NOTHING, edited['tome'])):
        code, log, rep = t.editzone(exe, zone, {'remove_world_surfaces': {'boxes': [box], 'occlusion': mode}, 'patch': False},
                                    os.path.join(work, 'mode'))
        o = (rep.get('remove_world_surfaces') or {}).get('occlusion') or {}
        got = tome_of(os.path.join(work, 'mode', zone + '.ff'), os.path.join(work, 'mode_tome')) if code == 0 else b''
        check(code == 0 and got == want and o.get('opened') is (mode == 'open'),
              '"occlusion": "%s" %s' % (mode, 'keeps the stock tome' if mode == 'stock' else 'writes the conservative tome without a removal'))
        shutil.rmtree(os.path.join(work, 'mode'), ignore_errors=True)

    # 3. The sun shadow baked again at the stock direction, with a control bake that removes nothing.
    if sun:
        bakes = {}
        for name, box in (('control', NOTHING), ('removed', BOX)):
            edits = {'remove_world_surfaces': {'boxes': [box], 'rebake_sun_shadow': True, 'acts': t.ACTS}, 'patch': False}
            code, log, rep = t.editzone(exe, zone, edits, os.path.join(work, 'sun_' + name))
            if code != 0:
                print(log[-3000:])
            check(code == 0 and rep.get('sun_shadow_rebaked_for_removal') is True and rep.get('sun_vector') == rep.get('sun_vector_before')
                  and rep.get('sun_visions') == [] and rep.get('sun_worldspawn_sundirection') is False,
                  '%s: the sun shadow is baked again at the stock sun direction %s; visions and worldspawn untouched' % (name, rep.get('sun_vector')))
            zone_out = os.path.join(work, 'sun_' + name, zone + '.ff')
            bakes[name] = (world(zone_out, os.path.join(work, 'sun_world')), rep)
            if name == 'removed':
                test = [l for l in t.acts(['--test', '-o', os.path.join(work, 'test'), zone_out]) if l.startswith('Replay:')]
                check(tail(test) == tail(stock_test), 'ACTS tests the re-baked zone like the stock zone: %s' % test)
                shutil.rmtree(os.path.join(work, 'test'), ignore_errors=True)
            shutil.rmtree(os.path.join(work, 'sun_' + name), ignore_errors=True)
        control, removed = bakes['control'][0], bakes['removed'][0]
        check(control['tome'] == stock_tome and removed['tome'] == edited['tome'],
              'the control bake (nothing removed, "occlusion": "auto") keeps the stock tome; the re-baked removal opens it')
        check(control['sun_dir'] == stock['sun_dir'] == removed['sun_dir'], 'the baked shadow keeps the stock sun direction %s' % stock['sun_dir'])
        cw = bakes['control'][1]['sun_shadow']['world_triangles']
        rw = bakes['removed'][1]['sun_shadow']['world_triangles']
        # The casters are the lit-opaque surfaces with the sun-shadow flag (1), not the sky.
        casting = sum(n for i, n in by_surface.items() if i < stock['opaque_end'] and stock['surfaces'][i][2] & 1
                      and '/sky_' not in stock['surfaces'][i][1])
        check(cw - rw == casting, 'the %d removed casting triangles no longer cast: %d world caster triangles, %d in the control bake' %
              (casting, rw, cw))
        check(removed['sun'] != control['sun'], 'the re-baked sun shadow differs from the control bake (%d / %d bytes)' %
              (removed['sun_size'], control['sun_size']))
    shutil.rmtree(os.path.join(work, 'out'), ignore_errors=True)
    print('RESULT: %s (%d failed)' % ('PASS' if not failures else 'FAIL', len(failures)))
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
