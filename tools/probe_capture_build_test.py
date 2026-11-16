"""Offline proof of a built map's reflection probes (roadmap F.10 / 5.5; docs/research/123).

The stock lit shader multiplies a probe's picture by the luminance of the baked light, so stock probes are stored
luminance-normalised: mean luminance 0.82 .. 0.93 on the stock maps measured, with colour. A built map's probe was
stored grey at a mean of 0.14: its reflections were about six times too dark and had no colour. The build now marks
its probe pictures as the editor's own captures; the converter keeps their colour and stores them at the stock level.
This proof builds tests/data/iw8_sample_room.map through the editor's headless build (IW8Radiant.exe --compile, which
captures the probes first), reads the built zone's probe images back (ACTS gfx_map and the decompressed zone, core
reader) and checks:
  - the zone loads in the game's own loaders (ACTS: 0 failed);
  - every probe's mean luminance is in the stock range (0.6 .. 1.3);
  - the probes are coloured (mean of (max - min) / max over the texels at least 0.05; the old build: 0.001).
The package the build writes into the editor's output folder is deleted afterwards.

Usage: py -3 -I tools/probe_capture_build_test.py <work dir> [--editor <IW8Radiant.exe>] [--exe <zonetool.exe>]
Prints PASS / FAIL lines and RESULT ok|fail."""
import os, re, shutil, subprocess, sys  # noqa: E401

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
ACTS = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'bin', 'release', 'iw8r-acts.exe')
REPLAY = r'E:/IW8/Builds/1.20-replay/game_dx12_ship_replay.exe'
OODLE = r'E:/IW8/Builds/1.20-replay/oo2core_7_win64.dll'

failures = 0


def check(ok, what):
    global failures
    print(('PASS ' if ok else 'FAIL ') + what, flush=True)
    if not ok:
        failures += 1


shutil.rmtree(work, ignore_errors=True)
os.makedirs(os.path.join(work, 'map'))
name = 'iw8r_prbprf'
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
log_path = os.path.join(out, 'compile-log.txt')
log = open(log_path, encoding='utf-8', errors='replace').read() if os.path.isfile(log_path) else ''
captured = re.search(r'(\d+) of (\d+) reflection probe\(s\) use the editor capture', log)
check(bool(captured) and captured.group(1) == captured.group(2) and int(captured.group(1)) >= 1,
      'every probe has the editor\'s capture (%s)' % (captured.group(0) if captured else 'no line in the log'))
stored = re.findall(r'reflection probe (\d+) is an editor capture: colour kept, stored at ([0-9.]+)x', log)
check(len(stored) >= 1, 'the converter read %d probe(s) as editor captures (%s)' %
      (len(stored), ', '.join('probe %s x%s' % s for s in stored[:4])))

ff = os.path.join(package, map_id + '.ff') if package else ''
json_path = dec_path = ''
if ff and os.path.isfile(ff):
    acts_out = os.path.join(work, 'acts')
    run = subprocess.run([ACTS, '--noUpdater', 'fastfile', '-r', 'mw19replay', '-g', REPLAY, '--oodle', OODLE, '-a', 'gfx_map', '-l', '-o', acts_out,
                          ff], stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    text = run.stdout.decode('utf-8', 'replace')
    loaded = re.search(r'(\d+) assets loaded, \d+ tested, (\d+) failed', text)
    check(bool(loaded) and loaded.group(2) == '0', 'the game\'s loaders accept the zone (%s)' % (loaded.group(0) if loaded else 'no result'))
    gdir = os.path.join(acts_out, 'mw19replay', map_id, 'assets', 'gfx_map', 'maps', 'mp')
    gfiles = sorted(f for f in os.listdir(gdir) if f.endswith('.asset.json')) if os.path.isdir(gdir) else []
    json_path = os.path.join(gdir, gfiles[0]) if gfiles else ''
    dec_out = os.path.join(work, 'dec')
    os.makedirs(dec_out, exist_ok=True)
    subprocess.run([ACTS, '--noUpdater', 'fastfile', '-r', 'mw19replay', '-g', REPLAY, '--oodle', OODLE, '-d', '--noAssetDump', '-l', '-o', dec_out,
                    ff], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, cwd=dec_out)
    decs = [os.path.join(b, f) for b, _d, fs_ in os.walk(dec_out) for f in fs_ if f.endswith('.ff.dec')]
    dec_path = decs[0] if decs else ''
check(bool(json_path) and bool(dec_path), 'the built zone\'s world export and decompressed zone are read')
if json_path and dec_path:
    env2 = dict(os.environ, IW8R_TEST_PROBE_JSON=json_path, IW8R_TEST_PROBE_DEC=dec_path, IW8R_TEST_PROBE_EXPECT='0.6 1.3 0.05')
    run = subprocess.run([tests], env=env2, cwd=root, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    text = run.stdout.decode('utf-8', 'replace')
    line = next((l.strip() for l in text.splitlines() if 'probereport:' in l and 'probe(s)' in l), '')
    check('[  OK  ] stock_probes_zone_report' in text and line != '',
          'stock level and colour: ' + (line.replace('probereport: ', '') or 'no result line')[:200])

if package and os.path.basename(package.rstrip('/\\')) == map_id:
    shutil.rmtree(package, ignore_errors=True)
print('RESULT %s (%d failure(s))' % ('ok' if not failures else 'fail', failures))
sys.exit(1 if failures else 0)
