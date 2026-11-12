"""Offline proof of streamed collision tiles for a built map (docs/research/90_collision_tiles.md).

1. Without the switch the new converter gives byte-identical zones (a small map keeps its collision in one piece).
2. With "collision_tiles": true in the extraction's collision JSON:
   - the package holds the five zones, 1,024 <map>_cg_NNNNN_tr.ff tile zones and <map>_cg_ls_tr.ff;
   - the main zone's world shape list has one slot per tile with collision (tile index, counts, bounds, contents),
     and the slots hold together exactly the vertices, triangles and hulls of the one-piece build;
   - a tile zone holds one collisiontile whose Havok data is rooted at an hknpCompressedMeshShape, with the grid
     position, centre and slot the game expects; an empty tile holds an empty collisiontile;
   - the combined package holds all 1,024 tiles;
   - the main zone's streaming info names every file with costs that follow the zone block sizes, the 1,024
     collisiontile stubs and the tile count;
   - ACTS loads the main zone, a tile and the combined package with 0 failed.

Usage: py -3 -I tools/collision_tiles_test.py <extraction folder> <map id> <work folder> [--new <zonetool.exe>]
Nothing is written outside the work folder. The game folder is only read."""
import glob, json, os, re, shutil, struct, subprocess, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import scriptable_offline_test as t

TILE, GRID = 8192.0, 32


def zonetool(exe, *args):
    r = subprocess.run([exe, '-game', 'iw8', '-path', t.GAME] + list(args), capture_output=True, text=True, errors='replace')
    return r.returncode, r.stdout + r.stderr


def export_fields(zone_file, kind, out):
    if os.path.isdir(out):
        shutil.rmtree(out)
    t.acts_export(zone_file, kind, out)
    files = sorted(glob.glob(os.path.join(out, '**', kind, '**', '*.asset.json'), recursive=True))
    return [json.load(open(f, encoding='utf-8'))['asset']['fields'] for f in files]


def blob(fields, key, path):
    node = fields.get(key)
    if not node:
        return None
    open(path, 'wb').write(bytes.fromhex(node['bytes']))
    return path


def dump_item(exe, path, item):
    code, out = zonetool(exe, 'havokinfo', path, '--dump', str(item))
    body = out.split('  item %d ' % item)[-1].split('pointers:')[0]
    return bytes.fromhex(''.join(''.join(body.splitlines()[1:]).split()))


def shape_list(exe, path):
    """The per-slot arrays of a HavokPhysicsShapeList blob."""
    code, out = zonetool(exe, 'havokinfo', path, '--dump', '1')
    root = out.split('  item 1 HavokPhysicsShapeList')[-1]
    targets = dict((int(a, 16), int(b)) for a, b in re.findall(r'\+0x([0-9A-F]+)->(\d+)', root.split('pointers:')[-1].split('\n')[0]))
    def ints(offset):
        raw = dump_item(exe, path, targets[offset]) if offset in targets else b''
        return list(struct.unpack('<%di' % (len(raw) // 4), raw))
    raw = dump_item(exe, path, targets[0x50]) if 0x50 in targets else b''
    floats = struct.unpack('<%df' % (len(raw) // 4), raw)
    bounds = [(floats[i * 8:i * 8 + 3], floats[i * 8 + 4:i * 8 + 7]) for i in range(len(floats) // 8)]
    shapes = len(dump_item(exe, path, targets[0x0])) // 8 if 0x0 in targets else 0
    return dict(indices=ints(0x10), verts=ints(0x30), tris=ints(0x40), bounds=bounds, contents=ints(0x78), convex=ints(0x88), shapes=shapes,
                text=out)


def blocks(exe, zone_file):
    code, out = zonetool(exe, 'inspect', zone_file)
    return {int(m.group(1)): int(m.group(2), 16) for m in re.finditer(r'block (\d+):\s+0x([0-9A-Fa-f]+)', out)}


def pages(n):
    return max(1, (n + 0xFFFF) // 0x10000)


def main():
    args = sys.argv[1:]
    new = t.NEW
    if '--new' in args:
        new = args[args.index('--new') + 1]
        del args[args.index('--new'):args.index('--new') + 2]
    extraction, map_id, work = os.path.abspath(args[0]), args[1], os.path.abspath(args[2])
    metadata = os.path.join(os.path.dirname(extraction), 'metadata.json')
    os.makedirs(work, exist_ok=True)
    failures = []

    def check(ok, text):
        print(('  ok   ' if ok else '  FAIL ') + text)
        if not ok:
            failures.append(text)

    print('1. without the switch')
    code_a, _ = t.build(t.LIVE, extraction, map_id, os.path.join(work, 'live'), metadata)
    code_b, _ = t.build(new, extraction, map_id, os.path.join(work, 'plain'), metadata)
    za, zb = t.zones(os.path.join(work, 'live'), map_id), t.zones(os.path.join(work, 'plain'), map_id)
    check(code_a == 0 and code_b == 0 and len(za) == 5 and za.keys() == zb.keys(), 'both converters finish with the five zones')
    for name in sorted(za):
        check(za[name] == zb.get(name), '%s is byte-identical' % name)
    plain_zone = os.path.join(work, 'plain', map_id, map_id + '.ff')
    plain_list = shape_list(new, blob(export_fields(plain_zone, 'col_map', os.path.join(work, 'acts_plain'))[0], 'havokWorldShapeData',
                                      os.path.join(work, 'plain_world.hk')))
    print('       one piece: %d slot(s), %d vertices, %d triangles, %d hulls' % (
        len(plain_list['indices']), sum(plain_list['verts']), sum(plain_list['tris']), sum(plain_list['convex'])))

    print('2. "collision_tiles": true')
    copy = os.path.join(work, 'extraction')
    if os.path.isdir(copy):
        shutil.rmtree(copy)
    shutil.copytree(extraction, copy)
    shutil.copy(metadata, os.path.join(work, 'metadata.json'))
    col_path = os.path.join(copy, 'extracted', 'maps', 'mp', map_id + '.d3dbsp.replay-collision.json')
    col = json.load(open(col_path, encoding='utf-8'))
    col['collision_tiles'] = True
    json.dump(col, open(col_path, 'w', encoding='utf-8'))
    out = os.path.join(work, 'tiled')
    code, log = t.build(new, copy, map_id, out, os.path.join(work, 'metadata.json'))
    check(code == 0, 'the converter finishes (exit %d)' % code)
    for line in log.splitlines():
        if 'collision tile' in line.lower() or 'collision: ' in line:
            print('       ' + line.strip()[:230])
    if code != 0:
        print(log[-3000:])
        return 1
    folder = os.path.join(out, map_id)
    names = sorted(os.listdir(folder))
    tile_files = [n for n in names if re.fullmatch(re.escape(map_id) + r'_cg_\d{5}_tr\.ff', n)]
    check(len(tile_files) == 1024 and tile_files[0].endswith('_cg_00000_tr.ff') and tile_files[-1].endswith('_cg_01023_tr.ff'),
          'the package holds %d collision tile zones (00000 .. 01023)' % len(tile_files))
    check(map_id + '_cg_ls_tr.ff' in names, 'and the combined package %s_cg_ls_tr.ff' % map_id)
    rest = [n for n in names if n not in tile_files and n != map_id + '_cg_ls_tr.ff']
    check(sorted(n for n in rest if n.endswith('.ff')) == sorted(z + '.ff' for z in (map_id, 'srv_' + map_id, 'eng_' + map_id, 'ww_' + map_id, 'techsets_' + map_id)),
          'beside the five zones of every map')

    zone = os.path.join(folder, map_id + '.ff')
    col_fields = export_fields(zone, 'col_map', os.path.join(work, 'acts_tiled'))[0]
    world = shape_list(new, blob(col_fields, 'havokWorldShapeData', os.path.join(work, 'tiled_world.hk')))
    slots = len(world['indices'])
    check(slots >= 2 and world['indices'][0] == -1 and world['shapes'] == 1, 'the world list has %d slots; slot 0 is the map\'s own shape, the only one with a pointer' % slots)
    tiles_in_list = world['indices'][1:]
    check(tiles_in_list == sorted(set(tiles_in_list)) and all(0 <= i < 1024 for i in tiles_in_list), 'the other slots are tiles, ascending: %s' % tiles_in_list[:12])
    check(all(len(world[k]) == slots for k in ('verts', 'tris', 'contents', 'convex')) and len(world['bounds']) == slots, 'every per-slot array has %d entries' % slots)
    check(sum(world['verts']) == sum(plain_list['verts']) and sum(world['tris']) == sum(plain_list['tris']) and sum(world['convex']) == sum(plain_list['convex']),
          'the slots hold together what the one-piece build holds: %d vertices, %d triangles, %d hulls' % (sum(world['verts']), sum(world['tris']), sum(world['convex'])))
    ok_bounds = True
    for slot, tile in enumerate(tiles_in_list, start=1):
        gx, gy = tile % GRID, tile // GRID
        cx, cy = (-GRID / 2 + gx + 0.5) * TILE, (-GRID / 2 + gy + 0.5) * TILE
        lo, hi = world['bounds'][slot]
        mx, my = (lo[0] + hi[0]) * 16.0, (lo[1] + hi[1]) * 16.0     # the middle of the slot's bounds, in game units
        ok_bounds &= abs(mx - cx) <= TILE and abs(my - cy) <= TILE
    check(ok_bounds, 'each tile slot\'s bounds lie on its own tile (Havok units of 32 game units, absolute)')

    # A tile with collision, and an empty one.
    first = tiles_in_list[0]
    tile_zone = os.path.join(folder, '%s_cg_%05d_tr.ff' % (map_id, first))
    tf = export_fields(tile_zone, 'collisiontile', os.path.join(work, 'acts_tile'))
    check(len(tf) == 1, 'tile %d: ACTS exports one collisiontile' % first)
    if tf:
        f = tf[0]
        gx, gy = first % GRID, first // GRID
        origin = tuple(f['origin']['v'])
        check(f['index'] == first and f['gridId'] == [gx, gy] and origin == ((-GRID / 2 + gx + 0.5) * TILE, (-GRID / 2 + gy + 0.5) * TILE, 0.0),
              'index %d, grid %s, centre %s' % (f['index'], f['gridId'], origin))
        check(f['worldCollisionShapeIdx'] == 1 and f['havokTileShapeDataSize'] > 0 and f['staticModelCollision'] is None,
              'its slot in the world list is %d, with %d bytes of Havok data' % (f['worldCollisionShapeIdx'], f['havokTileShapeDataSize']))
        code, info = zonetool(new, 'havokinfo', blob(f, 'havokTileShapeData', os.path.join(work, 'tile.hk')))
        check('item 1 hknpCompressedMeshShape x1' in info and 'self-describing' in info, 'the Havok data is rooted at an hknpCompressedMeshShape, as stock tiles')
        check('HavokPhysicsShapeList' not in info.split('\n', 2)[-1], 'and has no shape list of its own')
    empty = next(i for i in range(1024) if i not in tiles_in_list)
    ef = export_fields(os.path.join(folder, '%s_cg_%05d_tr.ff' % (map_id, empty)), 'collisiontile', os.path.join(work, 'acts_empty'))
    check(len(ef) == 1 and ef[0]['index'] == empty and ef[0]['havokTileShapeData'] is None and ef[0]['havokTileShapeDataSize'] == 0 and
          ef[0]['worldCollisionShapeIdx'] == 0, 'an empty tile (%d) holds a collisiontile without data, slot 0' % empty)
    pf = export_fields(os.path.join(folder, map_id + '_cg_ls_tr.ff'), 'collisiontile', os.path.join(work, 'acts_package'))
    with_data = sorted(f['index'] for f in pf if f['havokTileShapeDataSize'])
    check(len(pf) == 1024 and sorted(f['index'] for f in pf) == list(range(1024)) and with_data == tiles_in_list,
          'the combined package holds all 1,024 tiles, %d of them with collision' % len(with_data))

    # Zone headers: transient file type 3, as the stock collision zones.
    for name in ('%s_cg_%05d_tr.ff' % (map_id, first), map_id + '_cg_ls_tr.ff'):
        head = open(os.path.join(folder, name), 'rb').read(0x14)
        check(head[:4] == b'IWff' and head[18] == 3, '%s has transient file type %d in its header (stock collision zones: 3)' % (name, head[18]))
    check(open(zone, 'rb').read(0x14)[18] == 0, 'the main zone keeps type 0')

    # The streaming info.
    si_path = os.path.join(work, 'streaminfo.json')
    code, text = zonetool(new, 'streaminfo', zone, si_path)
    check(code == 0 and os.path.exists(si_path), 'the main zone has a streaming info')
    if os.path.exists(si_path):
        si = json.load(open(si_path, encoding='utf-8'))
        ti = si.get('transientInfo') or {}
        files = ti.get('transientFiles', [])
        check(si.get('name') == map_id and ti.get('transientCollisionTileCount') == 1024, 'named %s, with a collision tile count of %s' % (si.get('name'), ti.get('transientCollisionTileCount')))
        want = [map_id] + ['%s_cg_%05d_tr' % (map_id, i) for i in range(1024)] + [map_id + '_cg_ls_tr']
        check([f['name'] for f in files] == want and [f['type'] for f in files] == [0] + [3] * 1025, 'its files: the map (type 0), the 1,024 tiles and the combined package (type 3)')
        check(ti.get('costs', {}).get('transientCostCount') == len(files), 'one cost row per file')
        ok_cost = files and files[0].get('residentPages') == 0 and files[0].get('tempPages') == 0
        samples = [1 + first, 1 + empty, 1025]
        for at in samples:
            b = blocks(new, os.path.join(folder, files[at]['name'] + '.ff'))
            ok_cost = ok_cost and files[at].get('residentPages') == pages(b.get(5, 0)) and files[at].get('tempPages') == pages(b.get(1, 0) + b.get(2, 0))
        check(bool(ok_cost), 'costs follow the block sizes of the zones (checked on a full tile, an empty tile and the package: %s)' % [
            (files[at]['name'][-11:], files[at].get('residentPages'), files[at].get('tempPages')) for at in samples])
        assets = ti.get('transientAssets', [])
        check([a['name'] for a in assets] == ['collisiontile%d' % i for i in range(1024)] and all(a['type'] == 107 and a['mpParentIndex'] == 0xFFFFFFFF for a in assets),
              'its transient assets: collisiontile0 .. collisiontile1023 (type 107)')

    for name, label in (('%s_cg_%05d_tr.ff' % (map_id, first), 'a tile'), (map_id + '_cg_ls_tr.ff', 'the combined package')):
        replay = t.acts_test(os.path.join(folder, name), os.path.join(work, 'acts_test_' + name[:-3]))
        check(bool(replay) and all(' 0 failed' in l for l in replay), 'ACTS loads %s with 0 failed: %s' % (label, replay))
    # The main zone: ACTS cannot test a streaming info that has transient costs (it fails the same way on the stock
    # mp_br_quarry and mp_donetsk); every other asset must pass.
    test_out = os.path.join(work, 'acts_test_' + map_id)
    replay = t.acts_test(zone, test_out)
    failed = []
    for journal in glob.glob(os.path.join(test_out, '**', 'assets.jsonl'), recursive=True):
        for line in open(journal, encoding='utf-8'):
            if '"status":"failed"' in line:
                entry = json.loads(line)
                failed.append((entry.get('type'), (entry.get('field_issues') or [{}])[0].get('reason', '')))
    check(bool(replay) and failed == [('streaminginfo', 'Missing count context: TransientCosts')],
          'ACTS loads the main zone; its one failure is the streaming info, which ACTS cannot test on the stock big maps either: %s %s' % (replay, failed))
    print('RESULT: %s (%d failed)' % ('PASS' if not failures else 'FAIL', len(failures)))
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
