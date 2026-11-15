"""Offline proof of a map's own HUD fonts (roadmap 4.27; owner 2026-11-15; src/core/hud_fonts.h).

Builds tests/data/iw8_sample_room.map with "<map>.hud/fonts.txt" (a label and a glyph set in a TrueType font taken
from the Windows fonts folder) and a map script that writes with them, through the editor's headless build
(IW8Radiant.exe --compile: the editor draws the font pictures, extraction, converter with gsc-tool, ACTS), and checks:
  - the editor drew the pictures (compile log "HUD fonts: N picture(s)") and the extraction holds them;
  - the generated map script holds the helpers and calls iw8r_font_init() before user_main;
  - gsc-tool compiled that script (the converter's own script, not its minimap-only fallback);
  - the converter carried every font picture with a 2D material of its name;
  - the game's loaders accept every zone (ACTS --test), and the built zones read back hold the pictures and materials.
The package the build writes into the editor's output folder is deleted afterwards (it is this proof's own map).

Usage: py -3 -I tools/hud_font_build_test.py <work dir> [--editor <IW8Radiant.exe>] [--exe <zonetool.exe>]
Prints PASS / FAIL lines and RESULT ok|fail."""
import os, re, shutil, subprocess, sys  # noqa: E401

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
ACTS = r'E:/mw124/atian-cod-tools/build/bin/Release/acts.exe'
REPLAY = r'E:/IW8/Builds/1.20-replay/game_dx12_ship_replay.exe'
OODLE = r'E:/IW8/Builds/1.20-replay/oo2core_7_win64.dll'
MAP_ID = 'mp_iw8r_fontprf'
WANT = ['hud_title', 'hud_digits_1', 'hud_digits_2', 'hud_digits_0', 'hud_digits_c58']

failures = 0


def check(ok, what):
    global failures
    print(('PASS ' if ok else 'FAIL ') + what, flush=True)
    if not ok:
        failures += 1


font = next((f for f in (r'C:/Windows/Fonts/impact.ttf', r'C:/Windows/Fonts/arialbd.ttf', r'C:/Windows/Fonts/arial.ttf') if os.path.isfile(f)), '')
if not font:
    print('SKIP no TrueType font in C:/Windows/Fonts')
    print('RESULT ok (skipped)')
    sys.exit(0)
shutil.rmtree(work, ignore_errors=True)
hud = os.path.join(work, 'iw8r_fontprf.hud')
os.makedirs(hud)
shutil.copyfile(font, os.path.join(hud, 'Proof Font.ttf'))
open(os.path.join(hud, 'fonts.txt'), 'w', encoding='utf-8', newline='\n').write(
    '# the proof map\'s fonts\n'
    'label  | title  | Proof Font.ttf | 48 | #ffcc00 | Cinder Rail\n'
    'glyphs | digits | Proof Font.ttf | 40 | #ffffff | 0123456789:\n')
open(os.path.join(work, 'font_user.gsc'), 'w', encoding='utf-8', newline='\n').write(
    'user_main()\n{\n    title = iw8r_font_label("title", 20, 40, 48);\n'
    '    clock = iw8r_font_text("digits", "12:30", 20, 100, 40);\n    wait 5;\n    iw8r_font_clear(clock);\n}\n')
src = open(os.path.join(root, 'tests', 'data', 'iw8_sample_room.map'), encoding='utf-8').read()
src = src.replace('"classname" "worldspawn"', '"classname" "worldspawn"\n"iw8r_script" "font_user.gsc"', 1)
map_path = os.path.join(work, 'iw8r_fontprf.map')
open(map_path, 'w', encoding='utf-8', newline='\n').write(src)
out = os.path.join(work, 'build')
env = dict(os.environ)
if converter:
    env['IW8R_TOOL_ZONETOOL'] = converter
subprocess.run([editor, '--compile', map_path, '--out', out], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, env=env)


def text_of(path):
    return open(path, encoding='utf-8', errors='replace').read() if os.path.isfile(path) else ''


result, log = text_of(os.path.join(out, 'compile-result.txt')), text_of(os.path.join(out, 'compile-log.txt'))
first = result.splitlines()[0] if result else ''
check(first.startswith('ok map=' + MAP_ID), 'build ok (%s)' % first[:120])
m = re.search(r'package=(.+)$', first)
package = m.group(1).strip() if m else ''
both = result + log
drawn = re.search(r'HUD fonts: (\d+) picture\(s\)', both)
check(drawn is not None and int(drawn.group(1)) == 12, 'editor: ' + (drawn.group(0) if drawn else 'no "HUD fonts" line') + ' (1 label + 11 glyphs)')

ext = os.path.join(work, 'iw8build', MAP_ID, 'extraction', 'extracted')
for n in WANT:
    p = os.path.join(ext, 'images', n + '.dds')
    check(os.path.isfile(p) and open(p, 'rb').read(4) == b'DDS ', 'extraction: images/%s.dds' % n)
script = ''
for base, _dirs, files in os.walk(os.path.join(work, 'iw8build', MAP_ID)):
    for f in files:
        if f.endswith('.gsc'):
            t = text_of(os.path.join(base, f))
            if 'iw8r_font_init' in t:
                script = t
check(bool(script), 'the generated map script holds the font helpers')
if script:
    i, u = script.find('    iw8r_font_init();'), script.find('level thread user_main();')
    check(0 <= i < u, 'main() calls iw8r_font_init() before user_main')
    check('iw8r_font_text(set, text, x, y, height)' in script and 'level.iw8r_font_chars["digits"]' in script, 'helpers and the digits table')
fallback = [l for l in log.splitlines() if 'minimap-only' in l.lower() or ('gsc' in l.lower() and 'error' in l.lower())]
compiled = re.search(r'scripts: compiled converted map script \((\d+) bytes gscbin', log)
check(not fallback and compiled is not None,
      'gsc-tool compiled the map script (%s)' % (fallback[0][:120] if fallback else compiled.group(0) if compiled else 'no compile line'))
for n in WANT:
    check('included HUD image ' + n in log and 'HUD image ' + n + ' with its 2D material' in log, 'converter: %s carried with its 2D material' % n)
# iw8r_font_init precaches every picture by its literal name: the level's material table lists all 12.
mats = re.search(r'level NetConstStrings: .*?(\d+) HUD material\(s\)', log)
check(mats is not None and int(mats.group(1)) >= 12, 'level material table: ' + (mats.group(0) if mats else 'no line'))
acts = re.findall(r'ACTS --test (\S+): exit (\d+)', log)
check(len(acts) >= 3 and all(code == '0' for _, code in acts), 'ACTS: %s' % ', '.join('%s %s' % a for a in acts))

ff = os.path.join(package, MAP_ID + '.ff') if package else ''
tff = os.path.join(package, 'techsets_' + MAP_ID + '.ff') if package else ''
if ff and os.path.isfile(ff) and os.path.isfile(tff):
    found = {'material': set(), 'image': set()}
    for zone_file, zone in ((ff, MAP_ID), (tff, 'techsets_' + MAP_ID)):
        acts_out = os.path.join(work, 'acts_' + zone)
        subprocess.run([ACTS, '--noUpdater', 'fastfile', '-r', 'mw19replay', '-g', REPLAY, '--oodle', OODLE, '-a', 'material,image',
                        '-l', '-o', acts_out, zone_file], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        for base, _dirs, files in os.walk(os.path.join(acts_out, 'mw19replay', zone)):
            for f in files:
                for n in WANT:
                    if f.startswith(n + '.') or f.startswith(n + '_') and not f.startswith(n + '_c'):
                        kind = 'material' if os.sep + 'material' in base else 'image' if os.sep + 'image' in base else ''
                        if kind and (f.split('.')[0] == n or f.startswith(n + '.')):
                            found[kind].add(n)
    check(found['material'] >= set(WANT), 'built zones: materials %s' % sorted(found['material']))
    check(found['image'] >= set(WANT), 'built zones: images %s' % sorted(found['image']))
else:
    check(False, 'built zones ' + ff)

if package and os.path.basename(package.rstrip('/\\')) == MAP_ID:
    shutil.rmtree(package, ignore_errors=True)
print('RESULT %s (%d failure(s))' % ('ok' if not failures else 'fail', failures))
sys.exit(1 if failures else 0)
