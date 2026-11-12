"""Runs the converter's editzone on the files the editor wrote for a stock rebuild (the GUI test leaves them under
<gui out>/stock_rebuild_edit) and checks that an unchanged stock document rebuilds to the stock entities and spawns.

Usage: py -3 -I tools/stock_edit_editor_check.py <folder with edit.ents and edits.json> [--zone mp_shipment]
           [--exe <zonetool.exe>]"""
import json, os, shutil, subprocess, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import stock_edit_test as t


def main():
    args = sys.argv[1:]
    zone, exe = 'mp_shipment', r'E:\mw124\x64-zt\build\bin\x64\Release\zonetool.exe'
    while '--zone' in args:
        i = args.index('--zone'); zone = args[i + 1]; del args[i:i + 2]
    while '--exe' in args:
        i = args.index('--exe'); exe = args[i + 1]; del args[i:i + 2]
    work = os.path.abspath(args[0])
    out = os.path.join(work, 'out')
    shutil.rmtree(out, ignore_errors=True)
    r = subprocess.run([exe, '-game', 'iw8', '-path', t.GAME, 'editzone', zone, os.path.join(work, 'edits.json'), out],
                       capture_output=True, text=True, errors='replace')
    print('converter exit', r.returncode)
    if r.returncode != 0:
        print((r.stdout + r.stderr)[-1500:])
        return 1
    report = json.load(open(os.path.join(out, zone + '.edit-report.json'), encoding='utf-8'))
    print('report', report)
    failures = 0

    def check(ok, text):
        nonlocal failures
        print(('  ok   ' if ok else '  FAIL ') + text)
        failures += 0 if ok else 1

    stock_file = os.path.join(t.GAME, 'zone', zone + '.ff')
    built = os.path.join(out, zone + '.ff')
    se = t.export(stock_file, 'map_ents', os.path.join(work, 'check_stock'))[0]
    be = t.export(built, 'map_ents', os.path.join(work, 'check_built'))[0]
    sm, bm = t.fields(se), t.fields(be)
    check(t.entity_text(bm) == t.entity_text(sm), 'the rebuilt entity string equals the stock one (%d characters)' % len(t.entity_text(bm)))
    ss, bs = t.spawn_rows(sm, t.strings_of(se)), t.spawn_rows(bm, t.strings_of(be))
    check(ss == bs, 'the rebuilt spawn list equals the stock one (%d records)' % len(bs))
    if ss != bs:
        print('       counts', len(ss), len(bs))
        for i in [i for i, (x, y) in enumerate(zip(ss, bs)) if x != y][:3]:
            print('       stock', ss[i]); print('       ours ', bs[i])
    test = [l for l in t.acts(['--test', '-o', os.path.join(work, 'check_test'), built]) if l.startswith('Replay:')]
    check(bool(test) and ' 0 failed' in test[0], 'ACTS loads the rebuilt zone: %s' % test)
    print('RESULT:', 'PASS' if not failures else 'FAIL')
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
