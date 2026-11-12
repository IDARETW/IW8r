"""Offline proof of retexturing a stock map (editzone "carry" + "replace_materials" with named materials).

On mp_shipment, the world material used by the most surfaces (not the sky) is replaced with tm2co/me_curb_stone_01,
carried from techsets_mp_backlot2. Checked: exactly those surfaces name the new material, every other surface keeps
its material, ACTS tests the map zone with 0 failed.

Usage: py -3 -I tools/stock_retexture_test.py <work folder> [--exe <zonetool.exe>]"""
import collections, glob, os, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import stock_edit_test as t
from stock_sky_swap_test import surface_materials

NEW = 'tm2co/me_curb_stone_01'


def main():
    args = sys.argv[1:]
    exe = t.EXE
    if '--exe' in args:
        exe = args[args.index('--exe') + 1]
        del args[args.index('--exe'):args.index('--exe') + 2]
    work = os.path.abspath(args[0])
    os.makedirs(work, exist_ok=True)
    failures = []

    def check(ok, text):
        print(('  ok   ' if ok else '  FAIL ') + text)
        if not ok:
            failures.append(text)

    zone = 'mp_shipment'
    stock_file = os.path.join(t.GAME, 'zone', zone + '.ff')
    before = surface_materials(stock_file, os.path.join(work, 'stock_gfx'))
    counts = collections.Counter(m for m in before if '/sky_' not in m and m != NEW)
    old, n = counts.most_common(1)[0]
    print('%s: %d surfaces; %s on %d -> %s' % (zone, len(before), old, n, NEW))
    edits = {'carry': [{'zone': 'techsets_mp_backlot2', 'type': 'material', 'name': NEW}],
             'replace_materials': [{'from': old, 'to': NEW}], 'patch': False}
    code, log, rep = t.editzone(exe, zone, edits, os.path.join(work, 'r'))
    check(code == 0 and rep.get('world_surfaces_rematerialed') == n, 'editzone re-materials %d surfaces (%s)' % (n, rep.get('world_surfaces_rematerialed')))
    if code != 0:
        print(log[-2000:])
        return 1
    out_file = os.path.join(work, 'r', zone + '.ff')
    after = surface_materials(out_file, os.path.join(work, 'r_gfx'))
    check(len(after) == len(before) and all((b == NEW) if a == old else (b == a) for a, b in zip(before, after)),
          'exactly the %d surfaces of %s now name %s' % (n, old, NEW))
    lines = [l for l in t.acts(['--test', '-o', os.path.join(work, 'r_test'), out_file]) if l.startswith('Replay:')]
    check(bool(lines) and ' 0 failed' in lines[0], 'ACTS tests the map zone: %s' % lines)
    for f in glob.glob(os.path.join(work, 'r', '*.ff')):
        os.remove(f)
    print('RESULT: %s (%d failed)' % ('PASS' if not failures else 'FAIL', len(failures)))
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
