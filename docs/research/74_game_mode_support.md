# 74. Why custom maps only accept Team Deathmatch in the Replay lobby

> **Correction (2026-11-11, owner):** the proxy DLL is not installed. The game runs from a recompiled exe
> (game_dx12_ship_replay_dw.exe / _local_dw.exe) that has custom-map support built in. Everything below that cites the
> proxy source describes logic that may or may not have been carried into that exe; none of it is confirmed for it.


Read-only research. No file outside this document was changed. Abbreviations: `[PX]` = `E:\mw124\mw120rproxy\mw120rproxy\`, `[RPL]` = `E:\IW8\Builds\1.20-replay\`, `[X64]` = `E:\mw124\x64-zt\src\`.

## Short answer

1. The "does not support this game mode" text is not in the proxy source, the x64-zt source, or the 1.20 exe (exact-string search, ASCII and UTF-16, found 0 hits). Its origin is NOT FOUND. The likely check is the lobby's map-row filter, because the proxy writes a fixed `war` into every custom row's mode fields (`[PX]custom_map_ui.cpp:65-66`). This link is INFERRED.
2. A custom map's supported modes are not read from anything the owner's map supplies. `map.json` has no gametype field. The proxy hard-codes `war` in the cloned map row (`+0xB0` and `+0xD0`, `[PX]custom_map_ui.cpp:65-66`) and its own test asserts that (`[PX]tests\custom_map_tests.cpp:1638`). No stock mapTable or gametype table was found in the repos.
3. Other modes need (a) a `gametypes` list in the editor's map.json, which the converter must also pass through (the converter rewrites map.json with a fixed key set), (b) a proxy change in `custommapui` and `custommaps` to read that list and write each row's mode fields instead of `war` (about 20 to 40 lines plus a test), and (c) converter and editor changes so the built map carries the objects each mode needs.
4. The built `.ents` has only TDM and DM-style spawns and no objects for Domination, Search and Destroy, or Hardpoint. Other modes would need those added in the map.
5. Not determined: where the exact message is printed, what the row fields at `+0xB0` and `+0xD0` mean to the game, and whether the game would run Domination, S&D or Free-for-all on the current build.

## 1. Where the message comes from

- Proxy source: no match for "game mode", "gametype" with a support check, or "does not support this game mode". Hits for "does not support" in `[PX]` are unrelated (collision contents, MinHook, numpy vendor code). The proxy never writes or reads a mode-support result.
- Replay exe `E:\IW8\Builds\1.20-replay\game_dx12_ship_replay.exe`: `grep -a` for "does not support this" returned 0 in ASCII and 0 in UTF-16LE. The exe has related but different strings, for example `This game mode does not support more than one objective location` and `%s is not supported in this game mode` (script runtime, not the lobby). These are not the lobby message.
- x64-zt (`E:\mw124\x64-zt\src`): the message is not there. Searches for `mapTable`, `maptable`, `gameTypesTable`, `supportedGameTypes` returned nothing. `mp/mapinfo` and `mp/mapTable.csv` are not in the converter.
- Research docs (`E:\IW8-Radiant\docs\research`): no note records a stock mapTable or the mode-support check. Doc `47_quick_match_live_atmosphere.md:77-78` and `:301` say only `war` is evidenced for `ui_gametype`.
- The owner's ROADMAP F.11 (`E:\IW8-Radiant\docs\ROADMAP.md:94`) says the check is in the proxy's custom-map entry. That agrees with the row evidence below, but the proxy source does not contain the check itself. Treat the ROADMAP line as a hypothesis.

Conclusion for question 1: the exact condition is NOT FOUND. The only mechanism in our code that could make the game reject other modes is the row data the proxy injects. That data always says `war`.

## 2. Where a custom map's supported modes are decided

Evidence in the proxy (`[PX]`):

- `custom_map_ui.cpp:16` stride `0x4D8`, capacity 128. `:18` list count at RVA `0xC4FD8B4`, rows at `0xC4FD8B8`.
- `custom_map_ui.cpp:37-43` clones the `mp_shipment` row as a template.
- `custom_map_ui.cpp:59-66` writes the custom row: title at `+0x00`, id at `+0x20`, description at `+0x30`, `mw120r/<id>` at `+0x50` and `+0x70`, then `war` at `+0xB0` (32 bytes) and `war` at `+0xD0` (1024 bytes). `:68` writes `aliens = 0` at `+0x4D0`.
- The field meanings at `+0xB0` and `+0xD0` are NOT verified. Their names (for example a mode list) are not in our docs. The proxy only writes `war` there. Doc `05_runtime_evidence_1_20.md:226` gives the same layout.
- `[PX]developer_ui.cpp:175-176`: selecting a package submits `set NSQLTTMRMP <id>` and `set MOLPOSLOMO war`. The comment at `:175` says manifests call the mode "tdm" and the game calls it "war".
- `[PX]tests\custom_map_tests.cpp:1638` asserts that the registered row's `+0xD0` equals `war` ("TDM map filter registered").

Where `map.json` fits:

- `[PX]custom_maps.cpp:296-318` reads only `id`, `title`, `name` and `description` from `map.json`. Any other key is ignored. No gametype or mode key is read.
- `[X64]convert\iw3_iw8\convert.cpp:244-296` (`validateMetadata`, `writeMetadata`) writes the final `map.json` with fixed keys: `id`, `title`, `description`, `compassRotation`, `ladderRungDistance`, `minimapScale`, `exposureAdjust`. No gametype key.
- `E:\IW8-Radiant\src\qtapp\compile_panel.cpp:910-930` (`metadataJson`) writes the editor metadata with `id`, `title`, `description`, `compassRotation`, `minimapScale`, `ladderRungDistance`, `exposureAdjust`. No gametype key. The editor passes this metadata to the converter as `--metadata` (`compile_panel.cpp:548-552`), so the converter's `writeMetadata` would drop an unknown key.
- Built output `%USERPROFILE%\AppData\Local\IW8 Radiant Suite\IW8 Radiant Suite\x64zt_output\mp_iw8r_fix1\map.json` contains only `id` and `title`.

Game table row injected by the converter for the custom map: NOT FOUND. The proxy writes the row itself at load time. The converter does not write a mapTable or gametype row (no hits in `[X64]`).

Editor-side mode choice: `E:\IW8-Radiant\src\core\devmap_plan.cpp:69` (`setGametypeCommand`) and `:73-77` (`startMatchCommands`) set `MOLPOSLOMO` to the mode the user picks (default `war`), then start with `xpartygo` or `sv_systemlinkStartMatch`. This is the editor's own way round the lobby filter: it sets the mode dvar directly and does not go through the custom row. The Quick match route has not been tested in game (doc 47).

## 3. What would have to change, and in which project

(a) Editor (`E:\IW8-Radiant`): add a `gametypes` array to `metadataJson` (`compile_panel.cpp:910-930`), for example `["war","dom","sd","dm"]`, from a worldspawn key such as `iw8r_gametypes`. Only `war` is evidenced today, so offer only that value until a test proves more (doc 47, line 301, says the same).

(b) Proxy (`E:\mw124\mw120rproxy\mw120rproxy`):
- `custom_maps.cpp:296-318`: parse an optional `gametypes` string array and store it on `custommaps::Package`.
- `custom_map_ui.cpp:65-66`: write the first supported mode at `+0xB0` and the list (or a single mode) at `+0xD0`, instead of the fixed `war`. The list format at `+0xD0` must be confirmed first (see section 5).
- `developer_ui.cpp:176`: write the chosen mode from the package, not the fixed `war`. `SelectPackage` also needs a mode choice in the browser UI.
- Update `tests\custom_map_tests.cpp:1638` and add a test for a package that lists `dom`.

(c) Converter (`E:\mw124\x64-zt`):
- `convert.cpp:244-296`: add a `gametypes` field to `MapMetadata`, `readMetadata` and `writeMetadata`, so the value survives into the package's `map.json`.
- The mode objects and spawns would come from the converter's entity path, not from the metadata. Relevant code: `iw3_fastfile.cpp:10249-10290` (`WithCompletedModeSpawns`) copies TDM spawns to DOM only when at least 8 TDM spawns exist and no DOM spawns exist. This is the IW3 route. Doc 32 (section 6) says the editor's spawn templates write `mp_dom_spawn*` and `mp_sd_spawn_attacker/defender`, but the in-game effect is UNKNOWN.

Order of work: (c) and (a) first, so the value reaches the built `map.json`; then (b); then an owner test of each mode. Changing the proxy alone does nothing, because the built package carries no mode data.

## 4. What the built map contains for other modes

Entity file: `E:\IW8-Radiant\out\agent\mp_iw8r_fix1\iw8build\mp_iw8r_fix1\extraction\extracted\maps\mp\mp_iw8r_fix1.d3dbsp.ents` (2799 bytes).

Classnames present (count):
- Spawns: `info_player_start` (1), `mp_tdm_spawn` (1), `mp_tdm_spawn_allies_start` (1), `mp_tdm_spawn_axis_start` (1), `mp_dm_spawn_start` (1), `mp_global_intermission` (1).
- Other: `worldspawn`, `script_model` (5), `script_origin` (4), `script_brushmodel` (2), `trigger_multiple` (5), `trigger_use` (1), `trigger_hurt` (1), `fx_origin` (1).

Per mode:
- TDM (`war`): has `mp_tdm_spawn` plus allies and axis starts. Only one `mp_tdm_spawn`, so the converter's 8-spawn completion (`iw3_fastfile.cpp:10249`) would not run on this map. Map has enough for the spawn classes, but a real match needs more spawns (not checked in game).
- Free-for-all (dm): there is no `mp_dm_spawn`; the file has `mp_dm_spawn_start`, which is not one of the class names the research docs list for DM (doc 13 says the editor remaps `mp_dm_spawn` to `mp_tdm_spawn`). Whether `mp_dm_spawn_start` is recognised is UNVERIFIED. The DM gate (doc 40 section 3.1, `dm.gsc:8-9`) deletes every gameobject entity, so DM needs no objects, only spawns.
- Domination (`dom`): no `mp_dom_spawn*` entries and no `script_gameobjectname dom` entity. Doc 40 (section 3.1 and row for DOM capture trigger) and doc 32 (section 6) say DOM needs `dom onslaught` triggers or flags plus `mp_dom_spawn`, `mp_dom_spawn_allies_start`, `mp_dom_spawn_axis_start`. Missing.
- Search and Destroy (`sd`): no `mp_sd_spawn_attacker` or `mp_sd_spawn_defender`, and no `bombzone` trigger (doc 40 SD row, doc 32 section 6). Missing.
- Hardpoint (`koth`): no hardpoint zone trigger (doc 40 hardpoint row). Missing.

The file has one line matching a loose search for "spawnflags", and no `script_gameobjectname` key at all.

Caveat: the `.ents` is an extraction intermediate. The zone (`.ff`) spawnList may differ (doc 26 section 3, doc 32 section 6). Not checked.

## 5. What could not be determined

- The exact text origin for "does not support this game mode". Not in the proxy, the converter, or the exe (ASCII and UTF-16). It may be in a localized string table, a zone file, or the DW client. NOT FOUND. I did not search the whole Builds folder; a recursive grep over `E:\IW8\Builds\1.20-replay` was started and stopped because it was too slow.
- The meaning of row fields `+0xB0` (32 bytes) and `+0xD0` (1024 bytes). The proxy writes `war`; the format is UNVERIFIED. Confirming it needs the Replay struct or a read of a stock Shipment row (not done).
- The stock map table and its columns. NOT FOUND in `[X64]`, the research docs, or `[PX]`.
- Whether the game runs DM, DOM, SD or KOTH on a custom package once the row allows it. Needs an owner test after the proxy and converter changes.
- Whether the zone's spawnList matches the `.ents` for spawns (not checked).
- Whether `mp_dm_spawn_start` is a recognised spawn class (UNVERIFIED).
