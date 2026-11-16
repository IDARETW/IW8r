import io, os, re

# The editor, its offline tools and the tools scripts use the project's own Replay reader (bin/<mode>/iw8r-acts.exe,
# built from src/tools/acts) instead of the ACTS fork checkout. The parity tools (acts_parity.py, zt_parity.py) keep
# the fork as their reference.

R = 'E:/IW8-Radiant/'
FORK_FWD = 'E:/mw124/atian-cod-tools/build/bin/Release/acts.exe'
FORK_BACK = r'E:\mw124\atian-cod-tools\build\bin\Release\acts.exe'


def rw(p, fn):
    s = io.open(R + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    t = fn(s.replace('\r\n', '\n'))
    io.open(R + p, 'w', encoding='utf-8', newline='').write(t.replace('\n', nl))


def cpp(s):
    s = s.replace('{"acts", "%s"},' % FORK_FWD,
                  '{"acts", QCoreApplication::applicationDirPath() + "/iw8r-acts.exe"},  // src/tools/acts')
    s = s.replace('qEnvironmentVariable("IW8R_TOOL_ACTS", "%s")' % FORK_FWD,
                  'qEnvironmentVariable("IW8R_TOOL_ACTS", QCoreApplication::applicationDirPath() + "/iw8r-acts.exe")')
    assert FORK_FWD not in s
    if 'QCoreApplication' in s and '#include <QCoreApplication>' not in s:
        s = s.replace('#include <Q', '#include <QCoreApplication>\n#include <Q', 1)
    return s


for f in ('src/qtapp/main_window.cpp', 'src/qtapp/asset_audit.cpp', 'src/qtapp/material_cache.cpp', 'src/qtapp/model_cache.cpp',
          'src/qtapp/sound_preview.cpp', 'src/qtapp/stock_sky.cpp', 'src/qtapp/world_cache.cpp'):
    rw(f, cpp)

OURS = "os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'bin', 'release', 'iw8r-acts.exe')"
pat = re.compile(r"""^ACTS = r?['"]E:[\\/]mw124[\\/]atian-cod-tools[\\/]build[\\/]bin[\\/]Release[\\/]acts\.exe['"]""", re.M)


def py(s):
    s2 = pat.sub('ACTS = ' + OURS, s)
    assert s2 != s
    if not re.search(r'^import .*\bos\b', s2, re.M) and not re.search(r'^import os', s2, re.M):
        s2 = re.sub(r'^(import [^\n]*)$', r'\1\nimport os', s2, count=1, flags=re.M)
    return s2


for f in sorted(os.listdir(R + 'tools')):
    if not f.endswith('.py') or f in ('acts_parity.py', 'zt_parity.py'):
        continue
    text = io.open(R + 'tools/' + f, encoding='utf-8').read()
    if pat.search(text):
        rw('tools/' + f, py)
        print('tools/' + f)

rw('tools/m2_rebuild_mapents.ps1', lambda s: s.replace(
    "    [string]$Acts = '%s'" % FORK_BACK, "    [string]$Acts = ''  # empty: bin\\release\\iw8r-acts.exe (src/tools/acts)").replace(
    "if (-not $ZoneTool) { $ZoneTool = Join-Path $root 'bin\\release\\iw8r-zt.exe' }\n",
    "if (-not $ZoneTool) { $ZoneTool = Join-Path $root 'bin\\release\\iw8r-zt.exe' }\nif (-not $Acts) { $Acts = Join-Path $root 'bin\\release\\iw8r-acts.exe' }\n"))
print('ok')
