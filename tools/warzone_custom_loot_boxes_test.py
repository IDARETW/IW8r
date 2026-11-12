"""Offline proof of loot clusters and zones on a custom map (docs/research/89_warzone_work.md section 6d).

The extraction's collision JSON gets loot points and "loot_boxes" (two named zones, two clusters, one cluster in each
zone); the converter must give every point the first cluster and zone whose box holds it, list the points of each
cluster and zone (a zone without its cache points), give each cluster the zone of its points, list each zone's
clusters, name the zones, raise the loot counts, and ACTS must load the zone with 0 failed.

Usage: py -3 -I tools/warzone_custom_loot_boxes_test.py <extraction folder> <map id> <work folder> [--new <zonetool.exe>]
Nothing is written outside the work folder. The game folder is only read."""
import glob, json, os, shutil, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import scriptable_offline_test as t
import warzone_loot_rules as rules
from warzone_custom_loot_test import map_ents

ZONES = [{'name': 'map_quarry', 'mid': [0, 0, 0], 'half': [512, 512, 256]},
         {'name': 'map_hospital', 'mid': [2048, 0, 0], 'half': [512, 512, 256]}]
CLUSTERS = [{'mid': [0, 0, 0], 'half': [128, 128, 64]},
            {'mid': [2048, 0, 0], 'half': [128, 128, 64]}]
POINTS = [
    {'origin': [10, 10, 0]},                    # cluster 0, zone 1
    {'origin': [300, 0, 0]},                    # zone 1 only (free)
    {'origin': [2050, 20, 0]},                  # cluster 1, zone 2
    {'origin': [-20, 30, 0], 'cache': True},    # cache point in zone 1 (not listed by the zone)
    {'origin': [9000, 9000, 0]},                # outside everything
    {'origin': [2000, -50, 10]},                # cluster 1, zone 2
]
# (cluster, zone) wanted per point
WANT = [(0, 1), (0xFFFD, 1), (1, 2), (0xFFFB, 1), (0xFFFD, 0), (1, 2)]


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

    copy = os.path.join(work, 'extraction')
    if os.path.isdir(copy):
        shutil.rmtree(copy)
    shutil.copytree(extraction, copy)
    shutil.copy(metadata, os.path.join(work, 'metadata.json'))
    col_path = os.path.join(copy, 'extracted', 'maps', 'mp', map_id + '.d3dbsp.replay-collision.json')
    col = json.load(open(col_path, encoding='utf-8'))
    col['loot_points'] = [{'origin': p['origin'], 'angles': [0, 0, 0], 'cache': p.get('cache', False)} for p in POINTS]
    col['loot_boxes'] = {'clusters': CLUSTERS, 'zones': ZONES}
    json.dump(col, open(col_path, 'w', encoding='utf-8'))
    out = os.path.join(work, 'boxes')
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
    me = map_ents(zone, os.path.join(work, 'acts_me'))
    check(me is not None, 'ACTS exports the map_ents')
    if me is None:
        return 1
    loot = me['spawnGroupLoot']
    raw = rules.array(loot.get('points'), '<6f2H', 28)
    got = [(p['cluster'], p['zone']) if isinstance(p, dict) else (p[6], p[7]) for p in raw]
    check(got == WANT, 'every point has the first cluster and zone whose box holds it: %s' % [(hex(c), z) for c, z in got])
    clusters = (loot.get('clusters') or {}).get('values') or []
    zones = (loot.get('zones') or {}).get('values') or []
    check(loot['clusterCount'] == 2 and len(clusters) == 2 and loot['zoneCount'] == 2 and len(zones) == 2, 'two clusters and two zones')
    if len(clusters) == 2:
        check(rules.u16s(clusters[0].get('points')) == [0] and rules.u16s(clusters[1].get('points')) == [2, 5],
              'cluster 0 lists point 0, cluster 1 lists points 2 and 5')
        check(clusters[0]['zone'] == 1 and clusters[1]['zone'] == 2, 'each cluster has the zone of its points (%s, %s)' % (clusters[0]['zone'], clusters[1]['zone']))
        mid, half = rules.bounds_of(clusters[1]['bounds'])
        check(list(mid) == [2048, 0, 0] and list(half) == [128, 128, 64], 'cluster 1 keeps its box %s %s' % (mid, half))
    if len(zones) == 2:
        tables = glob.glob(os.path.join(work, 'acts_me', '**', 'script_strings.json'), recursive=True)
        strings = json.load(open(tables[0], encoding='utf-8')) if tables else []
        names = [strings[z['name']['value']] if z['name']['value'] < len(strings) else None for z in zones]
        check(names == ['map_quarry', 'map_hospital'], 'the zones are named %s' % names)
        check(rules.u16s(zones[0].get('points')) == [0, 1] and rules.u16s(zones[1].get('points')) == [2, 5],
              'zone 1 lists points 0 and 1 (not the cache point 3), zone 2 lists points 2 and 5')
        check(rules.u16s(zones[0].get('clusters')) == [0] and rules.u16s(zones[1].get('clusters')) == [1], 'each zone lists its cluster')
    sm = me['scriptableMapEnts']
    check(sm['lootInstanceCount'] == len(POINTS), 'lootInstanceCount is %d' % sm['lootInstanceCount'])
    print('RESULT: %s (%d failed)' % ('PASS' if not failures else 'FAIL', len(failures)))
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
