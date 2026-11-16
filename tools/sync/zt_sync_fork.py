import os, shutil, subprocess, sys

# Copies what the editor needs from the x64-zt fork (zonetool by the x64-zt authors, GPL-3; the IW8 Radiant fork,
# branch iw8radiant/build-extraction) into src/tools/zonetool, and the libraries it uses into third_party.
# Kept: cli, common, core (minus the CASC reader, which only H2 uses), games iw8 + iw3 (the converter's source game),
# convert/iw3_iw8 (the map pipeline the editor's builds go through). Left out: games h1, h2, iw6, iw7, s1, s2, their
# converters (iw7_iw8 included), shader-tool, minizip, CascLib, stb.
# Usage: zt_sync_fork.py <x64-zt checkout>

fork = sys.argv[1]
root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))).replace('\\', '/')
dst = root + '/src/tools/zonetool'


def git(path, *args):
    return subprocess.run(['git', '-C', path] + list(args), capture_output=True, text=True).stdout.strip()


def copytree(src, out, keep=lambda rel: True):
    n = 0
    for base, dirs, files in os.walk(src):
        dirs[:] = [d for d in dirs if d not in ('.git', '__pycache__')]
        for f in files:
            rel = os.path.relpath(os.path.join(base, f), src).replace('\\', '/')
            if not keep(rel):
                continue
            os.makedirs(os.path.dirname(os.path.join(out, rel)), exist_ok=True)
            shutil.copyfile(os.path.join(base, f), os.path.join(out, rel))
            n += 1
    return n


# -- x64-zt sources
# src/tools/zonetool/xmake.lua is ours (the build of this copy): only the copied parts are replaced.
for d in ('src', 'generated'):
    shutil.rmtree(os.path.join(dst, d), ignore_errors=True)
count = 0
for d in ('cli', 'common', 'core', 'games/iw8', 'games/iw3', 'convert/iw3_iw8'):
    count += copytree(os.path.join(fork, 'src', d), os.path.join(dst, 'src', d),
                      lambda rel, d=d: not (d == 'core' and rel == 'io/casc_files.cpp'))
# The S2 zone signing key (cli/resources/s2_zone_key.der) is for S2 zones only: left out with its resource line.
os.remove(os.path.join(dst, 'src/cli/resources/s2_zone_key.der'))
rc = os.path.join(dst, 'src/cli/resource.rc')
text = open(rc, encoding='utf-8', newline='').read()
line = 'S2_ZONE_KEY                RCDATA "resources/s2_zone_key.der"'
assert text.count(line) == 1
text = text.replace(line + '\r\n', '').replace(line + '\n', '')
open(rc, 'w', encoding='utf-8', newline='').write(text)
for f in ('LICENSE', 'README.md'):
    shutil.copyfile(os.path.join(fork, f), os.path.join(dst, f))
os.makedirs(dst + '/generated', exist_ok=True)
with open(dst + '/generated/enabled_games.hpp', 'w', encoding='utf-8', newline='\n') as fh:
    fh.write('#pragma once\n\n// IW8 Radiant builds only IW8 and the IW3 map converter (src/tools/zonetool/UPSTREAM.txt).\n'
             '#define ZONETOOL_GAMES(x) x(iw3) x(iw8)\n#define ZONETOOL_CONVERTERS(x) x(iw3, iw8)\n')
rev = git(fork, 'log', '-1', '--format=%H %ad', '--date=short')
desc = git(fork, 'describe', '--tags', '--always')
branch = git(fork, 'branch', '--show-current')
count_n = git(fork, 'rev-list', '--count', 'HEAD')
with open(dst + '/src/version.h', 'w', encoding='utf-8', newline='\n') as fh:
    fh.write('// Version of the x64-zt fork this copy came from (written by tools/sync/zt_sync_fork.py).\n\n'
             '#define GIT_DESCRIBE "%s"\n#define GIT_DIRTY 0\n#define GIT_HASH "%s"\n#define GIT_TAG "latest"\n'
             '#define GIT_BRANCH "%s"\n\n#define VERSION_PRODUCT_RC 0,0,0\n#define VERSION_PRODUCT "0.0.0"\n'
             '#define VERSION_FILE_RC 0,0,0,%s\n#define VERSION_FILE "0.0.0.%s"\n\n#define VERSION GIT_DESCRIBE\n'
             '#define SHORTVERSION VERSION_PRODUCT\n' % (desc, rev.split()[0], branch, count_n, count_n))
with open(dst + '/src/version.hpp', 'w', encoding='utf-8', newline='\n') as fh:
    fh.write('#include ".\\version.h"\n')
with open(dst + '/UPSTREAM.txt', 'w', encoding='utf-8', newline='\n') as fh:
    fh.write('x64-zt fork (zonetool), branch %s, %s (%s); upstream https://github.com/Joelrau/x64-zt\n' % (branch, rev, desc))
    fh.write('Kept: src/cli, src/common, src/core (minus io/casc_files.cpp: CASC storage is H2 only), src/games/iw8,\n'
             'src/games/iw3, src/convert/iw3_iw8. Left out: the other games and converters, the S2 zone signing key\n'
             '(cli/resources/s2_zone_key.der and its resource.rc line), shader-tool, CascLib, stb.\n'
             'generated/enabled_games.hpp and src/version.h are written by tools/sync/zt_sync_fork.py.\n'
             'Built by xmake.lua (targets zt_*, iw8r-zt). Licence: LICENSE (GPL-3).\n')

# -- libraries (only the files the fork's premake scripts compile or include)
libs = {
    'zstd': ('deps/zstd', lambda r: r in ('LICENSE', 'COPYING') or (r.startswith('lib/') and r.split('/')[1] in (
        'common', 'compress', 'decompress', 'dictBuilder', 'zstd.h', 'zdict.h', 'zstd_errors.h')
        and r.endswith(('.c', '.h')))),
    'lz4': ('deps/lz4', lambda r: r == 'LICENSE' or (r.startswith('lib/') and r.count('/') == 1 and r.endswith(('.c', '.h')))
            or r == 'lib/LICENSE'),
    'minizip': ('deps/zlib/contrib/minizip', lambda r: r.count('/') == 0 and (r.endswith(('.c', '.h'))
                or r in ('LICENSE.Info-Zip', 'MiniZip64_info.txt')) and r not in ('miniunz.c', 'minizip.c')),
    'nlohmann_json': ('deps/json', lambda r: r == 'LICENSE.MIT' or r == 'single_include/nlohmann/json.hpp'
                      or r == 'single_include/nlohmann/json_fwd.hpp'),
    'gsl': ('deps/GSL', lambda r: r == 'LICENSE' or r.startswith('include/gsl/')),
    'glaze': ('deps/glaze', lambda r: r == 'LICENSE' or r.startswith('include/')),
    'libtomcrypt': ('deps/libtomcrypt', lambda r: r == 'LICENSE' or (r.startswith('src/') and (
        r.endswith('.h') or (r.endswith('.c') and not r.startswith('src/encauth/ocb3/')))) ),  # *tab.c are #included
    'libtommath': ('deps/libtommath', lambda r: r == 'LICENSE' or (r.count('/') == 0 and r.endswith(('.c', '.h')))),
    'directxtex': ('deps/DirectXTex', lambda r: r == 'LICENSE' or (r.startswith('Common/') and r.count('/') == 1 and r.endswith('.h'))
                   or (r.startswith('DirectXTex/') and r.count('/') == 1 and r.endswith(('.cpp', '.h', '.inl'))
                       and r != 'DirectXTex/BCDirectCompute.cpp')),  # its header is included by DirectXTexCompressGPU.cpp
}
for name, (sub, keep) in libs.items():
    out = os.path.join(root, 'third_party', name)
    shutil.rmtree(out, ignore_errors=True)
    n = copytree(os.path.join(fork, sub), out, keep)
    with open(os.path.join(out, 'UPSTREAM.txt'), 'w', encoding='utf-8', newline='\n') as fh:
        fh.write('%s from the x64-zt fork\'s %s at %s (%s); only the files its build uses.\n'
                 % (name, sub, git(os.path.join(fork, sub), 'rev-parse', 'HEAD') or '?',
                    git(os.path.join(fork, sub), 'describe', '--tags', '--always') or '?'))
    print(name, n, 'files')
print('zonetool', count, 'files from', branch, rev)
