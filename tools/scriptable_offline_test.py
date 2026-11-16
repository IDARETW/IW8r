"""Offline proof of the converter's scriptable-instance support (docs/research/86_scriptable_objects_plan.md).

1. Builds an extraction with the live converter and with the worktree converter and compares the zones byte for byte
   (a map without scriptables must not change).
2. Adds `scriptable_instances` to the extraction's collision JSON, builds with the worktree converter, exports
   map_ents and dynentitylist of the result with ACTS and prints / checks what the game-format reader sees.

Usage: py -3 -I tools/scriptable_offline_test.py <extraction folder> <map id> <work folder> [--new <zonetool.exe>]
Nothing is written outside the work folder. The game folder is only read."""
import glob, hashlib, json, os, shutil, subprocess, sys

LIVE = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'bin', 'release', 'iw8r-zt.exe')
NEW = r'C:\zsw-iw8r\scriptables\build\bin\x64\Release\zonetool.exe'
GAME = r'E:\IW8\Builds\1.20-replay'
ACTS = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'bin', 'release', 'iw8r-acts.exe')
GSC = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'bin', 'release', 'iw8r-gsc.exe')


def build(exe, extraction, map_id, out, metadata):
    if os.path.isdir(out):
        shutil.rmtree(out)
    os.makedirs(out)
    args = [exe, '-game', 'iw8', '-path', GAME, 'buildextraction', extraction, '--map', map_id, '--replay',
            os.path.join(GAME, 'game_dx12_ship_replay.exe'), '--output', out, '--metadata', metadata, '--acts', ACTS,
            '--gsc_tool', GSC, '--iw3_mode_assets', 'off']
    r = subprocess.run(args, capture_output=True, text=True, errors='replace')
    log = r.stdout + r.stderr
    open(os.path.join(out, 'zonetool.log'), 'w', encoding='utf-8').write(log)
    return r.returncode, log


def zones(out, map_id):
    found = {}
    for path in glob.glob(os.path.join(out, map_id, '*.ff')):
        found[os.path.basename(path)] = hashlib.sha256(open(path, 'rb').read()).hexdigest()
    return found


def acts_export(zone_file, kind, out):
    args = [ACTS, '--noUpdater', 'fastfile', '-r', 'mw19replay', '-g', os.path.join(GAME, 'game_dx12_ship_replay.exe'),
            '--oodle', os.path.join(GAME, 'oo2core_7_win64.dll'), '-a', kind, '-o', out, zone_file]
    r = subprocess.run(args, capture_output=True, text=True, errors='replace')
    return (r.stdout + r.stderr).strip().splitlines()[-2:]


def acts_test(zone_file, out):
    args = [ACTS, '--noUpdater', 'fastfile', '-r', 'mw19replay', '-g', os.path.join(GAME, 'game_dx12_ship_replay.exe'),
            '--oodle', os.path.join(GAME, 'oo2core_7_win64.dll'), '--test', '-o', out, zone_file]
    r = subprocess.run(args, capture_output=True, text=True, errors='replace')
    return [l for l in (r.stdout + r.stderr).splitlines() if l.startswith('Replay:')]


# The instances of the test, as the editor writes them. The numbers of each definition are the stock ones (research 86).
CHEVRON = dict(definition='hardpoint_chevron', server_instanced_parts=4, usable_parts=0, physics_limit=False,
               max_dynent_parts=5, instance_dynent=True, sparse_occluder=False, collision_flags=0, physics_dynents=0)
DOOR = dict(definition='scriptable_door_wooden_panel_03_painted_mp', server_instanced_parts=1, usable_parts=1,
            physics_limit=True, max_dynent_parts=2, instance_dynent=True, sparse_occluder=True, collision_flags=1,
            physics_dynents=0)
CONE = dict(definition='com_trafficcone_01', server_instanced_parts=1, usable_parts=0, physics_limit=False,
            max_dynent_parts=1, instance_dynent=True, sparse_occluder=False, collision_flags=0, physics_dynents=1)
CAR = dict(definition='veh8_civ_lnd_decho_rebel_darkblue', server_instanced_parts=29, usable_parts=0,
           physics_limit=False, max_dynent_parts=47, instance_dynent=False, sparse_occluder=False, collision_flags=0,
           physics_dynents=3)
INSTANCES = [
    dict(CHEVRON, origin=[64, 32, 16], angles=[0, 0, 0], targetname='hp_test', script_noteworthy='zone_a'),
    dict(DOOR, origin=[-96, 128, 8], angles=[0, 90, 0]),
    dict(CONE, origin=[10, -20, 4], angles=[1.5, 150.25, -0.5]),
    dict(CAR, origin=[200, 200, 0], angles=[0, 45, 0]),
    dict(CHEVRON, origin=[-64, -32, 16], angles=[0, 180, 0]),
    dict(DOOR, origin=[96, -128, 8], angles=[0, 360, 0], targetname='door_b'),
]


def main():
    args = sys.argv[1:]
    new = NEW
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

    print('1. a map without scriptables')
    code_a, _ = build(LIVE, extraction, map_id, os.path.join(work, 'live'), metadata)
    code_b, _ = build(new, extraction, map_id, os.path.join(work, 'new'), metadata)
    za, zb = zones(os.path.join(work, 'live'), map_id), zones(os.path.join(work, 'new'), map_id)
    check(code_a == 0 and code_b == 0, 'both converters finish (exit %d / %d)' % (code_a, code_b))
    check(len(za) > 0 and za.keys() == zb.keys(), 'same zone files: %s' % sorted(za))
    for name in sorted(za):
        check(za[name] == zb.get(name), '%s is byte-identical' % name)

    print('2. the same map with %d scriptable instances' % len(INSTANCES))
    copy = os.path.join(work, 'extraction')
    if os.path.isdir(copy):
        shutil.rmtree(copy)
    shutil.copytree(extraction, copy)
    shutil.copy(metadata, os.path.join(work, 'metadata.json'))
    col_path = os.path.join(copy, 'extracted', 'maps', 'mp', map_id + '.d3dbsp.replay-collision.json')
    col = json.load(open(col_path, encoding='utf-8'))
    col['scriptable_instances'] = INSTANCES
    json.dump(col, open(col_path, 'w', encoding='utf-8'))
    out = os.path.join(work, 'scr')
    code, log = build(new, copy, map_id, out, os.path.join(work, 'metadata.json'))
    check(code == 0, 'the converter finishes (exit %d)' % code)
    for line in log.splitlines():
        if 'scriptable' in line.lower():
            print('       ' + line.strip()[:200])
    if code != 0:
        print(log[-3000:])
        return 1
    zone = os.path.join(out, map_id, map_id + '.ff')
    replay = acts_test(zone, os.path.join(work, 'acts_test'))
    print('       ACTS loader test: %s' % replay)
    check(bool(replay) and all(' 0 failed' in l for l in replay), 'ACTS loads and tests the map zone with 0 failed')
    dump = os.path.join(work, 'acts')
    if os.path.isdir(dump):
        shutil.rmtree(dump)
    for kind in ('map_ents', 'dynentitylist', 'scriptable'):
        acts_export(zone, kind, dump)
    me_files = glob.glob(os.path.join(dump, '**', 'map_ents', '**', '*.asset.json'), recursive=True)
    dl_files = glob.glob(os.path.join(dump, '**', 'dynentitylist', '**', '*.asset.json'), recursive=True)
    check(len(me_files) == 1 and len(dl_files) == 1, 'ACTS exports one map_ents and one dynentitylist')
    if len(me_files) != 1 or len(dl_files) != 1:
        return 1
    me = json.load(open(me_files[0], encoding='utf-8'))['asset']['fields']
    dl = json.load(open(dl_files[0], encoding='utf-8'))['asset']['fields']
    sm = me['scriptableMapEnts']
    n = len(INSTANCES)
    server = sum(1 for i in INSTANCES if i['server_instanced_parts'])
    fixed = sum(i['server_instanced_parts'] for i in INSTANCES)
    usable = sum(i['usable_parts'] for i in INSTANCES)
    limits = sum(1 for i in INSTANCES if i['physics_limit'])
    head = {k: v for k, v in sm.items() if not isinstance(v, (dict, list))}
    print('       map-wide:', head)
    check(sm['mapInstanceCount'] == n and sm['totalInstanceCount'] == 0 and sm['lootInstanceCount'] == 0, 'mapInstanceCount %d' % n)
    check(sm['serverInstanceCount'] == server, 'serverInstanceCount %d' % server)
    check(sm['serverFixedPartCount'] == fixed, 'serverFixedPartCount %d' % fixed)
    check(sm['serverUsablePartCount'] == usable, 'serverUsablePartCount %d' % usable)
    check(set(sm['physicsLimits'].values()) == {limits}, 'every physics limit is %d' % limits)
    values = sm['mapInstances']['values']
    check(len(values) == n, 'ACTS reads %d instance records' % n)
    strings = {}
    for index, (want, got) in enumerate(zip(INSTANCES, values)):
        d = got['initialDef']
        name = str(d.get('name') if isinstance(d, dict) else d)
        ok = name.lstrip(',') == want['definition'] and isinstance(d, dict) and d.get('asset_reference') is True
        ok = ok and all(abs(a - b) < 1e-4 for a, b in zip(got['initialOrigin']['v'], want['origin']))
        ok = ok and all(abs(a - b) < 1e-4 for a, b in zip(got['initialAngles']['v'], want['angles']))
        ok = ok and got['classname']['value'] != 0 and got['flags'] == 0
        ok = ok and (got['targetname']['value'] != 0) == bool(want.get('targetname'))
        ok = ok and (got['script_noteworthy']['value'] != 0) == bool(want.get('script_noteworthy'))
        strings.setdefault(want['definition'], set()).add(got['classname']['value'])
        check(ok, 'instance %d: %s at %s (definition read as "%s")' % (index, want['definition'], want['origin'], name))
    check(all(len(v) == 1 for v in strings.values()) and len({next(iter(v)) for v in strings.values()}) == len(strings),
          'one class-name string per definition: %s' % {k: sorted(v) for k, v in strings.items()})
    tree = sm['spatialPopulationTree']['values'][0]
    check(tree['partitionCount'] == 1, 'the scriptable tree is one leaf')

    entries = dl['dynEntDefList'][0]['values'] if dl['dynEntDefList'][0] else []
    want_entries = []
    for index, i in enumerate(INSTANCES):
        if i['instance_dynent']:
            want_entries.append(('SCRIPTABLEINST', index, 0, i['max_dynent_parts'], 3 if i['sparse_occluder'] else 0, False, i['collision_flags']))
        for sub in range(i['physics_dynents']):
            want_entries.append(('SCRIPTABLEPHYSICS', index, sub, i['max_dynent_parts'], 255, True, 0))
    got_entries = [(e['type']['name'][12:], e['scriptableMapIndex'], e['scriptableSubIndex'], e['___u15']['scriptableMaxPartCount'],
                    e['priority']['value'], e['spawnActive'], e['collisionFlags']) for e in entries]
    clutter = [e for e in got_entries if e[0].startswith('CLUTTER')]
    scr_entries = [e for e in got_entries if not e[0].startswith('CLUTTER')]
    if clutter:
        print('       the map also has %d clutter entries (they come first)' % len(clutter))
    check(scr_entries == want_entries and got_entries[:len(clutter)] == clutter, 'dynamic entities: %d entries in stock order (type, instance, sub, parts, priority, active, flags)' % len(want_entries))
    if got_entries != want_entries:
        print('       got ', got_entries)
        print('       want', want_entries)
    check(all(e['baseModel'] is None and e['clientId'] == [0, 0] and e['basis']['value'] == 0 for e in entries if not e['type']['name'].endswith(('CLUTTER', 'NOSHADOW'))), 'entries have no model, client id 0 / 0, model basis')
    # chains
    def walk(head):
        seen = []
        node = head
        while node != 0xFFFFFFFF and len(seen) <= len(entries):
            seen.append(node)
            node = entries[node]['spatialNode']['spatialNodeNext']
        return seen
    phys = [k for k, e in enumerate(got_entries) if e[0] == 'SCRIPTABLEPHYSICS']
    radial = [k for k, e in enumerate(got_entries) if (e[0] == 'SCRIPTABLEINST' and e[4] == 0) or e[0].startswith('CLUTTER')]
    occl = [k for k, e in enumerate(got_entries) if e[0] == 'SCRIPTABLEINST' and e[4] == 3]
    check(walk(dl['dynEntNoSpatialList']['spatialNodeNext']) == phys[::-1] and dl['dynEntNoSpatialListTail']['spatialNodeNext'] == phys[0],
          'no-spatial list links the physics entries %s' % phys)
    pops = [p['values'][0] for p in dl['dynEntSpatialPopulation']]
    for label, pop, want in (('radial', pops[0], radial), ('sparse occluder', pops[1], occl)):
        ok = pop['partitionCount'] == 1 and pop['currentPopulation'] == len(want)
        ok = ok and walk(pop['buckets']['values'][0]['spatialNodeNext']) == want[::-1]
        ok = ok and pop['bucketTails']['values'][0]['spatialNodeNext'] == want[0] and pop['partitionIds']['values'] == [0]
        check(ok, '%s population links %s' % (label, want))
    parts = [e[3] for e in got_entries]
    check(me['dynEntCount'] == [len(entries), 0] and me['dynEntCountTotal'] == len(entries), 'MapEnts dynEntCount %d' % len(entries))
    check(me['dynEntNoSpatialCount'] == [len(phys), 0], 'MapEnts dynEntNoSpatialCount %d' % len(phys))
    check(me['dynEntMaxPosePartCount'] == max(parts), 'MapEnts dynEntMaxPosePartCount %d' % max(parts))
    check(me['dynEntsWithExtraPosePartsCount'] == sum(1 for p in parts if p > 1), 'MapEnts dynEntsWithExtraPosePartsCount %d' % sum(1 for p in parts if p > 1))
    mp = [p['values'][0] for p in me['dynEntSpatialPopulation']]
    ext = mp[1]['tree']['values'][0]['extents']['values'][0]
    print('       sparse-occluder leaf extents', [round(x, 1) for x in ext['mins']['v']], [round(x, 1) for x in ext['maxs']['v']])
    e0 = entries[0]['initialPose']
    print('       entry 0 pose quat', [round(x, 4) for x in e0['quat']['v']])
    print('RESULT: %s (%d failed)' % ('PASS' if not failures else 'FAIL', len(failures)))
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
