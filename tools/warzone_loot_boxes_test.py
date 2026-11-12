"""Offline proof of new loot clusters and zones on a stock Warzone map (docs/research/89_warzone_work.md section 9).

editzone with "loot_boxes" (one new zone, one new cluster inside it) and "loot_points" (the stock points plus three
new free points inside the new cluster and one cache point inside the new zone, all "auto"):
- the stock clusters and zones keep their boxes, zones of clusters and point lists;
- the new cluster takes the new zone, lists the three new points; the new zone has its name, lists the three free
  points (not the cache point) and the new cluster;
- the loot count and the three server counts move by four; ACTS tests the zone like the stock zone.

Usage: py -3 -I tools/warzone_loot_boxes_test.py <work folder> [--zone mp_br_tut] [--exe <zonetool.exe>]
Nothing is written outside the work folder; the game folder is only read."""
import glob, os, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import stock_edit_test as t
from warzone_loot_edit_test import loot_of, as_json


def main():
    args = sys.argv[1:]
    zone, exe = 'mp_br_tut', t.EXE
    while '--zone' in args:
        i = args.index('--zone'); zone = args[i + 1]; del args[i:i + 2]
    while '--exe' in args:
        i = args.index('--exe'); exe = args[i + 1]; del args[i:i + 2]
    work = os.path.abspath(args[0])
    os.makedirs(work, exist_ok=True)
    failures = []

    def check(ok, text):
        print(('  ok   ' if ok else '  FAIL ') + text)
        if not ok:
            failures.append(text)

    stock_file = os.path.join(t.GAME, 'zone', zone + '.ff')
    stock_export = t.export(stock_file, 'map_ents', os.path.join(work, 'stock_me'))[0]
    points, clusters, zones, counts = loot_of(stock_export)
    strings = t.strings_of(stock_export)
    stock_names = [strings[z['name']['value']] for z in (t.fields(stock_export)['spawnGroupLoot'].get('zones') or {}).get('values') or []]
    print('stock %s: %d loot points, %d clusters, %d zones %s' % (zone, len(points), len(clusters), len(zones), stock_names[:4]))

    # A place outside every stock box (the new boxes must not start inside a stock one: "first box" would win).
    far = (60000.0, 60000.0, 0.0)
    name = stock_names[0] if stock_names else 'map_quarry'  # a name the map's loot tables know
    edits = {
        'loot_boxes': {
            'zones': [{'name': name, 'mid': [far[0], far[1], far[2]], 'half': [1024, 1024, 256]}],
            'clusters': [{'mid': [far[0], far[1], far[2]], 'half': [128, 128, 64]}],
        },
        'loot_points': as_json(points) + [
            {'origin': [far[0] + dx, far[1] + dy, far[2]], 'angles': [0, 45, 0], 'cluster': 'auto', 'zone': 'auto'}
            for dx, dy in ((0, 0), (64, 0), (0, -64))
        ] + [{'origin': [far[0] + 512, far[1], far[2]], 'angles': [0, 0, 0], 'cluster': 'cache', 'zone': 'auto'}],
    }
    code, log, rep = t.editzone(exe, zone, edits, os.path.join(work, 'boxes'))
    check(code == 0 and rep.get('loot_clusters_added') == 1 and rep.get('loot_zones_added') == 1,
          'editzone finishes and reports one cluster and one zone added (%s)' % {k: rep.get(k) for k in ('loot_clusters_added', 'loot_zones_added', 'loot_points')})
    if code != 0:
        print(log[-2000:])
        return 1
    out_file = os.path.join(work, 'boxes', zone + '.ff')
    export = t.export(out_file, 'map_ents', os.path.join(work, 'boxes_me'))[0]
    b_points, b_clusters, b_zones, b_counts = loot_of(export)
    nc, nz = len(clusters), len(zones)  # index of the new cluster, 0-based index of the new zone
    check(len(b_clusters) == nc + 1 and len(b_zones) == nz + 1, '%d clusters and %d zones (one more each)' % (len(b_clusters), len(b_zones)))
    check(b_points[:len(points)] == points, 'the %d stock points read back unchanged' % len(points))
    check(all(b_clusters[i][0] == clusters[i][0] and b_clusters[i][1] == clusters[i][1] and b_clusters[i][2] == clusters[i][2] for i in range(nc)),
          'every stock cluster keeps its zone, point list and box')
    check(all(sorted(b_zones[i][1]) == sorted(zones[i][1]) and sorted(b_zones[i][2]) == sorted(zones[i][2]) and b_zones[i][3] == zones[i][3]
              for i in range(nz)), 'every stock zone keeps its point list, cluster list and box')
    new = list(range(len(points), len(points) + 3))
    cache = len(points) + 3
    check(all(b_points[i][2] == nc and b_points[i][3] == nz + 1 for i in new),
          'the three new points are in the new cluster %d and the new zone %d: %s' % (nc, nz + 1, [(b_points[i][2], b_points[i][3]) for i in new]))
    check(b_points[cache][2] == 0xFFFB and b_points[cache][3] == nz + 1, 'the new cache point names the new zone (cluster 0x%X, zone %d)' % (b_points[cache][2], b_points[cache][3]))
    if len(b_clusters) > nc:
        zone_field, plist, box = b_clusters[nc]
        check(zone_field == nz + 1 and plist == new, 'the new cluster has zone %d and lists %s' % (zone_field, plist))
        check(all(abs(a - b) < 0.01 for a, b in zip(box[0] + box[1], list(far) + [128, 128, 64])), 'the new cluster box reads back: %s' % (box,))
    if len(b_zones) > nz:
        named, plist, clist, box = b_zones[nz]
        bstrings = t.strings_of(export)
        zrec = t.fields(export)['spawnGroupLoot']['zones']['values'][nz]
        got_name = bstrings[zrec['name']['value']] if zrec['name']['value'] < len(bstrings) else None
        check(got_name == name, 'the new zone is named %s' % got_name)
        check(sorted(plist) == new and clist == [nc], 'the new zone lists the three free points %s (not the cache point) and the new cluster %s' % (plist, clist))
    want = {k: (v + 4 if k != 'mapInstanceCount' else v) for k, v in counts.items()}
    check(b_counts == want, 'the loot count and the three server counts rose by 4 (%s)' % b_counts)

    def results(lines):
        return lines[0].split('loaded,', 1)[1].strip() if lines and 'loaded,' in lines[0] else None
    test = [l for l in t.acts(['--test', '-o', os.path.join(work, 'boxes_test'), out_file]) if l.startswith('Replay:')]
    stock_test = [l for l in t.acts(['--test', '-o', os.path.join(work, 'stock_test'), stock_file]) if l.startswith('Replay:')]
    check(results(test) is not None and results(test) == results(stock_test), 'ACTS tests the edited zone like the stock zone: %s (stock %s)' % (test, stock_test))
    for f in glob.glob(os.path.join(work, 'boxes', '*.ff')):
        os.remove(f)
    print('RESULT: %s (%d failed)' % ('PASS' if not failures else 'FAIL', len(failures)))
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
