# 93. Does a custom map load `common_br_mp` in a `br` match?

Date: 2026-11-12. Read-only study of `E:\IW8\Builds\1.20-replay\game_dx12_ship_replay_dw.exe` with its linker map
(`tools\dw_disasm.py`, plus a scratch rel32 / absolute-pointer cross-reference scanner outside the project) and of the
DW client source `E:\DW\IW8` (read only). Answers research 89 section 8 "Open" item 1 and the open line of research 87
section 6. Nothing was run in the game.

Labels: **VERIFIED** = read in the exe's code (function + address) or in a named source file / exported table.
**INFERRED** = follows from verified facts, not read or run directly.

Short answer: **no**. The engine picks the zone that holds Warzone's shared assets **by map name**, from the string
table `mp/levelZoneParents.csv`. The game type plays no part. A custom map has no row in that table, so it gets
`common_core_mp` in a `br` match like any other match. The DW client does not change this for custom maps.

---

## 1. Which code picks the extra zones

### 1.1 Game mode zones: by game mode, not game type (VERIFIED)

`Com_FastFile_RequestGameModeFastFiles` (0x1410DA010) picks zones by game mode (MP / CP / SP) and the `alt` dvar only.
For MP it requests `global_stream_mp` + `global_mp`, `global_core_mp`, `common_stream_mp` + `common_mp` and
`common_base_mp`, or their `*_alt_mp` names (0x1410DA0FC-0x1410DA1DF). It reads no game type and names no `common_br_mp`.
(Flags per module: research 91 section 1.1.)

### 1.2 Level zone: the map and its voice zone only (VERIFIED)

`DB_DecideLevelLoad` (0x140F81570) calls only `BT_Feature`, `Com_IsCpMap`, `Content_FormatCommonVoMtxName` and
`Core_strcopy`: the map zone (flags 0x900) and an optional VO/MTX zone. No game type. (The DW dedicated server checks
the same thing: it requires `decision.count == 1` and the map name, `integration\dedicated\ReplayDedicatedLevel.cpp:134-138`.)

### 1.3 The "parent" zone: chosen by map name from `mp/levelZoneParents.csv` (VERIFIED)

- `Com_FastFile_Init` (0x1410D8D50) loads the table once at start-up: `lea rcx, "mp/levelZoneParents.csv"` /
  `lea rdx, g_Com_FastFile_levelZoneParentsTable` / `call` the string table getter (0x1410D8DE9-0x1410D8DF7).
  Cross-reference scan: the global 0x14C557E10 is used only there and by the two functions below.
- `Com_FastFile_RequestParentMpFastfile(module, mapName)` (0x1410DB2F0):
  `StringTable_Lookup(levelZoneParents, column 0, mapName, column 1)` (0x1410DB300-0x1410DB30F). If the result is
  null or empty, it falls back to `common_core_mp`, or `common_core_alt_mp` when the `fastfileAltLaunch` dvar is set
  (0x1410DB321-0x1410DB33A). Unless `Online_DcQos_IsEngaged`, it calls
  `ComFastFileLoadModule_SetRequestedZone(module, parent, 0x8200000)` (0x1410DB347-0x1410DB353).
- `ComFastFileLoadModule_SetRequestedZone` (0x1410DC370) **clears** the module's zone slots (three 0x48-byte slots at
  module + 0x110) and writes the one new name (0x1410DC402-0x1410DC43D). So a module holds `common_core_mp` **or**
  the parent from the table, never both.
- Callers, all with game mode 2 (MP) only, the module record 0x14C557A08 (computed from the `lea`; the tool's
  `gl_14C4EBA08` label is shifted), and a map name:
  - `Com_FastFile_UpdatePauseAndLevelRequests` (0x1410DABE0): map name = dvar `ui_mapname` (0x1410DACA1-0x1410DACB3).
  - `Com_FastFile_UpdateLevelPreloadRequest_SyncGameLoad` (0x1410DAEC0): its first argument (0x1410DAF3A-0x1410DAF44),
    reached from `Com_FastFile_BeginSyncLoadRequests` (0x1410DA940). That the argument is the map name is INFERRED
    (same function, same table, column 0 holds map names).
- Record 0x14C557A08 has zone flags **0x20** (research 91 section 1.1); the DW dedicated server loads `common_core_mp`
  with flags 32 for the same reason (`ReplayDedicatedLevel.cpp:47`, comment "0x10b52f0 (level parent)").
- `LUI_CoD_GetCommonCoreName` (0x1410D8D00) is the same lookup without the request. The techsets-file filter at
  0x140EE9940-0x140EE9B11 (unnamed, in the `Online_DcQos` area) uses it, so the shader preload follows the same map
  rule (INFERRED for its purpose; the call is read).
- The only code-reachable copy of the string `common_br_mp` in the exe is at 0x142450EC8, in the pointer pair
  {`common_core_mp`, `common_br_mp`} at 0x142450EA0. No rel32 or absolute reference to that pair or that string was
  found by the scan; the string `common_core_mp` (0x142450EB8) is used only by the two functions above. So the exe
  never names `common_br_mp` in code: it comes from the table.

### 1.4 The table (VERIFIED, ACTS export of `code_post_gfx`)

`out\warzone_study\code_post_gfx\mw19replay\code_post_gfx\assets\stringtable\mp\levelzoneparents.csv.443.csv`, all 12 rows:

| map (column 0) | parent (column 1) | map zone installed in `zone\` |
| --- | --- | --- |
| `mp_donetsk`, `mp_br_tut`, `mp_br_quarry`, `mp_bm_tut` | `common_br_mp` | yes |
| `mp_br_mechanics`, `mp_br_sector`, `mp_br_gaspocket`, `mp_br_money`, `mp_kstenod`, `mp_donetsk2`, `mp_bm_bunker01` | `common_br_mp` | no |
| `mp_lc_br_tut` | `common_br_alt_mp` (not installed) | no |

Every other map, every custom map included, falls back to `common_core_mp`.

### 1.5 Game type: not used for zones (VERIFIED for the functions above)

None of the functions in 1.1-1.3 reads `g_gametype` / `ui_gametype`. The game-type format strings in the exe
(`%s/gametype_%s%s` in `G_ScrMP_AddGametypeScriptRefs`, `%s/bots_gametype_%s%s` in `GScr_MainMP_LoadScripts_shared`)
name scripts, not zones. **A `br` match on Shipment or on a custom map loads `common_core_mp`, not `common_br_mp`;
Verdansk loads `common_br_mp` in any game type** (INFERRED from the code path; not run).

## 2. The DW client (VERIFIED, `E:\DW\IW8`)

- No source file in `integration`, `src`, `launcher` or `tools` names `levelZoneParents`, `common_br` or the parent
  request, except the dedicated server, which **refuses** any map whose parent is not `common_core_mp`
  ("This level qualification requires a different native parent archive", `ReplayDedicatedLevel.cpp:106-111`).
- The exe's `StringTable_Lookup` (0x141421BA0) is the DW replacement (`DW_MapAvailability.cpp:725-750`; it calls
  `DW_CustomOperators_TableView` at 0x141421BB8). It answers custom-map columns only for `mp/mapInfo.csv`
  (`CustomMapInfo`, `DW_MapAvailability.cpp:788-811`), and `DW_CustomOperators_TableView` changes only the four
  `mp/cac/*heads/bodies.csv` tables. `mp/levelZoneParents.csv` gets the plain native row search.
- Custom maps (`ScanCustomMaps`, `DW_MapAvailability.cpp:439-523`): the id is the folder name, `mp_` + `[a-z0-9_]`, at
  most 15 characters (`MapId`, lines 176-188); an installed stock zone of the same name wins (line 468); the package's
  five zones are routed by file name (`CustomZonePath`). Nothing adds zones for the game type.

## 3. What a custom map needs for `common_br_mp` to load

- The game type alone is not enough (1.5). No mapinfo column and no map.json key is read for this (1.3, 2).
- A map zone cannot add a row: the table pointer is taken once in `Com_FastFile_Init`, before any map zone, and the
  parent request runs before the level zone loads (INFERRED from the call order; a map-zone copy of the table would
  also be discarded as a duplicate, research 91 section 1.2).
- The only route without changing the client: **give the package the id of an uninstalled row of the table**
  (`mp_br_sector`, `mp_br_money`, `mp_kstenod`, `mp_donetsk2`, `mp_bm_bunker01`, `mp_br_mechanics`,
  `mp_br_gaspocket`; not `mp_lc_br_tut`, whose parent is not installed). The native lookup then returns
  `common_br_mp` (INFERRED from the read code; not run). Costs:
  - `common_core_mp` is then **not** loaded (1.3, `SetRequestedZone` replaces). Of its 24 scriptable definitions only
    `ks_airdrop_chute_mp` and `ks_manual_turret_mp` are also in `common_br_mp` (ACTS exports in
    `out\scriptable_study\common_core_mp` and `...\common_br_mp`); `hardpoint_chevron` and the killstreak scriptables
    would be missing. Other `common_core_mp` assets a package references were not compared.
  - Other per-map lookups keyed by these stock names (`mp/mapInfo.csv` has a row for `mp_br_mechanics`; transient
    zone lists, BR tables) were not checked.
  - The DW dedicated server rejects such a map (section 2).
- The clean route is a client change: a custom-map override of the parent lookup (for example a map.json key read in
  `ScanCustomMaps` and answered for `mp/levelZoneParents.csv` in the DW `StringTable_Lookup`). That is DW client work,
  not editor work.

## 4. What this means for the editor

- Today a Warzone object placed **by name** from `common_br_mp` (`br_plunder_box`, `brloot_rock`, the ammo boxes, the
  tablet; research 89) does **not** resolve on a custom map in a `br` match: the map gets `common_core_mp`. The
  converter's warning for extraction-site assets that live only in `common_br_mp` holds for every custom map.
- Options, best first:
  1. **Carry** those definitions (and the models / effects / materials they need) into the map zone, as the editor
     already does for stock-map definitions. With `common_br_mp` not loaded, the names are new and take the normal
     add path (research 91 section 1.3). INFERRED; check zone size and that every referenced asset is carried.
  2. Ask for the DW client override (section 3, last point); then by-name placement works and nothing is carried.
  3. Offer the uninstalled BR ids as map names only with the costs above stated (no `common_core_mp`); not recommended.
- The rule "worldspawn `iw8r_br` or a `br_prematch_insertion_point` means `common_br_mp` is loaded" in research 89 is
  wrong for custom map ids; the condition is the map's id being a `common_br_mp` row of `mp/levelZoneParents.csv`.
- Unchanged: on stock BR maps edited in place (`mp_donetsk`, `mp_br_quarry`, `mp_br_tut`, `mp_bm_tut`)
  `common_br_mp` is loaded in every game type, so by-name references there are fine.

## Built on this (2026-11-12)

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

