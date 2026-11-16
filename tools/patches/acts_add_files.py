"""Adds fork files to acts_sync_fork.py's FILES list (each header's same-named .cpp too when the fork has one).
Usage: acts_add_files.py <ACTS checkout> <fork-relative path>..."""
import io, os, sys

fork = sys.argv[1]
p = 'E:/IW8-Radiant/tools/patches/acts_sync_fork.py'
s = io.open(p, encoding='utf-8', newline='').read()
anchor = "    'LICENSE.md',\n"
assert s.count(anchor) == 1
added = []
for rel in sys.argv[2:]:
    cands = [rel]
    base, ext = os.path.splitext(rel)
    if ext in ('.hpp', '.h'):
        cands += [base + '.cpp', base + '.c']
    for c in cands:
        line = "    '%s',\n" % c
        if os.path.isfile(os.path.join(fork, c)) and line not in s:
            s = s.replace(anchor, line + anchor)
            added.append(c)
io.open(p, 'w', encoding='utf-8', newline='').write(s)
print('added', len(added), ':', ' '.join(added))
