"""Checks the rules by which a stock map's loot block (MapEnts.spawnGroupLoot) follows from its points alone
(docs/research/87_warzone_survey.md 2.4, research 89). Read-only on the exports under out/scriptable_study.

For each map: rebuilds the clusters and zones from the points (cluster and zone index per point) and compares with
the stored arrays: point lists, the zone of each cluster, cluster lists of each zone, and the bounds.

Usage: py -3 -I tools/warzone_loot_rules.py [zone ...]"""
import glob, json, os, struct, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def find(zone):
    hits = glob.glob(os.path.join(ROOT, 'out', 'scriptable_study', zone, '**', 'map_ents', '**', '*.asset.json'), recursive=True)
    return hits[0] if hits else None


def array(node, fmt, size):
    """An ACTS array node as tuples: decoded values, or raw bytes unpacked with fmt."""
    if node is None:
        return []
    if 'values' in node:
        return node['values']
    raw = bytes.fromhex(node.get('bytes', ''))
    return [struct.unpack_from(fmt, raw, i * size) for i in range(len(raw) // size)]


def u16s(node):
    if node is None:
        return []
    if 'values' in node:
        return [v if isinstance(v, int) else v.get('value', v) for v in node['values']]
    raw = bytes.fromhex(node.get('bytes', ''))
    return list(struct.unpack('<%dH' % (len(raw) // 2), raw))


def vec(v):
    return tuple(v['v']) if isinstance(v, dict) else tuple(v)


def bounds_of(b):
    """ACTS Bounds node -> (midPoint, halfSize)."""
    return vec(b['midPoint']), vec(b['halfSize'])


def main():
    zones = sys.argv[1:] or ['mp_br_tut', 'mp_bm_tut', 'mp_backlot2', 'mp_br_quarry', 'mp_donetsk']
    for zone in zones:
        path = find(zone)
        if not path:
            print(zone, ': no map_ents export'); continue
        loot = json.load(open(path, encoding='utf-8'))['asset']['fields']['spawnGroupLoot']
        pts = array(loot.get('points'), '<6f2H', 28)
        points = []
        for p in pts:
            if isinstance(p, dict):
                points.append((vec(p['origin']), vec(p['angles']), p['cluster'], p['zone']))
            else:
                points.append((p[0:3], p[3:6], p[6], p[7]))
        clusters = (loot.get('clusters') or {}).get('values') or []
        zlist = (loot.get('zones') or {}).get('values') or []
        print('%s: %d points, %d clusters, %d zones' % (zone, len(points), len(clusters), len(zlist)))
        if not points:
            continue
        bad = {'cluster points': 0, 'cluster zone': 0, 'cluster bounds': 0, 'zone points': 0, 'zone clusters': 0, 'zone bounds': 0}
        examples = []

        def box(indices):
            if not indices:
                return None
            lo = [min(points[i][0][a] for i in indices) for a in range(3)]
            hi = [max(points[i][0][a] for i in indices) for a in range(3)]
            return tuple((lo[a] + hi[a]) / 2 for a in range(3)), tuple((hi[a] - lo[a]) / 2 for a in range(3))

        def near(a, b, tol=0.01):
            return all(abs(x - y) <= tol for x, y in zip(a, b))

        by_cluster, by_zone = {}, {}
        for i, p in enumerate(points):
            by_cluster.setdefault(p[2], []).append(i)
            if p[3]:
                by_zone.setdefault(p[3] - 1, []).append(i)
        for ci, c in enumerate(clusters):
            got = u16s(c.get('points'))
            want = by_cluster.get(ci, [])
            if sorted(got) != want:
                bad['cluster points'] += 1
            elif got != want and len(examples) < 2:
                examples.append('cluster %d lists its points in another order than ascending' % ci)
            zones_of = {points[i][3] for i in want}
            if want and (len(zones_of) != 1 or c['zone'] not in zones_of):
                bad['cluster zone'] += 1
                if len(examples) < 4:
                    examples.append('cluster %d zone field %s, its points have zone %s' % (ci, c['zone'], sorted(zones_of)))
            b = box(want)
            mid, half = bounds_of(c['bounds'])
            if b and not (near(b[0], mid) and near(b[1], half)):
                bad['cluster bounds'] += 1
                if len(examples) < 6:
                    examples.append('cluster %d bounds mid %s half %s, points box mid %s half %s' % (ci, [round(x, 2) for x in mid], [round(x, 2) for x in half], [round(x, 2) for x in b[0]], [round(x, 2) for x in b[1]]))
            if not want and len(examples) < 7 and (any(abs(x) > 0.01 for x in mid) or any(abs(x) > 0.01 for x in half)):
                examples.append('empty cluster %d has bounds mid %s half %s' % (ci, mid, half))
        for zi, z in enumerate(zlist):
            got = u16s(z.get('points'))
            want = by_zone.get(zi, [])
            if sorted(got) != want:
                bad['zone points'] += 1
            gotc = u16s(z.get('clusters'))
            wantc = sorted(ci for ci, c in enumerate(clusters) if by_cluster.get(ci) and points[by_cluster[ci][0]][3] == zi + 1)
            if sorted(gotc) != wantc:
                bad['zone clusters'] += 1
                if len(examples) < 9:
                    examples.append('zone %d lists clusters %s..., expected %s...' % (zi, sorted(gotc)[:6], wantc[:6]))
            b = box(want)
            mid, half = bounds_of(z['bounds'])
            if b and not (near(b[0], mid) and near(b[1], half)):
                bad['zone bounds'] += 1
                if len(examples) < 11:
                    examples.append('zone %d bounds mid %s half %s, points box mid %s half %s' % (zi, [round(x, 2) for x in mid], [round(x, 2) for x in half], [round(x, 2) for x in b[0]], [round(x, 2) for x in b[1]]))
        special = sorted({p[2] for p in points if p[2] >= len(clusters)})
        print('   mismatches:', bad, ' special cluster values:', [hex(x) for x in special])
        for e in examples:
            print('   ', e)


if __name__ == '__main__':
    main()
