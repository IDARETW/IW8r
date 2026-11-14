import io

# Docs for research 116: edited stock maps install as whole unsigned zones, not as patch files.

ROOT = 'E:/IW8-Radiant/'


def patch(p, a, b):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == 1, (p, s.count(a), a[:70])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


patch('CHANGELOG.md', '''## 2026-11-13

''', '''## 2026-11-13

- **Fixed: an installed stock edit stopped the game ("Disc read error [3.1]: '<map>.fp'").** The game reads a patch
  file of one of its signed zones only when the patch carries the game's own signature, which nobody else can make;
  every stock edit was installed as such a patch. File > Install Edited Stock Map Into Game now installs the edited
  zones as whole files, which the game loads unsigned: the stock zone file (and its stock patch) is moved to
  `iw8r_stock` in the game folder and File > Restore Stock Map In Game moves it back. An old patch file of this
  editor in the zone folder is removed by the install. With no rebuild in the session the install can take the folder
  of an earlier rebuild. While an edit is installed, the editor and the rebuild read the stock map from the kept file.
  Found in the game's code, checked offline; not seen in game (research 116).

''')
patch('docs/PROGRESS.md', '''## Latest (2026-11-13)

''', '''## Latest (2026-11-13)

- Owner (2026-11-13 night): the Shipment toon patch crashed the game in the lobby, "Disc read error [3.1]:
  'techsets_mp_shipment.fp'" (the owner had copied the .fp into zone\\ by hand; no editor markers). Research 116 (IDA,
  game_dx12_ship_replay.exe): error 3 = DB_AsyncIWFileLoad_PumpDecodeQueue's result; a patch's signed flag comes from
  its base header (DB_ReadFastfileHeaderData checks target then base), which must equal the signed stock zone header,
  so its data must start with an RSA-signed IWffs100 block. No unsigned patch of a stock zone can load. The owner's
  client (E:/DW/IW8 integration/replay/DB_ResetFileReader.cpp) does load unsigned IWffc100 zones under any name.
  Change: core stock_install installStockZones / restoreStockZones / installedStockZones /
  installedStockZoneOverrides / unsignedReplayZone / unsignedPatchOfSignedZone (old patch functions kept for
  cleanup); editor installEditedStockZones (folder dialog when no rebuild in the session), stockKeepDir =
  <game>/iw8r_stock, stockZoneForReading reads the kept zone, stockEditJson + "stock_zones" + "patch": false.
  Converter x64-zt 5bb98fb9 (worktree, deployed, old exe out/zt_backup/zonetool_14bff0d5.exe): xfile::use_zone_file,
  editzone "stock_zones". Proof: core tests; editzone with stock_zones writes the same Shipment toon zone byte for
  byte (out/agent/toon_ship/edit2 vs edit). Deliverable for the owner: install the folder out/agent/toon_ship/edit
  (File > Install Edited Stock Map Into Game, choose the folder). NOT seen in game: an unsigned zone under a stock
  map's zone name. tools/patches/zt_stock_zones.py, stock_zone_install.py, stock_install_stray_patch.py.
''')
patch('docs/USER_GUIDE.md', '''  Map Into Game installs the patch files as for any stock edit. The filter cannot use textures or the edge inputs,
  and it does not reach characters, weapons or effects.''', '''  Map Into Game installs the changed zone as for any stock edit. The filter cannot use textures or the edge inputs,
  and it does not reach characters, weapons or effects.''')
patch('docs/USER_GUIDE.md', '''5. File > Install Edited Stock Map Into Game puts the edit into the game. It copies small patch files (`<map>.fp`)
   beside the stock zones after showing you the list; the stock zones themselves are not changed, and a stock
   patch of the same name is saved first. Close the game before installing. File > Restore Stock Map In Game
   takes the edit out again.
''', '''5. File > Install Edited Stock Map Into Game puts the edit into the game. After showing you the list, it moves the
   stock zone files of the map (`<map>.ff`, `techsets_<map>.ff` and a stock patch `<map>.fp`) to the folder
   `iw8r_stock` in the game folder and copies the edited zone files into the `zone` folder under the same names.
   Close the game before installing. File > Restore Stock Map In Game deletes the edited files and moves the stock
   files back; do not delete or rename the files in `iw8r_stock` yourself. The game cannot take a stock edit as a
   small patch file: it reads a patch of its own zones only with its own signature (research 116). If no map was
   rebuilt in this session, the install asks for a folder: choose the `out` folder of an earlier rebuild.
''')
patch('docs/features/FEATURES.md', '''- The rebuild also writes small patch files (`<map>.fp`) instead of a new zone. The game applies a patch beside the stock zone of the same name, so the stock zone is never replaced. (not yet seen in game)
''', '''- The rebuild writes whole zone files. The game reads a patch file of its own signed zones only with its own signature, so an edit cannot go in as a small patch (research 116). (tested offline)
''')
patch('docs/features/FEATURES.md', '''- The game ships its own patch files with real map data. Every stock edit starts from the stock zone with its stock
  patch applied, also while an earlier edit of the same map is installed (the saved stock patch is used then). (tested
  offline)
- File > Install Edited Stock Map Into Game copies the patch files after a confirmation that lists them. A stock patch of the same name is saved first. Close the game first. File > Restore Stock Map In Game takes the edit out again. Only the owner runs these. (not yet seen in game)
''', '''- The game ships its own patch files with real map data. Every stock edit starts from the stock zone with its stock
  patch applied, also while an earlier edit of the same map is installed (the stock files kept at install are used
  then). (tested offline)
- File > Install Edited Stock Map Into Game installs the edited zone files after a confirmation that lists them. The stock zone files of the same name, and their stock patch, are moved to `iw8r_stock` in the game folder first. Close the game first. File > Restore Stock Map In Game deletes the edit and moves the stock files back. Only the owner runs these. (not yet seen in game)
''')
patch('docs/TEST_CHECKLIST.md', '''- [ ] Close the game. File > Install Edited Stock Map Into Game: the dialog lists `mp_shipment.fp` (and
      `techsets_mp_shipment.fp`). Confirm.
''', '''- [ ] Close the game. File > Install Edited Stock Map Into Game: the dialog lists `mp_shipment.ff` (and
      `techsets_mp_shipment.ff`) and the folder `iw8r_stock` the stock files move to. Confirm.
''')
patch('docs/TEST_CHECKLIST.md', '''Patch: `out/agent/toon_ship/edit/patch/techsets_mp_shipment.fp` (or rebuild it: open Shipment, Tools > Shader Editor,
toon, Use for This Map, File > Rebuild Stock Zone, File > Install Edited Stock Map Into Game). Game closed while
installing; File > Restore Stock Map In Game (or deleting the copied `.fp`) takes it out again.

''', '''The first try (a patch file `techsets_mp_shipment.fp` copied into `zone`) stopped the game with "Disc read error
[3.1]": the game takes no unsigned patch of a stock zone (research 116). Delete that `.fp` from the `zone` folder if
it is still there (the install below also removes it).

Install, game closed: start the editor again (it was rebuilt), File > Install Edited Stock Map Into Game, answer Yes
to "choose a folder", pick `E:\\IW8-Radiant\\out\\agent\\toon_ship\\edit`. The dialog lists
`techsets_mp_shipment.ff`; confirm. The stock file moves to `iw8r_stock` in the game folder. To take it out: open
Shipment in the editor (File > Open IW8 Stock Map), then File > Restore Stock Map In Game.
(Or rebuild it yourself first: open Shipment, Tools > Shader Editor, toon, Use for This Map, File > Rebuild Stock
Zone, then install.)

''')
patch('docs/research/README.md', '''| 115 | [Stock lit shader filter](115_stock_lit_shader_filter.md) | A custom shader on a stock map: a colour filter spliced into the game's compiled lit pixel shaders (DXBC splice, checksum, editzone lit_filter; Shipment) |
''', '''| 115 | [Stock lit shader filter](115_stock_lit_shader_filter.md) | A custom shader on a stock map: a colour filter spliced into the game's compiled lit pixel shaders (DXBC splice, checksum, editzone lit_filter; Shipment) |
| 116 | [Stock zone install](116_stock_zone_install.md) | Why a patch of a stock zone stops the game ("Disc read error [3.1]": signed zones need signed patches) and the install as whole unsigned zones (iw8r_stock, editzone stock_zones) |
''')
patch('docs/research/115_stock_lit_shader_filter.md', '''edited zone is 2.1 MB (stock 1.7 MB); its patch `techsets_mp_shipment.fp` is 140 KB.
''', '''edited zone is 2.1 MB (stock 1.7 MB); its patch `techsets_mp_shipment.fp` is 140 KB. **The patch does not load in
the game** (research 116: a patch of a signed zone needs the game's signature); the whole edited zone is installed
instead.
''')
s = io.open(ROOT + 'docs/research/88_stock_map_editing.md', encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
i = s.index(nl) + len(nl)
note = (nl + '> **2026-11-13:** the patch files (`<zone>.fp`) this note installs do not load in the game on its signed stock' + nl +
        '> zones. Edits are installed as whole unsigned zones now: [research 116](116_stock_zone_install.md).' + nl)
if '116_stock_zone_install.md' not in s:
    s = s[:i] + note + s[i:]
    io.open(ROOT + 'docs/research/88_stock_map_editing.md', 'w', encoding='utf-8', newline='').write(s)
print('ok')
