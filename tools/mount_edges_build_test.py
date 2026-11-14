"""Offline proof of mount edges on a built map (research 125): the edge list the game queries for weapon mounting.

Builds a small map through the editor's headless build (IW8Radiant.exe --compile): the sample room with a crate, a
wall made of two brushes, a pillar of two stacked brushes, a clip brush against the crate and a ladder. Then reads the
built zone's edge list and map entities back with ACTS and checks:
  - the build log names the mount edges, and the asset holds as many (plus two per ladder);
  - the asset's shape is the stock one: the same checks pass on a stock map's edge list (mp_m_speedball, read from the
    game folder), so the checker's rules are the game's: every tree's root, the parent index of every inner node, the
    grid index of every child by its slot, leaf ranges, the node set counts;
  - every edge sits in every leaf whose cube it crosses, and a query box round any mount edge finds it through the
    MOUNT tree (the walk the game does, research 125 section 2.2), never through another tree;
  - the metadata: the faces in the order the game expects (their cross product along the edge), ledge tops with the top
    face first (angle 0), right angles as the half float 15944; the expected edges of the crate, the wall and the
    pillar are there with their full joined lengths; nothing where the clip brush or a neighbour covers an edge;
  - mount adjacency (the edges a mounted player can move between): the rule of research 125 section 11, written here
    in Python, gives exactly the stock map's own tables, and exactly the built map's tables (the 16-unit wall's two
    top edges and the corners at its ends are neighbours over their whole length; the crate and the pillar have none);
  - the ladder's edges are still one leaf in the LADDER tree;
  - the map entities: one edge list, used query types with bit 0 (mount), with bit 2 (ladder), without bit 1 (mantle
    stays on the trace path).
The package the build writes into the editor's output folder is deleted afterwards (this proof's own map).

Usage: py -3 -I tools/mount_edges_build_test.py <work dir> [--editor <IW8Radiant.exe>] [--exe <zonetool.exe>]
Prints PASS / FAIL lines and RESULT ok|fail."""
import json, math, os, re, shutil, struct, subprocess, sys, zlib  # noqa: E401

here = os.path.dirname(os.path.abspath(__file__))
root = os.path.dirname(here)
args = sys.argv[1:]
editor = os.path.join(root, 'bin', 'release', 'IW8Radiant.exe')
converter = ''
for flag in ('--editor', '--exe'):
    if flag in args:
        value = os.path.abspath(args[args.index(flag) + 1])
        del args[args.index(flag):args.index(flag) + 2]
        if flag == '--editor':
            editor = value
        else:
            converter = value
work = os.path.abspath(args[0])
ACTS = r'E:/mw124/atian-cod-tools/build/bin/Release/acts.exe'
REPLAY = r'E:/IW8/Builds/1.20-replay/game_dx12_ship_replay.exe'
OODLE = r'E:/IW8/Builds/1.20-replay/oo2core_7_win64.dll'
STOCK = r'E:/IW8/Builds/1.20-replay/zone/mp_m_speedball.ff'
MAP_ID = 'mp_iw8r_mntprf'
MAT = 'custom/mnt_wall'
SUFFIX = ' %s 128 128 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0'
SLOTS = [(0, 0, 0), (0, 0, 1), (0, 1, 1), (0, 1, 0), (1, 1, 0), (1, 1, 1), (1, 0, 1), (1, 0, 0)]
ROOT_PARENT = 0xFFFFF8

failures = 0


def check(ok, what):
    global failures
    print(('PASS ' if ok else 'FAIL ') + what, flush=True)
    if not ok:
        failures += 1


def png(path, w, h, rgba):
    raw = b''.join(b'\x00' + bytes(rgba) * w for _ in range(h))

    def chunk(t, d):
        return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    open(path, 'wb').write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0)) +
                           chunk(b'IDAT', zlib.compress(raw)) + chunk(b'IEND', b''))


def pt(x, y, z):
    return '( %g %g %g )' % (x, y, z)


def box(x0, y0, z0, x1, y1, z1, material=MAT):
    s = SUFFIX % material
    poly = [(x0, y0), (x1, y0), (x1, y1), (x0, y1)]
    lines = ['{']
    lines.append(' %s %s %s%s' % (pt(x0, y0, z0), pt(x1, y0, z0), pt(x1, y1, z0), s))
    lines.append(' %s %s %s%s' % (pt(x1, y1, z1), pt(x1, y0, z1), pt(x0, y0, z1), s))
    for k in range(4):
        (ax, ay), (bx, by) = poly[k], poly[(k + 1) % 4]
        lines.append(' %s %s %s%s' % (pt(ax, ay, z1), pt(bx, by, z1), pt(bx, by, z0), s))
    lines.append('}')
    return '\n'.join(lines) + '\n'


# ---- the edge list of an ACTS export, as plain lists ----
def half(h):
    e, m = (h >> 10) & 0x1F, h & 0x3FF
    v = math.ldexp(m, -24) if e == 0 else math.ldexp(m + 1024, e - 25)
    return -v if h & 0x8000 else v


def node_of(n):
    return {'grid': list(n['uniformGridIndex']), 'depth': n['depth'], 'flags': n['flags']['value'],
            'a': n['___u3']['parentNodeIndex'], 'b': n['___u4']['childNodeSetIndex']}


def read_edge_list(path):
    f = json.load(open(path, encoding='utf-8'))['asset']['fields']
    out = {'static': f['staticQueryTypes'], 'dynamic': f['dynamicQueryTypes'], 'valid': f['valid'], 'numEdges': f['numEdges'],
           'numStatic': f['numStaticEdges'], 'mins': f['mins']['v'], 'maxs': f['maxs']['v']}
    out['edges'] = [(e[0]['v']['m128_f32'], e[1]['v']['m128_f32']) for e in (f['edges'] or {}).get('values', [])]
    out['meta'] = [(m['packedAdjacentNormal'], m['packedOpenAngle'], m['flags'][0]) for m in (f['edgeMetadata'] or {}).get('values', [])]
    out['trees'] = []
    for o in (f['edgeOctrees'] or {}).get('values', []):
        hw = o['nodeHalfWidth'][0]['v']['m128_f32'][0]
        out['trees'].append({'bucket': o['edgeBucket']['value'], 'flags': o['flags'][0], 'half': hw,
                             'halves': [x['v']['m128_f32'][0] for x in o['nodeHalfWidth']],
                             'min': o['minExtent']['v']['m128_f32'][:3], 'root': node_of(o['rootNode']), 'sets': o['nodeSetCount'],
                             'rsq': o['nodeBoundingRadiusSq']})
    sets = f.get('edgeOctreeNodeSets')
    out['sets'] = [[node_of(n) for n in s['node']] for s in (sets or {}).get('values', [])] if sets else []
    idx = f.get('edgeIndices')
    if idx and 'bytes' in idx:  # a long list comes as little-endian hex bytes
        raw = bytes.fromhex(idx['bytes'])
        out['indices'] = list(struct.unpack('<%dI' % (len(raw) // 4), raw))
    else:
        out['indices'] = list((idx or {}).get('values', [])) if idx else []
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


def seg_in_cube(a, b, lo, side, grow=0.01):
    t0, t1 = 0.0, 1.0
    for k in range(3):
        mn, mx = lo[k] - grow, lo[k] + side + grow
        d = b[k] - a[k]
        if abs(d) < 1e-12:
            if a[k] < mn or a[k] > mx:
                return False
            continue
        u0, u1 = (mn - a[k]) / d, (mx - a[k]) / d
        if u0 > u1:
            u0, u1 = u1, u0
        t0, t1 = max(t0, u0), min(t1, u1)
        if t0 > t1:
            return False
    return True


def leaves_of(el, tree):
    """Yields (node, cube lowest corner, cube side) of every leaf; raises ValueError on a broken tree."""
    r = tree['root']
    if not r['flags'] & 4:
        raise ValueError('the root lacks the root flag')
    if r['flags'] & 2:
        yield r, tree['min'], 2.0 * tree['half']
        return
    stack = [(r['b'], ROOT_PARENT, [0, 0, 0], 0)]
    seen = 0
    while stack:
        si, parent, grid, depth = stack.pop()
        if si >= len(el['sets']):
            raise ValueError('a node set index is out of range')
        seen += 1
        for slot, n in enumerate(el['sets'][si]):
            if n['depth'] != depth + 1:
                raise ValueError('a node depth is not its parent\'s plus one')
            if n['grid'] != [grid[k] * 2 + SLOTS[slot][k] for k in range(3)]:
                raise ValueError('a grid index does not fit its slot')
            side = 2.0 * tree['half'] / (1 << n['depth'])
            if n['flags'] & 2:
                if n['a'] + n['b'] > len(el['indices']) and n['b']:
                    raise ValueError('a leaf range is outside the index list')
                if bool(n['flags'] & 1) != (n['b'] == 0):
                    raise ValueError('a leaf\'s empty flag does not fit its count')
                yield n, [tree['min'][k] + n['grid'][k] * side for k in range(3)], side
            else:
                if n['a'] != parent:
                    raise ValueError('an inner node does not name its parent')
                stack.append((n['b'], si * 8 + slot, n['grid'], depth + 1))
    if seen != tree['sets']:
        raise ValueError('the node set count (%d) does not fit the nodes (%d)' % (tree['sets'], seen))


def structure(el, name, complete):
    """The shape checks. `complete`: also ask that a leaf lists every edge of its bucket that crosses its cube."""
    listed_bad = missing = leaves = 0
    try:
        for tree in el['trees']:
            if tree['flags'] & 1 == 0:
                continue  # a tree the game does not search (stock: an empty ladder tree)
            bit = {0: 1, 1: 10, 2: 4}.get(tree['bucket'], 0)
            mine = [i for i, m in enumerate(el['meta']) if m[2] & bit and el['edges'][i][0][3] == 0]
            for k in range(1, 16):
                if abs(tree['halves'][k] - tree['half'] / (1 << k)) > 1e-3 * tree['half']:
                    raise ValueError('the half widths are not halved by depth')
            for n, lo, side in leaves_of(el, tree):
                leaves += 1
                ids = el['indices'][n['a']:n['a'] + n['b']]
                for i in ids:
                    if i >= len(el['edges']) or not seg_in_cube(el['edges'][i][0], el['edges'][i][1], lo, side, 0.05):
                        listed_bad += 1
                if complete:
                    have = set(ids)
                    for i in mine:
                        if i not in have and seg_in_cube(el['edges'][i][0], el['edges'][i][1], lo, side, 0.0):
                            missing += 1
    except ValueError as e:
        check(False, '%s: %s' % (name, e))
        return
    check(leaves > 0 and listed_bad == 0 and missing == 0,
          '%s: %d trees, %d leaves, every listed edge crosses its leaf%s' %
          (name, len(el['trees']), leaves, ', no crossing edge is missing' if complete else ''))


def query(el, bucket, mins, maxs):
    out = set()
    for tree in el['trees']:
        if tree['bucket'] != bucket or not tree['flags'] & 1:
            continue
        for n, lo, side in leaves_of(el, tree):
            if all(lo[k] <= maxs[k] and lo[k] + side >= mins[k] for k in range(3)):
                out.update(el['indices'][n['a']:n['a'] + n['b']])
    return out


def normals(edge, meta):
    a, b = edge
    d = [b[k] - a[k] for k in range(3)]
    ln = math.sqrt(sum(x * x for x in d))
    d = [x / ln for x in d]
    if abs(d[2]) > 0.999:
        ref = [1 - d[0] * d[0], -d[1] * d[0], -d[2] * d[0]]
    else:
        ref = [-d[0] * d[2], -d[1] * d[2], 1 - d[2] * d[2]]
    rl = math.sqrt(sum(x * x for x in ref))
    ref = [x / rl for x in ref]

    def rot(v, ang):
        c = [d[1] * v[2] - d[2] * v[1], d[2] * v[0] - d[0] * v[2], d[0] * v[1] - d[1] * v[0]]
        k = sum(d[i] * v[i] for i in range(3)) * (1 - math.cos(ang))
        return [v[i] * math.cos(ang) + c[i] * math.sin(ang) + d[i] * k for i in range(3)]
    adj = rot(ref, meta[0])
    return d, adj, rot(adj, half(meta[1]))


def export(ff, out_dir, types):
    shutil.rmtree(out_dir, ignore_errors=True)
    subprocess.run([ACTS, '--noUpdater', 'fastfile', '-r', 'mw19replay', '-g', REPLAY, '--oodle', OODLE, '-a', types, '-l', '-o', out_dir, ff],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    zone = os.path.splitext(os.path.basename(ff))[0]
    found = {}
    for dirpath, _, files in os.walk(os.path.join(out_dir, 'mw19replay', zone, 'assets')):
        for f in files:
            if f.endswith('.asset.json'):
                found.setdefault(os.path.basename(os.path.normpath(dirpath)) if 'edgelist' in dirpath else 'map_ents', os.path.join(dirpath, f))
    return found


# ---- --check <zone.ff>: only read an already built map's edge list back (shape, completeness, the game's walk) ----
if '--check' in sys.argv:
    zone_file = os.path.abspath(sys.argv[sys.argv.index('--check') + 1])
    scratch = os.path.join(os.path.splitdrive(work)[0] + os.sep, 't', 'mntc')
    got = export(zone_file, scratch, 'edgelist')
    if 'edgelist' in got:
        el = read_edge_list(got['edgelist'])
        mount = [i for i, md in enumerate(el['meta']) if md[2] & 1]
        check(el['valid'] and len(mount) > 0, '%s: %d edges, %d for mounting, trees %s, %d node sets' %
              (os.path.basename(zone_file), el['numEdges'], len(mount), [t['bucket'] for t in el['trees']], len(el['sets'])))
        structure(el, 'edge list', True)
        lost = 0
        for i in mount:
            a, b = el['edges'][i]
            mid = [(a[k] + b[k]) * 0.5 for k in range(3)]
            lost += i not in query(el, 0, [x - 40 for x in mid], [x + 40 for x in mid])
        check(lost == 0, 'a query box round each of the %d mount edges finds it in the MOUNT tree' % len(mount))
        adjacency_check(el, 'adjacency', 0)
        depth =max((n['depth'] for s_ in el['sets'] for n in s_), default=0)
        most = max((n['b'] for s_ in el['sets'] for n in s_ if n['flags'] & 2), default=0)
        print('     deepest node %d, most edges in a leaf %d' % (depth, most))
    else:
        check(False, 'edge list export (ACTS) from ' + zone_file)
    shutil.rmtree(scratch, ignore_errors=True)
    print('RESULT ' + ('ok' if failures == 0 else 'fail'))
    sys.exit(0 if failures == 0 else 1)

# ---- the map ----
shutil.rmtree(work, ignore_errors=True)
os.makedirs(os.path.join(work, 'materials'))
png(os.path.join(work, 'materials', 'mnt_wall.png'), 16, 16, (128, 120, 110, 255))
open(os.path.join(work, 'materials', 'custom__mnt_wall.iw8mat.json'), 'w', encoding='utf-8').write(
    json.dumps({'name': MAT, 'color': 'mnt_wall.png', 'surface': 'concrete', 'alphaTest': False, 'twoSided': False, 'scale': [128, 128]}) + '\n')
brushes = ''.join([
    box(-96, -96, 0, -32, -32, 56),                 # a crate
    box(-32, -96, 0, 32, -32, 96, 'tools/clip'),    # a clip brush against its +x side: covers that side's edges
    box(0, 100, 0, 64, 116, 48),                    # a wall of two brushes side by side
    box(64, 100, 0, 128, 116, 48),
    box(150, -150, 0, 182, -118, 24),               # a pillar of two stacked brushes
    box(150, -150, 24, 182, -118, 96),
    box(-200, 60, 0, -168, 64, 96, 'tools/ladder'),  # a ladder
])
src = open(os.path.join(root, 'tests', 'data', 'iw8_sample_room.map'), encoding='utf-8').read()
mark = '}\n// entity 1\n'
assert src.count(mark) == 1, src.count(mark)
src = src.replace(mark, brushes + mark, 1)
map_path = os.path.join(work, 'iw8r_mntprf.map')
open(map_path, 'w', encoding='utf-8', newline='\n').write(src)
out = os.path.join(work, 'build')
env = dict(os.environ)
if converter:
    env['IW8R_TOOL_ZONETOOL'] = converter
subprocess.run([editor, '--compile', map_path, '--out', out], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, env=env)


def text_of(name):
    p = os.path.join(out, name)
    return open(p, encoding='utf-8', errors='replace').read() if os.path.isfile(p) else ''


result, log = text_of('compile-result.txt'), text_of('compile-log.txt')
first = result.splitlines()[0] if result else ''
check(first.startswith('ok map=' + MAP_ID), 'build ok (%s)' % first[:120])
m = re.search(r'package=(.+)$', first)
package = m.group(1).strip() if m else ''
line = next((l for l in (result + '\n' + log).splitlines() if 'mount edges: ' in l and 'wall corner' in l), '')
mm = re.search(r'mount edges: (\d+) wall corner\(s\) and (\d+) ledge top\(s\)', line)
check(mm is not None, 'build log: ' + (line.strip()[:160] or '-'))
wanted = int(mm.group(1)) + int(mm.group(2)) if mm else -1
conv = next((l for l in log.splitlines() if 'mount edges for the map' in l), '')
check(bool(conv), 'converter: ' + (conv.strip()[:120] or '(no mount edge line)'))
acts_lines = re.findall(r'ACTS\s+(\S+)\s+exit\s+(-?\d+)', log)
if acts_lines:
    check(all(code == '0' for _, code in acts_lines), 'ACTS loader test: %s' % ', '.join('%s %s' % a for a in acts_lines))

# ---- the stock list passes the same shape checks ----
scratch = os.path.join(os.path.splitdrive(work)[0] + os.sep, 't', 'mnt')
stock = export(STOCK, os.path.join(scratch, 's'), 'edgelist')
if 'edgelist' in stock:
    sl = read_edge_list(stock['edgelist'])
    structure(sl, 'stock mp_m_speedball (%d edges)' % sl['numEdges'], False)
    turned = 0
    mounts = 0
    for e, md in zip(sl['edges'], sl['meta']):
        if not md[2] & 1 or e[0][3] != 0 or abs(half(md[1]) - 1.5703) > 0.01:
            continue
        d, adj, oth = normals(e, md)
        c = [adj[1] * oth[2] - adj[2] * oth[1], adj[2] * oth[0] - adj[0] * oth[2], adj[0] * oth[1] - adj[1] * oth[0]]
        mounts += 1
        turned += sum(c[k] * d[k] for k in range(3)) > 0.9
    check(mounts > 100 and turned == mounts, 'stock: the two faces of %d right-angled mount edges turn about the edge the way this proof reads them' % mounts)
    adjacency_check(sl, 'stock adjacency', 100)
else:
    check(False, 'stock edge list export (ACTS)')

# ---- the built list ----
ff = os.path.join(package, MAP_ID + '.ff') if package else ''
built = export(ff, os.path.join(scratch, 'b'), 'edgelist,map_ents') if ff and os.path.isfile(ff) else {}
if 'edgelist' in built:
    el = read_edge_list(built['edgelist'])
    mount = [i for i, md in enumerate(el['meta']) if md[2] & 1]
    ladder = [i for i, md in enumerate(el['meta']) if md[2] & 4]
    check(el['valid'] and el['numEdges'] == len(el['edges']) == el['numStatic'] and len(mount) == wanted and len(ladder) >= 2 and len(ladder) % 2 == 0,
          'asset: %d edges, %d for mounting (the log: %d), %d ladder' % (el['numEdges'], len(mount), wanted, len(ladder)))
    check(el['static'] == 5 and el['dynamic'] == 5, 'query types: static %d, dynamic %d (1 mount + 4 ladder)' % (el['static'], el['dynamic']))
    buckets = [t['bucket'] for t in el['trees']]
    check(buckets == [0, 2] and all(t['flags'] == 3 for t in el['trees']), 'trees by bucket: %s' % buckets)
    structure(el, 'built map', True)
    lt = [t for t in el['trees'] if t['bucket'] == 2]
    check(bool(lt) and lt[0]['root']['flags'] == 6 and lt[0]['root']['b'] == len(ladder) and
          sorted(el['indices'][lt[0]['root']['a']:lt[0]['root']['a'] + len(ladder)]) == sorted(ladder),
          'the ladder tree is one leaf with the ladder\'s %d edges (two per climbable face)' % len(ladder))
    # The walk the game does: a box round each mount edge finds it in the MOUNT tree; the ladder tree gives none.
    lost = wrong_tree = 0
    for i in mount:
        a, b = el['edges'][i]
        mid = [(a[k] + b[k]) * 0.5 for k in range(3)]
        got = query(el, 0, [x - 40 for x in mid], [x + 40 for x in mid])
        lost += i not in got
        wrong_tree += i in query(el, 2, [x - 40 for x in mid], [x + 40 for x in mid])
    check(lost == 0 and wrong_tree == 0, 'a query box round each of the %d mount edges finds it in the MOUNT tree only' % len(mount))
    # Mount adjacency: one word per edge, the pairs the stock rule gives, among them the 16-unit wall's two top edges
    # and the corners at its ends; the crate and the pillar (wider than 16) have none.
    adjacency_check(el, 'built adjacency', 6)
    check(el['adjacencyWords'] == el['numEdges'], 'adjacency: one word per edge (%d of %d)' % (el['adjacencyWords'], el['numEdges']))

    def index_of(p_, q_):
        for i_, (a_, b_) in enumerate(el['edges']):
            if (all(abs(a_[k] - p_[k]) < 0.05 for k in range(3)) and all(abs(b_[k] - q_[k]) < 0.05 for k in range(3))) or \
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
    check(crate >= 0 and pillar >= 0 and not any(crate in k or pillar in k for k in el['adjacency']), 'the crate\'s and the pillar\'s corners have no neighbour')
    far = query(el, 0, [5000, 5000, 5000], [5100, 5100, 5100])
    check(not far, 'a query box far from the map finds no edge')
    # The metadata.
    bad_turn = bad_ledge = bad_angle = 0
    for i in mount:
        d, adj, oth = normals(el['edges'][i], el['meta'][i])
        c = [adj[1] * oth[2] - adj[2] * oth[1], adj[2] * oth[0] - adj[0] * oth[2], adj[0] * oth[1] - adj[1] * oth[0]]
        bad_turn += sum(c[k] * d[k] for k in range(3)) < 0.9
        bad_angle += el['meta'][i][1] != 15944
        if abs(d[2]) < 0.1:
            bad_ledge += not (el['meta'][i][0] == 0 and abs(adj[2] - 1) < 1e-3 and abs(oth[2]) < 2e-3)
        else:
            bad_ledge += not (abs(adj[2]) < 2e-3 and abs(oth[2]) < 2e-3)
    check(bad_turn == 0 and bad_ledge == 0 and bad_angle == 0,
          'metadata: faces in the game\'s order, ledge tops with the top face first, right angles as 15944')

    def has(p, q_, flags=1):
        for i, (a, b) in enumerate(el['edges']):
            a, b = a[:3], b[:3]
            if el['meta'][i][2] != flags:
                continue
            if (all(abs(a[k] - p[k]) < 0.05 for k in range(3)) and all(abs(b[k] - q_[k]) < 0.05 for k in range(3))) or \
               (all(abs(a[k] - q_[k]) < 0.05 for k in range(3)) and all(abs(b[k] - p[k]) < 0.05 for k in range(3))):
                return True
        return False

    def touches_plane(axis, value, lo, hi):
        """A mount edge lying in the plane axis = value inside the box lo..hi."""
        for i in mount:
            a, b = el['edges'][i]
            if abs(a[axis] - value) < 0.05 and abs(b[axis] - value) < 0.05 and all(lo[k] - 0.05 <= min(a[k], b[k]) and max(a[k], b[k]) <= hi[k] + 0.05 for k in range(3)):
                return True
        return False
    check(has((-96, -96, 0), (-96, -96, 56)) and has((-96, -32, 0), (-96, -32, 56)) and has((-96, -96, 56), (-96, -32, 56)) and
          has((-96, -96, 56), (-32, -96, 56)), 'the crate: its open corners and top edges')
    check(not touches_plane(0, -32, (-32, -96, 0), (-32, -32, 56)), 'the crate\'s side against the clip brush has no edge')
    check(has((0, 100, 48), (128, 100, 48)) and has((0, 116, 48), (128, 116, 48)) and has((0, 100, 48), (0, 116, 48)) and
          has((0, 100, 0), (0, 100, 48)) and has((128, 116, 0), (128, 116, 48)), 'the wall of two brushes: one top edge over both, its end corners')
    check(not touches_plane(0, 64, (64, 100, 0), (64, 116, 48)), 'no edge at the joint of the two wall brushes')
    check(all(has((x, y, 0), (x, y, 96)) for x in (150, 182) for y in (-150, -118)) and has((150, -150, 96), (182, -150, 96)),
          'the pillar of two stacked brushes: four corners of the whole height, its top edges')
    # The clip brush (x -32..32, y -96..-32, z 0..96) stands against the sample room's own pillar (y -32..32, which keeps
    # its edges): an edge of the clip itself lies at y < -32 and reaches above the crate (z 56) beside it.
    clip_edges = [i for i in mount if all(-32.05 <= el['edges'][i][e][0] <= 32.05 and -96.05 <= el['edges'][i][e][1] < -32.05 for e in (0, 1))
                  and max(el['edges'][i][0][2], el['edges'][i][1][2]) > 56.05]
    check(not clip_edges, 'the clip brush itself gives no edge')
else:
    check(False, 'built edge list export (ACTS) from ' + (ff or '(no package)'))

if 'map_ents' in built:
    f = json.load(open(built['map_ents'], encoding='utf-8'))['asset']['fields']
    used, lists = f.get('edgeListUsedQueryTypes'), f.get('numEdgeLists')
    check(used == 5 and lists == 1, 'map entities: used query types %s (mount 1 + ladder 4, no mantle bit 2), %s edge list' % (used, lists))
else:
    check(False, 'built map entities export (ACTS)')

shutil.rmtree(scratch, ignore_errors=True)
if package and os.path.basename(package.rstrip('/\\')) == MAP_ID:
    shutil.rmtree(package, ignore_errors=True)
print('RESULT ' + ('ok' if failures == 0 else 'fail'))
sys.exit(0 if failures == 0 else 1)
