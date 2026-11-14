"""Offline proof of a built map's airspace (roadmap F.14 / 6.21; docs/research/121).

Killstreak aircraft and their cameras fly well above and beside a map. Replay 1.20 fails the camera's occlusion query
when the camera is outside the box of the map's occlusion data, and then draws no players or vehicles (research 121,
read in the Replay exe). Every stock map's occlusion data reaches at least 131072 units from the origin. A built map
gets the same reach: the build grows the one visibility cell (worldspawn iw8r_airspace "<up> [out]", default
131072 131072; "0" keeps it at the geometry) and the converter makes the occlusion data cover the cell.
This proof builds tests/data/iw8_sample_room.map through the editor's headless build (IW8Radiant.exe --compile),
reads each built zone's occlusion data back (ACTS gfx_map, core reader) and checks:
  - default: points 1500, 5500 and 30000 units above the map, 9000 and 30000 units beside it and 2000 below it are
    inside a cell, and a view from there sees the map's objects; points 200000 units away are outside;
  - iw8r_airspace "16384 8192": inside up to those distances, outside past them;
  - iw8r_airspace 0: the air points are outside every cell (what every build was before);
  - default with the occlusion bake (iw8r_umbra_bake 1): the air points are inside a cell and see the map.
The packages the builds write into the editor's output folder are deleted afterwards.

Usage: py -3 -I tools/airspace_build_test.py <work dir> [--editor <IW8Radiant.exe>] [--exe <zonetool.exe>]
Prints PASS / FAIL lines and RESULT ok|fail."""
import json, os, re, shutil, subprocess, sys  # noqa: E401

here = os.path.dirname(os.path.abspath(__file__))
root = os.path.dirname(here)
args = sys.argv[1:]
editor = os.path.join(root, 'bin', 'release', 'IW8Radiant.exe')
converter = ''
if '--editor' in args:
    editor = os.path.abspath(args[args.index('--editor') + 1])
    del args[args.index('--editor'):args.index('--editor') + 2]
if '--exe' in args:
    converter = os.path.abspath(args[args.index('--exe') + 1])
    del args[args.index('--exe'):args.index('--exe') + 2]
work = os.path.abspath(args[0])
tests = os.path.join(root, 'bin', 'release', 'iw8r_tests.exe')
ACTS = r'E:/mw124/atian-cod-tools/build/bin/Release/acts.exe'
REPLAY = r'E:/IW8/Builds/1.20-replay/game_dx12_ship_replay.exe'
OODLE = r'E:/IW8/Builds/1.20-replay/oo2core_7_win64.dll'

failures = 0


def check(ok, what):
    global failures
    print(('PASS ' if ok else 'FAIL ') + what, flush=True)
    if not ok:
        failures += 1


def build(name, extra_key):
    """Builds the sample as mp_<name>; returns (gfx_map json path or '', world bounds or None, package folder)."""
    map_id = 'mp_' + name
    folder = os.path.join(work, name)
    os.makedirs(folder)
    src = open(os.path.join(root, 'tests', 'data', 'iw8_sample_room.map'), encoding='utf-8').read()
    if extra_key:
        src = src.replace('"classname" "worldspawn"', '"classname" "worldspawn"\n' + extra_key, 1)
    map_path = os.path.join(folder, name + '.map')
    open(map_path, 'w', encoding='utf-8', newline='\n').write(src)
    out = os.path.join(folder, 'build')
    env = dict(os.environ)
    if converter:
        env['IW8R_TOOL_ZONETOOL'] = converter
    subprocess.run([editor, '--compile', map_path, '--out', out], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, env=env)
    result_path = os.path.join(out, 'compile-result.txt')
    result = open(result_path, encoding='utf-8', errors='replace').read() if os.path.isfile(result_path) else ''
    first = result.splitlines()[0] if result else ''
    check(first.startswith('ok map=' + map_id), '%s: build ok (%s)' % (name, first[:90]))
    m = re.search(r'package=(.+)$', first)
    package = m.group(1).strip() if m else ''
    world_path = os.path.join(folder, 'iw8build', map_id, 'extraction', 'extracted', 'maps', 'mp', map_id + '.d3dbsp.replay-world.json')
    bounds = None
    if os.path.isfile(world_path):
        w = json.load(open(world_path, encoding='utf-8'))
        bounds = w.get('bounds')
    gpath = ''
    ff = os.path.join(package, map_id + '.ff') if package else ''
    if ff and os.path.isfile(ff):
        acts_out = os.path.join(folder, 'acts')
        subprocess.run([ACTS, '--noUpdater', 'fastfile', '-r', 'mw19replay', '-g', REPLAY, '--oodle', OODLE, '-a', 'gfx_map',
                        '-l', '-o', acts_out, ff], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        gdir = os.path.join(acts_out, 'mw19replay', map_id, 'assets', 'gfx_map', 'maps', 'mp')
        gfiles = sorted(f for f in os.listdir(gdir) if f.endswith('.asset.json')) if os.path.isdir(gdir) else []
        gpath = os.path.join(gdir, gfiles[0]) if gfiles else ''
    return gpath, bounds, package, map_id


def corners(bounds):
    """(mins, maxs) of the world bounds as the world.json writes them."""
    if isinstance(bounds, dict):
        lo = bounds.get('mins') or bounds.get('min')
        hi = bounds.get('maxs') or bounds.get('max')
        if lo and hi:
            return [float(v) for v in lo], [float(v) for v in hi]
        mid, half = bounds.get('midPoint') or bounds.get('mid'), bounds.get('halfSize') or bounds.get('half')
        if mid and half:
            return [float(m) - float(h) for m, h in zip(mid, half)], [float(m) + float(h) for m, h in zip(mid, half)]
    if isinstance(bounds, list) and len(bounds) == 2:
        return [float(v) for v in bounds[0]], [float(v) for v in bounds[1]]
    if isinstance(bounds, list) and len(bounds) == 6:
        return [float(v) for v in bounds[:3]], [float(v) for v in bounds[3:]]
    return None


def run_points(gpath, inside, outside):
    fmt = lambda pts: ';'.join('%.1f %.1f %.1f' % tuple(p) for p in pts)
    env = dict(os.environ, IW8R_TEST_GFXMAP_JSON=gpath, IW8R_TEST_AIR_INSIDE=fmt(inside), IW8R_TEST_AIR_OUTSIDE=fmt(outside))
    run = subprocess.run([tests], env=env, cwd=root, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    text = run.stdout.decode('utf-8', 'replace')
    line = next((l.strip() for l in text.splitlines() if l.strip().startswith('airspace:') and 'air point' in l), '')
    return '[  OK  ] umbra_built_zone_airspace' in text, line


shutil.rmtree(work, ignore_errors=True)
os.makedirs(work)
packages = []

def spots(c):
    lo, hi = c
    mid = [(lo[0] + hi[0]) / 2, (lo[1] + hi[1]) / 2, (lo[2] + hi[2]) / 2]
    above = lambda d: [mid[0], mid[1], hi[2] + d]
    beside = lambda d: [hi[0] + d, mid[1], hi[2] + 100]
    below = lambda d: [mid[0], mid[1], lo[2] - d]
    return mid, above, beside, below


def case(name, key, inside_of, outside_of, what):
    g, b, package, map_id = build(name, key)
    packages.append((package, map_id))
    c = corners(b)
    check(bool(g) and c is not None, '%s: occlusion data and world bounds read (%s)' % (name, json.dumps(b)[:80] if b else '-'))
    if not (g and c):
        return
    mid, above, beside, below = spots(c)
    ok, line = run_points(g, inside_of(mid, above, beside, below), outside_of(mid, above, beside, below))
    check(ok and line != '', what + ': ' + (line or 'no result line')[:230])


case('iw8r_airprf', '',
     lambda mid, above, beside, below: [above(1500), above(5500), above(30000), beside(9000), beside(30000), below(2000)],
     lambda mid, above, beside, below: [above(200000), beside(200000)],
     'default airspace')
case('iw8r_airown', '"iw8r_airspace" "16384 8192"',
     lambda mid, above, beside, below: [above(1500), above(16000), beside(8000), below(8000)],
     lambda mid, above, beside, below: [above(17000), beside(8500), below(8500)],
     'iw8r_airspace "16384 8192"')
case('iw8r_airoff', '"iw8r_airspace" "0"',
     lambda mid, above, beside, below: [mid],
     lambda mid, above, beside, below: [above(1500), above(5500), beside(9000), below(2000)],
     'without airspace the air points are outside the cell')
case('iw8r_airbak', '"iw8r_umbra_bake" "1"',
     lambda mid, above, beside, below: [above(1500), above(5500), above(30000), beside(9000), below(2000)],
     lambda mid, above, beside, below: [above(200000), beside(200000)],
     'default airspace with the occlusion bake')

for package, map_id in packages:
    if package and os.path.basename(package.rstrip('/\\')) == map_id:
        shutil.rmtree(package, ignore_errors=True)
print('RESULT %s (%d failure(s))' % ('ok' if not failures else 'fail', failures))
sys.exit(1 if failures else 0)
