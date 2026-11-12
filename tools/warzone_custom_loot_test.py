"""Offline proof of loot points on a custom map (docs/research/89_warzone_work.md).

1. A map without loot points: the live and the new converter give byte-identical zones.
2. `loot_points` added to the extraction's collision JSON: the new converter writes them as MapEnts.spawnGroupLoot
   (no clusters, no zones), the loot count and the three server counts rise by the number of points, nothing else in
   map_ents changes, and ACTS loads the zone with 0 failed.

Usage: py -3 -I tools/warzone_custom_loot_test.py <extraction folder> <map id> <work folder> [--new <zonetool.exe>]
Nothing is written outside the work folder. The game folder is only read."""
import glob, json, os, shutil, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import scriptable_offline_test as t
import warzone_loot_rules as rules

POINTS = [
    {'origin': [64, 32, 0], 'angles': [0, 90, 0], 'cache': False},
    {'origin': [-64.5, 32, 0], 'angles': [0, 0, 0], 'cache': False},
    {'origin': [0, -96, 0.25], 'angles': [1.5, 180, -2], 'cache': True},
    {'origin': [128, 128, 0], 'cache': False},
    {'origin': [-128, -128, 0], 'angles': [0, 270, 0]},
]


def map_ents(zone, out):
    if os.path.isdir(out):
        shutil.rmtree(out)
    t.acts_export(zone, 'map_ents', out)
    files = glob.glob(os.path.join(out, '**', 'map_ents', '**', '*.asset.json'), recursive=True)
    return json.load(open(files[0], encoding='utf-8'))['asset']['fields'] if len(files) == 1 else None


def strip(node):
    """An export node without the addresses, which differ from run to run."""
    if isinstance(node, dict):
        return {k: strip(v) for k, v in node.items() if k != 'address'}
    if isinstance(node, list):
        return [strip(v) for v in node]
    return node


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

    print('1. a map without loot points')
    code_a, _ = t.build(t.LIVE, extraction, map_id, os.path.join(work, 'live'), metadata)
    code_b, _ = t.build(new, extraction, map_id, os.path.join(work, 'new'), metadata)
    za, zb = t.zones(os.path.join(work, 'live'), map_id), t.zones(os.path.join(work, 'new'), map_id)
    check(code_a == 0 and code_b == 0, 'both converters finish (exit %d / %d)' % (code_a, code_b))
    check(len(za) > 0 and za.keys() == zb.keys(), 'same zone files: %s' % sorted(za))
    for name in sorted(za):
        check(za[name] == zb.get(name), '%s is byte-identical' % name)
    base = map_ents(os.path.join(work, 'new', map_id, map_id + '.ff'), os.path.join(work, 'acts_base'))
    check(base is not None, 'ACTS exports the map_ents of the map without loot')
    if base is None:
        return 1

    print('2. the same map with %d loot points' % len(POINTS))
    copy = os.path.join(work, 'extraction')
    if os.path.isdir(copy):
        shutil.rmtree(copy)
    shutil.copytree(extraction, copy)
    shutil.copy(metadata, os.path.join(work, 'metadata.json'))
    col_path = os.path.join(copy, 'extracted', 'maps', 'mp', map_id + '.d3dbsp.replay-collision.json')
    col = json.load(open(col_path, encoding='utf-8'))
    col['loot_points'] = POINTS
    col['loot_zone'] = 'map_quarry'
    json.dump(col, open(col_path, 'w', encoding='utf-8'))
    out = os.path.join(work, 'loot')
    code, log = t.build(new, copy, map_id, out, os.path.join(work, 'metadata.json'))
    check(code == 0, 'the converter finishes (exit %d)' % code)
    for line in log.splitlines():
        if 'loot' in line.lower():
            print('       ' + line.strip()[:200])
    if code != 0:
        print(log[-3000:])
        return 1
    zone = os.path.join(out, map_id, map_id + '.ff')
    replay = t.acts_test(zone, os.path.join(work, 'acts_test'))
    check(bool(replay) and all(' 0 failed' in l for l in replay), 'ACTS loads and tests the map zone with 0 failed: %s' % replay)
    me = map_ents(zone, os.path.join(work, 'acts_loot'))
    check(me is not None, 'ACTS exports the map_ents')
    if me is None:
        return 1
    loot = me['spawnGroupLoot']
    raw = rules.array(loot.get('points'), '<6f2H', 28)
    got = [(tuple(rules.vec(p['origin'])), tuple(rules.vec(p['angles'])), p['cluster'], p['zone']) if isinstance(p, dict)
           else (tuple(p[0:3]), tuple(p[3:6]), p[6], p[7]) for p in raw]
    want = [(tuple(float(v) for v in p['origin']), tuple(float(v) for v in p.get('angles', [0, 0, 0])), 0xFFFB if p.get('cache') else 0xFFFD, 1)
            for p in POINTS]
    check(loot['pointCount'] == len(POINTS) and got == want, 'the %d points read back: origin, angles, free or cache, zone 1' % len(POINTS))
    check(loot['clusterCount'] == 0 and loot['clusters'] is None, 'no clusters')
    zones = (loot.get('zones') or {}).get('values') or []
    check(loot['zoneCount'] == 1 and len(zones) == 1, 'one loot zone')
    if zones:
        z = zones[0]
        tables = glob.glob(os.path.join(work, 'acts_loot', '**', 'script_strings.json'), recursive=True)
        strings = json.load(open(tables[0], encoding='utf-8')) if tables else []
        name = z['name']['value']
        listed = rules.u16s(z.get('points'))
        mid, half = rules.bounds_of(z['bounds'])
        inside = all(abs(p[0][a] - mid[a]) <= half[a] for p in got for a in range(3))
        check(name < len(strings) and strings[name] == 'map_quarry', 'named map_quarry (a zone of the stock loot tables): %s' % (strings[name] if name < len(strings) else name))
        check(listed == [i for i, p in enumerate(POINTS) if not p.get('cache')], 'it lists every point except the cache point: %s' % listed)
        check(inside and z['clusterCount'] == 0, 'its box holds every point')
    sm, sb = me['scriptableMapEnts'], base['scriptableMapEnts']
    n = len(POINTS)
    check(sm['lootInstanceCount'] == n and sb['lootInstanceCount'] == 0, 'lootInstanceCount is %d (0 without loot)' % sm['lootInstanceCount'])
    for key in ('serverInstanceCount', 'serverFixedPartCount', 'serverUsablePartCount'):
        check(sm[key] == sb[key] + n, '%s rose by %d (%d -> %d)' % (key, n, sb[key], sm[key]))
    check(sm['mapInstanceCount'] == sb['mapInstanceCount'] and sm['totalInstanceCount'] == sb['totalInstanceCount'] and
          sm['reservedInstanceCount'] == sb['reservedInstanceCount'], 'the map, total and reserved instance counts are unchanged')
    # Everything else of map_ents equals the map without loot.
    changed = {'lootInstanceCount', 'serverInstanceCount', 'serverFixedPartCount', 'serverUsablePartCount', 'ffMemCost'}
    a, b = strip(me), strip(base)
    other = sorted(k for k in a if k not in ('spawnGroupLoot', 'scriptableMapEnts') and a[k] != b.get(k))
    check(not other, 'every other field of map_ents equals the map without loot %s' % (other or ''))
    other_sm = sorted(k for k in a['scriptableMapEnts'] if k not in changed and a['scriptableMapEnts'][k] != b['scriptableMapEnts'].get(k))
    check(not other_sm, 'every other scriptable field equals the map without loot %s' % (other_sm or ''))
    print('       ffMemCost %s (without loot %s; stock maps 1240)' % (sm.get('ffMemCost'), sb.get('ffMemCost')))
    print('RESULT: %s (%d failed)' % ('PASS' if not failures else 'FAIL', len(failures)))
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
