import io

# Follow-up of bake_settings.py: the panel header names the core type, and a string literal broken by a shell heredoc.

ROOT = 'E:/IW8-Radiant/'


def read(p):
    return io.open(ROOT + p, encoding='utf-8', newline='').read()


def write(p, s):
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s)


H = 'src/qtapp/compile_panel.h'
s = read(H)
assert s.count('const struct MapBakeOptions& options') == 1
s = s.replace('const struct MapBakeOptions& options', 'const MapBakeOptions& options')
if '#include "bake_options.h"' not in s:
    i = s.index('#include "')
    s = s[:i] + '#include "bake_options.h"\n' + s[i:]
write(H, s)

for p in ('tests/bake_options_tests.cpp', 'tools/patches/bake_settings.py'):
    s = read(p)
    for nl in ('\r\n', '\n'):
        broken = 'parseMap("iwmap 4' + nl + '" + text, next)'
        if broken in s:
            s = s.replace(broken, 'parseMap("iwmap 4' + ('\\n' if p.startswith('tests') else '\\\\n') + '" + text, next)')
    write(p, s)
    print(p, 'parseMap("iwmap 4\\n" + text' in s or 'iwmap 4\\\\n' in s)
print('ok')
