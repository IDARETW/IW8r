"""Offline proof of a map's own HUD images (roadmap 4.27; docs/research/36 section 8.2.1).

Builds tests/data/iw8_sample_room.map with a picture in "<map>.hud" and a map script that draws it, through the
editor's headless build (IW8Radiant.exe --compile: extraction, converter, ACTS), and checks:
  - the extraction holds images/hud_<name>.dds;
  - the converter carried the image and wrote a 2D material of its name;
  - the game's loaders accept every zone (ACTS --test);
  - the built zones, read back with ACTS, hold the image (map zone) and its material (the map's technique zone).
The package the build writes into the editor's output folder is deleted afterwards (it is this proof's own map).

Usage: py -3 -I tools/hud_image_build_test.py <work dir> [--editor <IW8Radiant.exe>] [--exe <zonetool.exe>]
--exe builds with that converter instead of the configured one (IW8R_TOOL_ZONETOOL).
Prints PASS / FAIL lines and RESULT ok|fail."""
import os, re, shutil, struct, subprocess, sys, zlib  # noqa: E401

here = os.path.dirname(os.path.abspath(__file__))
root = os.path.dirname(here)
args = sys.argv[1:]
editor = os.path.join(root, 'bin', 'release', 'IW8Radiant.exe')
converter = ''
if '--editor' in args:
    editor = os.path.abspath(args[args.index('--editor') + 1])
    del args[args.index('--editor'):args.index('--editor') + 2]
if '--exe' in args:
    converter = os.path.abspath(args[args.index('--exe') + 1])
    del args[args.index('--exe'):args.index('--exe') + 2]
work = os.path.abspath(args[0])
ACTS = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'bin', 'release', 'iw8r-acts.exe')
REPLAY = r'E:/IW8/Builds/1.20-replay/game_dx12_ship_replay.exe'
OODLE = r'E:/IW8/Builds/1.20-replay/oo2core_7_win64.dll'
MAP_ID = 'mp_iw8r_hudprf'

failures = 0


def check(ok, what):
    global failures
    print(('PASS ' if ok else 'FAIL ') + what, flush=True)
    if not ok:
        failures += 1


def png(path, w, h):
    # An RGBA picture: a red disc on a transparent ground.
    rows = b''
    for y in range(h):
        rows += b'\x00'
        for x in range(w):
            inside = (x - w / 2) ** 2 + (y - h / 2) ** 2 < (w * 0.4) ** 2
            rows += bytes((220, 40, 40, 255)) if inside else bytes((0, 0, 0, 0))

    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data) & 0xffffffff)

    open(path, 'wb').write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0)) +
                           chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b''))


shutil.rmtree(work, ignore_errors=True)
os.makedirs(os.path.join(work, 'iw8r_hudprf.hud'))
png(os.path.join(work, 'iw8r_hudprf.hud', 'Proof Flag.png'), 64, 64)
open(os.path.join(work, 'hud_user.gsc'), 'w', encoding='utf-8', newline='\n').write(
    'user_main()\n{\n    icon = newhudelem();\n    icon.x = 20;\n    icon.y = 120;\n'
    '    icon setshader( "hud_proof_flag", 64, 64 );\n}\n')
src = open(os.path.join(root, 'tests', 'data', 'iw8_sample_room.map'), encoding='utf-8').read()
src = src.replace('"classname" "worldspawn"', '"classname" "worldspawn"\n"iw8r_script" "hud_user.gsc"', 1)
map_path = os.path.join(work, 'iw8r_hudprf.map')
open(map_path, 'w', encoding='utf-8', newline='\n').write(src)
out = os.path.join(work, 'build')
env = dict(os.environ)
if converter:
    env['IW8R_TOOL_ZONETOOL'] = converter
subprocess.run([editor, '--compile', map_path, '--out', out], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, env=env)


def text_of(name):
    p = os.path.join(out, name)
    return open(p, encoding='utf-8', errors='replace').read() if os.path.isfile(p) else ''


result, log = text_of('compile-result.txt'), text_of('compile-log.txt')
first = result.splitlines()[0] if result else ''
check(first.startswith('ok map=' + MAP_ID), 'build ok (%s)' % first[:120])
m = re.search(r'package=(.+)$', first)
package = m.group(1).strip() if m else ''
both = result + log
check('1 HUD image(s)' in both and 'hud_proof_flag' in both,
      'editor: ' + next((l for l in both.splitlines() if 'HUD image(s)' in l), '-')[:150])
check('included HUD image hud_proof_flag' in log, 'converter: the image is carried beside the minimap')
check('HUD image hud_proof_flag with its 2D material' in log, 'converter: a 2D material of its name')
acts = re.findall(r'ACTS --test (\S+): exit (\d+)', log)
check(len(acts) >= 3 and all(code == '0' for _, code in acts), 'ACTS: %s' % ', '.join('%s %s' % a for a in acts))

ext = os.path.join(work, 'iw8build', MAP_ID, 'extraction', 'extracted')
dds = os.path.join(ext, 'images', 'hud_proof_flag.dds')
check(os.path.isfile(dds) and open(dds, 'rb').read(4) == b'DDS ', 'extraction: images/hud_proof_flag.dds')

# The built zones: the image is in the map zone, its material in the map's technique zone (where the minimap's is).
ff = os.path.join(package, MAP_ID + '.ff') if package else ''
tff = os.path.join(package, 'techsets_' + MAP_ID + '.ff') if package else ''
if ff and os.path.isfile(ff) and os.path.isfile(tff):
    found = {'material': [], 'image': []}
    for zone_file, zone in ((ff, MAP_ID), (tff, 'techsets_' + MAP_ID)):
        acts_out = os.path.join(work, 'acts_' + zone)
        subprocess.run([ACTS, '--noUpdater', 'fastfile', '-r', 'mw19replay', '-g', REPLAY, '--oodle', OODLE, '-a', 'material,image',
                        '-l', '-o', acts_out, zone_file], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        for base, _dirs, files in os.walk(os.path.join(acts_out, 'mw19replay', zone)):
            for f in files:
                if 'hud_proof_flag' in f:
                    kind = 'material' if os.sep + 'material' in base else 'image' if os.sep + 'image' in base else ''
                    if kind:
                        found[kind].append(zone + ': ' + f)
    check(len(found['material']) >= 1, 'built zones: material hud_proof_flag (%s)' % (found['material'][:1] or '-'))
    check(len(found['image']) >= 1, 'built zones: image hud_proof_flag (%s)' % (found['image'][:1] or '-'))
else:
    check(False, 'built zones ' + ff)

if package and os.path.basename(package.rstrip('/\\')) == MAP_ID:
    shutil.rmtree(package, ignore_errors=True)
print('RESULT %s (%d failure(s))' % ('ok' if not failures else 'fail', failures))
sys.exit(1 if failures else 0)
