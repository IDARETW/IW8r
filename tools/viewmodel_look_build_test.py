"""Offline proof of the first-person look (research 128): the editor builds the shared zone of the weapon and hands
with the toon shader's filter, without a dialog and without installing anything.

Runs IW8Radiant.exe --viewmodel-look data/shaders/toon.iw8shader.hlsl --out <work> and checks:
  - the editor's report: the materials matched, the technique sets filtered, no shader refused, ACTS 0 failed, "ok";
  - the edits file: only the techset zone, no patch file, the names kept, the material pattern;
  - the converter's report: the names kept, no material renamed, every filtered technique bound to the scene depth;
  - the built zone, read back with ACTS (tools/scene_depth_build_test.py --stock): the filtered techniques give the
    scene depth (code image 0) to a pixel shader that loads from it;
  - the game folder's own zone file is unchanged (size and time) by the build.

Usage: py -3 -I tools/viewmodel_look_build_test.py <work dir> [--editor <IW8Radiant.exe>]
Prints PASS / FAIL lines and RESULT ok|fail."""
import json, os, shutil, subprocess, sys  # noqa: E401

here = os.path.dirname(os.path.abspath(__file__))
root = os.path.dirname(here)
args = sys.argv[1:]
editor = os.path.join(root, 'bin', 'release', 'IW8Radiant.exe')
if '--editor' in args:
    editor = os.path.abspath(args[args.index('--editor') + 1])
    del args[args.index('--editor'):args.index('--editor') + 2]
work = os.path.abspath(args[0])
GAME_ZONE = r'E:/IW8/Builds/1.20-replay/zone/techsets_global_stream_mp.ff'
KEPT_ZONE = r'E:/IW8/Builds/1.20-replay/iw8r_stock/techsets_global_stream_mp.ff'

failures = 0


def check(ok, what):
    global failures
    print(('PASS ' if ok else 'FAIL ') + what, flush=True)
    if not ok:
        failures += 1


def stamp(p):
    return (os.path.getsize(p), os.path.getmtime(p)) if os.path.isfile(p) else None


shutil.rmtree(work, ignore_errors=True)
os.makedirs(work)
before = (stamp(GAME_ZONE), stamp(KEPT_ZONE))
shader = os.path.join(root, 'data', 'shaders', 'toon.iw8shader.hlsl')
run = subprocess.run([editor, '--viewmodel-look', shader, '--out', work], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
p = os.path.join(work, 'viewmodel-look.txt')
lines = open(p, encoding='utf-8').read().splitlines() if os.path.isfile(p) else []
check(run.returncode == 0 and bool(lines) and lines[-1].startswith('ok '), 'the editor built the look (exit %d): %s' % (run.returncode, lines[-1][:120] if lines else '-'))
check(any(l.startswith('ACTS Replay:') and ' 0 failed' in l for l in lines), 'ACTS: ' + next((l for l in lines if l.startswith('ACTS')), '-'))
check(not any(l.startswith('FAIL') for l in lines), 'no FAIL line in the report')

p = os.path.join(work, 'edits.json')
edits = json.load(open(p, encoding='utf-8')) if os.path.isfile(p) else {}
lit = edits.get('lit_filter', {})
check(edits.get('techsets_only') is True and edits.get('patch') is False and lit.get('keep_names') is True and
      lit.get('materials') == '(_vm_|/vm_|_vm$|_arm_[lr]_)' and os.path.isfile(lit.get('program', '')) and
      os.path.isfile(lit.get('screen_program', '')), 'edits file: techset zone only, no patch, names kept, the weapon and arm materials')

p = os.path.join(work, 'out', 'global_stream_mp.edit-report.json')
rep = (json.load(open(p, encoding='utf-8')) if os.path.isfile(p) else {}).get('lit_filter', {})
check(rep.get('keep_names') is True and rep.get('materials_renamed') == 0 and rep.get('technique_sets', 0) >= 500 and
      rep.get('materials_matched', 0) >= 5000 and rep.get('shaders_refused') == 0 and rep.get('screen_refused') == 0 and
      rep.get('techniques_screen_unbound') == 0 and rep.get('techniques_with_screen') == rep.get('techniques'),
      'converter: %s sets of %s materials, %s shaders, %s techniques with the screen, 0 refused, 0 unbound, no material renamed' %
      (rep.get('technique_sets'), rep.get('materials_matched'), rep.get('shaders'), rep.get('techniques_with_screen')))

zone = os.path.join(work, 'out', 'techsets_global_stream_mp.ff')
check(os.path.isfile(zone) and os.path.getsize(zone) > 8 * 1024 * 1024, 'zone written: %s bytes' % (os.path.getsize(zone) if os.path.isfile(zone) else 0))
if os.path.isfile(zone):
    # A short scratch folder: ACTS cannot write below a long path.
    scratch = os.path.join(os.path.splitdrive(work)[0] + os.sep, 't', 'fplp')
    shutil.rmtree(scratch, ignore_errors=True)
    r = subprocess.run([sys.executable, '-I', os.path.join(here, 'scene_depth_build_test.py'), os.path.join(work, 'out'), '--stock',
                        '--min', '500', '--work', scratch], capture_output=True, text=True, errors='replace')
    out = (r.stdout + r.stderr).splitlines()
    last = next((l for l in reversed(out) if l.strip()), '-')
    check(r.returncode == 0, 'the built zone gives the scene depth to its filtered techniques: ' + last.strip()[:200])
    shutil.rmtree(scratch, ignore_errors=True)
check((stamp(GAME_ZONE), stamp(KEPT_ZONE)) == before, 'the game folder\'s zone file is untouched by the build')

print('RESULT ' + ('ok' if failures == 0 else 'fail'))
sys.exit(0 if failures == 0 else 1)
