import io


def load(p):
    s = io.open(p, encoding='utf-8', newline='').read()
    return s, ('\r\n' if '\r\n' in s else '\n')


def rep(p, pairs):
    s, nl = load(p)
    for a, b in pairs:
        a = a.replace('\n', nl); b = b.replace('\n', nl)
        assert s.count(a) == 1, (p, s.count(a), a[:80])
        s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


s, nl = load('docs/research/93_br_zone_loading.md')
s = s.rstrip() + nl + nl + '''## Built on this (2026-11-12)

- Buy stations and the other `common_br_mp` scriptables placed on a custom Warzone map are **carried** from
  `common_br_mp` into the map zone (Real stock scripted objects), not placed by name.
- Loot items: a map with loot points carries the `brloot_*` definitions of its loot table (worldspawn mode dvar
  `loot_table_name`, default `mp/loot_item_defs.csv`) from `common_br_mp`. The item lists of the 13 stock tables are in
  `data/asset_catalog/iw8_br_loot_items.tsv` (`tools/build_br_loot_items.py` from the `code_post_gfx` export); items the
  zone does not hold (6 in three old tables) are left out with a count.
- Converter (x64-zt 43d1d757): scriptable definitions can be carried without placed instances (the zone write refused
  a carried definition when the map had no scriptable instance).
- Proof: `tools/br_loot_items_carry_test.py` (the `_s3` table, 142 items: 530 assets into the map zone and 89 into the
  techsets zone, map zone 35 MB, ACTS 0 failed, all 142 definitions in the zone); an end-to-end build of the Warzone
  test map with four loot points and `loot_table_name=mp/loot_item_defs_s3.csv` through `IW8Radiant.exe --compile`
  (same numbers, ACTS 0 failed); core test `compile_iw8_loot_points`. Four materials the cash items name
  (`mo/mtl_lm_cash_stack_01`, the `lm_accessory_don_currency_01_a` pair, `mo/offhand_vm_stim_body`) are not carried
  (the converter warns). Whether loot then appears in a match is not seen in game.
''' + nl
io.open('docs/research/93_br_zone_loading.md', 'w', encoding='utf-8', newline='').write(s)

rep('docs/research/89_warzone_work.md', [
 ('''buy stations are carried now; the loot item
   definitions (`brloot_*`, 240 in that zone) the loot tables spawn on loot points are not, so an own map's loot
   points may give no items; two ATV tyre textures are missing.''', '''buy stations and the loot item definitions of
   the map's loot table are carried now (research 93, "Built on this"); two ATV tyre textures are missing.'''),
])

rep('docs/features/FEATURES.md', [
 ('''Loot items on an own map: the loot item definitions live in the Warzone zone, which a custom map does not load (research 93), and are not copied yet.''',
  '''Loot items on an own map are carried from the Warzone zone (research 93) but not yet seen in game.'''),
 ('''- Several loot areas on your own map:''', '''- Loot items on your own map: a custom map never loads the game's Warzone zone (research 93), so the build copies the
  loot item definitions of the map's loot table (mode dvar `loot_table_name`, default `mp/loot_item_defs.csv`; the
  Trios table has 142 items, about 35 MB) into the map. (tested offline; not yet seen in game)
- Several loot areas on your own map:'''),
])

rep('CHANGELOG.md', [
 ('''- **Fix: "no circle" no longer breaks a Warzone map.**''', '''- **Loot items on own maps.** A map with loot points carries the loot item definitions of its loot table from the
  Warzone zone, which a custom map never loads (research 93). Built and ACTS-checked offline; not seen in game.
- **Fix: "no circle" no longer breaks a Warzone map.**'''),
])

p = 'docs/ROADMAP.md'
s, nl = load(p)
lines = s.split(nl)
for i, line in enumerate(lines):
    if line.startswith('| 13.4 |'):
        a = 'ACTS-checked offline)'
        assert a in line
        lines[i] = line.replace(a, 'ACTS-checked offline; loot points carry the loot item definitions of the map\'s loot table, x64-zt 43d1d757)', 1)
io.open(p, 'w', encoding='utf-8', newline='').write(nl.join(lines))
print('ok')
