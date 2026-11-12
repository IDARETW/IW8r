"""Offline proof of the stock sun edit (editzone "sun", roadmap 11.6).

editzone with {"sun": {"color": [r, g, b], "intensity": x}} on a stock zone: the report names the sun primary light
(the first stage's sunPrimaryLightIndex) with its old and new values; ACTS exports the ComWorld of the rebuilt zone,
where that light has the new colour and intensity and every other primary light is unchanged; ACTS tests the zone like
the stock zone.

Usage: py -3 -I tools/stock_sun_edit_test.py <work folder> [--zone mp_shipment] [--exe <zonetool.exe>]
Nothing is written outside the work folder. The game folder is only read."""
import glob, json, os, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import stock_edit_test as t

COLOR = [1.0, 0.55, 0.35]
INTENSITY = 7.5


def strip(node):
    """An export node without the addresses, which differ from run to run."""
    if isinstance(node, dict):
        return {k: strip(v) for k, v in node.items() if k != 'address'}
    if isinstance(node, list):
        return [strip(v) for v in node]
    return node


def lights_of(export):
    f = t.fields(export)
    lights = f.get('primaryLights')
    if isinstance(lights, dict):
        lights = lights.get('values') or []
    return strip(lights or [])


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
    stock_exports = t.export(stock_file, 'com_map', os.path.join(work, 'stock_com'))
    check(len(stock_exports) == 1, 'ACTS exports the stock ComWorld (%d file(s))' % len(stock_exports))
    if len(stock_exports) != 1:
        return 1
    stock = lights_of(stock_exports[0])
    code, log, rep = t.editzone(exe, zone, {'sun': {'color': COLOR, 'intensity': INTENSITY}, 'patch': False}, os.path.join(work, 'sun'))
    check(code == 0 and 'sun_primary_light' in rep, 'editzone finishes and names the sun light (%s)' %
          {k: rep.get(k) for k in ('sun_primary_light', 'sun_color_before', 'sun_intensity_before', 'sun_color', 'sun_intensity')})
    if code != 0:
        print(log[-2000:])
        return 1
    index = rep['sun_primary_light']
    out_file = os.path.join(work, 'sun', zone + '.ff')
    edited_exports = t.export(out_file, 'com_map', os.path.join(work, 'sun_com'))
    edited = lights_of(edited_exports[0]) if len(edited_exports) == 1 else []
    check(len(edited) == len(stock) and len(stock) > index, '%d primary lights, the sun is light %d' % (len(edited), index))
    if len(edited) == len(stock) and len(stock) > index:
        sun = edited[index]
        color = sun.get('colorLinearSrgb')
        if isinstance(color, dict):
            color = color.get('v') or color.get('values')
        check(color is not None and all(abs(a - b) < 1e-5 for a, b in zip(color, COLOR)), 'the sun has the new colour %s' % (color,))
        check(abs(sun.get('intensity', -1) - INTENSITY) < 1e-5, 'the sun has the new intensity %s' % sun.get('intensity'))
        before = stock[index]
        others = [i for i in range(len(stock)) if i != index and stock[i] != edited[i]]
        check(not others, 'every other primary light is unchanged %s' % (others[:5] or ''))
        same = {k: v for k, v in sun.items() if k not in ('colorLinearSrgb', 'intensity')} == \
               {k: v for k, v in before.items() if k not in ('colorLinearSrgb', 'intensity')}
        check(same, 'the sun keeps its direction and every other field')

    def results(lines):
        return lines[0].split('loaded,', 1)[1].strip() if lines and 'loaded,' in lines[0] else None
    test = [l for l in t.acts(['--test', '-o', os.path.join(work, 'sun_test'), out_file]) if l.startswith('Replay:')]
    stock_test = [l for l in t.acts(['--test', '-o', os.path.join(work, 'stock_test'), stock_file]) if l.startswith('Replay:')]
    check(results(test) is not None and results(test) == results(stock_test), 'ACTS tests the edited zone like the stock zone: %s (stock %s)' % (test, stock_test))
    for f in glob.glob(os.path.join(work, 'sun', '*.ff')):
        os.remove(f)
    print('RESULT: %s (%d failed)' % ('PASS' if not failures else 'FAIL', len(failures)))
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
