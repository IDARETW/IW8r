import io

p = 'src/qtapp/worker_guard.h'
s = io.open(p, encoding='utf-8').read()
bad = "*p == '\\')"
good = "*p == '\\\\')"
assert s.count(bad) == 1, s.count(bad)
s = s.replace(bad, good)
io.open(p, 'w', encoding='utf-8').write(s)
print('ok')
