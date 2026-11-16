"""Parity of iw8r-acts.exe (src/tools/acts) with the ACTS fork's acts.exe on Replay 1.20 exports.

ACTS's own output is not the same from one run to the next (payloads carry run-time addresses, manifests carry
timings), so the reference runs twice: wherever its two runs agree, ours must give the same value or bytes; where they
disagree, the field is noise and only its presence is checked. JSON files (.json, .jsonl) are compared value by value,
other files byte for byte.

Usage: py -3 -I tools/acts_parity.py <scratch (short path)> <case> [<case>...] [--names-from <file>]
  A case is  name=<arguments after the common "fastfile -r mw19replay -g <exe> --oodle <dll>">  where {zone} is the
  game's zone folder, {xpaks} every populated .xpak as "--xpak <file>" (as the editor's ACTS runner passes them) and
  {out} the output folder. A case named serve:<name> sends each ';'-separated JSON request to one --serve process.
Exit code 0 when every case matches.
"""
import json, os, shutil, subprocess, sys, time

GAME = r'E:\IW8\Builds\1.20-replay'
EXE = GAME + r'\game_dx12_ship_replay.exe'
OODLE = GAME + r'\oo2core_7_win64.dll'
REF = r'E:\mw124\atian-cod-tools\build\bin\Release\acts.exe'
OURS = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'bin', 'release', 'iw8r-acts.exe')

args = sys.argv[1:]
scratch = args.pop(0)
cases = [a for a in args if '=' in a]


def xpaks():
    out = []
    for f in sorted(os.listdir(GAME + r'\zone')):
        p = os.path.join(GAME, 'zone', f)
        if f.endswith('.xpak') and os.path.getsize(p) >= 64 * 1024:
            out += ['--xpak', p]
    return out


def expand(text, out):
    res = []
    for tok in text.split():
        if tok == '{xpaks}':
            res += xpaks()
        else:
            res.append(tok.replace('{zone}', GAME + r'\zone').replace('{out}', out))
    return res


def run(exe, case_args, out, serve_requests=None, structured=False):
    env = dict(os.environ)
    if structured:
        env['ACTS_REPLAY_STRUCTURED'] = '1'  # as the editor's sound preview runs it
    shutil.rmtree(out, ignore_errors=True)
    os.makedirs(out)
    base = [exe, '--noUpdater', 'fastfile', '-r', 'mw19replay', '-g', EXE, '--oodle', OODLE]
    t = time.time()
    if serve_requests is None:
        p = subprocess.run(base + expand(case_args, out), capture_output=True, text=True, errors='replace', env=env)
        return p.returncode, p.stdout + p.stderr, time.time() - t
    p = subprocess.Popen(base + expand(case_args, out), stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                         stderr=subprocess.STDOUT, text=True, errors='replace', env=env)
    log, answers = [], 0
    for line in p.stdout:
        log.append(line)
        if line.startswith('@acts '):
            o = json.loads(line[6:])
            if o.get('ready') or 'id' in o:
                if answers < len(serve_requests):
                    # A request: <zone file>><types>[><names, comma separated>][>g]
                    parts = serve_requests[answers].split('>')
                    req = {'id': answers + 1, 'zone': os.path.join(GAME, 'zone', parts[0]),
                           'out': os.path.join(out, 'r%d' % (answers + 1)), 'types': parts[1]}
                    if len(parts) > 2 and parts[2] and parts[2] != 'g':
                        req['names'] = parts[2].split(',')
                    if parts[-1] == 'g':
                        req['geometry'] = True
                    p.stdin.write(json.dumps(req) + '\n')
                    p.stdin.flush()
                    answers += 1
                else:
                    p.stdin.close()
    p.wait()
    return p.returncode, ''.join(log), time.time() - t


def files(base):
    res = {}
    for d, _, fs in os.walk(base):
        for f in fs:
            p = os.path.join(d, f)
            res[os.path.relpath(p, base).replace('\\', '/')] = p
    return res


def load_json(p):
    with open(p, 'rb') as fh:
        data = fh.read()
    if p.endswith('.jsonl'):
        return [json.loads(l) for l in data.decode('utf-8', 'replace').splitlines() if l.strip()]
    return json.loads(data.decode('utf-8', 'replace'))


def volatile(path):
    # Wall-clock timings (manifest timing_ms, *_ms fields) differ from run to run by nature.
    last = path.rsplit('.', 1)[-1]
    return '.timing_ms' in path or last.endswith('_ms')


def walk(a, b, c, path, out):
    """a, b: the reference's two runs; c: ours. Appends (path, reason) for real differences."""
    if volatile(path):
        return
    if a == b:
        if c != a:
            if type(a) == type(c) and isinstance(a, dict):
                for k in set(a) | set(c):
                    if k not in c or k not in a:
                        out.append((path + '.' + str(k), 'missing in ' + ('ours' if k not in c else 'ref')))
                    else:
                        walk(a[k], b[k], c[k], path + '.' + str(k), out)
            elif type(a) == type(c) and isinstance(a, list) and len(a) == len(c):
                for i in range(len(a)):
                    walk(a[i], b[i], c[i], '%s[%d]' % (path, i), out)
            else:
                out.append((path, 'ref %s ours %s' % (json.dumps(a)[:80], json.dumps(c)[:80])))
        return
    # the reference disagrees with itself: noise, but the structure must still match where it can be followed
    if isinstance(a, dict) and isinstance(b, dict) and isinstance(c, dict):
        for k in set(a) | set(b) | set(c):
            if k in a and k in b and k in c:
                walk(a[k], b[k], c[k], path + '.' + str(k), out)
            elif (k in a and k in b) != (k in c):
                out.append((path + '.' + str(k), 'present in ' + ('ref only' if k not in c else 'ours only')))
    elif isinstance(a, list) and isinstance(b, list) and isinstance(c, list) and len(a) == len(b) == len(c):
        for i in range(len(a)):
            walk(a[i], b[i], c[i], '%s[%d]' % (path, i), out)
    elif type(c) != type(a) and type(a) == type(b):
        out.append((path, 'type ref %s ours %s' % (type(a).__name__, type(c).__name__)))


def compare(da, db, dc):
    fa, fb, fc = files(da), files(db), files(dc)
    same = noisy = 0
    problems = []
    for rel in sorted(set(fa) | set(fb) | set(fc)):
        if rel not in fc:
            if rel in fa and rel in fb:
                problems.append((rel, 'only in the reference'))
            continue
        if rel not in fa and rel not in fb:
            problems.append((rel, 'only in ours'))
            continue
        if rel not in fa or rel not in fb:
            noisy += 1  # the reference writes it only sometimes
            continue
        ba, bb, bc = (open(x, 'rb').read() for x in (fa[rel], fb[rel], fc[rel]))
        if ba == bb == bc:
            same += 1
            continue
        if rel.endswith(('.json', '.jsonl')):
            try:
                ja, jb, jc = load_json(fa[rel]), load_json(fb[rel]), load_json(fc[rel])
            except ValueError as e:
                problems.append((rel, 'json: %s' % e))
                continue
            out = []
            walk(ja, jb, jc, '', out)
            if out:
                problems += [(rel + ' ' + p, r) for p, r in out[:5]]
            elif ja == jb:
                same += 1
            else:
                noisy += 1
        elif ba == bb:
            problems.append((rel, 'bytes differ (%d vs %d)' % (len(ba), len(bc))))
        else:
            noisy += 1
            if len(bc) != len(ba) and len(ba) == len(bb):
                problems.append((rel, 'size %d vs reference %d' % (len(bc), len(ba))))
    return same, noisy, problems


def strip_log(text, out):
    # The output folder differs per side; ACTS prints it with either slash.
    lines = []
    for l in text.splitlines():
        if not l.strip() or l.startswith('Atian tools '):
            continue
        for form in (out, out.replace('\\', '/'), out.replace('/', '\\')):
            l = l.replace(form, '{out}')
        lines.append(l.rstrip())
    return lines


ok = True
for case in cases:
    name, case_args = case.split('=', 1)
    serve = None
    if name.startswith('serve:'):
        case_args, reqs = case_args.split('|', 1)
        serve = [r for r in reqs.split(';') if r.strip()]
    structured = name.endswith('+s')  # name+s: with ACTS_REPLAY_STRUCTURED=1
    root = os.path.join(scratch, name.replace(':', '_').replace('+', '_'))
    ra = run(REF, case_args, os.path.join(root, 'ref_a'), serve, structured)
    rb = run(REF, case_args, os.path.join(root, 'ref_b'), serve, structured)
    rc = run(os.path.abspath(OURS), case_args, os.path.join(root, 'ours'), serve, structured)
    same, noisy, problems = compare(os.path.join(root, 'ref_a'), os.path.join(root, 'ref_b'), os.path.join(root, 'ours'))
    for side, r in (('ref_a', ra), ('ref_b', rb), ('ours', rc)):
        with open(os.path.join(root, side + '.log'), 'w', encoding='utf-8') as fh:
            fh.write(r[1])
    la, lc = strip_log(ra[1], os.path.join(root, 'ref_a')), strip_log(rc[1], os.path.join(root, 'ours'))
    log_note = 'log same' if la == lc else 'log differs (%d vs %d lines)' % (len(la), len(lc))
    if ra[0] != rc[0]:
        problems.append(('exit code', 'ref %d ours %d' % (ra[0], rc[0])))
    status = 'OK' if not problems else 'DIFF'
    ok = ok and not problems
    print('%-4s %-14s files same %d, noise-only %d, problems %d; exit %d/%d; %s; %.0f s / %.0f s'
          % (status, name, same, noisy, len(problems), ra[0], rc[0], log_note, ra[2], rc[2]))
    for rel, why in problems[:12]:
        print('       %s: %s' % (rel, why))
sys.exit(0 if ok else 1)
