"""Offline proof of removing placed scriptables in the converter's editzone ("scriptable_instances" "remove"): two
Warzone buy stations (br_plunder_box, no dynamic entity of their own) are dropped.

- the instance list equals the stock list without those two (order kept);
- the counts and the six physics limits fall by the definition's numbers;
- every scriptable dynamic entity still names the same instance (same definition and origin) after the renumbering;
- ACTS tests the zone like the stock one; an instance that has a dynamic entity is removed too.

Usage: py -3 -I tools/warzone_instances_remove_test.py <work folder> [--zone mp_br_quarry] [--exe <zonetool.exe>]
Nothing is written outside the work folder. The game folder is only read."""
import glob, os, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import stock_edit_test as t
from warzone_instances_edit_test import catalogue, instances_of


def dyn_refs(zone_file, out):
    """(side, entry, scriptableMapIndex) of every scriptable dynamic entity (types 6 and 7 per the converter's enum,
    read as the export names them)."""
    files = t.export(zone_file, 'dynentitylist', out)
    refs = []
    for path in files:
        f = t.fields(path)
        lists = f.get('dynEntDefList') or []
        for side, node in enumerate(lists):
            for i, d in enumerate((node or {}).get('values') or []):
                if 'scriptableMapIndex' in d and str(d.get('type', '')).upper().find('SCRIPTABLE') >= 0:
                    refs.append((side, i, d['scriptableMapIndex']))
    return refs


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
    stock_refs = dyn_refs(stock_file, os.path.join(work, 'stock_dyn'))
    boxes = [i for i, s in enumerate(stock) if s['definition'].lstrip(',') == 'br_plunder_box']
    print('stock %s: %d instances, %d buy stations, %d scriptable dynamic entities' % (zone, len(stock), len(boxes), len(stock_refs)))
    check(len(boxes) >= 2 and len(stock_refs) > 0, 'the map has buy stations and scriptable dynamic entities')
    if len(boxes) < 2:
        return 1
    gone = [boxes[0], boxes[len(boxes) // 2]]
    edits = {'scriptable_instances': {'remove': [dict(index=i, **numbers) for i in gone]}, 'patch': False}
    code, log, rep = t.editzone(exe, zone, edits, os.path.join(work, 'r'))
    check(code == 0 and rep.get('scriptable_instances_removed') == 2, 'editzone finishes and removes 2 (%s)' %
          {k: rep.get(k) for k in ('scriptable_instances_removed', 'scriptable_dynamic_entities_renumbered', 'scriptable_instances')})
    if code != 0:
        print(log[-2000:])
        return 1
    out_file = os.path.join(work, 'r', zone + '.ff')
    got, got_counts, got_limits = instances_of(t.export(out_file, 'map_ents', os.path.join(work, 'r_me'))[0])
    want = [s for i, s in enumerate(stock) if i not in gone]
    check(got == want, 'the instance list is the stock list without the two (%d -> %d)' % (len(stock), len(got)))
    d_server = 2 if numbers['server_instanced_parts'] else 0
    check(got_counts['mapInstanceCount'] == counts['mapInstanceCount'] - 2 and
          got_counts['serverInstanceCount'] == counts['serverInstanceCount'] - d_server and
          got_counts['serverFixedPartCount'] == counts['serverFixedPartCount'] - 2 * numbers['server_instanced_parts'] and
          got_counts['serverUsablePartCount'] == counts['serverUsablePartCount'] - 2 * numbers['usable_parts'],
          'the counts fall by the definition numbers (%s -> %s)' % (counts, got_counts))
    main_l = 2 if numbers['physics_limit'] else 0
    detail_l = 2 if numbers['physics_limit_detail'] else 0
    ok_limits = all(got_limits[k] == limits[k] - (detail_l if 'Detailed' in k else main_l) for k in limits)
    check(ok_limits, 'the six physics limits fall by 2 (%s)' % {k: (limits[k], got_limits[k]) for k in limits})
    got_refs = dyn_refs(out_file, os.path.join(work, 'r_dyn'))
    # The reserved physics entries name no instance (500000): they must keep that value.
    same = len(got_refs) == len(stock_refs) and all(
        a[:2] == b[:2] and ((a[2] >= len(stock) and b[2] == a[2]) or (a[2] < len(stock) and b[2] < len(got) and stock[a[2]] == got[b[2]]))
        for a, b in zip(stock_refs, got_refs))
    moved = sum(1 for a, b in zip(stock_refs, got_refs) if a[2] != b[2])
    check(same, 'every scriptable dynamic entity still names the same instance (%d renumbered)' % moved)

    def results(lines):
        return lines[0].split('loaded,', 1)[1].strip() if lines and 'loaded,' in lines[0] else None
    test = [l for l in t.acts(['--test', '-o', os.path.join(work, 'r_test'), out_file]) if l.startswith('Replay:')]
    stock_test = [l for l in t.acts(['--test', '-o', os.path.join(work, 'stock_test'), stock_file]) if l.startswith('Replay:')]
    check(results(test) is not None and results(test) == results(stock_test), 'ACTS tests the edited zone like the stock zone: %s' % test)
    # An instance with a dynamic entity is removed too since x64-zt 45112b43 (its entries go with it; checked in detail
    # by tools/dynent_copy_remove_test.py).
    with_dyn = next(r[2] for r in stock_refs if r[2] < len(stock))
    code2, log2, _ = t.editzone(exe, zone, {'scriptable_instances': {'remove': [dict(index=with_dyn, **numbers)]}, 'patch': False}, os.path.join(work, 'x'))
    check(code2 == 0, 'removing instance %d (it has a dynamic entity) works%s' % (with_dyn, '' if code2 == 0 else ': ' + log2[-300:]))
    for f in glob.glob(os.path.join(work, '*', zone + '.ff')):
        os.remove(f)
    print('RESULT: %s (%d failed)' % ('PASS' if not failures else 'FAIL', len(failures)))
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
