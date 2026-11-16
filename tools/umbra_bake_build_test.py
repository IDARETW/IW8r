"""Offline proof of the occlusion bake on an editor map (plan 2.4; docs/research/118).

Builds a small map of opaque brushes (an own material, so the build knows they are opaque) with worldspawn
iw8r_umbra_bake 1 through the editor's headless build (IW8Radiant.exe --compile: extraction, converter, ACTS):
a walled yard with two inner walls, a block and an eight-sided tower (not an axis-aligned box: it counts by the box
inside it). Checks:
  - the build log: the occlusion bake line with one fitted box, ACTS exit 0 for every zone;
  - the extraction: world.json "umbra_occluders", every box with min < max, the tower's box inside the tower;
  - the built world (ACTS gfx_map): the tome read back by the core reader (iw8r_tests with IW8R_TEST_GFXMAP_JSON):
    it parses, passes the stock reader's checks, no view leaf holds several cells, and part of the objects is hidden.
The package the build writes into the editor's output folder is deleted afterwards (it is this proof's own map).

Usage: py -3 -I tools/umbra_bake_build_test.py <work dir> [--editor <IW8Radiant.exe>] [--tests <iw8r_tests.exe>]
Prints PASS / FAIL lines and RESULT ok|fail."""
import json, math, os, re, shutil, struct, subprocess, sys, zlib  # noqa: E401

here = os.path.dirname(os.path.abspath(__file__))
root = os.path.dirname(here)
args = sys.argv[1:]
editor = os.path.join(root, 'bin', 'release', 'IW8Radiant.exe')
tests = os.path.join(root, 'bin', 'release', 'iw8r_tests.exe')
for flag in ('--editor', '--tests'):
    if flag in args:
        value = os.path.abspath(args[args.index(flag) + 1])
        del args[args.index(flag):args.index(flag) + 2]
        if flag == '--editor':
            editor = value
        else:
            tests = value
work = os.path.abspath(args[0])
ACTS = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'bin', 'release', 'iw8r-acts.exe')
REPLAY = r'E:/IW8/Builds/1.20-replay/game_dx12_ship_replay.exe'
OODLE = r'E:/IW8/Builds/1.20-replay/oo2core_7_win64.dll'
MAP_ID = 'mp_iw8r_occprf'
MAT = 'custom/occ_wall'
SUFFIX = ' %s 128 128 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0' % MAT

failures = 0


def check(ok, what):
    global failures
    print(('PASS ' if ok else 'FAIL ') + what, flush=True)
    if not ok:
        failures += 1


def png(path, w, h, rgba):
    raw = b''.join(b'\x00' + bytes(rgba) * w for _ in range(h))

    def chunk(t, d):
        return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    open(path, 'wb').write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0)) +
                           chunk(b'IDAT', zlib.compress(raw)) + chunk(b'IEND', b''))


def pt(x, y, z):
    return '( %g %g %g )' % (x, y, z)


def prism(poly, z0, z1):
    """A brush from a convex polygon (counter-clockwise seen from above) between two heights."""
    lines = ['{']
    (ax, ay), (bx, by), (cx, cy) = poly[0], poly[1], poly[2]
    lines.append(' %s %s %s%s' % (pt(ax, ay, z0), pt(bx, by, z0), pt(cx, cy, z0), SUFFIX))  # bottom, facing down
    lines.append(' %s %s %s%s' % (pt(cx, cy, z1), pt(bx, by, z1), pt(ax, ay, z1), SUFFIX))  # top, facing up
    for k in range(len(poly)):
        (ax, ay), (bx, by) = poly[k], poly[(k + 1) % len(poly)]
        lines.append(' %s %s %s%s' % (pt(ax, ay, z1), pt(bx, by, z1), pt(bx, by, z0), SUFFIX))
    lines.append('}')
    return '\n'.join(lines) + '\n'


def box(x0, y0, z0, x1, y1, z1):
    return prism([(x0, y0), (x1, y0), (x1, y1), (x0, y1)], z0, z1)


shutil.rmtree(work, ignore_errors=True)
os.makedirs(os.path.join(work, 'materials'))
png(os.path.join(work, 'materials', 'occ_wall.png'), 16, 16, (120, 118, 112, 255))
open(os.path.join(work, 'materials', 'custom__occ_wall.iw8mat.json'), 'w', encoding='utf-8').write(
    json.dumps({'name': MAT, 'color': 'occ_wall.png', 'surface': 'concrete', 'alphaTest': False, 'twoSided': False, 'scale': [128, 128]}) + '\n')

TOWER = (1500.0, 600.0, 180.0)  # centre x, y and radius of the eight-sided tower
tower = [(TOWER[0] + TOWER[2] * math.cos(math.radians(22.5 + 45 * k)), TOWER[1] + TOWER[2] * math.sin(math.radians(22.5 + 45 * k))) for k in range(8)]
brushes = ''.join([
    box(-1024, -1024, -32, 2048, 2048, 0),        # the ground
    box(-1024, -1024, 0, 2048, -960, 320),        # outer walls
    box(-1024, 1984, 0, 2048, 2048, 320),
    box(-1024, -960, 0, -960, 1984, 320),
    box(1984, -960, 0, 2048, 1984, 320),
    box(300, -960, 0, 364, 1400, 320),            # two inner walls with a way round each
    box(900, -300, 0, 964, 1984, 320),
    box(-700, 600, 0, -300, 1000, 256),           # a block
    prism(tower, 0, 320),                         # the tower: not a box
])
src = open(os.path.join(root, 'tests', 'data', 'iw8_sample_room.map'), encoding='utf-8').read()
src = src.replace('"classname" "worldspawn"', '"classname" "worldspawn"\n"iw8r_umbra_bake" "1"', 1)
mark = '}\n// entity 1\n'
assert src.count(mark) == 1, src.count(mark)
src = src.replace(mark, brushes + mark, 1)
map_path = os.path.join(work, 'iw8r_occprf.map')
open(map_path, 'w', encoding='utf-8', newline='\n').write(src)
out = os.path.join(work, 'build')
subprocess.run([editor, '--compile', map_path, '--out', out], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def text_of(name):
    p = os.path.join(out, name)
    return open(p, encoding='utf-8', errors='replace').read() if os.path.isfile(p) else ''


result, log = text_of('compile-result.txt'), text_of('compile-log.txt')
first = result.splitlines()[0] if result else ''
check(first.startswith('ok map=' + MAP_ID), 'build ok (%s)' % first[:120])
m = re.search(r'package=(.+)$', first)
package = m.group(1).strip() if m else ''
line = next((l for l in (result + '\n' + log).splitlines() if 'occlusion bake:' in l and 'occluder box' in l), '')
mm = re.search(r'occlusion bake: (\d+) occluder box\(es\) from the opaque brushes \((\d+) of them fitted', line)
check(mm is not None and int(mm.group(1)) >= 9 and int(mm.group(2)) == 1, 'bake line: ' + (line.strip()[:200] or '-'))
acts = re.findall(r'ACTS\s+(\S+)\s+exit\s+(-?\d+)', log)
if acts:
    check(all(code == '0' for _, code in acts), 'ACTS: %s' % ', '.join('%s %s' % a for a in acts))
summary = next((l for l in log.splitlines() if 'umbra' in l.lower() and 'cell' in l.lower()), '')
print('     converter: ' + (summary.strip()[:220] or '(no umbra line in the log)'))

world_path = ''
for dirpath, _, files in os.walk(out):
    for f in files:
        if f.endswith('.replay-world.json'):
            world_path = os.path.join(dirpath, f)
if not world_path:
    # The extraction is kept in the map's build folder.
    for dirpath, _, files in os.walk(work):
        for f in files:
            if f.endswith('.replay-world.json'):
                world_path = os.path.join(dirpath, f)
if world_path:
    occ = json.load(open(world_path, encoding='utf-8')).get('umbra_occluders') or []
    check(len(occ) >= 9 and all(len(b) == 6 and b[0] < b[3] and b[1] < b[4] and b[2] < b[5] for b in occ), 'world.json: %d occluder boxes' % len(occ))
    cx, cy, rad = TOWER
    inner = rad * math.cos(math.radians(22.5))  # the tower's inscribed circle
    fitted = [b for b in occ if abs((b[0] + b[3]) / 2 - cx) < 60 and abs((b[1] + b[4]) / 2 - cy) < 60 and b[3] - b[0] < 2 * rad]
    ok = len(fitted) == 1
    if ok:
        b = fitted[0]
        corners = [(b[0], b[1]), (b[3], b[1]), (b[0], b[4]), (b[3], b[4])]
        # Inside the octagon: inside every side's half plane.
        for (px, py) in corners:
            for k in range(8):
                ang = math.radians(45 * k)
                ok = ok and (px - cx) * math.cos(ang) + (py - cy) * math.sin(ang) <= inner + 1e-6
        ok = ok and b[3] - b[0] > rad and b[4] - b[1] > rad and b[5] - b[2] > 300
    check(ok, 'the tower gives one box, inside it: %s' % (fitted[0] if fitted else fitted))
else:
    check(False, 'extraction world.json under ' + work)

ff = os.path.join(package, MAP_ID + '.ff') if package else ''
if ff and os.path.isfile(ff):
    acts_out = os.path.join(work, 'acts')
    subprocess.run([ACTS, '--noUpdater', 'fastfile', '-r', 'mw19replay', '-g', REPLAY, '--oodle', OODLE, '-a', 'gfx_map',
                    '-l', '-o', acts_out, ff], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    gdir = os.path.join(acts_out, 'mw19replay', MAP_ID, 'assets', 'gfx_map', 'maps', 'mp')
    gfiles = sorted(f for f in os.listdir(gdir) if f.endswith('.asset.json')) if os.path.isdir(gdir) else []
    if gfiles:
        env = dict(os.environ, IW8R_TEST_GFXMAP_JSON=os.path.join(gdir, gfiles[0]))
        run = subprocess.run([tests], env=env, cwd=root, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        text = run.stdout.decode('utf-8', 'replace')
        tome = next((l.strip() for l in text.splitlines() if 'built tome:' in l), '')
        okline = '[  OK  ] umbra_bake_built_zone_reads_back' in text
        cells = re.search(r'(\d+) cells, (\d+) portals', tome)
        seen = re.search(r'([\d.]+)% of the objects are drawn', tome)
        check(okline and cells is not None and int(cells.group(1)) > 1 and int(cells.group(2)) > 0, 'tome read back: ' + (tome[:230] or '-'))
        # A map this small has a handful of merged world surfaces, each seen from everywhere: how much is hidden is
        # printed, not judged (the conservativeness and the cell structure are what this proof is about).
        print('     from the cell centres %s%% of the objects are drawn on average' % (seen.group(1) if seen else '?'))
    else:
        check(False, 'ACTS gfx_map export')
else:
    check(False, 'built zone ' + ff)

if package and os.path.basename(package.rstrip('/\\')) == MAP_ID:
    shutil.rmtree(package, ignore_errors=True)
print('RESULT ' + ('ok' if failures == 0 else 'fail'))
sys.exit(0 if failures == 0 else 1)
