"""Parity of iw8r-zt.exe with another zonetool build on `editzone` (stock map rebuilds, the first-person look).

Copies an editor work folder (edits.json and the files it names) into <scratch>/<side>, points edits.json at the copy,
runs `-game iw8 -path <game folder> editzone <zone> edits.json out` with each converter and compares out/ byte for byte.
The game folder and the stock zones are only read.

Usage: py -3 -I tools/zt_editzone_parity.py <work folder> <zone> <scratch> [--ref <exe>] [--ours <exe>]
"""
import argparse, filecmp, os, shutil, subprocess, sys, time

ap = argparse.ArgumentParser()
ap.add_argument('work')
ap.add_argument('zone')
ap.add_argument('scratch')
ap.add_argument('--ref', default=r'E:\mw124\x64-zt\build\bin\x64\Release\zonetool.exe')
ap.add_argument('--ours', default=os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'bin', 'release', 'iw8r-zt.exe'))
ap.add_argument('--game', default=r'E:\IW8\Builds\1.20-replay')
a = ap.parse_args()


def run(side, exe):
    root = os.path.abspath(os.path.join(a.scratch, side))
    shutil.rmtree(root, ignore_errors=True)
    shutil.copytree(a.work, root, ignore=shutil.ignore_patterns('out', '*.log', 'converter-log.txt'))
    edits = os.path.join(root, 'edits.json')
    text = open(edits, encoding='utf-8').read()
    src = os.path.abspath(a.work)
    for form in (src.replace('/', '\\').replace('\\', '\\\\'), src.replace('\\', '/')):
        text = text.replace(form, root.replace('\\', '\\\\') if '\\\\' in form else root.replace('\\', '/'))
    open(edits, 'w', encoding='utf-8').write(text)
    os.makedirs(os.path.join(root, 'out'))
    t = time.time()
    with open(os.path.join(root, 'log.txt'), 'wb') as log:
        rc = subprocess.run([exe, '-game', 'iw8', '-path', a.game, 'editzone', a.zone, edits, os.path.join(root, 'out')],
                            cwd=root, stdout=log, stderr=subprocess.STDOUT).returncode
    print('%s: exit %d in %.0f s' % (side, rc, time.time() - t))
    return os.path.join(root, 'out'), rc


def files(base):
    res = {}
    for d, _, fs in os.walk(base):
        for f in fs:
            p = os.path.join(d, f)
            res[os.path.relpath(p, base).replace('\\', '/')] = p
    return res


(oa, ra), (ob, rb) = run('ref', a.ref), run('ours', os.path.abspath(a.ours))
fa, fb = files(oa), files(ob)
same = diff = 0
for rel in sorted(set(fa) | set(fb)):
    if rel not in fa or rel not in fb:
        print('ONLY %s %s' % ('ref' if rel in fa else 'ours', rel))
        diff += 1
    elif filecmp.cmp(fa[rel], fb[rel], shallow=False):
        same += 1
    else:
        diff += 1
        print('DIFF %s (%d vs %d bytes)' % (rel, os.path.getsize(fa[rel]), os.path.getsize(fb[rel])))
print('files same %d, different %d' % (same, diff))
sys.exit(0 if diff == 0 and ra == rb == 0 and same > 0 else 1)
