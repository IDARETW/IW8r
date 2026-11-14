"""Offline proof of a map's own text (roadmap 4.27; docs/research/36 section 8.2.2).

Builds tests/data/iw8_sample_room.map with a "<map>.str" text file and a map script that uses one entry, through the
editor's headless build (IW8Radiant.exe --compile: extraction, converter, ACTS), and checks:
  - the extraction holds localize/strings.tsv with the entries under the map's id;
  - the converter registered them in eng_<map>;
  - the game's loaders accept every zone (ACTS --test);
  - eng_<map>.ff, read back with ACTS, holds each entry's name and text.
The package the build writes into the editor's output folder is deleted afterwards (it is this proof's own map).

Usage: py -3 -I tools/map_strings_build_test.py <work dir> [--editor <IW8Radiant.exe>] [--exe <zonetool.exe>]
--exe builds with that converter instead of the configured one (IW8R_TOOL_ZONETOOL).
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
MAP_ID = 'mp_iw8r_strprf'
PREFIX = MAP_ID.upper()
HINT = 'Press ^3[{+activate}]^7 to open the proof gate'
TITLE_LINES = ('Proof room', 'second line')

failures = 0


def check(ok, what):
    global failures
    print(('PASS ' if ok else 'FAIL ') + what, flush=True)
    if not ok:
        failures += 1


shutil.rmtree(work, ignore_errors=True)
os.makedirs(work)
open(os.path.join(work, 'iw8r_strprf.str'), 'w', encoding='utf-8', newline='\n').write(
    '// text of the proof map\n'
    'REFERENCE       HINT_GATE\n'
    'LANG_ENGLISH    "' + HINT + '"\n'
    'REFERENCE       TITLE\n'
    'LANG_ENGLISH    "' + TITLE_LINES[0] + chr(92) + 'n' + TITLE_LINES[1] + '"\n')
open(os.path.join(work, 'str_user.gsc'), 'w', encoding='utf-8', newline='\n').write(
    'user_main()\n{\n    label = newhudelem();\n    label.x = 20;\n    label.y = 100;\n'
    '    label settext( &"' + PREFIX + '/TITLE" );\n}\n')
src = open(os.path.join(root, 'tests', 'data', 'iw8_sample_room.map'), encoding='utf-8').read()
src = src.replace('"classname" "worldspawn"', '"classname" "worldspawn"\n"iw8r_script" "str_user.gsc"', 1)
# A use trigger with a hint: the sample's brush trigger (room_trigger), copied as a trigger_use with the key "hint".
TRIGGER_HINT = 'Press ^3[{+activate}]^7 to use the proof lever'
at = src.find('"targetname" "room_trigger"')
start = src.rfind('\n{', 0, at) + 1 if at >= 0 else -1
depth, end = 0, -1
for i in range(start, len(src)) if start >= 0 else []:
    if src[i] == '{':
        depth += 1
    elif src[i] == '}':
        depth -= 1
        if depth == 0:
            end = i + 1
            break
copied = ''
if start >= 0 and end > start:
    block = src[start:end]
    copied = block.replace('"targetname" "room_trigger"', '"targetname" "proof_lever"\n"hint" "' + TRIGGER_HINT + '"')
    copied = re.sub(r'"classname" "[a-z_]+"', '"classname" "trigger_use"', copied, count=1)
    src += '\n' + copied + '\n'
map_path = os.path.join(work, 'iw8r_strprf.map')
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
check('2 text entries for the map script' in both, 'editor: ' + next((l for l in both.splitlines() if 'text entr' in l), '-')[:150])
check(copied != '' and '1 use trigger(s) show a hint' in both, 'editor: the use trigger with the key hint (' +
      next((l for l in both.splitlines() if 'show a hint' in l), 'no line')[:80] + ')')
check('3 localized string(s) of the map' in log, 'converter: ' + next((l for l in log.splitlines() if 'localized string(s) of the map' in l), '-')[:150])
gsc_path = os.path.join(work, 'iw8build', MAP_ID, 'extraction', 'extracted', 'maps', 'mp', MAP_ID + '.gsc')
gsc = open(gsc_path, encoding='utf-8', errors='replace').read() if os.path.isfile(gsc_path) else ''
check('t.hint = &"' + PREFIX + '/TRIGGER_HINT_1";' in gsc and 'p iw8r_hint_show(hint, hintid);' in gsc,
      'map script: the trigger carries its hint and the script shows it')
check('not in the map' not in both, 'editor: the script names only entries that exist')
acts = re.findall(r'ACTS --test (\S+): exit (\d+)', log)
check(len(acts) >= 3 and all(code == '0' for _, code in acts), 'ACTS: %s' % ', '.join('%s %s' % a for a in acts))

tsv_path = os.path.join(work, 'iw8build', MAP_ID, 'extraction', 'extracted', 'localize', 'strings.tsv')
tsv = open(tsv_path, encoding='utf-8').read() if os.path.isfile(tsv_path) else ''
check(tsv == PREFIX + '/HINT_GATE\t' + HINT + '\n' + PREFIX + '/TITLE\t' + TITLE_LINES[0] + chr(92) + 'n' + TITLE_LINES[1] + '\n' +
      PREFIX + '/TRIGGER_HINT_1\t' + TRIGGER_HINT + '\n',
      'extraction: localize/strings.tsv (%d bytes)' % len(tsv))

# The built eng_ zone: every asset ACTS can export from it, searched for the names and the texts.
eff = os.path.join(package, 'eng_' + MAP_ID + '.ff') if package else ''
if eff and os.path.isfile(eff):
    acts_out = os.path.join(work, 'acts_eng')
    subprocess.run([ACTS, '--noUpdater', 'fastfile', '-r', 'mw19replay', '-g', REPLAY, '--oodle', OODLE, '-l', '-o', acts_out, eff],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    blob = b''
    for base, _dirs, files in os.walk(acts_out):
        for f in files:
            p = os.path.join(base, f)
            if os.path.getsize(p) < 8 * 1024 * 1024:
                blob += open(p, 'rb').read() + b'\n'
    raw = open(eff, 'rb').read()
    low = blob.lower()
    for name in (PREFIX + '/HINT_GATE', PREFIX + '/TITLE'):
        check(name.lower().encode() in low or name.lower().replace('/', '\\/').encode() in low, 'eng zone read back: the entry ' + name)
    check(b'open the proof gate' in blob, 'eng zone read back: the hint text')
    check(b'use the proof lever' in blob, "eng zone read back: the use trigger's hint")
    check(TITLE_LINES[0].encode() in blob and TITLE_LINES[1].encode() in blob, 'eng zone read back: both lines of the title')
    print('     (eng zone %d bytes, ACTS export %d bytes)' % (len(raw), len(blob)))
else:
    check(False, 'built zone ' + eff)

if package and os.path.basename(package.rstrip('/\\')) == MAP_ID:
    shutil.rmtree(package, ignore_errors=True)
print('RESULT %s (%d failure(s))' % ('ok' if not failures else 'fail', failures))
sys.exit(1 if failures else 0)
