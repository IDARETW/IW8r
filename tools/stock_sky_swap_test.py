"""Offline proof of swapping a stock map's sky (editzone "carry" + "replace_materials", roadmap 11.6).

The target zone's sky material (the world material named tw/sky_<map>) is replaced by another map's sky, carried in
from that map's zone. Checked on the rebuilt zones: every world surface that used the old sky now names the new one and
no other surface changed its material; ACTS tests the map zone like the stock one and the techsets zone with 0 failed.

Usage: py -3 -I tools/stock_sky_swap_test.py <work folder> [--zone mp_shipment] [--from-zone mp_backlot2] [--exe <zonetool.exe>]
Nothing is written outside the work folder. The game folder is only read."""
import collections, glob, json, os, re, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import stock_edit_test as t


def surface_materials(zone_file, out):
    files = t.export(zone_file, 'gfx_map', out)
    path = [f for f in files if f.endswith('.json')][0]
    f = t.fields(path)
    surfs = f['surfaces']['surfaces']
    surfs = surfs.get('values') if isinstance(surfs, dict) else surfs
    return [((s.get('material') or {}).get('name') or '').lstrip(',') for s in surfs]


def main():
    args = sys.argv[1:]
    zone, from_zone, exe = 'mp_shipment', 'mp_backlot2', t.EXE
    for flag in ('--zone', '--from-zone', '--exe'):
        while flag in args:
            i = args.index(flag)
            if flag == '--zone': zone = args[i + 1]
            elif flag == '--from-zone': from_zone = args[i + 1]
            else: exe = args[i + 1]
            del args[i:i + 2]
    work = os.path.abspath(args[0])
    os.makedirs(work, exist_ok=True)
    failures = []

    def check(ok, text):
        print(('  ok   ' if ok else '  FAIL ') + text)
        if not ok:
            failures.append(text)

    stock_file = os.path.join(t.GAME, 'zone', zone + '.ff')
    before = surface_materials(stock_file, os.path.join(work, 'stock_gfx'))
    skies = collections.Counter(m for m in before if re.match(r'^[a-z0-9]+/sky_', m))
    old = skies.most_common(1)[0][0] if skies else None
    new = 'tw/sky_' + from_zone
    print('%s: %d surfaces, sky %s on %d; new sky %s from %s' % (zone, len(before), old, skies.get(old, 0), new, from_zone))
    check(old is not None, 'the map has a sky material')
    if old is None:
        return 1
    edits = {'carry': [{'zone': 'techsets_' + from_zone, 'type': 'material', 'name': new}], 'replace_materials': [{'from': '@sky', 'to': new}], 'patch': False}
    code, log, rep = t.editzone(exe, zone, edits, os.path.join(work, 's'))
    check(code == 0 and rep.get('world_surfaces_rematerialed') == skies[old],
          'editzone finishes and re-materials %s surfaces (%s)' % (skies[old], {k: rep.get(k) for k in ('world_surfaces_rematerialed', 'assets_added', 'techset_assets_added')}))
    if code != 0:
        print(log[-2500:])
        return 1
    out_file = os.path.join(work, 's', zone + '.ff')
    after = surface_materials(out_file, os.path.join(work, 's_gfx'))
    check(len(after) == len(before), 'the same %d surfaces' % len(after))
    check(all((b == new) if a == old else (b == a) for a, b in zip(before, after)),
          'every surface of %s now names %s, every other surface keeps its material' % (old, new))

    def acts_line(zone_path, out):
        lines = [l for l in t.acts(['--test', '-o', out, zone_path]) if l.startswith('Replay:')]
        return lines[0] if lines else None
    test = acts_line(out_file, os.path.join(work, 's_test'))
    stock_test = acts_line(stock_file, os.path.join(work, 'stock_test'))
    same = test and stock_test and test.split('tested,', 1)[1] == stock_test.split('tested,', 1)[1]
    check(bool(same), 'ACTS tests the map zone like the stock zone: %s (stock %s)' % (test, stock_test))
    tech = os.path.join(work, 's', 'techsets_' + zone + '.ff')
    if os.path.exists(tech):
        tl = acts_line(tech, os.path.join(work, 't_test'))
        check(tl is not None and ' 0 failed' in tl, 'ACTS tests the techsets zone with the carried sky: %s' % tl)
    for f in glob.glob(os.path.join(work, 's', '*.ff')):
        os.remove(f)
    print('RESULT: %s (%d failed)' % ('PASS' if not failures else 'FAIL', len(failures)))
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
