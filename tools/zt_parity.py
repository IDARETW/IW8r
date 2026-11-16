"""Parity of iw8r-zt.exe (src/tools/zonetool) with another zonetool build (the x64-zt fork's exe).

Runs `buildextraction` on the same extraction with both converters, each in its own copy of the build folder and its
own output folder, then compares every output file byte for byte. The game folder is only read (-path / --replay).

Usage: py -3 -I tools/zt_parity.py <build root (holds extraction/)> <map id> <scratch dir>
           [--ref <zonetool.exe>] [--ours <iw8r-zt.exe>] [--acts <acts.exe>] [--gsc <iw8r-gsc.exe>]
           [--replay <game exe>] [--no-copy]
Exit code 0 when every output file is identical.
"""
import argparse, filecmp, hashlib, os, shutil, subprocess, sys, time

ap = argparse.ArgumentParser()
ap.add_argument('build_root')
ap.add_argument('map_id')
ap.add_argument('scratch')
ap.add_argument('--ref', default=r'E:\mw124\x64-zt\build\bin\x64\Release\zonetool.exe')
ap.add_argument('--ours', default=os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'bin', 'release', 'iw8r-zt.exe'))
ap.add_argument('--acts', default=r'E:\mw124\atian-cod-tools\build\bin\Release\acts.exe')
ap.add_argument('--gsc', default=os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'bin', 'release', 'iw8r-gsc.exe'))
ap.add_argument('--replay', default=r'E:\IW8\Builds\1.20-replay\game_dx12_ship_replay.exe')
ap.add_argument('--no-copy', action='store_true', help='use the extraction in place (it is only read)')
ap.add_argument('--only', choices=['ref', 'ours'], help='run one side only (e.g. the reference twice for determinism)')
ap.add_argument('--compare', action='store_true', help='compare the outputs of an earlier run without running')
a = ap.parse_args()
a.build_root = os.path.abspath(a.build_root)


def run(side, exe):
    root = os.path.join(a.scratch, side)
    shutil.rmtree(root, ignore_errors=True)
    os.makedirs(root)
    if a.no_copy:
        work, extraction = os.path.join(root, 'work'), os.path.join(a.build_root, 'extraction')
        os.makedirs(work)
    else:
        work = os.path.join(root, 'work')
        shutil.copytree(a.build_root, work, ignore=shutil.ignore_patterns('validate', 'probe_capture'))
        extraction = os.path.join(work, 'extraction')
    out = os.path.join(root, 'out')
    args = [exe, '-game', 'iw8', '-path', os.path.dirname(a.replay), 'buildextraction', extraction, '--map', a.map_id,
            '--replay', a.replay, '--output', out]
    meta = os.path.join(work if not a.no_copy else a.build_root, 'metadata.json')
    if os.path.isfile(meta):
        args += ['--metadata', meta]
    if os.path.isfile(a.acts):
        args += ['--acts', a.acts]
    if os.path.isfile(a.gsc):
        args += ['--gsc_tool', a.gsc]
    args += ['--iw3_mode_assets', 'off']
    t = time.time()
    with open(os.path.join(root, 'log.txt'), 'wb') as log:
        rc = subprocess.run(args, cwd=work, stdout=log, stderr=subprocess.STDOUT).returncode
    print('%s: exit %d in %.0f s' % (side, rc, time.time() - t))
    return out, rc


def files(base):
    res = {}
    for d, _, fs in os.walk(base):
        for f in fs:
            p = os.path.join(d, f)
            res[os.path.relpath(p, base).replace('\\', '/')] = p
    return res


sides = []
if a.only != 'ours':
    sides.append(('ref', a.ref))
if a.only != 'ref':
    sides.append(('ours', os.path.abspath(a.ours)))
outs = [(os.path.join(a.scratch, s, 'out'), 0) for s, _ in sides] if a.compare else [run(s, e) for s, e in sides]
if len(outs) < 2:
    sys.exit(outs[0][1])
fa, fb = files(outs[0][0]), files(outs[1][0])
# _stock_cache/ holds ACTS exports of stock zones made during the build (the same acts.exe for both sides): reported
# apart, since ACTS's own output is not the converter's.
same = diff = cache_diff = 0
for rel in sorted(set(fa) | set(fb)):
    if rel.startswith('_stock_cache/'):
        if rel not in fa or rel not in fb or not filecmp.cmp(fa[rel], fb[rel], shallow=False):
            cache_diff += 1
        continue
    if rel not in fa or rel not in fb:
        print('ONLY %s %s' % ('ref' if rel in fa else 'ours', rel))
        diff += 1
    elif filecmp.cmp(fa[rel], fb[rel], shallow=False):
        same += 1
    else:
        diff += 1
        print('DIFF %s (%d vs %d bytes)' % (rel, os.path.getsize(fa[rel]), os.path.getsize(fb[rel])))
print('files same %d, different %d (ACTS stock cache files different: %d)' % (same, diff, cache_diff))
sys.exit(0 if diff == 0 and outs[0][1] == outs[1][1] == 0 else 1)
