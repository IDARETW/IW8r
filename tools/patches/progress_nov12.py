import io

p = 'docs/PROGRESS.md'
s = io.open(p, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
head = '# Progress note (handoff)' + nl + nl
assert s.startswith(head)
add = '''## Resume here (2026-11-12, afternoon)

- Owner goal: finished product (stock map editing, Warzone editing, map and mode creation); no game tests unless
  absolutely needed. Commit and push to IDARETW/IW8r as work lands (identity "IW8 Radiant <iw8radiant@localhost>",
  no co-author trailer). Converter work in C:\\zsw-iw8r\\scriptables (branch iw8radiant/scriptables), live copy
  E:\\mw124\\x64-zt fast-forwarded and rebuilt after each converter commit (old exe in out/zt_backup). Live is 43d1d757.
- Read from the game exe today (docs/research): 91 (game type scripts: a map zone cannot replace a stock one, a new
  name can be started, the map script runs after the game type main), 92 (GSC far calls link at load by exact name;
  numbered stock files link, named ones need matching name forms; a failed link drops the match or exits the game),
  93 (a custom map never loads common_br_mp: the extra zone follows the map name in mp/levelZoneParents.csv).
- Built today (all offline-proven, none seen in game): loot clusters and zones on stock and own maps
  (iw8r_loot_cluster / iw8r_loot_zone; tools/warzone_loot_boxes_test.py, warzone_custom_loot_boxes_test.py); mode rules
  (worldspawn iw8r_mode_rules: health, speed, one weapon, infinite ammo); Warzone definitions and the loot item
  definitions of the map's loot table carried from common_br_mp into own maps (tools/br_loot_items_carry_test.py);
  the "no circle" switch no longer writes the unlinkable br_gametypes::disablefeature call; Bridge Two Faces; Blend By
  Slope; terrain blends of stock maps textured (material cache map zone fallback); the feature list pictures redone
  (IW8Radiant.exe --feature-shots); preflight no longer flags methods called on self.
- Tests at the last commit: core 139,719 checks / 0, GUI 935 / 0.
- Open (in scope): 13.6 a new game type script under a new name (research 92 says calls into numbered stock scripts
  link; needs a second script file in the map zone and a far-call preflight); removing a placed object on a stock map
  (indices in dynamic entities and the spatial tree); 11.5 removing stock world geometry; 11.6 sun and sky relight.
  Prefabs: zip import / export of bundles (map_package extractZip handles stored entries only; stb has an inflater),
  convert a plain .map to a bundle, placement hints (manifest surface and yaw_step are parsed, unused).
- Disk: E: had 3.5 GB free; this session's scratch output under out/ was deleted (owner copies kept).

'''.replace('\n', nl)
s = head + add + s[len(head):]
io.open(p, 'w', encoding='utf-8', newline='').write(s)
print('ok')
