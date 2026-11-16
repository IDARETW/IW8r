"""Offline proof of a built map's physics bounds (docs/research/124).

The game sizes its physics world from the collision asset's broadphase box, and a body that leaves that box is
frozen in place. Every small stock map stores the same box, (-30720, -31744, -14336) .. (34816, 33792, 51200); a
built map stored its geometry padded by 16384 / 8192, which ends about 9,000 units up and 16,000 out: lower and
nearer than killstreak aircraft fly. The converter now stores at least the stock box.
This proof builds tests/data/iw8_sample_room.map through the editor's headless build, reads the built zone's
collision asset back (ACTS) and checks that the zone loads (0 failed) and that its broadphase box holds the stock box
and the map's own geometry with its padding.
The package the build writes into the editor's output folder is deleted afterwards.

Usage: py -3 -I tools/physics_bounds_build_test.py <work dir> [--editor <IW8Radiant.exe>] [--exe <zonetool.exe>]
Prints PASS / FAIL lines and RESULT ok|fail."""
import glob, json, os, re, shutil, subprocess, sys  # noqa: E401

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
ACTS = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'bin', 'release', 'iw8r-acts.exe')
REPLAY = r'E:/IW8/Builds/1.20-replay/game_dx12_ship_replay.exe'
OODLE = r'E:/IW8/Builds/1.20-replay/oo2core_7_win64.dll'
STOCK_MIN, STOCK_MAX = (-30720.0, -31744.0, -14336.0), (34816.0, 33792.0, 51200.0)

failures = 0


def check(ok, what):
    global failures
    print(('PASS ' if ok else 'FAIL ') + what, flush=True)
    if not ok:
        failures += 1


shutil.rmtree(work, ignore_errors=True)
os.makedirs(os.path.join(work, 'map'))
name = 'iw8r_bpprf'
map_id = 'mp_' + name
map_path = os.path.join(work, 'map', name + '.map')
shutil.copyfile(os.path.join(root, 'tests', 'data', 'iw8_sample_room.map'), map_path)
out = os.path.join(work, 'out')
env = dict(os.environ)
if converter:
    env['IW8R_TOOL_ZONETOOL'] = converter
subprocess.run([editor, '--compile', map_path, '--out', out], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, env=env)
result_path = os.path.join(out, 'compile-result.txt')
result = open(result_path, encoding='utf-8', errors='replace').read() if os.path.isfile(result_path) else ''
first = result.splitlines()[0] if result else ''
check(first.startswith('ok map=' + map_id), 'build ok (%s)' % first[:90])
m = re.search(r'package=(.+)$', first)
package = m.group(1).strip() if m else ''
ff = os.path.join(package, map_id + '.ff') if package else ''
box = None
if ff and os.path.isfile(ff):
    acts_out = os.path.join(work, 'acts')
    run = subprocess.run([ACTS, '--noUpdater', 'fastfile', '-r', 'mw19replay', '-g', REPLAY, '--oodle', OODLE, '-a', 'col_map', '-l', '-o', acts_out,
                          ff], stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    text = run.stdout.decode('utf-8', 'replace')
    loaded = re.search(r'(\d+) assets loaded, \d+ tested, (\d+) failed', text)
    check(bool(loaded) and loaded.group(2) == '0', 'the game\'s loaders accept the zone (%s)' % (loaded.group(0) if loaded else 'no result'))
    hits = glob.glob(os.path.join(acts_out, '**', 'assets', 'col_map', '**', '*.asset.json'), recursive=True)
    if hits:
        fields = json.load(open(hits[0], encoding='utf-8'))['asset']['fields']
        vec = lambda v: [float(x) for x in (v.get('v') if isinstance(v, dict) else v)]
        box = (vec(fields['broadphaseMin']), vec(fields['broadphaseMax']))
check(box is not None, 'the built zone\'s collision asset is read')
if box:
    lo, hi = box
    check(all(lo[k] <= STOCK_MIN[k] and hi[k] >= STOCK_MAX[k] for k in range(3)),
          'the broadphase box holds the stock box: %s .. %s' % (' '.join('%g' % v for v in lo), ' '.join('%g' % v for v in hi)))
    # The sample room is about 600 units wide: its own padded box (16384 around, 8192 up and down) is inside too.
    check(all(lo[k] <= -300 - (16384 if k < 2 else 8192) + 600 and hi[k] >= 300 + (16384 if k < 2 else 8192) - 600 for k in range(3)),
          'and the map\'s own geometry with its padding')

if package and os.path.basename(package.rstrip('/\\')) == map_id:
    shutil.rmtree(package, ignore_errors=True)
print('RESULT %s (%d failure(s))' % ('ok' if not failures else 'fail', failures))
sys.exit(1 if failures else 0)
