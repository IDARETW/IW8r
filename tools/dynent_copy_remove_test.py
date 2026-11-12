"""Offline proof of removing and copying placed scriptables that have dynamic entities of their own (converter editzone
"scriptable_instances" "remove" and "add"; docs/research/89_warzone_work.md section 8).

One edit on a stock zone (default mp_br_quarry) removes instances of every kind the zone has (an ordinary radial entry
whose bucket keeps others, one that is alone in its bucket, a door, a no-spatial entry, an instance with several
entries) and copies instances of every kind (a sign near its original, one into a radial partition that has no bucket
yet, a door into another door area, a door into an empty door leaf, a no-spatial one, a multi-entry one, a buy station
without entries). Checked on the rebuilt zone:

- the instance list is the stock list without the removed ones plus the copies; counts and the six physics limits
  follow the definitions' numbers;
- the dynamic entity list is the stock list without the removed instances' entries, every kept entry renumbered to
  the same instance (same definition, origin, angles), and the copies' entries (the "like" entry with the copy's pose,
  instance index and walked partition) inserted after the last entry that names a real instance; the 64 reserved
  entries (500000) are kept;
- every chain is exact: each bucket of population 0 (radial) and 1 (sparse occluders) lists exactly the entries of its
  partition in descending order with its tail, partitionIds ascending, one bucket per partition that has entries; the
  no-spatial chain lists the partition-0 entries; currentPopulation and the counts follow;
- MapEnts: dynamic entity counts, no-spatial count, extra pose parts, the spatial trees (split planes unchanged, the
  extents grown by the copies' origins along their paths); everything else in map_ents unchanged;
- GfxWorld: dynamic entity clients = stock + the entry change (stock: MapEnts total + 10), words, motion bits, light
  shadow bits per client; every other GfxWorld field unchanged;
- ACTS tests the zone like the stock one; a copy of an entry that does not follow the stock rule is refused.

Usage: py -3 -I tools/dynent_copy_remove_test.py <work folder> [--zone mp_br_quarry] [--exe <zonetool.exe>]
Nothing is written outside the work folder. The game folder is only read."""
import collections, glob, hashlib, json, os, shutil, struct, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import stock_edit_test as t
from warzone_instances_edit_test import catalogue, instances_of
from dynent_pose_rules import quat, same_rotation

NONE = 4294967295
SPARSE = 'DYNENT_PRIORITY_SPARSE_OCCLUDER'
GFX_DYN = ('dpvsDyn', 'sceneDynModel', 'dynEntMotionBitsEntries', 'dynEntMotionBits', 'numPrimaryLightDynEntShadowVisEntries',
           'primaryLightDynEntShadowVis')
ME_DYN = ('scriptableMapEnts', 'dynEntCount', 'dynEntCountTotal', 'dynEntNoSpatialCount', 'dynEntsWithExtraPosePartsCount',
          'dynEntMaxPosePartCount', 'dynEntSpatialPopulation')


def f32(v):
    return struct.unpack('<f', struct.pack('<f', float(v)))[0]


def name(v):
    return v['name'] if isinstance(v, dict) and 'name' in v else str(v)


def values(node):
    return node.get('values') if isinstance(node, dict) else node


def strip(node, drop=('address',)):
    if isinstance(node, dict):
        return {k: strip(v, drop) for k, v in node.items() if k not in drop}
    if isinstance(node, list):
        return [strip(v, drop) for v in node]
    return node


def digest(node):
    return hashlib.sha1(json.dumps(strip(node), sort_keys=True).encode()).hexdigest()


def tree(me, pop):
    """The MapEnts spatial tree of a population ({} when the map has none: no doors)."""
    node = me['dynEntSpatialPopulation'][pop]
    if not node or not values(node):
        return {'partitions': [], 'extents': []}
    return values(values(node)[0]['tree'])[0]


def walk(nodes, o, path=None):
    i = 0
    while nodes[i]['axis'] != 3:
        if path is not None:
            path.append(i)
        n = nodes[i]
        i = i + 1 if o[n['axis']] < n['dist'] else n['rightChild']
    if path is not None:
        path.append(i)
    return i


def gfx_summary(zone_file, out):
    files = t.export(zone_file, 'gfx_map', out)
    g = t.fields(files[0])
    d = g['dpvsDyn']
    s = dict(clients=list(d['dynEntClientCount']), words=list(d['dynEntClientWordCount']), motion=list(g['dynEntMotionBitsEntries']),
             shadow=list(g['numPrimaryLightDynEntShadowVisEntries']), scene=len(values(g['sceneDynModel']) or []),
             cellbits=[(x or {}).get('count') for x in d['dynEntCellBits']],
             vis=[(x or {}).get('count') if isinstance(x, dict) and 'count' in x else (len(x['bytes']) // 2 if isinstance(x, dict) and 'bytes' in x else None)
                  for x in d['dynEntVisData'][0]],
             cells=g['dpvsPlanes'].get('cellCount'),
             others={k: digest(v) for k, v in g.items() if k not in GFX_DYN})
    del g
    shutil.rmtree(out, ignore_errors=True)
    return s


def load(zone_file, work, tag):
    me_file = t.export(zone_file, 'map_ents', os.path.join(work, tag + '_me'))[0]
    me = t.fields(me_file)
    inst, counts, limits = instances_of(me_file)
    dyn = t.fields(t.export(zone_file, 'dynentitylist', os.path.join(work, tag + '_dyn'))[0])
    return me, inst, counts, limits, dyn


def chains(dyn):
    """(problems, bucket count per population): every chain against the rule."""
    ents = values(dyn['dynEntDefList'][0])
    nxt = [e['spatialNode']['spatialNodeNext'] for e in ents]
    problems = []

    def read(head):
        m, i = [], head
        while i != NONE and i < len(ents) and len(m) <= len(ents):
            m.append(i)
            i = nxt[i]
        return m
    groups = [collections.defaultdict(list), collections.defaultdict(list)]
    no_spatial = []
    for i, e in enumerate(ents):
        if name(e['priority']) == SPARSE:
            groups[1][e['spatialPartitionIndex']].append(i)
        elif e['spatialPartitionIndex'] != 0:
            groups[0][e['spatialPartitionIndex']].append(i)
        else:
            no_spatial.append(i)
    ns = read(dyn['dynEntNoSpatialList']['spatialNodeNext'])
    if ns != sorted(no_spatial, reverse=True) or (ns[-1] if ns else NONE) != dyn['dynEntNoSpatialListTail']['spatialNodeNext']:
        problems.append('the no-spatial chain')
    sizes = []
    for p in (0, 1):
        pop = (values(dyn['dynEntSpatialPopulation'][p]) or [None])[0] if dyn['dynEntSpatialPopulation'][p] else None
        if not pop:
            pop = dict(currentPopulation=0, partitionCount=0)
        pid = values(pop.get('partitionIds')) or []
        heads, tails = values(pop.get('buckets')) or [], values(pop.get('bucketTails')) or []
        if pid != sorted(pid) or len(set(pid)) != len(pid):
            problems.append('population %d: partitionIds not ascending and unique' % p)
        if set(pid) != set(groups[p]):
            problems.append('population %d: buckets %s for partitions %s' % (p, sorted(set(pid) ^ set(groups[p]))[:6], len(groups[p])))
        for b, head in enumerate(heads):
            m = read(head['spatialNodeNext'])
            if m != sorted(groups[p].get(pid[b], []), reverse=True) or (m[-1] if m else NONE) != tails[b]['spatialNodeNext']:
                problems.append('population %d bucket %d (partition %d)' % (p, b, pid[b]))
        if pop['currentPopulation'] != sum(len(v) for v in groups[p].values()) or pop['partitionCount'] != len(pid):
            problems.append('population %d: currentPopulation %d / partitionCount %d' % (p, pop['currentPopulation'], pop['partitionCount']))
        sizes.append(len(pid))
    return problems, sizes, len(no_spatial)


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
        print(('PASS ' if ok else 'FAIL ') + text)
        if not ok:
            failures.append(text)

    cat = catalogue()
    stock_file = os.path.join(t.GAME, 'zone', zone + '.ff')
    me, inst, counts, limits, dyn = load(stock_file, work, 'stock')
    ents = values(dyn['dynEntDefList'][0])
    trees = [values(tree(me, p)['partitions']) or [] for p in (0, 1)]
    problems, sizes, n_ns = chains(dyn)
    check(not problems, 'stock %s: %d entries, %d instances; every chain follows the rule (buckets %s, no-spatial %d) %s' %
          (zone, len(ents), len(inst), sizes, n_ns, problems[:3]))

    # The candidates, by kind.
    per = collections.defaultdict(list)
    for i, e in enumerate(ents):
        if 'SCRIPTABLE' in name(e['type']) and e['scriptableMapIndex'] < len(inst):
            per[e['scriptableMapIndex']].append(i)
    bucket_size = collections.Counter((0 if name(e['priority']) != SPARSE else 1, e['spatialPartitionIndex']) for e in ents)

    def kind(k):
        v = per[k]
        if len(v) > 1:
            return 'multi'
        e = ents[v[0]]
        if name(e['priority']) == SPARSE:
            return 'door'
        return 'radial' if e['spatialPartitionIndex'] else 'nospatial'

    def numbers(k):
        row = cat.get(inst[k]['definition'].lstrip(','))
        if not row:
            return None
        return dict(server_instanced_parts=int(row['server_instanced_parts']), usable_parts=int(row['usable_parts']),
                    physics_limit=row['physics_limit'] == '1', physics_limit_detail=row['physics_limit_detail'] == '1')

    def follows(k):  # every entry is where the stock rule puts it
        for j in per[k]:
            e = ents[j]
            o = e['initialPose']['origin']['v']
            if not (all(abs(a - b) < 1e-2 for a, b in zip(o, inst[k]['origin'])) and same_rotation(e['initialPose']['quat']['v'], quat(*inst[k]['angles']))):
                return False
            p = 1 if name(e['priority']) == SPARSE else (0 if e['spatialPartitionIndex'] else None)
            if p is not None and walk(trees[p], o) != e['spatialPartitionIndex']:
                return False
        return True
    by_kind = collections.defaultdict(list)
    for k in sorted(per):
        if numbers(k) and follows(k):
            by_kind[kind(k)].append(k)
    print('     candidates: %s' % {k: len(v) for k, v in by_kind.items()})
    check(len(by_kind['radial']) >= 3, 'the zone has radial instances to remove and copy (doors: %d)' % len(by_kind['door']))
    if len(by_kind['radial']) < 3:
        return 1

    def pkey(k):
        e = ents[per[k][0]]
        return (1 if name(e['priority']) == SPARSE else 0, e['spatialPartitionIndex'])
    remove = []
    remove.append(next(k for k in by_kind['radial'] if bucket_size[pkey(k)] >= 2))
    alone = [k for k in by_kind['radial'] if bucket_size[pkey(k)] == 1]
    if alone:
        remove.append(alone[0])
    if by_kind['door']:
        remove.append(by_kind['door'][len(by_kind['door']) // 2])
    for extra in ('nospatial', 'multi'):
        if by_kind[extra]:
            remove.append(by_kind[extra][0])
    removed = set(remove)
    keep = lambda kd: [k for k in by_kind[kd] if k not in removed]

    adds = []  # (like, origin, angles, what)
    sign = keep('radial')[0]
    o = inst[sign]['origin']
    adds.append((sign, [o[0] + 4.0, o[1] + 4.0, o[2]], [0, 30, 0], 'a radial copy near its original'))
    # A radial copy into a leaf that has no bucket yet: the middle of the leaf's cell (x, y), the original's height.
    have0 = {e['spatialPartitionIndex'] for e in ents if name(e['priority']) != SPARSE and e['spatialPartitionIndex']}
    ext0 = values(tree(me, 0)['extents'])
    empty0 = [i for i, n in enumerate(trees[0]) if n['axis'] == 3 and i not in have0]
    far = None
    for leaf in empty0:
        b = ext0[leaf]
        mid = [(b['mins']['v'][a] + b['maxs']['v'][a]) / 2 for a in (0, 1)] + [o[2]]
        if all(abs(x) < 1e6 for x in mid) and walk(trees[0], mid) == leaf:
            far = mid
            break
    if far:
        adds.append((keep('radial')[1], far, [0, 200, 0], 'a radial copy into partition %d (no bucket yet)' % walk(trees[0], far)))
    door = keep('door')[0] if keep('door') else None
    other_area = next((k for k in keep('door') if pkey(k) != pkey(door)), None) if door is not None else None
    if other_area is not None:
        od = inst[other_area]['origin']
        adds.append((door, [od[0] + 8.0, od[1], od[2]], [0, 90, 0], 'a door copy into door area %d' % pkey(other_area)[1]))
    have1 = {e['spatialPartitionIndex'] for e in ents if name(e['priority']) == SPARSE}
    ext1 = values(tree(me, 1)['extents'])
    for leaf in [i for i, n in enumerate(trees[1]) if door is not None and n['axis'] == 3 and i not in have1]:
        b = ext1[leaf]
        mid = [(b['mins']['v'][a] + b['maxs']['v'][a]) / 2 for a in (0, 1)] + [inst[door]['origin'][2]]
        if walk(trees[1], mid) == leaf:
            adds.append((door, mid, [0, 0, 0], 'a door copy into the empty door leaf %d (no bucket yet)' % leaf))
            break
    for extra in ('nospatial', 'multi'):
        if keep(extra):
            x = keep(extra)[0]
            ox = inst[x]['origin']
            adds.append((x, [ox[0] + 16.0, ox[1], ox[2]], [0, 45, 0], 'a %s copy' % extra))
    plain = [i for i, s in enumerate(inst) if i not in per and s['definition'].lstrip(',') == 'br_plunder_box']
    if plain:
        ob = inst[plain[0]]['origin']
        adds.insert(1, (plain[0], [ob[0], ob[1] + 64.0, ob[2]], [0, 10, 0], 'a buy station (no dynamic entity) between them'))
    for k in remove:
        print('     remove %d %s (%s, %d entries)' % (k, inst[k]['definition'], kind(k), len(per[k])))
    for like, origin, angles, what in adds:
        print('     add like %d %s: %s' % (like, inst[like]['definition'], what))
    edits = {'scriptable_instances': {'remove': [dict(index=k, **numbers(k)) for k in remove],
                                      'add': [dict(like=l, origin=o_, angles=a, **numbers(l)) for l, o_, a, _ in adds]}, 'patch': False}
    code, log, rep = t.editzone(exe, zone, edits, os.path.join(work, 'e'))
    check(code == 0, 'editzone finishes (%s)' % {k: v for k, v in rep.items() if 'dynamic' in k or 'streamed' in k or 'gfx' in k})
    if code != 0:
        print(log[-3000:])
        print('RESULT fail')
        return 1
    out_file = os.path.join(work, 'e', zone + '.ff')
    me2, inst2, counts2, limits2, dyn2 = load(out_file, work, 'e')
    ents2 = values(dyn2['dynEntDefList'][0])

    # Instances.
    kept = [k for k in range(len(inst)) if k not in removed]
    new_index = {k: i for i, k in enumerate(kept)}
    want_inst = [inst[k] for k in kept]
    for like, origin, angles, _ in adds:
        s = dict(inst[like])
        s.update(origin=tuple(f32(v) for v in origin), angles=tuple(f32(v) for v in angles), names=(0, inst[like]['names'][1], 0, 0, inst[like]['names'][4]))
        want_inst.append(s)
    check(inst2 == want_inst, 'instances: the stock list without the %d removed, then the %d copies (%d -> %d)' % (len(remove), len(adds), len(inst), len(inst2)))
    want_counts = dict(counts)
    want_limits = dict(limits)
    for k, sign_ in [(k, -1) for k in remove] + [(l, 1) for l, _, _, _ in adds]:
        n = numbers(k)
        want_counts['mapInstanceCount'] += sign_
        want_counts['serverInstanceCount'] += sign_ * (1 if n['server_instanced_parts'] else 0)
        want_counts['serverFixedPartCount'] += sign_ * n['server_instanced_parts']
        want_counts['serverUsablePartCount'] += sign_ * n['usable_parts']
        for key in want_limits:
            if n['physics_limit_detail'] if 'Detailed' in key else n['physics_limit']:
                want_limits[key] += sign_
    check(counts2 == want_counts and limits2 == want_limits, 'instance counts %s and the six physics limits %s follow the definitions' %
          (counts2, sorted(set(limits2.values()))))

    # The dynamic entity list, entry by entry.
    insert_at = max(i for i, e in enumerate(ents) if 'SCRIPTABLE' in name(e['type']) and e['scriptableMapIndex'] < len(inst)) + 1
    want = []
    copies = []  # positions of the copies' entries in the new list, with their (like entry, copy number)
    for i, e in enumerate(ents):
        if i == insert_at:
            for c, (like, origin, angles, _) in enumerate(adds):
                for j in per.get(like, []):
                    copies.append((len(want), j, c))
                    want.append(None)
        if 'SCRIPTABLE' in name(e['type']) and e['scriptableMapIndex'] < len(inst):
            if e['scriptableMapIndex'] in removed:
                continue
            e = dict(e, scriptableMapIndex=new_index[e['scriptableMapIndex']])
        want.append(e)
    no_link = ('address', 'spatialNode')
    same_kept = len(want) == len(ents2) and all(w is None or strip(w, no_link) == strip(g, no_link) for w, g in zip(want, ents2))
    check(same_kept, 'dynamic entities: %d -> %d; every kept entry equals its stock entry apart from its link and instance number' % (len(ents), len(ents2)))
    names_same = all(w is None or w['scriptableMapIndex'] >= len(inst2) or 'SCRIPTABLE' not in name(w['type']) or
                     (inst2[g['scriptableMapIndex']]['definition'], inst2[g['scriptableMapIndex']]['origin'], inst2[g['scriptableMapIndex']]['angles']) ==
                     (w2['definition'], w2['origin'], w2['angles']) for w, g, w2 in
                     ((w, g, want_inst[w['scriptableMapIndex']] if w and w['scriptableMapIndex'] < len(want_inst) else None) for w, g in zip(want, ents2)))
    renum = sum(1 for i, e in enumerate(ents) if 'SCRIPTABLE' in name(e['type']) and e['scriptableMapIndex'] < len(inst) and e['scriptableMapIndex'] not in removed
                and new_index[e['scriptableMapIndex']] != e['scriptableMapIndex'])
    check(names_same, 'every kept entry names the same instance (definition, origin, angles) after the renumbering (%d renumbered; report %s)' %
          (renum, rep.get('scriptable_dynamic_entities_renumbered')))
    reserved = [e for e in ents if 'SCRIPTABLE' in name(e['type']) and e['scriptableMapIndex'] >= len(inst)]
    reserved2 = [e for e in ents2 if 'SCRIPTABLE' in name(e['type']) and e['scriptableMapIndex'] >= len(inst2)]
    check(len(reserved) == len(reserved2) and all(strip(a, no_link) == strip(b, no_link) for a, b in zip(reserved, reserved2)),
          'the %d reserved physics entries (500000) are kept' % len(reserved))
    gone = sum(len(per[k]) for k in remove)
    check(rep.get('scriptable_dynamic_entities_removed') == gone and rep.get('scriptable_dynamic_entities_added') == len(copies),
          '%d entries of the removed instances are gone, %d copies added' % (gone, len(copies)))
    pose_ok = part_ok = fields_ok = 0
    t0, t1 = [values(tree(me2, p)['partitions']) or [] for p in (0, 1)]
    for at, j, c in copies:
        like, origin, angles, _ = adds[c]
        g, src = ents2[at], ents[j]
        pose_ok += all(abs(a - b) < 1e-3 for a, b in zip(g['initialPose']['origin']['v'], origin)) and same_rotation(g['initialPose']['quat']['v'], quat(*angles))
        if name(src['priority']) == SPARSE:
            part_ok += g['spatialPartitionIndex'] == walk(t1, origin)
        elif src['spatialPartitionIndex']:
            part_ok += g['spatialPartitionIndex'] == walk(t0, origin)
        else:
            part_ok += g['spatialPartitionIndex'] == 0
        fields_ok += strip(g, no_link + ('initialPose', 'scriptableMapIndex', 'spatialPartitionIndex')) == strip(src, no_link + ('initialPose', 'scriptableMapIndex', 'spatialPartitionIndex')) \
            and g['scriptableMapIndex'] == len(kept) + c
    check(pose_ok == len(copies), 'copies: pose = the copy origin and AnglesToQuat of its angles (%d of %d)' % (pose_ok, len(copies)))
    check(part_ok == len(copies), 'copies: partition = the walk of their origin in the tree of their population (%d of %d)' % (part_ok, len(copies)))
    check(fields_ok == len(copies), 'copies: every other field of the like entry, and the copy\'s instance number (%d of %d)' % (fields_ok, len(copies)))
    problems2, sizes2, n_ns2 = chains(dyn2)
    check(not problems2, 'every chain exact: buckets %s -> %s (one per partition with entries, ascending ids, descending entries, tails), no-spatial %d -> %d, '
          'currentPopulation %s %s' % (sizes, sizes2, n_ns, n_ns2,
                                         [values(dyn2['dynEntSpatialPopulation'][p])[0]['currentPopulation'] for p in (0, 1)], problems2[:3]))
    delta = len(ents2) - len(ents)
    check(dyn2['dynEntCount'] == [dyn['dynEntCount'][0] + delta, dyn['dynEntCount'][1]] and dyn2['dynEntCountTotal'] == dyn['dynEntCountTotal'] + delta,
          'list counts %s -> %s' % (dyn['dynEntCount'], dyn2['dynEntCount']))

    # MapEnts.
    extra = sum(1 for e in ents2 if e['___u15']['scriptableMaxPartCount'] > 1) - sum(1 for e in ents if e['___u15']['scriptableMaxPartCount'] > 1)
    ok = (me2['dynEntCount'] == [me['dynEntCount'][0] + delta, me['dynEntCount'][1]] and me2['dynEntCountTotal'] == me['dynEntCountTotal'] + delta and
          me2['dynEntNoSpatialCount'] == [me['dynEntNoSpatialCount'][0] + n_ns2 - n_ns, me['dynEntNoSpatialCount'][1]] and
          me2['dynEntsWithExtraPosePartsCount'] == me['dynEntsWithExtraPosePartsCount'] + extra and
          me2['dynEntMaxPosePartCount'] == max([me['dynEntMaxPosePartCount']] + [e['___u15']['scriptableMaxPartCount'] for e in ents2]))
    check(ok, 'MapEnts: totals %s -> %s, no-spatial %s -> %s, extra pose parts %s -> %s' %
          (me['dynEntCountTotal'], me2['dynEntCountTotal'], me['dynEntNoSpatialCount'], me2['dynEntNoSpatialCount'],
           me['dynEntsWithExtraPosePartsCount'], me2['dynEntsWithExtraPosePartsCount']))
    tree_ok = True
    for p in (0, 1):
        a, b = tree(me, p), tree(me2, p)
        if strip(values(a['partitions']) or []) != strip(values(b['partitions']) or []):
            tree_ok = False
        want_ext = [dict(mins=list(x['mins']['v']), maxs=list(x['maxs']['v'])) for x in values(a['extents']) or []]
        for at, j, c in copies:
            src = ents[j]
            pop = 1 if name(src['priority']) == SPARSE else (0 if src['spatialPartitionIndex'] else None)
            if pop != p:
                continue
            path = []
            walk(trees[p], adds[c][1], path)
            for node in path:
                for k in range(3):
                    want_ext[node]['mins'][k] = min(want_ext[node]['mins'][k], float(adds[c][1][k]))
                    want_ext[node]['maxs'][k] = max(want_ext[node]['maxs'][k], float(adds[c][1][k]))
        got_ext = [dict(mins=list(x['mins']['v']), maxs=list(x['maxs']['v'])) for x in values(b['extents']) or []]
        close = len(got_ext) == len(want_ext) and all(abs(u - v) <= 1e-3 * max(1.0, abs(v)) for g_, w_ in zip(got_ext, want_ext)
                                                       for key in ('mins', 'maxs') for u, v in zip(g_[key], w_[key]))
        tree_ok = tree_ok and close
    check(tree_ok, 'MapEnts spatial trees: split planes unchanged, extents = stock grown by the copies\' origins along their paths')
    sm, sm2 = me['scriptableMapEnts'], me2['scriptableMapEnts']
    same_block = all(digest(sm[k]) == digest(sm2[k]) for k in sm if k not in ('mapInstances', 'mapInstanceCount', 'serverInstanceCount', 'serverFixedPartCount',
                                                                              'serverUsablePartCount', 'physicsLimits'))
    same_me = all(digest(me[k]) == digest(me2[k]) for k in me if k not in ME_DYN)
    check(same_block and same_me, 'MapEnts: every other field unchanged (reserved dynents, spatial tree of the block, trigger and spawn data, ...)')

    # GfxWorld.
    g1 = gfx_summary(stock_file, os.path.join(work, 'stock_gfx'))
    g2 = gfx_summary(out_file, os.path.join(work, 'e_gfx'))
    c1, c2 = g1['clients'][0], g2['clients'][0]
    factor = g1['words'][0] // ((c1 + 31) // 32)
    words = factor * ((c2 + 31) // 32)
    ok = (c1 == me['dynEntCountTotal'] + 10 and c2 == me2['dynEntCountTotal'] + 10 and g2['words'][0] == words and g2['motion'][0] == words and
          g2['shadow'][0] == g1['shadow'][0] // c1 * c2 and g2['scene'] == c2 and g2['cellbits'][0] == g2['cells'] * words and
          all(v in (None, words * 32) for v in g2['vis'][:12]) and g2['clients'][1] == g1['clients'][1] and g2['words'][1] == g1['words'][1])
    check(ok, 'GfxWorld dynamic entity clients %d -> %d (MapEnts total + 10), words %d -> %d, motion %s, shadow %s -> %s, scene models %d' %
          (c1, c2, g1['words'][0], g2['words'][0], g2['motion'], g1['shadow'], g2['shadow'], g2['scene']))
    diff = sorted(k for k in g1['others'] if g1['others'][k] != g2['others'].get(k))
    check(not diff, 'GfxWorld: every other field unchanged %s' % diff[:5])

    # ACTS.
    def results(lines):
        return lines[0].split('loaded,', 1)[1].strip() if lines and 'loaded,' in lines[0] else None

    def not_ok(folder):  # (type, name, status) of every asset ACTS did not read in full
        out = set()
        for path in glob.glob(os.path.join(folder, '**', 'assets.jsonl'), recursive=True):
            for line in open(path, encoding='utf-8'):
                r = json.loads(line)
                if r.get('status') != 'ok':
                    out.add((r.get('type'), r.get('name'), r.get('status')))
        return out
    test = [l for l in t.acts(['--test', '-o', os.path.join(work, 'e_test'), out_file]) if l.startswith('Replay:')]
    stock_test = [l for l in t.acts(['--test', '-o', os.path.join(work, 'stock_test'), stock_file]) if l.startswith('Replay:')]
    bad, stock_bad = not_ok(os.path.join(work, 'e_test')), not_ok(os.path.join(work, 'stock_test'))
    check(results(test) is not None and results(test) == results(stock_test) and bad == stock_bad,
          'ACTS tests the edited zone like the stock zone: %s (stock: %s; not read in full, both: %s)' % (test, stock_test, sorted(stock_bad)))

    # Refused: a copy of an entry that does not follow the stock rule (placed apart, or outside its walked partition).
    odd = next((k for k in sorted(per) if not follows(k) and numbers(k) and len(per[k]) == 1), None)
    if odd is not None:
        code3, log3, _ = t.editzone(exe, zone, {'scriptable_instances': {'add': [dict(like=odd, origin=inst[odd]['origin'], **numbers(odd))]}, 'patch': False},
                                    os.path.join(work, 'x'))
        check(code3 != 0 and ('stock rule' in log3 or 'placed apart' in log3), 'copying instance %d (its entry does not follow the stock rule) is refused with the reason' % odd)
    for f in glob.glob(os.path.join(work, '*', zone + '.ff')):
        os.remove(f)
    print('RESULT %s (%d failed)' % ('ok' if not failures else 'fail', len(failures)))
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
