"""Header and source closure of ACTS's mw19replay path (survey for src/tools/acts).

Follows #include from the given roots through the ACTS include folders; a header's same-named .cpp joins the closure
(and is followed too). Prints the files outside the standard library, grouped by folder, with line counts.
Usage: acts_closure.py <acts checkout> <root file>...
"""
import os, re, sys
from collections import defaultdict

acts = sys.argv[1]
roots = sys.argv[2:]
inc_dirs = ['src/core/acts', 'src/core/shared', 'src/core/lib', 'src/core/vm', 'include']
inc_re = re.compile(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]', re.M)
seen, missing = set(), defaultdict(set)


def resolve(name, frm):
    cands = [os.path.join(os.path.dirname(frm), name)] + [os.path.join(acts, d, name) for d in inc_dirs]
    for c in cands:
        if os.path.isfile(c):
            return os.path.normpath(c)
    return None


stack = [os.path.normpath(os.path.join(acts, r)) for r in roots]
while stack:
    f = stack.pop()
    if f in seen:
        continue
    seen.add(f)
    try:
        text = open(f, encoding='utf-8', errors='replace').read()
    except OSError:
        continue
    for name in inc_re.findall(text):
        r = resolve(name, f)
        if r:
            stack.append(r)
        elif '/' in name or '.' in name:
            missing[name].add(os.path.relpath(f, acts))
    base, ext = os.path.splitext(f)
    if ext in ('.hpp', '.h'):
        for e in ('.cpp', '.c'):
            if os.path.isfile(base + e):
                stack.append(base + e)

by_dir = defaultdict(list)
for f in seen:
    rel = os.path.relpath(f, acts).replace('\\', '/')
    by_dir[os.path.dirname(rel)].append(rel)
total = 0
for d in sorted(by_dir):
    n = sum(sum(1 for _ in open(os.path.join(acts, r), encoding='utf-8', errors='replace')) for r in by_dir[d])
    total += n
    print('%6d %3d %s' % (n, len(by_dir[d]), d))
print('total lines %d in %d files' % (total, len(seen)))
print('unresolved (libraries):', ', '.join(sorted(k for k in missing if not k.startswith(('windows', 'Windows'))))[:3000])
