import io

# Docs 2026-11-14 17:40: the first-person look as editor commands (File > First-Person Look, --viewmodel-look), its
# proof, the study of the stock shaders' mask reads, the full proof run on converter 3b2bcc93.

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

- **First-Person Look: the map shader on the weapon and hands, from the menu.** File > First-Person Look (weapon and
  hands) > Build From Shader takes the open map's shader (or asks for a shader file), builds the shared zone of the
  weapon and arm materials with the shader's filter and ink lines, and checks it with the game's loaders. Install Into
  Game and Restore In Game put it in and take it out, after a message that says the look is on every map while it is
  installed. From the command line: `IW8Radiant.exe --viewmodel-look <shader file> --out <folder>`, then
  `--install-stock <folder>\\out` and `--restore-stock global_stream_mp`. A browser cannot install or restore it.
  The toon look is already built into the editor's own folder: only Install is left. Offline proof
  `tools/viewmodel_look_build_test.py` (the editor's zone is the same, byte for byte, as the one checked by hand).
- **Shipment's popping objects: the thermal mask is ruled out.** `tools/stock_mask_reads.py` reads every stock lit
  shader of a zone and lists how it reads the image the ink lines take over: all 192 of Shipment's read it only in
  thermal view, never in normal play.
- **All offline proofs pass on the converter of today** (`3b2bcc93`), run from a copy of the editor
  (`tools/run_offline_proofs.py --editor --tests`), so the editor can be rebuilt while they run.

''')

patch('docs/USER_GUIDE.md', '''  gets the same ink lines. One cost: the rebuilt map's thermal sights look wrong (the lines use the slot of the
  thermal mask).
''', '''  gets the same ink lines. One cost: the rebuilt map's thermal sights look wrong (the lines use the slot of the
  thermal mask).
- **The weapon and hands (First-Person Look).** The weapon and hands a player holds do not belong to a map: their
  shaders are in a zone every map loads. File > First-Person Look (weapon and hands) > Build From Shader builds that
  zone with the open map's shader (or a shader file you pick): the filter and the ink lines go into the weapon and arm
  materials only, and every name stays as it is. The Console shows how many; the result is checked with the game's
  own loaders and is not installed. Install Into Game puts it in; Restore In Game takes it out. **While it is
  installed, weapons and hands have the look on every map**, also the weapons other players carry and some character
  arms, and thermal sights look wrong. Close the game before either step. Without a dialog:
  `IW8Radiant.exe --viewmodel-look <shader file> --out <folder>` builds (report in `<folder>\\viewmodel-look.txt`),
  `--install-stock <folder>\\out` installs, `--restore-stock global_stream_mp` restores.
''')

patch('docs/TEST_CHECKLIST.md', '''- [ ] Toon look on the first-person weapon and hands: the zone is built (out/agent/toon_vm/out); after it is installed,
      weapons and hands show the toon colours and ink lines on every map, and nothing else changes.''',
      '''- [ ] Toon look on the first-person weapon and hands: built and waiting in the editor. File > First-Person Look
      (weapon and hands) > Install Into Game (game closed); then weapons and hands show the toon colours and ink lines
      on every map, and nothing else changes. Restore In Game takes it out.''')

p = 'docs/PROGRESS.md'
s = read(p)
nl = '\r\n' if '\r\n' in s else '\n'
a = '  ADDED 2026-11-14 17:20.'
assert s.count(a) == 1
note = '''  ADDED 2026-11-14 17:40 (newest).
  - FIRST-PERSON LOOK is a product feature now: src/qtapp/main_window_look.cpp (buildViewmodelLook,
    buildViewmodelLookQuiet, installViewmodelLook, restoreViewmodelLook, prepareViewmodelLook), core
    stockSharedLookEditJson + kViewmodelLookZone / kViewmodelLookMaterials (stock_zone_edit), menu File > First-Person
    Look (weapon and hands), command line --viewmodel-look <shader> --out <folder> (report viewmodel-look.txt; the
    smoke screenshot is kept out of that run). Install / Restore are blocked for the browser (webCommandBlocked).
    The toon look is PRE-BUILT in <AppLocalData>\\first_person_look\\out (16,173,584 bytes, identical to
    out/agent/toon_vm/out): the owner only has to choose Install Into Game. NOT INSTALLED (it changes every map; the
    owner's step). ACTS prints "9 tested" for this zone in --test mode (41,051 loaded, 0 failed): the count is the
    export tasks, not the loaders run.
  - Proof tools/viewmodel_look_build_test.py (in tools/run_offline_proofs.py): passes.
  - tools/run_offline_proofs.py got --editor / --tests (passed on to the proofs that build through the editor): the
    full run uses out/audit_bin, so bin/release can be rebuilt meanwhile. Full run on converter 3b2bcc93: see below.
  - Shipment popping: tools/stock_mask_reads.py <techsets ff> <short work> shows all 192 stock lit shaders of
    Shipment read code image 79 only under cb7[13].x == 2 (thermal): RULED OUT. An Opus helper looks at the exe for
    the first-run pipeline-state compile (research 130).
'''.replace('\n', nl)
s = s.replace(a, note + a)
write(p, s)
print('ok')
