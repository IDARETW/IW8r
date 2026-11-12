"""Offline proof of the two scriptable physics limits and of a Warzone-zone definition placed by name (research 89).

Adds three scriptable instances to an extraction's collision JSON, with the numbers of the catalogue
(data/asset_catalog/iw8_scriptables.tsv): a Hardpoint chevron (no limit), a Hardpoint chevron marked "main limit only"
(what a stock sign or fence definition is; the chevron stands in because it is always loaded) and the Warzone buy
station br_plunder_box (both limits, definition in common_br_mp, placed by name). Builds with the converter, exports
map_ents with ACTS and checks the six limits and the counts.

Usage: py -3 -I tools/scriptable_limits_test.py <extraction folder> <map id> <work folder> [--new <zonetool.exe>]
Nothing is written outside the work folder. The game folder is only read."""
import csv, glob, json, os, shutil, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import scriptable_offline_test as t

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def catalogue():
    rows = {}
    with open(os.path.join(ROOT, 'data', 'asset_catalog', 'iw8_scriptables.tsv'), encoding='utf-8') as f:
        lines = [l for l in f if not l.startswith('#')]
    for row in csv.DictReader(lines, delimiter='\t'):
        rows[row['name']] = row
    return rows


def instance(row, origin, **more):
    d = dict(definition=row['name'], origin=origin, angles=[0, 0, 0], server_instanced_parts=int(row['server_instanced_parts']),
             usable_parts=int(row['usable_parts']), physics_limit=row['physics_limit'] == '1', max_dynent_parts=int(row['max_dynent_parts']),
             instance_dynent=row['instance_dynent'] == '1', sparse_occluder=row['sparse_occluder'] == '1',
             collision_flags=int(row['collision_flags']), physics_dynents=int(row['physics_dynents']))
    if row.get('physics_limit_detail', row['physics_limit']) != row['physics_limit']:
        d['physics_limit_detail'] = row['physics_limit_detail'] == '1'
    d.update(more)
    return d


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

    rows = catalogue()
    chevron, box = rows['hardpoint_chevron'], rows['br_plunder_box']
    check(box['zones'] == 'common_br_mp' and box['verified'] == '1' and box['physics_limit'] == '1' and box['physics_limit_detail'] == '1',
          'the catalogue knows the buy station: common_br_mp, both limits')
    instances = [
        instance(chevron, [64, 32, 16], targetname='hp_a'),
        instance(chevron, [-64, 32, 16], physics_limit=True, physics_limit_detail=False),
        instance(box, [0, -96, 0], targetname='kiosk_a'),
    ]
    copy = os.path.join(work, 'extraction')
    if os.path.isdir(copy):
        shutil.rmtree(copy)
    shutil.copytree(extraction, copy)
    shutil.copy(metadata, os.path.join(work, 'metadata.json'))
    col_path = os.path.join(copy, 'extracted', 'maps', 'mp', map_id + '.d3dbsp.replay-collision.json')
    col = json.load(open(col_path, encoding='utf-8'))
    col['scriptable_instances'] = instances
    json.dump(col, open(col_path, 'w', encoding='utf-8'))
    out = os.path.join(work, 'out')
    code, log = t.build(new, copy, map_id, out, os.path.join(work, 'metadata.json'))
    check(code == 0, 'the converter finishes (exit %d)' % code)
    for line in log.splitlines():
        if 'scriptable' in line.lower() or 'br_plunder' in line.lower():
            print('       ' + line.strip()[:220])
    if code != 0:
        print(log[-3000:])
        return 1
    zone = os.path.join(out, map_id, map_id + '.ff')
    replay = t.acts_test(zone, os.path.join(work, 'acts_test'))
    check(bool(replay) and all(' 0 failed' in l for l in replay), 'ACTS loads and tests the map zone with 0 failed: %s' % replay)
    dump = os.path.join(work, 'acts')
    if os.path.isdir(dump):
        shutil.rmtree(dump)
    t.acts_export(zone, 'map_ents', dump)
    files = glob.glob(os.path.join(dump, '**', 'map_ents', '**', '*.asset.json'), recursive=True)
    check(len(files) == 1, 'ACTS exports the map_ents')
    if len(files) != 1:
        return 1
    sm = json.load(open(files[0], encoding='utf-8'))['asset']['fields']['scriptableMapEnts']
    limits = sm['physicsLimits']
    main_want = sum(1 for i in instances if i['physics_limit'])
    detail_want = sum(1 for i in instances if i.get('physics_limit_detail', i['physics_limit']))
    for key in ('clientStandaloneMainRigidBodyCount', 'clientStandaloneMotionCount', 'serverStandaloneMainRigidBodyCount', 'serverStandaloneMotionCount'):
        check(limits[key] == main_want, '%s is %d (the instances with the main limit)' % (key, limits[key]))
    for key in ('clientStandaloneDetailedRigidBodyCount', 'serverStandaloneDetailedRigidBodyCount'):
        check(limits[key] == detail_want, '%s is %d (the instances with the detailed limit)' % (key, limits[key]))
    check(main_want == 2 and detail_want == 1, 'the test has 2 main-limit instances and 1 detailed-limit instance')
    names = [str((i['initialDef'] or {}).get('name') or (i['initialDef'] or {}).get('string') or '') for i in sm['mapInstances']['values']]
    check(sm['mapInstanceCount'] == 3 and [n.lstrip(',') for n in names] == ['hardpoint_chevron', 'hardpoint_chevron', 'br_plunder_box'],
          'the three instances read back by definition name: %s' % names)
    check(names[2].startswith(','), 'the buy station is a name reference (its definition stays in common_br_mp): "%s"' % names[2])
    server = sum(1 for i in instances if i['server_instanced_parts'])
    check(sm['serverInstanceCount'] == server and sm['serverFixedPartCount'] == sum(i['server_instanced_parts'] for i in instances) and
          sm['serverUsablePartCount'] == sum(i['usable_parts'] for i in instances),
          'the server counts follow the instances (%d, %d, %d)' % (sm['serverInstanceCount'], sm['serverFixedPartCount'], sm['serverUsablePartCount']))
    print('RESULT: %s (%d failed)' % ('PASS' if not failures else 'FAIL', len(failures)))
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
