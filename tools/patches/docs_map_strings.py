import io

# Docs for a map's own text (ROADMAP 4.27, 2026-11-14).

ROOT = 'E:/IW8-Radiant/'


def read(p):
    return io.open(ROOT + p, encoding='utf-8', newline='').read()


def write(p, s):
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s)


def patch(p, a, b):
    s = read(p)
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == 1, (p, s.count(a), a[:70])
    write(p, s.replace(a, b))


patch('CHANGELOG.md', '''## 2026-11-14

''', '''## 2026-11-14

- **Your own text in a map script: hint strings and labels.** Compile > Map Text File opens `<map name>.str` beside
  the map (a new one starts with an example), in the two-line form of the older games:
  `REFERENCE HINT_GATE` / `LANG_ENGLISH "Press ^3[{+activate}]^7 to open"`. The build carries the entries, and the
  script uses one as `&"<MAP ID>/HINT_GATE"` (the map id in capitals), for example in `sethintstring` or `settext`.
  The build says how many entries it carried, names the mistakes in the file, and points out a name the script uses
  that is not in it. Converter x64-zt `a2d67783` (deployed). Proven offline: `tools/map_strings_build_test.py`
  builds a map with a text file and a script; the game's loaders accept all five zones and both entries, names and
  text, are read back from the map's language zone. Whether the game shows them has not been seen.

''')
patch('docs/PROGRESS.md', '''  ROADMAP 4.27 HUD images: core hud_images.{h,cpp}''', '''  ROADMAP 4.27 map text: core map_strings.{h,cpp} (parseMapStrings of "<map>.str", mapStringsTsv,
  stringRefsInScript, mapStringsTemplate), CompileOptions::stringsFile / CompileReport::mapStrings, extraction file
  extracted/localize/strings.tsv, command MapTextFile. Converter a2d67783 (worktree, deployed; old exe
  out/zt_backup/zonetool_1c70f6ed.exe): ReadMapStrings -> PreparedMap::mapStrings -> iw8::localize::Register in
  eng_<map>. Proof tools/map_strings_build_test.py (in run_offline_proofs.py: 22 proofs).
  ROADMAP 4.27 HUD images: core hud_images.{h,cpp}''')
patch('docs/PLAN_REMAINING.md', '''  - fonts and LUI (4.27): HUD from the map script works with stock icons and, since 2026-11-14, with the map's own
    pictures (`<map name>.hud`, proven offline; game).''', '''  - fonts and LUI (4.27): HUD from the map script works with stock icons and, since 2026-11-14, with the map's own
    pictures (`<map name>.hud`) and the map's own text (`<map name>.str`), both proven offline; game.''')
R = 'docs/ROADMAP.md'
s = read(R)
a = 'custom TTF fonts and LUI have no map writer in x64-zt (deferred); in-game look NOT VERIFIED'
assert s.count(a) == 1, s.count(a)
s = s.replace(a, 'custom text (2026-11-14): `<map name>.str` beside the map (REFERENCE / LANG_ENGLISH pairs) becomes localized strings `<MAP ID>/<REFERENCE>` in `eng_<map>` (core `map_strings`, x64-zt `a2d67783`, proof `tools/map_strings_build_test.py`: names and text read back); custom TTF fonts and LUI have no map writer in x64-zt (deferred); in-game look NOT VERIFIED')
write(R, s)
patch('docs/USER_GUIDE.md', '''- **Painting a blend on a brush surface.**''', '''- **Your own text in the game.** Compile > Map Text File opens `<map name>.str` beside your map. Each entry is two
  lines: `REFERENCE HINT_GATE` and `LANG_ENGLISH "Press ^3[{+activate}]^7 to open"` (`\\n` is a line break, `^1` to
  `^7` are colours, `[{+activate}]` shows the key). In the map's script the entry is `&"MP_YOURMAP/HINT_GATE"`: your
  map id in capitals, a slash, the reference, for example `trig sethintstring( &"MP_YOURMAP/HINT_GATE" );` or
  `label settext( &"MP_YOURMAP/TITLE" );`. The build tells you if the script uses a name the file does not have.
- **Painting a blend on a brush surface.**''')
patch('docs/features/FEATURES.md', '''### Keys that make things move or act''', '''- Your own text for hint strings and HUD labels: `Compile > Map Text File` opens `<map name>.str` beside the map (`REFERENCE` / `LANG_ENGLISH` pairs); the build carries the entries as `<MAP ID>/<REFERENCE>` and the map script uses them as `&"<MAP ID>/<REFERENCE>"`. The build names mistakes in the file and names the script uses that are not in it. (not yet seen in game)

### Keys that make things move or act''')
patch('docs/TEST_CHECKLIST.md', '''## In game: your map's model light (2026-11-13), not seen in game''', '''## In game (optional, 2026-11-14): your own text
- [ ] Compile > Map Text File, keep the example entry, and in the map script (worldspawn `iw8r_script`) put
  `user_main() { t = newhudelem(); t.x = 20; t.y = 100; t settext( &"MP_YOURMAP/HINT_EXAMPLE" ); }` with your map id
  in capitals. Build and play: the text is on the screen. If the name itself shows (MP_YOURMAP/HINT_EXAMPLE), the
  game did not find the entry: say so.

## In game: your map's model light (2026-11-13), not seen in game''')
patch('docs/HANDOVER.md', '''`py -3 -I tools\\run_offline_proofs.py <work dir>`: 21 end-to-end proofs; all passed on the last build of 2026-11-14.''',
      '''`py -3 -I tools\\run_offline_proofs.py <work dir>`: 22 end-to-end proofs; all passed on the last build of 2026-11-14.''')
patch('docs/HANDOVER.md', '''blended materials (Patch > Vertex Paint Mode paints it).''', '''blended materials (Patch > Vertex Paint Mode paints it). A map script can draw your own pictures on the HUD
(Compile > HUD Images Folder) and show your own text (Compile > Map Text File).''')
print('ok')
