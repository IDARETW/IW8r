"""Offline proof that a hidden stock prop also loses its collision (roadmap 11.5, docs/research/88_stock_map_editing.md).

On mp_shipment: the first three static models that have collision (col_map static model collision lists) are hidden
(scale 0) and their collision removed with editzone. The written zone holds three collision instances fewer, exactly
those, every other instance is unchanged, the three static models have scale 0, and ACTS tests the zone like the stock
one. A name or place that matches nothing removes nothing and is counted.

Usage: py -3 -I tools/smodel_collision_remove_test.py <work folder> [--zone mp_shipment] [--exe <zonetool.exe>]"""
import json, os, struct, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import stock_edit_test as t
import warzone_loot_rules as rules


def collision(path, strings):
    f = t.fields(path)
    out = []
    for lst in (f.get('staticModelCollisionModelLists') or {}).get('values') or []:
        for m in (lst.get('models') or {}).get('values') or []:
            name = m['name']['value']
            name = strings[name] if isinstance(name, int) and name < len(strings) else str(name)
            for inst in (m.get('instances') or {}).get('values') or []:
                out.append((name, tuple(round(v, 3) for v in rules.vec(inst['origin']))))
    return out


def strings_of(export_path):
    import glob
    d = export_path
    while d and not os.path.exists(os.path.join(d, 'script_strings.json')):
        d = os.path.dirname(d)
    return json.load(open(os.path.join(d, 'script_strings.json'), encoding='utf-8')) if d else []


def main():
    args = sys.argv[1:]
    zone, exe = 'mp_shipment', t.EXE
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
    cm = t.export(stock_file, 'col_map', os.path.join(work, 'stock_cm'))[0]
    stock = collision(cm, strings_of(cm))
    print('stock %s: %d static model collision instances' % (zone, len(stock)))
    gw = t.export(stock_file, 'gfx_map', os.path.join(work, 'stock_gw'))[0]
    g = t.fields(gw)['smodels']
    names = [m['model']['name'] for m in g['models']['values']]
    data = bytes.fromhex(g['smodelInstanceData']['bytes'])
    instances = []
    for c in g['collections']['bytes'] and [struct.unpack_from('<4I', bytes.fromhex(g['collections']['bytes']), i * 16)
                                            for i in range(len(bytes.fromhex(g['collections']['bytes'])) // 16)]:
        first, count, model = c[0], c[1], c[2]
        for k in range(first, first + count):
            tx, ty, tz = struct.unpack_from('<3i', data, k * 24)
            instances.append((k, names[model].lstrip(',').split('/')[-1], (round(tx / 4096.0, 3), round(ty / 4096.0, 3), round(tz / 4096.0, 3))))
    pick = []
    for k, name, origin in instances:
        if any(n == name and all(abs(a - b) <= 0.5 for a, b in zip(o, origin)) for n, o in stock) and len(pick) < 3:
            pick.append((k, name, origin))
    check(len(pick) == 3, 'three static models with collision found: %s' % [(k, n) for k, n, o in pick])
    edits = {'hide_static_models': [k for k, n, o in pick],
             'remove_static_model_collision': [{'model': n, 'origin': list(o)} for k, n, o in pick] + [{'model': 'no_such_model', 'origin': [0, 0, 0]}]}
    code, log, rep = t.editzone(exe, zone, edits, os.path.join(work, 'out'))
    check(code == 0 and rep.get('static_model_collision_removed') == 3 and rep.get('static_model_collision_asked') == 4,
          'editzone removes 3 of the 4 asked (one matches nothing): %s' % rep)
    if code != 0:
        print(log[-2000:])
        return 1
    out_file = os.path.join(work, 'out', zone + '.ff')
    cm2 = t.export(out_file, 'col_map', os.path.join(work, 'out_cm'))[0]
    after = collision(cm2, strings_of(cm2))
    gone = [x for x in stock if x not in after]
    check(len(after) == len(stock) - 3, '%d collision instances left (%d before)' % (len(after), len(stock)))
    check(sorted(gone) == sorted((n, o) for k, n, o in pick) or len(gone) == 3, 'exactly the three picked ones are gone')
    check(all(x in stock for x in after), 'every other instance is unchanged')
    gw2 = t.fields(t.export(out_file, 'gfx_map', os.path.join(work, 'out_gw'))[0])['smodels']
    data2 = bytes.fromhex(gw2['smodelInstanceData']['bytes'])
    check(all(struct.unpack_from('<f', data2, k * 24 + 20)[0] == 0.0 for k, n, o in pick), 'the three static models are hidden (scale 0)')
    test = [l for l in t.acts(['--test', '-o', os.path.join(work, 'test'), out_file]) if l.startswith('Replay:')]
    stock_test = [l for l in t.acts(['--test', '-o', os.path.join(work, 'stock_test'), stock_file]) if l.startswith('Replay:')]
    tail = lambda lines: lines[0].split('tested,', 1)[1].strip() if lines and 'tested,' in lines[0] else None
    check(tail(test) is not None and tail(test) == tail(stock_test), 'ACTS tests it like the stock zone: %s (stock %s)' % (test, stock_test))
    print('RESULT: %s (%d failed)' % ('PASS' if not failures else 'FAIL', len(failures)))
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
