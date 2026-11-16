import os, shutil, subprocess, sys

# Copies what the IW8 compiler needs from a gsc-tool checkout (xensik/gsc-tool, GPL-3) into src/tools/gsc, then trims
# the decompiler and printer out of the context (gsc_trim_context.py). Usage: gsc_sync_upstream.py <gsc-tool checkout>

src = sys.argv[1]
dst = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))).replace('\\', '/') + '/src/tools/gsc'
files = [
    'LICENSE',
    'include/xsk/stdinc.hpp', 'include/xsk/pool.hpp',
    'include/xsk/gsc/assembler.hpp', 'include/xsk/gsc/compiler.hpp', 'include/xsk/gsc/context.hpp',
    'include/xsk/gsc/disassembler.hpp', 'include/xsk/gsc/lexer.hpp', 'include/xsk/gsc/parser.hpp',
    'include/xsk/gsc/preprocessor.hpp', 'include/xsk/gsc/engine/iw8.hpp',
    'src/gsc/assembler.cpp', 'src/gsc/compiler.cpp', 'src/gsc/context.cpp', 'src/gsc/disassembler.cpp',
    'src/gsc/lexer.cpp', 'src/gsc/parser.cpp', 'src/gsc/preprocessor.cpp',
]
dirs = ['include/xsk/utils', 'include/xsk/gsc/common', 'src/utils', 'src/gsc/common']
for d in ('include', 'src'):
    shutil.rmtree(os.path.join(dst, d), ignore_errors=True)
for d in dirs:
    for f in sorted(os.listdir(os.path.join(src, d))):
        files.append(d + '/' + f)
for f in sorted(os.listdir(os.path.join(src, 'src/gsc/engine'))):
    if f.startswith('iw8') and f.endswith('.cpp'):
        files.append('src/gsc/engine/' + f)
for f in files:
    os.makedirs(os.path.dirname(os.path.join(dst, f)), exist_ok=True)
    shutil.copyfile(os.path.join(src, f), os.path.join(dst, f))
rev = subprocess.run(['git', '-C', src, 'log', '-1', '--format=%H %ad', '--date=short'], capture_output=True, text=True).stdout.strip()
tag = subprocess.run(['git', '-C', src, 'describe', '--tags'], capture_output=True, text=True).stdout.strip()
with open(os.path.join(dst, 'UPSTREAM.txt'), 'w', encoding='utf-8', newline='\n') as fh:
    fh.write('https://github.com/xensik/gsc-tool ' + tag + ' ' + rev + '\n')
    fh.write('Only the IW8 compiler is kept (src/tools/gsc: utils, gsc common, lexer, parser, preprocessor, compiler,\n'
             'assembler, disassembler for compiled includes, engine/iw8*); the decompiler and printer are trimmed from\n'
             'the context by tools/sync/gsc_trim_context.py. The command line is ours: cli/iw8r_gsc.cpp.\n')
print(len(files), 'files from', tag, rev)
