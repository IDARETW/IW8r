"""Headers the copied ACTS files include that are not in src/tools/acts yet (resolved like iw8r-acts' include path:
iw8r/ first, then the copied tree, then the fork). Prints fork-relative paths, ready for acts_sync_fork.py's list.
Usage: acts_missing_headers.py <ACTS checkout>"""
import os, re, sys

fork = sys.argv[1]
ours = 'E:/IW8-Radiant/src/tools/acts'
dirs = ['src/core/acts', 'src/core/shared', 'src/core/lib', 'include']
inc_re = re.compile(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]', re.M)


def find(name, frm, base):
    for c in [os.path.join(os.path.dirname(frm), name)] + [os.path.join(base, d, name) for d in dirs]:
        if os.path.isfile(c):
            return os.path.normpath(c)
    return None


todo = []
for d, _, fs in os.walk(ours):
    for f in fs:
        if f.endswith(('.cpp', '.hpp', '.h')):
            todo.append(os.path.normpath(os.path.join(d, f)))
seen, missing = set(), set()
while todo:
    f = todo.pop()
    if f in seen:
        continue
    seen.add(f)
    text = open(f, encoding='utf-8', errors='replace').read()
    for name in inc_re.findall(text):
        if os.path.isfile(os.path.join(ours, 'iw8r', name)):
            continue
        local = find(name, f, ours)
        if local:
            todo.append(local)
            continue
        # A fork file: relative to the fork (the including file may be ours: map its folder into the fork).
        frm = f
        if f.startswith(os.path.normpath(ours)):
            frm = os.path.join(fork, os.path.relpath(f, ours))
        remote = find(name, frm, fork)
        if remote:
            rel = os.path.relpath(remote, fork).replace('\\', '/')
            if rel not in missing:
                missing.add(rel)
                todo.append(remote)
print('\n'.join(sorted(missing)))
