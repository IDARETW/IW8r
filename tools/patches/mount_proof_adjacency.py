import io

# tools/mount_edges_build_test.py: the mount adjacency tables. The rule is written once more here, in Python, and must
# give exactly the stock tables of mp_m_speedball and exactly the built map's tables.

p = 'E:/IW8-Radiant/tools/mount_edges_build_test.py'
s = io.open(p, encoding='utf-8', newline='').read()


def rep(a, b):
    global s
    assert s.count(a) == 1, (s.count(a), a[:70])
    s = s.replace(a, b)


rep('''        out['indices'] = list((idx or {}).get('values', [])) if idx else []
    return out
''', '''        out['indices'] = list((idx or {}).get('values', [])) if idx else []
    # Mount adjacency: {(edge, neighbour): (start, end, adjStart, adjEnd)}.
    out['adjacency'] = {}
    am, ar = f.get('edgeAdjacencyMetadata'), f.get('edgeAdjacency')
    if am and ar and am.get('values') and ar.get('values'):
        for i, w in enumerate(am['values']):
            for k in range(w['mountCount']):
                r = ar['values'][w['startIndex'] + k]
                out['adjacency'][(i, r['adjEdge'])] = (half(r['start']), half(r['end']), half(r['adjStart']), half(r['adjEnd']))
    out['adjacencyWords'] = len(am['values']) if am and am.get('values') else 0
    return out


def predicted_adjacency(el):
    """The neighbours of the static mount edges by the rule of research 125 section 11: {(edge, neighbour): (start, end)}."""
    def dot(a, b):
        return sum(a[i] * b[i] for i in range(3))
    mount = [i for i, md in enumerate(el['meta']) if md[2] & 1 and el['edges'][i][0][3] == 0]
    info = {}
    for i in mount:
        d, adj, oth = normals(el['edges'][i], el['meta'][i])
        info[i] = (d, adj, oth, math.dist(el['edges'][i][0][:3], el['edges'][i][1][:3]))
    cells = {}
    for i in mount:
        a, b = el['edges'][i]
        steps = max(1, int(math.ceil(info[i][3] / 16.0)))
        for k in range(steps + 1):
            pnt = [a[c] + (b[c] - a[c]) * k / steps for c in range(3)]
            cells.setdefault(tuple(int(math.floor(x / 64.0)) for x in pnt), set()).add(i)
    out = {}
    for i in mount:
        a, b = el['edges'][i]
        di, ai, oi, li = info[i]
        near = set()
        steps = max(1, int(math.ceil(li / 16.0)))
        for k in range(steps + 1):
            pnt = [a[c] + (b[c] - a[c]) * k / steps for c in range(3)]
            lo = [int(math.floor((x - 16.0) / 64.0)) for x in pnt]
            hi = [int(math.floor((x + 16.0) / 64.0)) for x in pnt]
            for x in range(lo[0], hi[0] + 1):
                for y in range(lo[1], hi[1] + 1):
                    for z in range(lo[2], hi[2] + 1):
                        near |= cells.get((x, y, z), set())
        for j in near:
            if j == i:
                continue
            dj, aj, oj, lj = info[j]
            if abs(dot(di, dj)) < 0.999:
                continue
            off = [el['edges'][j][0][c] - a[c] for c in range(3)]
            along = dot(off, di)
            across = [off[c] - di[c] * along for c in range(3)]
            apart = math.sqrt(dot(across, across))
            if apart < 0.5 or apart > 16.0:
                continue
            shared = None
            for (x, xo) in ((ai, oi), (oi, ai)):
                for (y, yo) in ((aj, oj), (oj, aj)):
                    if shared is None and dot(x, y) > 0.99:
                        shared = (x, xo, yo)
            if shared is None or abs(dot(across, shared[0])) > 0.5:
                continue
            if dot(shared[1], across) > -0.1 * apart or dot(shared[2], across) < 0.1 * apart:
                continue
            t0 = along / li
            t1 = dot([el['edges'][j][1][c] - a[c] for c in range(3)], di) / li
            lo_, hi_ = max(0.0, min(t0, t1)), min(1.0, max(t0, t1))
            if (hi_ - lo_) * li < 4.0:
                continue
            out[(i, j)] = (lo_, hi_)
    return out


def adjacency_check(el, name, minimum):
    """The asset's adjacency between static mount edges is exactly what the rule gives, with the same parts."""
    static = {k: v for k, v in el['adjacency'].items() if el['edges'][k[0]][0][3] == 0 and el['edges'][k[1]][0][3] == 0}
    want = predicted_adjacency(el)
    both = set(static) & set(want)
    off = sum(1 for k in both if abs(static[k][0] - want[k][0]) > 0.02 or abs(static[k][1] - want[k][1]) > 0.02)
    back = sum(1 for (i, j) in static if (j, i) not in static)
    check(len(static) >= minimum and set(static) == set(want) and off == 0 and back == 0,
          '%s: %d neighbour records between static mount edges; the rule gives %d, the same pairs with the same parts%s' %
          (name, len(static), len(want), '' if set(static) == set(want) else ' (only in the asset %d, only by the rule %d)' %
           (len(set(static) - set(want)), len(set(want) - set(static)))))
''')

rep('''    check(mounts > 100 and turned == mounts, 'stock: the two faces of %d right-angled mount edges turn about the edge the way this proof reads them' % mounts)
''', '''    check(mounts > 100 and turned == mounts, 'stock: the two faces of %d right-angled mount edges turn about the edge the way this proof reads them' % mounts)
    adjacency_check(sl, 'stock adjacency', 100)
''')

rep('''    far = query(el, 0, [5000, 5000, 5000], [5100, 5100, 5100])''', '''    # Mount adjacency: one word per edge, the pairs the stock rule gives, among them the 16-unit wall's two top edges
    # and the corners at its ends; the crate and the pillar (wider than 16) have none.
    adjacency_check(el, 'built adjacency', 6)
    check(el['adjacencyWords'] == el['numEdges'], 'adjacency: one word per edge (%d of %d)' % (el['adjacencyWords'], el['numEdges']))

    def index_of(p_, q_):
        for i_, (a_, b_) in enumerate(el['edges']):
            if (all(abs(a_[k] - p_[k]) < 0.05 for k in range(3)) and all(abs(b_[k] - q_[k]) < 0.05 for k in range(3))) or \\
               (all(abs(a_[k] - q_[k]) < 0.05 for k in range(3)) and all(abs(b_[k] - p_[k]) < 0.05 for k in range(3))):
                return i_
        return -1
    top_a, top_b = index_of((0, 100, 48), (128, 100, 48)), index_of((0, 116, 48), (128, 116, 48))
    end_a, end_b = index_of((0, 100, 0), (0, 100, 48)), index_of((0, 116, 0), (0, 116, 48))
    crate = index_of((-96, -96, 0), (-96, -96, 56))
    pillar = index_of((150, -150, 0), (150, -150, 96))
    full = (0.0, 1.0, 0.0, 1.0)
    check(el['adjacency'].get((top_a, top_b)) == full and el['adjacency'].get((top_b, top_a)) == full and
          el['adjacency'].get((end_a, end_b)) == full and el['adjacency'].get((end_b, end_a)) == full,
          'the 16-unit wall: its two top edges are neighbours over their whole length, and so are the corners at its end')
    check(crate >= 0 and pillar >= 0 and not any(crate in k or pillar in k for k in el['adjacency']), 'the crate\\'s and the pillar\\'s corners have no neighbour')
    far = query(el, 0, [5000, 5000, 5000], [5100, 5100, 5100])''')
io.open(p, 'w', encoding='utf-8', newline='').write(s)
print('ok')
