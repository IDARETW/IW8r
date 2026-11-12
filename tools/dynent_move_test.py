"""Offline proof of moving a placed scriptable that has an ordinary dynamic entity (editzone scriptable_instances
"move"; research 89 section 8): its dynamic entity follows (pose, spatial partition, bucket chain).

On a stock zone (default mp_br_quarry): one instance with an ordinary (radial) dynamic entity moved a little (same
partition) and one moved far into another partition that has a bucket. Checked on the rebuilt zone: the instances
moved; each dynamic entity's initialPose equals the instance placement (origin; quaternion of the angles); its
spatialPartitionIndex is the tree walk of the new origin; every bucket chain lists exactly the ordinary entries of its
partition in descending order with the right tail; every other dynamic entity is unchanged; ACTS tests the zone like the
stock one. A door (sparse occluder) move is refused.

Usage: py -3 -I tools/dynent_move_test.py <work folder> [--zone mp_br_quarry] [--exe <zonetool.exe>]"""
import collections, glob, os, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import stock_edit_test as t
from warzone_instances_edit_test import instances_of
from dynent_pose_rules import quat, same_rotation

NONE = 4294967295


def load(zone_file, work, tag):
    me = t.fields(t.export(zone_file, 'map_ents', os.path.join(work, tag + '_me'))[0])
    inst, _, _ = instances_of(t.export(zone_file, 'map_ents', os.path.join(work, tag + '_me2'))[0])
    dyn = t.fields(t.export(zone_file, 'dynentitylist', os.path.join(work, tag + '_dyn'))[0])
    nodes = me['dynEntSpatialPopulation'][0]['values'][0]['tree']['values'][0]['partitions']['values']
    return inst, dyn, nodes


def walk(nodes, o):
    i = 0
    while nodes[i]['axis'] != 3:
        n = nodes[i]
        i = i + 1 if o[n['axis']] < n['dist'] else n['rightChild']
    return i


def prio(e):
    p = e['priority']
    return p['name'] if isinstance(p, dict) else str(p)


def chains_ok(dyn):
    ents = dyn['dynEntDefList'][0]['values']
    nxt = [e['spatialNode']['spatialNodeNext'] for e in ents]
    pop = dyn['dynEntSpatialPopulation'][0]['values'][0]
    pid = pop['partitionIds']['values'] if isinstance(pop['partitionIds'], dict) else pop['partitionIds']
    groups = collections.defaultdict(list)
    for i, e in enumerate(ents):
        if prio(e) != 'DYNENT_PRIORITY_SPARSE_OCCLUDER' and e['spatialPartitionIndex'] != 0:
            groups[e['spatialPartitionIndex']].append(i)
    good = 0
    for b, head in enumerate(pop['buckets']['values']):
        m, i = [], head['spatialNodeNext']
        while i != NONE and i < len(ents) and len(m) < 10000:
            m.append(i)
            i = nxt[i]
        tail = pop['bucketTails']['values'][b]['spatialNodeNext']
        good += (m == sorted(groups.get(pid[b], []), reverse=True) and (m[-1] if m else NONE) == tail)
    return good, len(pop['buckets']['values']), set(pid)


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
    inst, dyn, nodes = load(stock_file, work, 'stock')
    ents = dyn['dynEntDefList'][0]['values']
    good, total, partitions = chains_ok(dyn)
    check(good == total, 'stock: %d of %d bucket chains list their partition exactly (the rule this edit keeps)' % (good, total))
    # Candidates: instances with exactly one ordinary, spatial dynamic entity.
    per = collections.defaultdict(list)
    for i, e in enumerate(ents):
        if e.get('scriptableMapIndex', NONE) < len(inst):
            per[e['scriptableMapIndex']].append(i)
    ordinary = [k for k, v in per.items() if len(v) == 1 and prio(ents[v[0]]) != 'DYNENT_PRIORITY_SPARSE_OCCLUDER'
                and ents[v[0]]['spatialPartitionIndex'] != 0]
    doors = [k for k, v in per.items() if any(prio(ents[j]) == 'DYNENT_PRIORITY_SPARSE_OCCLUDER' for j in v)]
    check(len(ordinary) >= 2 and doors, '%d instances with one ordinary dynamic entity, %d with a door-like one' % (len(ordinary), len(doors)))
    near_i = ordinary[0]
    o = inst[near_i]['origin']
    near_to = [o[0] + 4.0, o[1] + 4.0, o[2]]
    # A far move: the origin of another ordinary instance in another partition (that partition has a bucket).
    a_part = ents[per[near_i][0]]['spatialPartitionIndex']
    far_i = ordinary[1]
    target = next(k for k in ordinary if ents[per[k][0]]['spatialPartitionIndex'] != ents[per[far_i][0]]['spatialPartitionIndex'])
    far_to = [inst[target]['origin'][0] + 16.0, inst[target]['origin'][1], inst[target]['origin'][2]]
    edits = {'scriptable_instances': {'move': [{'index': near_i, 'origin': near_to, 'angles': [0, 30, 0]},
                                               {'index': far_i, 'origin': far_to, 'angles': [0, 200, 0]}]}, 'patch': False}
    code, log, rep = t.editzone(exe, zone, edits, os.path.join(work, 'm'))
    check(code == 0 and rep.get('scriptable_instances_moved_with_dynamic_entity') == 2,
          'editzone moves both (%s)' % {k: rep.get(k) for k in ('scriptable_instances_moved', 'scriptable_instances_moved_with_dynamic_entity', 'scriptable_dynamic_entities_relinked')})
    if code != 0:
        print(log[-2000:])
        return 1
    out_file = os.path.join(work, 'm', zone + '.ff')
    inst2, dyn2, nodes2 = load(out_file, work, 'm')
    ents2 = dyn2['dynEntDefList'][0]['values']
    for idx, to, ang in ((near_i, near_to, (0, 30, 0)), (far_i, far_to, (0, 200, 0))):
        e = ents2[per[idx][0]]
        pose = e['initialPose']
        check(all(abs(a - b) < 1e-3 for a, b in zip(inst2[idx]['origin'], to)) and all(abs(a - b) < 1e-3 for a, b in zip(pose['origin']['v'], to)),
              'instance %d and its dynamic entity are at %s' % (idx, to))
        check(same_rotation(pose['quat']['v'], quat(*ang)), 'its dynamic entity turned to %s' % (ang,))
        check(e['spatialPartitionIndex'] == walk(nodes2, to), 'its partition %d is the tree walk of the new origin' % e['spatialPartitionIndex'])
    check(ents2[per[far_i][0]]['spatialPartitionIndex'] != ents[per[far_i][0]]['spatialPartitionIndex'], 'the far move changed its partition')
    good2, total2, _ = chains_ok(dyn2)
    check(good2 == total2, 'every bucket chain lists its partition exactly, in descending order, with its tail (%d of %d)' % (good2, total2))
    moved_entries = {per[near_i][0], per[far_i][0]}
    def strip(node):
        if isinstance(node, dict):
            return {k: strip(v) for k, v in node.items() if k not in ('address', 'spatialNode')}
        if isinstance(node, list):
            return [strip(v) for v in node]
        return node
    others = [i for i in range(len(ents)) if i not in moved_entries and strip(ents[i]) != strip(ents2[i])]
    check(not others, 'every other dynamic entity is unchanged apart from its chain link %s' % others[:5])
    check([s for i, s in enumerate(inst2) if i not in (near_i, far_i)] == [s for i, s in enumerate(inst) if i not in (near_i, far_i)],
          'every other instance is unchanged')

    def results(lines):
        return lines[0].split('loaded,', 1)[1].strip() if lines and 'loaded,' in lines[0] else None
    test = [l for l in t.acts(['--test', '-o', os.path.join(work, 'm_test'), out_file]) if l.startswith('Replay:')]
    stock_test = [l for l in t.acts(['--test', '-o', os.path.join(work, 'stock_test'), stock_file]) if l.startswith('Replay:')]
    check(results(test) is not None and results(test) == results(stock_test), 'ACTS tests the edited zone like the stock zone: %s' % test)
    code3, log3, _ = t.editzone(exe, zone, {'scriptable_instances': {'move': [{'index': doors[0], 'origin': list(inst[doors[0]]['origin'])}]}, 'patch': False},
                                os.path.join(work, 'd'))
    check(code3 != 0 and 'sparse occluder' in log3, 'moving door %d is refused with the reason' % doors[0])
    for f in glob.glob(os.path.join(work, '*', zone + '.ff')):
        os.remove(f)
    print('RESULT: %s (%d failed)' % ('PASS' if not failures else 'FAIL', len(failures)))
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
