"""Offline trial of carrying a stock scriptable definition into a map (docs/research/86, step F3).

Adds instances whose definition lives in a stock map zone to a copy of an extraction, builds it with the given
converter, then checks with ACTS what the built zones hold.

Usage: py -3 -I tools/scriptable_carry_test.py <extraction> <map id> <work folder> [--exe <zonetool.exe>]
           [--def <definition>:<stock zone> ...]
Nothing is written outside the work folder. The game folder is only read."""
import csv, glob, json, os, shutil, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
NEW = r'C:\zsw-iw8r\scriptables\build\bin\x64\Release\zonetool.exe'
GAME = r'E:\IW8\Builds\1.20-replay'
ACTS = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'bin', 'release', 'iw8r-acts.exe')
GSC = r'C:\Users\user\Downloads\iw8 level maker\gsc-tool.exe'


def catalogue():
    rows = {}
    with open(os.path.join(ROOT, 'data', 'asset_catalog', 'iw8_scriptables.tsv'), encoding='utf-8') as f:
        lines = [l for l in f if not l.startswith('#')]
    for row in csv.DictReader(lines, delimiter='\t'):
        rows[row['name']] = row
    return rows


def instance(row, zone, origin, angles):
    out = dict(definition=row['name'], origin=origin, angles=angles,
               server_instanced_parts=int(row['server_instanced_parts']), usable_parts=int(row['usable_parts']),
               physics_limit=row['physics_limit'] == '1', max_dynent_parts=int(row['max_dynent_parts']),
               instance_dynent=row['instance_dynent'] == '1', sparse_occluder=row['sparse_occluder'] == '1',
               collision_flags=int(row['collision_flags']), physics_dynents=int(row['physics_dynents']))
    if zone:
        out['carry_zone'] = zone
    return out


def acts(args_tail):
    args = [ACTS, '--noUpdater', 'fastfile', '-r', 'mw19replay', '-g', os.path.join(GAME, 'game_dx12_ship_replay.exe'),
            '--oodle', os.path.join(GAME, 'oo2core_7_win64.dll')] + args_tail
    r = subprocess.run(args, capture_output=True, text=True, errors='replace')
    return (r.stdout + r.stderr).splitlines()


def strip_addresses(node):
    """Drops what differs between any two zones: load addresses, and script-string indices (a field whose value is
    only {"value": <index into the zone's own string list>})."""
    if isinstance(node, dict):
        if node.get('type') == 'scr_string_t' and isinstance(node.get('values'), list):
            return {'type': 'scr_string_t', 'count': node.get('count'), 'set': [bool(v.get('value')) for v in node['values']]}
        out = {}
        for k, v in node.items():
            if k == 'address' or (k == 'bytes' and 'active_member' in node):
                continue  # a union's raw bytes hold pointers
            if isinstance(v, dict) and set(v.keys()) == {'value'} and isinstance(v['value'], int) and (k.startswith('scr') or k.endswith('Name') or k.endswith('name')):
                out[k] = 'script string' if v['value'] else 'none'
                continue
            out[k] = strip_addresses(v)
        return out
    if isinstance(node, list):
        return [strip_addresses(v) for v in node]
    return node


def main():
    args = sys.argv[1:]
    exe = NEW
    wanted = []
    while '--exe' in args:
        i = args.index('--exe'); exe = args[i + 1]; del args[i:i + 2]
    while '--def' in args:
        i = args.index('--def'); wanted.append(tuple(args[i + 1].split(':'))); del args[i:i + 2]
    if not wanted:
        wanted = [('scriptable_door_wooden_panel_03_painted_mp', 'mp_backlot2')]
    extraction, map_id, work = os.path.abspath(args[0]), args[1], os.path.abspath(args[2])
    cat = catalogue()
    os.makedirs(work, exist_ok=True)
    copy = os.path.join(work, 'extraction')
    shutil.rmtree(copy, ignore_errors=True)
    shutil.copytree(extraction, copy)
    metadata = os.path.join(work, 'metadata.json')
    shutil.copy(os.path.join(os.path.dirname(extraction), 'metadata.json'), metadata)
    col_path = os.path.join(copy, 'extracted', 'maps', 'mp', map_id + '.d3dbsp.replay-collision.json')
    col = json.load(open(col_path, encoding='utf-8'))
    instances = [instance(cat['hardpoint_chevron'], '', [64, 32, 16], [0, 0, 0])]
    for n, (name, zone) in enumerate(wanted):
        instances.append(instance(cat[name], zone, [-96 + 96 * n, 128, 8], [0, 90, 0]))
        instances.append(instance(cat[name], zone, [-96 + 96 * n, -128, 8], [0, 270, 0]))
    col['scriptable_instances'] = instances
    json.dump(col, open(col_path, 'w', encoding='utf-8'))
    out = os.path.join(work, 'out')
    shutil.rmtree(out, ignore_errors=True)
    os.makedirs(out)
    cmd = [exe, '-game', 'iw8', '-path', GAME, 'buildextraction', copy, '--map', map_id, '--replay',
           os.path.join(GAME, 'game_dx12_ship_replay.exe'), '--output', out, '--metadata', metadata, '--acts', ACTS,
           '--gsc_tool', GSC, '--iw3_mode_assets', 'off']
    r = subprocess.run(cmd, capture_output=True, text=True, errors='replace')
    log = r.stdout + r.stderr
    open(os.path.join(work, 'zonetool.log'), 'w', encoding='utf-8').write(log)
    print('converter exit', r.returncode)
    for line in log.splitlines():
        if 'scriptable' in line.lower() or '[err' in line.lower() or 'error' in line.lower():
            print('   ', line.strip()[:260])
    if r.returncode != 0:
        print(log[-2500:])
        return 1
    failures = 0
    for zone in (map_id, 'techsets_' + map_id):
        path = os.path.join(out, map_id, zone + '.ff')
        lines = [l for l in acts(['--test', '-o', os.path.join(work, 'acts_test_' + zone), path]) if l.startswith('Replay:')]
        print('ACTS loader test %s (%.1f MB): %s' % (zone, os.path.getsize(path) / 1e6, lines))
        if not lines or any(' 0 failed' not in l for l in lines):
            failures += 1
    dump = os.path.join(work, 'acts')
    shutil.rmtree(dump, ignore_errors=True)
    zone_path = os.path.join(out, map_id, map_id + '.ff')
    for kind in ('map_ents', 'scriptable', 'xmodel', 'physicsasset'):
        acts(['-a', kind, '-o', dump, zone_path])
    me = glob.glob(os.path.join(dump, '**', 'map_ents', '**', '*.asset.json'), recursive=True)
    if me:
        sm = json.load(open(me[0], encoding='utf-8'))['asset']['fields']['scriptableMapEnts']
        for i in sm['mapInstances']['values']:
            d = i['initialDef']
            print('   instance definition:', d.get('name') if isinstance(d, dict) else d, '| reference only:', isinstance(d, dict) and d.get('asset_reference'))
    for name, zone in wanted:
        ours = glob.glob(os.path.join(dump, '**', 'scriptable', '**', name + '.*.asset.json'), recursive=True)
        stock = glob.glob(os.path.join(ROOT, 'out', 'scriptable_study', zone, '**', 'scriptable', '**', name + '.*.asset.json'), recursive=True)
        if not ours:
            print('   FAIL: the built zone does not hold the definition', name)
            failures += 1
            continue
        a = strip_addresses(json.load(open(ours[0], encoding='utf-8'))['asset']['fields'])
        if stock:
            b = strip_addresses(json.load(open(stock[0], encoding='utf-8'))['asset']['fields'])
            same = json.dumps(a, sort_keys=True) == json.dumps(b, sort_keys=True)
            print('   definition %s read back from the built zone %s the stock one (%d bytes of JSON)' % (
                name, 'EQUALS' if same else 'DIFFERS FROM', len(json.dumps(a))))
            if not same:
                failures += 1
                sa, sb = json.dumps(a, sort_keys=True), json.dumps(b, sort_keys=True)
                at = next((k for k in range(min(len(sa), len(sb))) if sa[k] != sb[k]), min(len(sa), len(sb)))
                print('      first difference at', at, '| ours:', sa[max(0, at - 120):at + 120])
                print('      stock:', sb[max(0, at - 120):at + 120])
    models = sorted(os.path.basename(p).split('.')[0] for p in glob.glob(os.path.join(dump, '**', 'xmodel', '**', '*.asset.json'), recursive=True))
    print('   models in the built zone: %d; door-like: %s' % (len(models), [m for m in models if 'door' in m][:8]))
    phys = glob.glob(os.path.join(dump, '**', 'physicsasset', '**', '*.asset.json'), recursive=True)
    print('   physics assets in the built zone:', len(phys))
    print('RESULT:', 'PASS' if not failures else 'FAIL (%d)' % failures)
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
