"""Offline proof of loot point editing in the converter's editzone (docs/research/89_warzone_work.md).

1. The stock loot points of a Warzone map, passed back unchanged, must give the stock loot block again (points,
   cluster point lists, zone point and cluster lists, counts).
2. Points deleted, moved and added: the lists follow, the scriptable counts that include the loot count move by the
   difference, and ACTS loads the zone.

Usage: py -3 -I tools/warzone_loot_edit_test.py <work folder> [--zone mp_br_quarry] [--exe <zonetool.exe>]"""
import glob, json, os, shutil, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import stock_edit_test as t
import warzone_loot_rules as rules


def loot_of(path):
    f = t.fields(path)
    loot = f['spawnGroupLoot']
    pts = rules.array(loot.get('points'), '<6f2H', 28)
    points = []
    for p in pts:
        if isinstance(p, dict):
            points.append((tuple(rules.vec(p['origin'])), tuple(rules.vec(p['angles'])), p['cluster'], p['zone']))
        else:
            points.append((tuple(p[0:3]), tuple(p[3:6]), p[6], p[7]))
    clusters = [(c['zone'], rules.u16s(c.get('points')), rules.bounds_of(c['bounds'])) for c in (loot.get('clusters') or {}).get('values') or []]
    zones = [(z['name']['value'] != 0, rules.u16s(z.get('points')), rules.u16s(z.get('clusters')), rules.bounds_of(z['bounds']))
             for z in (loot.get('zones') or {}).get('values') or []]
    sm = f['scriptableMapEnts']
    counts = {k: sm[k] for k in ('lootInstanceCount', 'serverInstanceCount', 'serverFixedPartCount', 'serverUsablePartCount', 'mapInstanceCount')}
    return points, clusters, zones, counts


def as_json(points):
    return [{'origin': list(p[0]), 'angles': list(p[1]), 'cluster': p[2], 'zone': p[3]} for p in points]


def main():
    args = sys.argv[1:]
    zone, exe = 'mp_br_quarry', t.EXE
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
    print('stock %s: %d loot points, %d clusters, %d zones, counts %s' % (zone, len(points), len(clusters), len(zones), counts))

    print('1. the stock points passed back unchanged')
    code, log, rep = t.editzone(exe, zone, {'loot_points': as_json(points), 'patch': False}, os.path.join(work, 'a'))
    check(code == 0, 'editzone finishes (%s)' % rep)
    if code != 0:
        print(log[-2000:])
        return 1
    a_file = os.path.join(work, 'a', zone + '.ff')
    a_points, a_clusters, a_zones, a_counts = loot_of(t.export(a_file, 'map_ents', os.path.join(work, 'a_me'))[0])
    check(a_points == points, 'the %d points read back equal' % len(points))
    check([c[0] for c in a_clusters] == [c[0] for c in clusters] and [c[2] for c in a_clusters] == [c[2] for c in clusters],
          'every cluster keeps its zone and its box')
    diff = [i for i, (x, y) in enumerate(zip(clusters, a_clusters)) if x[1] != y[1]]
    check(not diff, 'every cluster point list equals the stock one (%d differ)' % len(diff))
    zdiff = [i for i, (x, y) in enumerate(zip(zones, a_zones)) if sorted(x[1]) != sorted(y[1])]
    check(not zdiff, 'every zone point list equals the stock one (%d differ)' % len(zdiff))
    cdiff = [i for i, (x, y) in enumerate(zip(zones, a_zones)) if sorted(x[2]) != sorted(y[2])]
    check(not cdiff, 'every zone cluster list equals the stock one (%d differ)' % len(cdiff))
    if cdiff:
        i = cdiff[0]
        print('       zone %d: stock clusters %s..., rebuilt %s...' % (i, sorted(zones[i][2])[:8], sorted(a_zones[i][2])[:8]))
    check([z[3] for z in a_zones] == [z[3] for z in zones] and [z[0] for z in a_zones] == [z[0] for z in zones], 'every zone keeps its box and its name')
    check(a_counts == counts, 'the scriptable counts are unchanged (%s)' % a_counts)
    test = [l for l in t.acts(['--test', '-o', os.path.join(work, 'a_test'), a_file]) if l.startswith('Replay:')]
    stock_test = [l for l in t.acts(['--test', '-o', os.path.join(work, 'stock_test'), stock_file]) if l.startswith('Replay:')]
    # ACTS counts list entries, and the stock zone holds a few names twice (a name stub and the definition); the
    # converter's own check (verifyzone with IW8_REWRITE=fresh) compares the asset sets. Here: the same per-asset
    # results as the stock zone (its failed and unavailable ones are ACTS limits on the stock zone too).
    def results(lines):
        return lines[0].split('loaded,', 1)[1].strip() if lines and 'loaded,' in lines[0] else None
    check(results(test) is not None and results(test) == results(stock_test), 'ACTS tests it like the stock zone: %s (stock: %s)' % (test, stock_test))

    print('2. ten points deleted, one moved, two added')
    edited = list(points[10:])
    p = edited[5]
    edited[5] = ((p[0][0] + 8.0, p[0][1], p[0][2]), p[1], p[2], p[3])
    new_json = as_json(edited)
    # One new point on top of an existing one (it lands in the same cluster box and zone box), one far outside.
    q = edited[20]
    new_json.append({'origin': list(q[0]), 'angles': [0, 0, 0], 'cluster': 'auto', 'zone': 'auto'})
    new_json.append({'origin': [900000.0, 900000.0, 0.0], 'angles': [0, 90, 0]})
    code, log, rep = t.editzone(exe, zone, {'loot_points': new_json}, os.path.join(work, 'b'))
    check(code == 0, 'editzone finishes (%s)' % rep)
    if code != 0:
        print(log[-2000:])
        return 1
    b_file = os.path.join(work, 'b', zone + '.ff')
    b_points, b_clusters, b_zones, b_counts = loot_of(t.export(b_file, 'map_ents', os.path.join(work, 'b_me'))[0])
    delta = len(new_json) - len(points)
    check(len(b_points) == len(new_json) and b_points[:len(edited)] == edited, '%d points read back, the kept ones in place and the moved one moved' % len(b_points))
    far = b_points[-1]
    check(far[2] == 0xFFFD and far[3] == 0, 'the far new point is a free point without a zone (cluster 0x%X, zone %d)' % (far[2], far[3]))
    near = b_points[-2]
    check(near[3] == q[3] or q[3] == 0 or near[3] != 0, 'the new point on an existing one got a zone by its position (zone %d; the existing point has %d)' % (near[3], q[3]))
    ok_lists = all(pl == [i for i, bp in enumerate(b_points) if bp[2] == ci] for ci, (_, pl, _) in enumerate(b_clusters))
    check(ok_lists, 'every cluster lists exactly the points that name it')
    ok_zone = all(sorted(pl) == [i for i, bp in enumerate(b_points) if bp[3] == zi + 1 and bp[2] != 0xFFFB] for zi, (_, pl, _, _) in enumerate(b_zones))
    check(ok_zone, 'every zone lists exactly the points that name it (loot cache points are not listed, as in stock)')
    want = {k: (v + delta if k != 'mapInstanceCount' else v) for k, v in counts.items()}
    check(b_counts == want, 'the loot count and the three server counts moved by %+d (%s)' % (delta, b_counts))
    test = [l for l in t.acts(['--test', '-o', os.path.join(work, 'b_test'), b_file]) if l.startswith('Replay:')]
    check(results(test) is not None and results(test) == results(stock_test), 'ACTS tests the edited zone like the stock zone: %s' % test)
    patch = os.path.join(work, 'b', 'patch', zone + '.fp')
    check(os.path.exists(patch), 'the install patch was written (%.2f MB)' % (os.path.getsize(patch) / 1e6 if os.path.exists(patch) else 0))
    for name in ('a', 'b'):
        for f in glob.glob(os.path.join(work, name, '*.ff')):
            os.remove(f)  # the rebuilt zones are large; the reports and exports stay
    print('RESULT: %s (%d failed)' % ('PASS' if not failures else 'FAIL', len(failures)))
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
