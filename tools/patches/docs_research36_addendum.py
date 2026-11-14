import io

# Research 36: what was built from its section 8.2 on 2026-11-14, and two checks made then.

p = 'E:/IW8-Radiant/docs/research/36_map_ui_fonts.md'
s = io.open(p, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
a = '## 9. Unverified (consolidated)'
assert s.count(a) == 1
add = '''### 8.4 Built on 2026-11-14 (items 8.2.1 and 8.2.2)

- **HUD images (8.2.1).** Pictures in `<map name>.hud` beside the map become `extracted/images/hud_<name>.dds` (core
  `hud_images`; DXT5, square, the minimap's form, because the converter's reader for the map's own images takes BC1 /
  BC2 / BC3 only). The converter copies every `hud_*` image beside the minimap and writes a 2D material of the same
  name with `writeUiMaterial` (x64-zt `1c70f6ed`). The image lands in `<map>.ff`, the material in `techsets_<map>.ff`
  (where the minimap's material is). Proof: `tools/hud_image_build_test.py`.
- **Text (8.2.2).** `<map name>.str` beside the map (REFERENCE / LANG_ENGLISH pairs, core `map_strings`) becomes
  `extracted/localize/strings.tsv`; the converter registers each entry with `iw8::localize::Register` in `eng_<map>`
  (x64-zt `a2d67783`). Names are `<MAP ID>/<REFERENCE>` in capitals. Proof: `tools/map_strings_build_test.py`.
- **How the game finds a text (VERIFIED, Replay `SEH_StringEd_GetString` at `0x1413CC2A0`).** It is an ordinary asset
  lookup: `DB_FindXAssetHeader(0x29, name, 0)` and the entry's text. So an entry in any loaded zone is found by its
  name; the map's `eng_` zone is loaded with the map.
- **The stock naming (VERIFIED on the stock script dump).** Of 906 `&"FILE/REFERENCE"` names in the stock scripts, 904
  are all capitals (`SCRIPT_PLATFORM/HINT_MELEE_TAP`, `CREDITS/ATVI_COPYRIGHT1`). The map's names follow that form.
- Not seen in game: whether a script HUD element draws the map's material, and whether `sethintstring` shows the
  map's text (docs/TEST_CHECKLIST.md has both looks).

'''.replace('\n', nl)
s = s.replace(a, add + a)
io.open(p, 'w', encoding='utf-8', newline='').write(s)
print('ok')
