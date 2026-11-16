import os, shutil, subprocess, sys

# Copies what reading Replay 1.20 zones needs from the ACTS fork (atian-cod-tools by ate47, GPL-3 or MIT; the IW8
# Radiant fork, branch codex/mw2019-complete) into src/tools/acts: the mw19replay fastfile handler, the Replay schema
# reader and payload exporters (images, meshes, sounds, xpak), the IW fastfile decompressor and the ACTS core pieces
# they call. Not copied: the other games' handlers, tools, GSC, UI, PS4 and DLL parts. Files of ours in
# src/tools/acts/iw8r (the trimmed umbrella headers, the command line) and src/tools/acts/xmake.lua are kept.
# Usage: acts_sync_fork.py <ACTS checkout>

fork = sys.argv[1]
root = 'E:/IW8-Radiant'
dst = root + '/src/tools/acts'

FILES = [
    # Replay handler and the mw19 Replay code
    'src/core/acts/tools/fastfile/handlers/handler_game_mw19_replay.cpp',
    'src/core/acts/tools/mw19/mw19_ff_stream.hpp',
    'src/core/acts/tools/mw19/mw19_replay_bindings.hpp',
    'src/core/acts/tools/mw19/mw19_payload.cpp', 'src/core/acts/tools/mw19/mw19_payload.hpp',
    'src/core/acts/tools/mw19/mw19_schema.cpp', 'src/core/acts/tools/mw19/mw19_schema.hpp',
    'src/core/acts/tools/mw19/mw19_mesh.cpp', 'src/core/acts/tools/mw19/mw19_mesh.hpp',
    'src/core/acts/tools/mw19/mw19_xpak.cpp', 'src/core/acts/tools/mw19/mw19_xpak.hpp',
    'src/core/acts/tools/mw19/mw19_sound.cpp', 'src/core/acts/tools/mw19/mw19_sound.hpp',
    'src/core/acts/tools/mw19/mw19_image.cpp', 'src/core/acts/tools/mw19/mw19_image.hpp',
    'src/core/acts/tools/mw19/mw19_image_formats.hpp',
    # fastfile framework: option and handler types, the IW decompressor (Replay .ff, Oodle blocks, patches)
    'src/core/acts/tools/fastfile/fastfile_handlers.hpp',
    'src/core/acts/tools/fastfile/fastfile_handlers.cpp',
    'src/core/acts/tools/fastfile/fastfile_zone.hpp', 'src/core/acts/tools/fastfile/fastfile_zone.cpp',
    'src/core/acts/tools/fastfile/fastfile_flexible.hpp',
    'src/core/acts/tools/fastfile/fastfile_data_iw.hpp', 'src/core/acts/tools/fastfile/fastfile_data_iw.cpp',
    'src/core/acts/tools/fastfile/fastfile_bdiff.hpp', 'src/core/acts/tools/fastfile/fastfile_bdiff.cpp',
    'src/core/acts/tools/fastfile/fastfile_asset_pool.hpp', 'src/core/acts/tools/fastfile/fastfile_asset_pool.cpp',
    'src/core/acts/tools/fastfile/fastfile_dump.hpp',
    'src/core/acts/tools/fastfile/decompressors/decompressor_iw.cpp',
    # ACTS core
    'src/core/acts/hashutils.cpp', 'src/core/acts/hashutils.hpp',
    'src/core/acts/tools/tools.hpp', 'src/core/acts/tools/tools.cpp', 'src/core/acts/tools/tools_data.hpp',
    'src/core/acts/compatibility/scobalula_csi.hpp', 'src/core/acts/compatibility/scobalula_csi.cpp',
    'src/core/shared/includes_shared.hpp', 'src/core/shared/includes_shared.cpp',
    'src/core/shared/platform/platform.hpp', 'src/core/shared/platform/platform.cpp',
    'src/core/shared/core/logs.hpp', 'src/core/shared/core/logs.cpp',
    'src/core/shared/core/bytebuffer.hpp', 'src/core/shared/core/memory_allocator.hpp',
    'src/core/shared/core/hashes/hash_names.hpp',
    'src/core/shared/cli/clicolor.hpp', 'src/core/shared/cli/clicolor.cpp',
    'src/core/shared/hook/memory.hpp', 'src/core/shared/hook/memory.cpp',
    'src/core/shared/hook/process.hpp', 'src/core/shared/hook/process.cpp',
    'src/core/shared/hook/module_mapper.hpp', 'src/core/shared/hook/module_mapper.cpp',
    'src/core/shared/hook/error.hpp', 'src/core/shared/hook/error.cpp',
    'src/core/shared/deps/oodle.hpp', 'src/core/shared/deps/oodle.cpp',
    'src/core/shared/utils/utils.hpp', 'src/core/shared/utils/utils.cpp',
    'src/core/shared/utils/compress_utils.hpp', 'src/core/shared/utils/compress_utils.cpp',
    'src/core/shared/utils/data_utils.hpp', 'src/core/shared/utils/data_utils.cpp',
    'src/core/shared/utils/memapi.hpp',
    'src/core/shared/utils/hash.hpp',
    'src/core/shared/games/bo4/pool.hpp', 'src/core/shared/games/bo4/pool.cpp',
    'src/core/shared/utils/hash_mini.hpp',
    'src/core/shared/platform/platform_windows.hpp',
    'src/core/shared/platform/platform_windows.cpp',
    'src/core/shared/utils/crc.hpp',
    'src/core/shared/utils/crc.cpp',
    'src/core/shared/hook/scan_container.hpp',
    'src/core/shared/hook/scan_container.cpp',
    'src/core/shared/hook/library.hpp',
    'src/core/shared/hook/library.cpp',
    'src/core/shared/hook/scan_loader.hpp',
    'src/core/shared/hook/scan_loader.cpp',
    'src/core/acts/acts_api_impl/api_impl.hpp',
    'src/core/acts/acts_api_impl/api_impl.cpp',
    'src/core/shared/core/async.hpp',
    'src/core/shared/core/async.cpp',
    'src/core/shared/core/bytebuffer_abstract.hpp',
    'src/core/shared/core/bytebuffer_file.hpp',
    'src/core/shared/core/hashes/hash_store.hpp',
    'src/core/shared/core/hashes/hash_store.cpp',
    'src/core/shared/core/preprocessor.hpp',
    'src/core/shared/core/preprocessor.cpp',
    'src/core/shared/core/shared_cfg.hpp',
    'src/core/shared/core/shared_cfg.cpp',
    'src/core/shared/core/actsinfo.hpp',
    'src/core/shared/core/eventhandler.hpp',
    'src/core/shared/core/eventhandler.cpp',
    'src/core/acts/tools/fastfile/fastfile_flexible.cpp',
    'licenses/mit.md',
    'licenses/gpl3.md',
    'LICENSE.md',
]
EXTRA = [a for a in sys.argv[2:]]  # more files found while building


def git(path, *args):
    return subprocess.run(['git', '-C', path] + list(args), capture_output=True, text=True).stdout.strip()


for d in ('src', 'config', 'include'):
    shutil.rmtree(os.path.join(dst, d), ignore_errors=True)
missing = []
for rel in FILES + EXTRA:
    s = os.path.join(fork, rel)
    if not os.path.isfile(s):
        missing.append(rel)
        continue
    os.makedirs(os.path.dirname(os.path.join(dst, rel)), exist_ok=True)
    shutil.copyfile(s, os.path.join(dst, rel))
# The ACTS API headers (C structs the fastfile option and context derive from).
for f in sorted(os.listdir(os.path.join(fork, 'include/acts_api'))):
    if f.endswith('.h'):
        os.makedirs(os.path.join(dst, 'include/acts_api'), exist_ok=True)
        shutil.copyfile(os.path.join(fork, 'include/acts_api', f), os.path.join(dst, 'include/acts_api', f))
# The Replay schema read at run time goes with the editor's data (data/mw19/schema.json, shipped in the package's data
# folder); the handler finds it beside the exe or in a data folder above it (iw8r/replay_schema.hpp), not only in
# <exe folder>/data, which in a development build would shadow the editor's own data folder lookup.
os.makedirs(os.path.join(root, 'data/mw19'), exist_ok=True)
shutil.copyfile(os.path.join(fork, 'config/data/mw19/schema.json'), os.path.join(root, 'data/mw19/schema.json'))
h = os.path.join(dst, 'src/core/acts/tools/fastfile/handlers/handler_game_mw19_replay.cpp')
text = open(h, encoding='utf-8', newline='').read()
old = 'std::make_unique<Database>(utils::GetProgDir() / "data/mw19/schema.json")'
assert text.count(old) == 1
text = text.replace(old, 'std::make_unique<Database>(iw8r::ReplaySchemaPath())')
nl = '\r\n' if '\r\n' in text else '\n'
text = text.replace('#include <includes.hpp>' + nl, '#include <includes.hpp>' + nl + '#include <replay_schema.hpp>' + nl, 1)
assert 'replay_schema.hpp' in text
open(h, 'w', encoding='utf-8', newline='').write(text)
# Trims (the rest is the fork's code as it is):
# - the fastfile driver, hashutils and the tool registry are replaced by iw8r/ (only the Replay command line);
for rel in ('src/core/acts/tools/fastfile/fastfile_handlers.cpp', 'src/core/acts/hashutils.cpp', 'src/core/acts/tools/tools.cpp'):
    p = os.path.join(dst, rel)
    if os.path.isfile(p):
        os.remove(p)
# - fastfile_handlers.hpp keeps only a pointer to the GSC formatter: a declaration instead of the GSC headers.
p = os.path.join(dst, 'src/core/acts/tools/fastfile/fastfile_handlers.hpp')
text = open(p, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in text else '\n'
for inc in ('#include <tools/gsc/gsc_opcodes_load.hpp>', '#include <tools/gsc/decompiler/gsc_decompiler_formatter.hpp>'):
    assert text.count(inc + nl) == 1, inc
    text = text.replace(inc + nl, '')
text = text.replace('#include <tools/fastfile/fastfile_flexible.hpp>' + nl,
                    '#include <tools/fastfile/fastfile_flexible.hpp>' + nl
                    + 'namespace tool::gsc::formatter { class FormatterInfo; }  // iw8r: only a pointer is kept' + nl, 1)
open(p, 'w', encoding='utf-8', newline='').write(text)
# - fastfile_flexible.cpp includes core/config.hpp (rapidjson, magic_enum) without using it.
p = os.path.join(dst, 'src/core/acts/tools/fastfile/fastfile_flexible.cpp')
text = open(p, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in text else '\n'
assert text.count('#include <core/config.hpp>' + nl) == 1
text = text.replace('#include <core/config.hpp>' + nl, '')
open(p, 'w', encoding='utf-8', newline='').write(text)
# hash-library (Stephan Brumme, zlib licence): the SHA-256 the flexible fastfile header check uses.
hl = os.path.join(root, 'third_party', 'hash_library')
shutil.rmtree(hl, ignore_errors=True)
os.makedirs(hl)
for f in ('sha256.cpp', 'sha256.h', 'hash.h', 'LICENSE'):
    shutil.copyfile(os.path.join(fork, 'deps/hash-library', f), os.path.join(hl, f))
with open(os.path.join(hl, 'UPSTREAM.txt'), 'w', encoding='utf-8', newline='\n') as fh:
    fh.write('hash-library (https://create.stephan-brumme.com/hash-library/) from the ACTS fork\'s deps/hash-library at %s;\n'
             'only SHA-256 (iw8r-acts: the flexible fastfile header check).\n' % git(os.path.join(fork, 'deps/hash-library'), 'rev-parse', 'HEAD'))
rev = git(fork, 'log', '-1', '--format=%H %ad', '--date=short')
branch = git(fork, 'branch', '--show-current')
with open(dst + '/UPSTREAM.txt', 'w', encoding='utf-8', newline='\n') as fh:
    fh.write('ACTS fork (atian-cod-tools), branch %s, %s; upstream https://github.com/ate47/atian-cod-tools\n' % (branch, rev))
    fh.write('Kept: the mw19replay fastfile handler and the mw19 Replay code (schema, bindings, payload exporters for\n'
             'images, meshes, sounds and xpak), the fastfile option and handler types, the IW fastfile decompressor and the\n'
             'ACTS core pieces they call (logs, utils, hook, Oodle). Left out: every other game, tool, GSC, UI, PS4 and DLL\n'
             'part. iw8r/ holds our trimmed umbrella headers and the command line (iw8r-acts.exe). Licence: LICENSE.md\n'
             '(GPL-3 or MIT; used here under MIT). Copied by tools/patches/acts_sync_fork.py.\n')
print(len(FILES + EXTRA) - len(missing), 'files from', branch, rev)
if missing:
    print('missing:', missing)
