"""Offline proof of moving and adding placed scriptables in the converter's editzone (docs/research/89_warzone_work.md
section 6): the Warzone buy station (br_plunder_box), which has no dynamic entity of its own.

1. One buy station moved, two copies added: the instance list, the counts and the physics limits follow; every
   other instance is unchanged; ACTS tests the zone like the stock one.
2. An instance that has a dynamic entity (a door or any other) is refused with the reason.

Usage: py -3 -I tools/warzone_instances_edit_test.py <work folder> [--zone mp_br_quarry] [--exe <zonetool.exe>]"""
import csv, glob, json, os, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import stock_edit_test as t
import warzone_loot_rules as rules

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def catalogue():
    with open(os.path.join(ROOT, 'data', 'asset_catalog', 'iw8_scriptables.tsv'), encoding='utf-8') as f:
        lines = [l for l in f if not l.startswith('#')]
    return {row['name']: row for row in csv.DictReader(lines, delimiter='\t')}


def name_of(node):
    return str((node or {}).get('name') or (node or {}).get('string') or '')


def instances_of(path):
    f = t.fields(path)
    sm = f['scriptableMapEnts']
    out = []
    for i in (sm.get('mapInstances') or {}).get('values') or []:
        out.append(dict(definition=name_of(i['initialDef']), origin=tuple(rules.vec(i['initialOrigin'])), angles=tuple(rules.vec(i['initialAngles'])),
                        classname=i['classname']['value'], names=tuple(i[k]['value'] for k in ('target', 'targetname', 'script_linkname', 'script_linkto', 'script_noteworthy')),
                        flags=i['flags']))
    counts = {k: sm[k] for k in ('mapInstanceCount', 'lootInstanceCount', 'serverInstanceCount', 'serverFixedPartCount', 'serverUsablePartCount')}
    return out, counts, dict(sm['physicsLimits'])


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

    row = catalogue()['br_plunder_box']
    numbers = dict(server_instanced_parts=int(row['server_instanced_parts']), usable_parts=int(row['usable_parts']),
                   physics_limit=row['physics_limit'] == '1', physics_limit_detail=row['physics_limit_detail'] == '1')
    stock_file = os.path.join(t.GAME, 'zone', zone + '.ff')
    stock, counts, limits = instances_of(t.export(stock_file, 'map_ents', os.path.join(work, 'stock_me'))[0])
    boxes = [i for i, s in enumerate(stock) if s['definition'].lstrip(',') == 'br_plunder_box']
    print('stock %s: %d instances, %d buy stations, counts %s, limits %s' % (zone, len(stock), len(boxes), counts, sorted(set(limits.values()))))
    check(len(boxes) > 0, 'the map has buy stations')
    if not boxes:
        return 1

    print('1. one buy station moved, two copies added')
    first = boxes[0]
    o = stock[first]['origin']
    moved_to = [o[0] + 64.0, o[1], o[2]]
    adds = [dict(like=first, origin=[o[0], o[1] + 128.0, o[2]], angles=[0, 45, 0], **numbers),
            dict(like=first, origin=[o[0] - 256.0, o[1], o[2] + 8.0], **numbers)]
    edits = {'scriptable_instances': {'move': [{'index': first, 'origin': moved_to, 'angles': [0, 90, 0]}], 'add': adds}}
    code, log, rep = t.editzone(exe, zone, edits, os.path.join(work, 'a'))
    check(code == 0, 'editzone finishes (%s)' % rep)
    if code != 0:
        print(log[-2000:])
        return 1
    a_file = os.path.join(work, 'a', zone + '.ff')
    got, a_counts, a_limits = instances_of(t.export(a_file, 'map_ents', os.path.join(work, 'a_me'))[0])
    check(len(got) == len(stock) + 2, '%d instances read back (%d + 2)' % (len(got), len(stock)))
    same = all(g == s for i, (g, s) in enumerate(zip(got, stock)) if i != first)
    check(same, 'every other instance is unchanged')
    m = got[first]
    check(m['origin'] == tuple(float(v) for v in moved_to) and m['angles'] == (0.0, 90.0, 0.0) and m['definition'] == stock[first]['definition'] and
          m['classname'] == stock[first]['classname'], 'the moved buy station has its new place and angles')
    for k, add in enumerate(adds):
        g = got[len(stock) + k]
        check(g['definition'] == stock[first]['definition'] and g['classname'] == stock[first]['classname'] and g['flags'] == stock[first]['flags'] and
              g['origin'] == tuple(float(v) for v in add['origin']) and g['angles'] == tuple(float(v) for v in add.get('angles', [0, 0, 0])) and
              g['names'] == (0, stock[first]['names'][1], 0, 0, stock[first]['names'][4]),
              'copy %d: the definition and class name of the buy station at its place, its targetname and script_noteworthy, no links' % (k + 1))
    n = len(adds)
    want = dict(counts)
    want['mapInstanceCount'] += n
    want['serverInstanceCount'] += n * (1 if numbers['server_instanced_parts'] else 0)
    want['serverFixedPartCount'] += n * numbers['server_instanced_parts']
    want['serverUsablePartCount'] += n * numbers['usable_parts']
    check(a_counts == want, 'the instance count and the server counts rose by the definition\'s numbers (%s)' % a_counts)
    want_limits = {k: v + (n if ((numbers['physics_limit_detail'] if 'Detailed' in k else numbers['physics_limit'])) else 0) for k, v in limits.items()}
    check(a_limits == want_limits, 'the six physics limits rose by the definition\'s share (%s)' % sorted(set(a_limits.values())))
    test = [l for l in t.acts(['--test', '-o', os.path.join(work, 'a_test'), a_file]) if l.startswith('Replay:')]
    stock_test = [l for l in t.acts(['--test', '-o', os.path.join(work, 'stock_test'), stock_file]) if l.startswith('Replay:')]

    def results(lines):
        return lines[0].split('loaded,', 1)[1].strip() if lines and 'loaded,' in lines[0] else None
    check(results(test) is not None and results(test) == results(stock_test), 'ACTS tests it like the stock zone: %s (stock: %s)' % (test, stock_test))
    patch = os.path.join(work, 'a', 'patch', zone + '.fp')
    check(os.path.exists(patch), 'the install patch was written (%.2f MB)' % (os.path.getsize(patch) / 1e6 if os.path.exists(patch) else 0))

    print('2. an instance with a dynamic entity of its own is refused')
    other = next((i for i, s in enumerate(stock) if 'door' in s['definition']), None)
    if other is None:
        other = next(i for i, s in enumerate(stock) if i not in boxes)
    code, log, rep = t.editzone(exe, zone, {'scriptable_instances': {'move': [{'index': other, 'origin': [0, 0, 0]}]}, 'patch': False}, os.path.join(work, 'b'))
    refused = code != 0 and ('dynamic entity of its own' in log or 'sparse occluder' in log or 'placed apart' in log or 'would leave its area' in log)
    if code == 0:
        print('       note: instance %d (%s) has no dynamic entity in this zone' % (other, stock[other]['definition']))
    check(refused, 'moving instance %d (%s) is refused with the reason' % (other, stock[other]['definition']))
    code, log, rep = t.editzone(exe, zone, {'scriptable_instances': {'add': [dict(like=len(stock) + 5, origin=[0, 0, 0], **numbers)]}, 'patch': False},
                                os.path.join(work, 'c'))
    check(code != 0 and 'beyond the map' in log, 'an index beyond the list is refused')
    for name in ('a', 'b', 'c'):
        for f in glob.glob(os.path.join(work, name, '*.ff')):
            os.remove(f)
    print('RESULT: %s (%d failed)' % ('PASS' if not failures else 'FAIL', len(failures)))
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
