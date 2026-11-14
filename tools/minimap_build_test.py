"""Offline proof of the minimap's orientation on a built map (the owner's game test of 2026-11-14: the minimap was a
quarter turn off).

The game shows the minimap picture with world +Y at the top and +X on the right, over the square the map script hands
to setminimap. This proof builds the sample room with one tall block in its +X +Y corner twice through the editor's
headless build: once as it is (the build writes north = +Y, northyaw 90) and once with worldspawn northyaw 0 (north =
+X, which the converter turns a quarter). It reads the compass picture back from each built zone with ACTS and checks:
  - the converter's log: the minimap is turned 0 degrees for northyaw 90 and 90 degrees for northyaw 0;
  - in both built pictures the block is in the top right quarter (+X right, +Y up), where the game expects it;
  - the square the script gives setminimap is the map's corner square (north-west corner = lowest x, highest y).
The packages the builds write into the editor's output folder are deleted afterwards (this proof's own maps).

Usage: py -3 -I tools/minimap_build_test.py <work dir> [--editor <IW8Radiant.exe>] [--exe <zonetool.exe>]
Prints PASS / FAIL lines and RESULT ok|fail."""
import glob, os, re, shutil, struct, subprocess, sys  # noqa: E401

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
SUFFIX = ' tm2co/me_plaster_barrier_fence_wall 128 128 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0'

failures = 0


def check(ok, what):
    global failures
    print(('PASS ' if ok else 'FAIL ') + what, flush=True)
    if not ok:
        failures += 1


def pt(x, y, z):
    return '( %g %g %g )' % (x, y, z)


def box(x0, y0, z0, x1, y1, z1):
    poly = [(x0, y0), (x1, y0), (x1, y1), (x0, y1)]
    lines = ['{', ' %s %s %s%s' % (pt(x0, y0, z0), pt(x1, y0, z0), pt(x1, y1, z0), SUFFIX),
             ' %s %s %s%s' % (pt(x1, y1, z1), pt(x1, y0, z1), pt(x0, y0, z1), SUFFIX)]
    for k in range(4):
        (ax, ay), (bx, by) = poly[k], poly[(k + 1) % 4]
        lines.append(' %s %s %s%s' % (pt(ax, ay, z1), pt(bx, by, z1), pt(bx, by, z0), SUFFIX))
    lines.append('}')
    return '\n'.join(lines) + '\n'


def bc_colour_block(block):
    """The 16 red values (0..255) of a DXT1-style colour block."""
    c0, c1, bits = struct.unpack_from('<HHI', block, 0)
    r0, r1 = ((c0 >> 11) & 31) * 255 // 31, ((c1 >> 11) & 31) * 255 // 31
    table = [r0, r1, (2 * r0 + r1) // 3, (r0 + 2 * r1) // 3] if c0 > c1 else [r0, r1, (r0 + r1) // 2, 0]
    return [table[(bits >> (2 * i)) & 3] for i in range(16)]


def quadrant_brightness(dds_path):
    """Mean red of the four quarters of a DDS top level: [top left, top right, bottom left, bottom right]."""
    data = open(dds_path, 'rb').read()
    height, width = struct.unpack_from('<II', data, 12)
    fourcc = data[84:88]
    start = 128 + (20 if fourcc == b'DX10' else 0)
    red = [[0] * width for _ in range(height)]
    dxgi = struct.unpack_from('<I', data, 128)[0] if fourcc == b'DX10' else 0
    if fourcc == b'DXT5' or dxgi in (77, 78):
        for by in range(height // 4):
            for bx in range(width // 4):
                vals = bc_colour_block(data[start + (by * (width // 4) + bx) * 16 + 8:])
                for i, v in enumerate(vals):
                    red[by * 4 + i // 4][bx * 4 + i % 4] = v
    elif fourcc == b'DXT1' or dxgi in (71, 72):
        for by in range(height // 4):
            for bx in range(width // 4):
                vals = bc_colour_block(data[start + (by * (width // 4) + bx) * 8:])
                for i, v in enumerate(vals):
                    red[by * 4 + i // 4][bx * 4 + i % 4] = v
    else:
        for y in range(height):
            for x in range(width):
                red[y][x] = data[start + (y * width + x) * 4]
    out = []
    for q in range(4):
        x0, y0 = (q % 2) * width // 2, (q // 2) * height // 2
        total = sum(red[y][x] for y in range(y0, y0 + height // 2) for x in range(x0, x0 + width // 2))
        out.append(total / (width * height / 4))
    return out, width, height


shutil.rmtree(work, ignore_errors=True)
os.makedirs(work)
src = open(os.path.join(root, 'tests', 'data', 'iw8_sample_room.map'), encoding='utf-8').read()
mark = '}\n// entity 1\n'
assert src.count(mark) == 1, src.count(mark)
scratch = os.path.join(os.path.splitdrive(work)[0] + os.sep, 't', 'mmp')
shutil.rmtree(scratch, ignore_errors=True)
env = dict(os.environ)
if converter:
    env['IW8R_TOOL_ZONETOOL'] = converter
for name, keys, turn in (('iw8r_mmn', '', 0), ('iw8r_mme', '"northyaw" "0"\n', 90)):
    map_id = 'mp_' + name
    text = src.replace(mark, box(96, 96, 0, 240, 240, 400) + mark, 1)
    if keys:
        text = text.replace('"classname" "worldspawn"', '"classname" "worldspawn"\n' + keys.rstrip('\n'), 1)
    map_path = os.path.join(work, name + '.map')
    open(map_path, 'w', encoding='utf-8', newline='\n').write(text)
    out = os.path.join(work, 'build_' + name)
    subprocess.run([editor, '--compile', map_path, '--out', out], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, env=env)
    result = open(os.path.join(out, 'compile-result.txt'), encoding='utf-8', errors='replace').read() if os.path.isfile(os.path.join(out, 'compile-result.txt')) else ''
    log = open(os.path.join(out, 'compile-log.txt'), encoding='utf-8', errors='replace').read() if os.path.isfile(os.path.join(out, 'compile-log.txt')) else ''
    first = result.splitlines()[0] if result else ''
    check(first.startswith('ok map=' + map_id), '%s: build ok (%s)' % (map_id, first[:90]))
    m = re.search(r'package=(.+)$', first)
    package = m.group(1).strip() if m else ''
    rot = re.search(r'compass: rotating the minimap (\d+) degrees for northyaw ([\d.]+)', log)
    check(rot is not None and int(rot.group(1)) == turn, '%s: the converter turns the picture %s degrees (northyaw %s)' %
          (map_id, rot.group(1) if rot else '?', rot.group(2) if rot else '?'))
    sq = re.search(r'compass: (?:squared minimap region to )?northwest ([-\d.]+) ([-\d.]+), southeast ([-\d.]+) ([-\d.]+)', log)
    last = None
    for mm_ in re.finditer(r'compass: (?:squared minimap region to )?northwest ([-\d.]+) ([-\d.]+), southeast ([-\d.]+) ([-\d.]+)', log):
        last = [float(v) for v in mm_.groups()]
    check(last is not None and last[0] < last[2] and last[1] > last[3] and abs((last[2] - last[0]) - (last[1] - last[3])) < 1.0,
          '%s: the minimap square: north-west %s, south-east %s (lowest x and highest y first)' %
          (map_id, last[:2] if last else '?', last[2:] if last else '?'))
    ff = os.path.join(package, map_id + '.ff') if package else ''
    found = []
    if ff and os.path.isfile(ff):
        dest = os.path.join(scratch, name)
        subprocess.run([ACTS, '--noUpdater', 'fastfile', '-r', 'mw19replay', '-g', REPLAY, '--oodle', OODLE, '-a', 'image', '-l', '-o', dest, ff],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        found = glob.glob(os.path.join(dest, 'mw19replay', map_id, 'assets', 'image', '**', 'compass_map_' + map_id + '*.dds'), recursive=True)
    if found:
        q, w, h = quadrant_brightness(found[0])
        check(q.index(max(q)) == 1, '%s: in the built picture (%d x %d) the +X +Y block is in the top right quarter (brightness %s)' %
              (map_id, w, h, [round(v, 1) for v in q]))
    else:
        check(False, '%s: compass picture read back from the zone' % map_id)
    if package and os.path.basename(package.rstrip('/\\')) == map_id:
        shutil.rmtree(package, ignore_errors=True)

shutil.rmtree(scratch, ignore_errors=True)
print('RESULT ' + ('ok' if failures == 0 else 'fail'))
sys.exit(0 if failures == 0 else 1)
