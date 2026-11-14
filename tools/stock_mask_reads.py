"""Study for the stock map filter with the screen (research 130): how do a stock techset zone's lit pixel shaders read
the code image the screen filter takes over (code image 79, "Frontend Packed Stencil")?

The converter re-points that argument to the scene depth (code image 0) and the spliced filter reads the depth at its
register. That is only right when the stock shader's own reads of the register happen in a mode the game is not in
during normal play. This script exports the zone's techniques and pixel shaders with ACTS, and for every lit technique
with a code image 79 argument lists each instruction of its pixel shader that reads the register, with the chain of
conditions it sits under (each condition traced back to the instruction that computed it).

Usage: py -3 -I tools/stock_mask_reads.py <techsets_*.ff> <short work folder> [--index 79] [--show N]
Prints one line per distinct "conditions -> read" pattern with the number of shaders, and a verdict line:
  RESULT ok    every read sits under a condition on cb7[13].x (the thermal / night-vision switch)
  RESULT open  some read is unconditional or under another condition (listed)
"""
import collections, glob, json, os, re, shutil, subprocess, sys  # noqa: E401

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, here)
from scene_depth_build_test import ACTS, OODLE, REPLAY, arg_type, disassemble, techniques, walk_args  # noqa: E402

args = sys.argv[1:]
index = 79
show = 3
if '--index' in args:
    index = int(args[args.index('--index') + 1])
    del args[args.index('--index'):args.index('--index') + 2]
if '--show' in args:
    show = int(args[args.index('--show') + 1])
    del args[args.index('--show'):args.index('--show') + 2]
ff, work = os.path.abspath(args[0]), os.path.abspath(args[1])
out = os.path.join(work, 'acts')
shutil.rmtree(out, ignore_errors=True)
subprocess.run([ACTS, '--noUpdater', 'fastfile', '-r', 'mw19replay', '-g', REPLAY, '--oodle', OODLE, '-a', 'techset,pixelshader',
                '-l', '-o', out, ff], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
zone = os.path.splitext(os.path.basename(ff))[0]
zdir = os.path.join(out, 'mw19replay', zone, 'assets')
shaders = {os.path.basename(f): f for f in glob.glob(os.path.join(zdir, 'pixelshader', '**', '*.cso'), recursive=True)}
sets = glob.glob(os.path.join(zdir, 'techset', '**', '*.json'), recursive=True)
print('%s: %d technique set file(s), %d pixel shader(s)' % (zone, len(sets), len(shaders)))

WRITE = re.compile(r'^\s*(?:\d+:\s*)?([a-z_0-9]+(?:\([^)]*\))*)\s+(r\d+)\.([xyzw]+)\s*,')


def define(lines, at, reg, comp, depth=0):
    """The instruction that last wrote reg.comp before line `at`, with its own register operands traced one level."""
    for k in range(at - 1, -1, -1):
        m = WRITE.match(lines[k])
        if m and m.group(2) == reg and comp in m.group(3):
            text = re.sub(r'^\s*(?:\d+:\s*)?', '', lines[k]).strip()
            if depth < 2:
                srcs = text.split(',', 1)[1] if ',' in text else ''
                for sm in re.finditer(r'(r\d+)\.([xyzw])', srcs):
                    inner = define(lines, k, sm.group(1), sm.group(2), depth + 1)
                    if inner:
                        text += '  <=  ' + inner
                        break
            return text
    return ''


def norm(text):
    return re.sub(r'\br\d+\.[xyzw]+', 'r', text)


patterns = collections.Counter()
examples = collections.defaultdict(list)
studied = 0
no_shader = 0
per_shader = {}
for f in sets:
    j = json.load(open(f, encoding='utf-8', errors='replace'))
    techs = []
    techniques(j, techs)
    for name, t in techs:
        text = json.dumps(t)
        if 'LIT_FORWARDPLUS' not in (name or '') and 'LIT_FORWARDPLUS' not in text[:400]:
            continue
        found = []
        walk_args(t.get('args'), found)
        code = [x for x in found if 'CODE_TEXTURE' in arg_type(x) and int(x.get('shader', 0)) & 16 and int(x['index']) == index]
        if not code:
            continue
        names = [k for k in shaders if k.split('.')[0] in text]
        if not names:
            no_shader += 1
            continue
        key = (names[-1], int(code[0]['dest']))
        if key in per_shader:
            continue
        asm = disassemble(open(shaders[names[-1]], 'rb').read())
        if not asm:
            no_shader += 1
            continue
        studied += 1
        lines = asm.splitlines()
        reg = 't%d' % key[1]
        stack = []
        reads = []
        for n, line in enumerate(lines):
            body = re.sub(r'^\s*(?:\d+:\s*)?', '', line).strip()
            if body.startswith('if_nz') or body.startswith('if_z'):
                m = re.search(r'(r\d+)\.([xyzw])', body)
                stack.append((body.split()[0], define(lines, n, m.group(1), m.group(2)) if m else body))
            elif body.startswith('else'):
                if stack:
                    op, d = stack[-1]
                    stack[-1] = ('if_z' if op == 'if_nz' else 'if_nz', d)
            elif body.startswith('endif'):
                if stack:
                    stack.pop()
            elif re.search(r'\b%s\b' % reg, body) and not body.startswith('dcl_'):
                reads.append((n, body, list(stack)))
        if not reads:
            patterns['(the register is never read)'] += 1
            examples['(the register is never read)'].append(names[-1])
        for n, body, st in reads:
            sig = ' AND '.join('%s[%s]' % (op, norm(d)) for op, d in st) or '(no condition)'
            sig += '  ->  ' + norm(body)
            patterns[sig] += 1
            examples[sig].append(names[-1])
        per_shader[key] = reads

print('%d lit pixel shader(s) with code image %d studied (%d technique(s) without an exported shader)' % (studied, index, no_shader))
open_reads = 0
for sig, count in patterns.most_common():
    guarded = 'cb7[13].x' in sig.split('  ->  ')[0]
    never = sig.startswith('(the register')
    if not guarded and not never:
        open_reads += count
    print('%5d  %s%s' % (count, '' if guarded or never else 'OPEN  ', sig[:600]))
    for e in examples[sig][:show if not guarded and not never else 0]:
        print('         e.g. ' + e)
print('RESULT ' + ('ok' if open_reads == 0 and studied > 0 else 'open (%d read(s))' % open_reads))
