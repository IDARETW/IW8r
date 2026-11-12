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
    del f
    tz = t.fields(files['gfx_map_trzone'])['drawVerts']
    pos = bytes.fromhex(tz['posData']['bytes'])
    idx = array.array('H', bytes.fromhex(tz['indices']['bytes']))
    del tz
    shutil.rmtree(out, ignore_errors=True)
    return {'surfaces': surfs, 'opaque_end': opaque_end, 'pos': pos, 'idx': idx, 'sun': hashlib.sha256(sun_bytes).hexdigest(), 'sun_size': len(sun_bytes),
            'sun_dir': [round(v, 6) for v in ((params.get('sunDirection') or {}).get('v') or [])]}


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
        if flag == 'render':
            check(code == 0 and r.get('triangles_removed') == 0 and (r.get('collision') or {}).get('triangles_removed') == sum(tri_in.values())
                  and only_blob == blob, '"render": false removes no triangle and the same collision (%s)' % r.get('collision'))
        else:
            check(code == 0 and r.get('triangles_removed') == len(expected) and 'collision' not in r and only_blob == stock_blob,
                  '"collision": false removes the %s triangles and keeps the stock collision' % r.get('triangles_removed'))
        shutil.rmtree(os.path.join(work, 'only'), ignore_errors=True)

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
