# 45. Nav, water and doors: evidence for roadmap 6.23 and 6.24

Date: 2026-11-10. Read-only research. Nothing was built, converted, run or loaded into ACTS.

Roots used in citations:

- `X:` = `E:\mw124\x64-zt\src` (the x64-zt fork, read only).
- `ED:` = `E:\IW8-Radiant` (the editor).
- Stock: `%USERPROFILE%\AppData\Local\IW8 Radiant Suite\IW8 Radiant Suite\stock_cache\mp_shipment\...` (only mp_shipment is cached there). The mp_backlot2 numbers come from `ED:\data\stock_dumps\mp_backlot2` (same entity format, decoded the same way).
- Stock decode: `scratchpad\decode\ents.py` (read-only parse of `assets/map_ents/.../*.asset.json` hex entity string; key names from `ED:\data\generated\iw8_canonical_keys.tsv`). Decoded output: `scratchpad\decode\ship.json`, `scratchpad\decode\backlot.json`. The decoded counts match the 611 and 3234 entity totals in `ED:\docs\research\10_iw8_entity_inventory.md`.

## Summary

- **Nav:** x64-zt builds the navmesh (type 27), PathData (aipaths, 26) and tacgraph (28) from converted collision on every build unless `navigation=0`. Nothing is written to disk by default. The only intermediate file is a diagnostic JSON behind the env var `IW8_ZONETOOL_NAV_DEBUG`; it has areas, nodes, links and top heights and is usable for a camera overlay now. The editor already passes `node_pathnode`, `node_negotiation_begin` and `node_negotiation_end` through, and the converter reads them.
- **Blockers:** there is no nav-only volume, modifier or cost concept in the converter. Movement blocking comes from contents bits in the player trace mask `0x2014011`; sight blocking comes from solid bit `1`. A brush cannot block bots without also blocking players. Replay's data model does have nav modifiers and obstacle bounds, but x64-zt never emits them.
- **Traversal:** `node_negotiation_begin` (animscript + target) paired with `node_negotiation_end` (targetname). Stock uses only `jump_up_40/80`, `jump_down_40/80/136/160`, `jump_across_100` and `ladder_up`. Pairing is 1:1 in both stock maps.
- **Water:** the editor's water volume is a drawn, non-colliding brush with no contents bit. The converter has no water volume, no water asset and no swim state. Replay does name `physicscontents_water` in its trace masks, but the bit value is UNVERIFIED.
- **Doors:** stock maps have no door classname. Stock doors are `script_brushmodel` or animated `script_model` entities wired by `trigger_use_touch` and `target`. Opening them is script behaviour that is not in the stock cache. The fork has no door logic.
- **Mantle:** mantles are computed from collision at runtime. x64-zt writes no mantle hint, mantle entity or edge-mantle data. A hint brush produces nothing.

---

## 1. Navigation

### 1.1 What the converter builds

- The build call runs asynchronously beside the render load when `prepared && args.navigation`: `X:convert/iw3_iw8/convert.cpp:1542-1544`. The default is on (`convert.cpp:85`), and the option is `navigation=0` (`convert.cpp:2665`).
- The source is gathered from world collision and markers in `X:convert/iw3_iw8/iw3_fastfile.cpp:12825-12950`. Hulls are kept when `hull.model == 0` and their contents pass `playerMask = 0x2014011` (`:12829-12841`). Triangles come from the world mesh (`:12854-12863`) and from static models with a collision LOD, which always get `kBlocksMovement | kBlocksSight` (`:12877-12888`).
- `Build` is `X:games/iw8/common/replay_navigation.cpp:1635`. It returns an empty result with a warning when there are no spawns or pathnodes (`:1638-1644`) or no blocking collision (`:1649-1653`). So a map with no spawns and no `node_pathnode` gets no navigation at all.
- Outputs are registered in `Register` (`replay_navigation.cpp:2621-2637`):
  - aipaths (PathData) with per-node animscripts (`:2631`);
  - navmesh (NavMeshData): `EmitNavMesh` writes only the nav graph blob. Its root is a zero-filled `0xA8` block with one nav resource (`:2519-2541`). Obstacle, glass, modifier and link-creation counts are left at zero;
  - tacgraph (`:2636`).
- Origin labels in the asset ledger say "bot navigation generated from the converted collision" (`X:convert/iw3_iw8/convert.cpp:1432-1434`).
- Fixed pathnodes become `node_pathnode` MapEnts records appended to the entity string (`convert.cpp:1689-1706`, origins from `FixedPathNodes` at `replay_navigation.cpp:2368-2377`). Replay spawns fixed nodes only from these records (comment at `replay_navigation.h:68-71`). The cap is `kMaxFixedNodes = 3000` (`replay_navigation.cpp:107`, enforced at `:1858-1859`).
- Floors are seeded from spawns within 96 horizontal and 40 vertical units and from pathnodes within 72 and 36 (`replay_navigation.cpp:1679`, `:1693`). Seeds that do not land on walkable floor are counted and dropped.
- Constants that shape the mesh (cell 8, agent radius 15, step 18, height 72, floor normal 0.7, link distance 256) are at `replay_navigation.cpp:94-119`. The header comment says they are shipped mp_shipment values.

### 1.2 Intermediate files the editor could read

- **Default:** none. `Build` keeps everything in memory. The converter log lines (`replay_navigation.cpp:2297-2306`, "navigation: N fixed pathnodes ... zones") are the only default output.
- **Debug JSON (exists today):** if the environment variable `IW8_ZONETOOL_NAV_DEBUG` names a path, `Build` writes one JSON object (`replay_navigation.cpp:2312-2361`):
  - `minX`, `minY`, `cell`, `nx`, `ny` and `top`: the highest walkable z per grid column;
  - `areas`: one array per navmesh area of `[x, y, z, adjacent]` corners;
  - `nodes`: `[x, y, z, zone, [linked node indices]]` for each node;
  - `fixed`, `zones` and `spawns`.
  - It has no schema version, no negotiation flag, no animscript and no tacgraph. It is written only when `Build` runs, so it needs at least one spawn or pathnode.
- Nothing else in `X` references this variable (grep of `X:src` returns only `replay_navigation.cpp:2312`).
- **Editor launch point:** the converter is started with `QProcess` in `ED:src/qtapp/compile_panel.cpp:632` (`startProcess`). The editor sets no process environment anywhere (grep of `ED:src` finds no `setProcessEnvironment`, `putenv` or `qputenv` for it), so the variable is simply not set today.

### 1.3 Editor pass-through of node markers

- `ED:src/core/compile_iw8.cpp:507-509` treats `node_pathnode`, `node_negotiation_begin`, `node_negotiation_end` and `fx_origin` as known source-only classes, so they are not rejected.
- `ED:src/core/link_graph.cpp:71-97` checks that each `node_negotiation_begin` has a `target` that matches a `node_negotiation_end` `targetname`. It warns when several ends match and errors when none match.
- `X` reads them at `X:convert/iw3_iw8/iw3_fastfile.cpp:12917-12944`. `node_3d_pathnode` is not read by `X` (grep of `X:convert/iw3_iw8` returns only `node_pathnode` and the two negotiation classes). Backlot has 206 of them in stock, so an editor author should not expect them to affect nav.
- **Stale text to fix:** `ED:tools/gen_iw8_entity_defs.py:161-164` and `:189-191` still say these classes are "Source-only; dropped". `ED:docs/research/15_iw8_extraction_contract.md:209` lists `node_negotiation_*` and `node_pathnode` as "projected away without error". That was true before the fork change and is no longer true for nav; `ED:docs/ROADMAP.md:262` says the pass-through is verified in a built zone.

### 1.4 Blockers and modifiers

**What the converter has today**

- Only two inputs reach `Scene`: hull and triangle `flags`, each set from `contents` (`replay_navigation.cpp:251-252`, `:268`). Flags are:
  - `kBlocksMovement = 1` and `kBlocksSight = 2` (`replay_navigation.h:16-17`);
  - movement when `contents & 0x2014011`, sight when `contents & 1` (`iw3_fastfile.cpp:12829-12836`);
  - static-model triangles get both bits (`:12887`).
- The fork has no volume entity, no per-area cost, no "avoid" flag, and no nav modifier emission. Grep of `X` for `nav_blocker`, `navblocker`, `nav_modifier`, `nav_volume`, `avoid_volume` finds only the Replay data model (below).

**Editor brushes, mapped through those bits** (`ED:src/core/iw8_contents.cpp:53-73`, `ED:src/core/iw8_contents.h:18-25`):

| Editor tool | Contents | Effect on bot nav | Effect on sight |
|---|---|---|---|
| `clip` (solid), `caulk`, `nodraw`, `sky` | `0x1` | blocks | blocks |
| `clip_player` | `0x10000` (in `0x2014011`) | blocks (also blocks players) | no |
| `clip_nosight` | `0x1000` | no effect (not in mask) | no |
| `clip_weapon` / weaponClip | `0x400` | no effect | no |
| `water`, `hint`, `skip` | `0` | not collision, dropped | no |
| `trigger` | trigger hull contents, not in world collision | no | no |

- **Consequence:** there is no way today to block bots and not players. Any bit in `0x2014011` is a player trace bit too. The names of bits `0x4000` and `0x2000000` are UNVERIFIED.

**Replay data model (not emitted by x64-zt)**

- `X:games/iw8/structs.hpp:11516-11530` defines `nav_modifier_s`: boundary planes, position, target name, target, script noteworthy, script link name, user flags, layer flags, weight and active-on-load.
- `X:games/iw8/structs.hpp:11582-11592` gives `NavMeshData` counts for obstacle bounds, glass bounds, modifiers, link creation data and raw volumes.
- `X:games/iw8/neutral/world.cpp:808` and `:825` read and write them, but this is the dump direction (stock to neutral). Nothing writes them for a new map.
- `X:core/assets/world_data.hpp:351,415-428` is the same concept in the neutral model.
- Stock obstacle candidates: `collmap_scriptable` brushes with `_ai_obstacle` targetnames (ship `veh8_civ_lnd_victor40_ai_obstacle`, backlot `veh8_civ_lnd_secho_ai_obstacle`). UNVERIFIED that these feed `obstacle_bounds`.
- Modifier weight and flag semantics are UNVERIFIED (no Replay source here).

### 1.5 Traversal (negotiation) authoring

**Converter rules**

- A `node_negotiation_begin` needs a non-empty `animscript` and a non-empty `target`. Otherwise it is counted as incomplete and left out (`X:convert/iw3_iw8/iw3_fastfile.cpp:12921-12933`).
- Animscripts containing `wall_run`, `wallrun` or `boost` are IW7-only and dropped (`:12924-12926`).
- A begin pairs with an end whose `targetname` equals its `target` (`:12938-12945`). Unpaired begins are dropped.
- Begin and end are placed as Begin (type 16) and End (type 17) pathnodes with one negotiation link (`X:games/iw8/common/replay_navigation.cpp:116-118`, `:40`). A begin or end off the walkable mesh, or past the node cap, is dropped with a log line (`replay_navigation.cpp:1836`, `:1951`).
- The header comment cites Replay functions that read the animscript for the bot zone graph (`X:games/iw8/common/replay_navigation.h:31-34`: `Nav_SetupNavLinks`, `Bot_InitZoneGraph`, `Bot_AnimscriptToNegotiationType`). I did not verify these addresses.

**Stock values (decoded, read-only)**

Both stock maps pair 1:1: every begin's target matches exactly one end `targetname`, and no end name is duplicated (checked in `scratchpad\decode`).

| Map | Begins | Animscript values (count) |
|---|---|---|
| mp_shipment | 30 | `jump_down_80` (25), `jump_down_40` (5) |
| mp_backlot2 | 263 | `jump_up_40` (81), `jump_down_40` (75), `jump_across_100` (46), `jump_down_80` (38), `jump_down_136` (10), `jump_down_160` (9), `ladder_up` (2), `jump_up_80` (2) |

- The animscript key id is 87 (`ED:data/generated/iw8_canonical_keys.tsv`).
- The stock script string pool (`stock_cache\...\mp_shipment\script_strings.json`) also contains `traverse_jumpdown_40`, `traverse_jumpdown_56`, `traverse_jumpdown_130`, `traverse_jumpdown_136` and several `_rail_` and `_iw6` variants. No stock negotiation node uses them. Whether Replay maps them is UNVERIFIED.
- **Editor mismatch:** `ED:tools/make_dev_showcase.py:309-310` writes `animscript = traverse_jumpdown_40`. Stock nodes use `jump_down_40`. Whether the first spelling negotiates in Replay is UNVERIFIED. Use the stock spelling.
- The editor's entity defs say `animscript` is "string: any text" (`ED:docs/ENTITY_REFERENCE.md:1479`). A picker of the stock values would prevent typos.

### 1.6 Editor steps for 6.23 (nav)

**Can do now (no fork change)**

1. **Overlay from the debug JSON.** In `compile_panel.cpp:startProcess` (`:632`), before `proc_->start`, call `setProcessEnvironment` with `IW8_ZONETOOL_NAV_DEBUG = <build>/nav_debug.json` (absolute path). After the zones stage, parse the JSON and draw `areas` as polygon outlines, `nodes` and `links` as lines, and `top` as a height tint. Hook it into a new camera mode next to `CameraModeContents` (`ED:src/qtapp/main_window.cpp:1342`). Caveat: the file exists only when `Build` runs, and it is a diagnostic format with no version.
2. **Show node markers.** `node_pathnode` and the negotiation classes already appear in the entity filter (`ED:src/qtapp/main_window.cpp:1049`). Add begin-to-end arcs with their animscript labels.
3. **Traversal inspector.** Add an animscript picker for `node_negotiation_begin` with the stock values above. Keep `link_graph.cpp` checks. Show the pairing result from `link_graph.cpp` in the inspector.
4. **Blockers.** Document that a `clip_player` or solid `clip` brush blocks bots and players. Do not promise a nav-only blocker.

**Needs a fork change**

5. Emit a versioned `navigation.json` from `Build` (`replay_navigation.cpp:2312` area). Include negotiation flags, animscripts and zone ids so the overlay does not depend on the diagnostic format.
6. Emit `nav_modifier_s` and obstacle bounds from a new extraction list. Changes: `EmitNavMesh` (`replay_navigation.cpp:2519-2541`), `Register` (`:2621`), and a reader near `iw3_fastfile.cpp:12917`. Editor side: a new entity class in `compile_iw8.cpp:494-509` and a JSON list in the extraction. Semantics of weight and flags are UNVERIFIED.
7. A nav-only blocker needs a contents bit outside `0x2014011` with a verified name. That needs Replay's contents table, which is not in this repo (see section 4).

---

## 2. Water

### 2.1 Editor side

- The water tool rule is `{"water", 0, false, true, ...}`: contents 0, no collision, drawn, "water contents bit UNKNOWN" (`ED:src/core/iw8_contents.cpp:65`; header note `ED:src/core/iw8_contents.h:36,51`).
- `brushContents` sets `out.water = true` (`ED:src/core/iw8_contents.cpp:170`), but nothing reads that field (grep of `ED:src` finds no other use).
- `gatherBrush` emits a collision brush only when `bc.collides` (`ED:src/core/compile_iw8.cpp:301-303`). A water brush therefore produces no collision brush, no swim volume and no surface type on collision. Its faces are still drawn.
- Built-in material: the editor writes `builtin_materials/water.iw8mat.json` with `"surface": "water"` and a tinted rippled PNG (`ED:src/qtapp/compile_panel.cpp:529-552`). A user material named `water` is skipped (`:93`, `:535`).
- The in-game checklist says "water is a blue rippled box and walk-through" (`ED:docs/TEST_CHECKLIST.md:42`). That item is unchecked, so this is the expected behaviour, not a verified result.
- ROADMAP 6.8 (`ED:docs/ROADMAP.md:261`) records the same state: "non-colliding drawn volume + water surface type only".

### 2.2 Converter side

- **Drawn water is handled.** IW3 `wc_water` becomes an animated reflective material: a 1024-frame wave flipbook at 30 fps and a `MAP_WATER` techset variant (`X:convert/iw3_iw8/iw3_render_assets.cpp:2907-2940`, `:2481-2535`, `:6051-6066`, `:6968-7003`). This is a visual result, not a physics result.
- **Surface type:** IW3 surface bit `524288` maps to Replay surface type 21 (water) in `NativeSurfaceMaterial` (`X:convert/iw3_iw8/iw3_fastfile.cpp:6971-6999`). Its source is a collision brush, which a water volume does not create.
- **Contents:** `ConvertContents` keeps only `contents & 0x33683` (`iw3_fastfile.cpp:6949-6957`). The IW3 water bit, if it is `0x20`, is dropped, but the IW3 bit value is UNVERIFIED.
- **Replay bits:** with `contents_encoding = "replay"`, bits are kept with `kReplayContents = 0x27F7FFFB` (`X:games/iw8/common/replay_havok.h:22`, used at `iw3_fastfile.cpp:7118-7123`). That mask includes bit `0x20`, but the comment names only solid 1, foliage 2, vehicletrigger 8, glass 0x10 and useclip `0x20000000` (`replay_havok.h:20-21`). So `0x20` is probably a named Replay bit, but its name is UNVERIFIED.
- **Empty contents are skipped:** `if (!contents) continue;` in the brush loop (`iw3_fastfile.cpp:7185-7186`). That is why editor water (contents 0) never reaches collision.
- **Replay name:** an IW7 bullet-trace shim lists `physicscontents_water` among Replay's trace contents (`X:convert/iw7_iw8/export_effects.cpp:2154`, copied from Replay global_mp ScriptFile 1805 per the comment). Replay therefore has a water contents category. Its bit value is UNVERIFIED.
- **No water asset:** the IW8 asset enum (`X:games/iw8/iw8_structs.h`) has no physics-world, water-volume or water-preset type. Grep of `X:games/iw8` for `waterVolume`, `PhysWorld` and `PhysWater` finds nothing.
- **Swim-related data exists but is not produced:** `SuitDef.viewheight_swim` (`X:games/iw8/structs.hpp:15796`, offset assert `:25993`; copied in `X:games/iw8/neutral/gameplay.cpp:3459,3811`). The GSC builtins `allowswim` and `allowswimpredicted` are in the builtin table (`X:convert/iw3_iw8/iw8_gsc_builtins.inc:1225-1226`). Whether a water contents hull drives swimming in Replay is UNVERIFIED.

### 2.3 What is missing

1. A contents bit for editor water brushes. The name is `physicscontents_water`; the value is UNVERIFIED (candidate `0x20`, inside `kReplayContents`).
2. A converter path that keeps a non-solid water brush in collision. `iw3_fastfile.cpp:7185-7186` skips empty contents, and `:7118-7123` applies only to `contents_encoding = "replay"`.
3. A Replay-side consumer: either the engine reads the water contents hull, or a script calls `allowswim`. Neither is verified here.
4. A surface type on the water collision (21). Today it is set only on collision brushes (`compile_iw8.cpp:301` onward).

### 2.4 Editor steps for 6.24 (water)

**Can do now**

1. Keep the drawn water volume. Add a `trigger_multiple` over the volume and a GSC behaviour that calls `allowswim` on enter and clears it on exit. Use the Script dock pattern from 6.12 (`ED:docs/ROADMAP.md:265`). Whether this gives swimming in Replay is UNVERIFIED and needs an in-game test.
2. Add a warning in the preflight: a `water` brush has no collision and no swim state, so the build cannot make it swimmable by itself.

**Needs a fork change**

3. In `gatherBrush` (`ED:src/core/compile_iw8.cpp:301`), emit a water brush with the verified Replay water bit and `contents_encoding = "replay"`. In the converter, keep the brush when its contents are non-zero after `kReplayContents` masking (`iw3_fastfile.cpp:7185-7186`).
4. Verify the water bit name and value in Replay's contents table before step 3. That needs static analysis of the Replay executable or a Replay zone dump, which this task does not do.

---

## 3. Doors and mantle hints

### 3.1 Stock evidence (decoded, read-only)

- None of the 35 stock entity classes (`ED:docs/research/10_iw8_entity_inventory.md`) is named as a door. The decoded classes match that table.
- **Animated gates:** backlot has `script_model` entities with models `ee_fence_trainyard_gate_left_animated` and `ee_fence_trainyard_gate_right_animated`, targetnames `infil_door_left` and `infil_door_right`, plus `metal_chain_01_wrapped` as `infil_door_chain`.
- **Brush models:** ship has 51 `script_brushmodel` entities (46 with `spawnflags 1`, one `ScriptBrushModelDummyDefault` for `care_package_col`). Backlot has 58 (54 with `spawnflags 1`). What spawnflag 1 means is UNVERIFIED.
- **Wiring:** `trigger_use_touch` targets a `script_model` (7 in each map), and a `script_model` targets a `script_brushmodel` (6 in ship, 13 in backlot). This is the stock wiring pattern for interactive objects.
- The script string pool contains `door_open`, `door_touch`, `door_kick` and many `*_door_*` names. These look like animation or notetrack names. Their use is UNVERIFIED.
- The stock GSC that opens doors is not in the stock cache. How Replay opens stock doors is UNVERIFIED.

### 3.2 Converter

- Brush models come from `*N` submodels in `ReadBrushModels` (`X:convert/iw3_iw8/iw3_fastfile.cpp:3668`). A `script_brushmodel` keeps its collision submodel (`ED:src/core/compile_iw8.cpp:1012-1014` gives it hulls and render surfaces; `ED:src/core/collision_world.cpp:23` treats it as blocking).
- The only `script_brushmodel` special case in `X` is glass (`X:convert/iw3_iw8/iw3_fastfile.cpp:4097-4122`). There is no door logic in `X`. Door motion must come from script.

### 3.3 Mantle

Update 2026-11-14: research 125 settles the open point below from the Replay exe. Mantling needs no edge data while
the map's edge query types leave the mantle bit clear (the converter does); weapon mounting needs mount edges.

- Replay mantles are computed from collision at runtime (`ED:docs/ROADMAP.md:261`, and the editor mirror in `ED:src/core/player_mantle.cpp:10-21`: min ledge 16, max 72, high-mantle branch at 58). The editor's constants are cited to `walk_mantle.cpp` but that source is not in this repo.
- **No mantle data from the fork.** Stock has no mantle or hint classname (doc 10). Grep of `X:convert/iw3_iw8` for `mantle` finds only GSC builtins.
- **Edge data exists in the format but is not written.** The IW8 BSP has `BSP_VERSION_EDGE_MANTLE_ON_OVER_FLAGS = 176` (`X:games/iw8/structs.hpp:11838`) and map edge lists (`X:games/iw8/iw8_map_structs.h:222-225`; `X:games/iw8/dump.cpp:462`). `X` never writes an edge list. The only mention is an origin label (`X:convert/iw3_iw8/convert.cpp:1436`). Whether Replay needs edge data for a mantle over an edge is UNVERIFIED.
- **Script overrides exist:** `allowmantle`, `canmantle`, `cancelmantle`, `forcemantle` and `setmantlesoundflavor` are in the GSC builtin table (`X:convert/iw3_iw8/iw8_gsc_builtins.inc:1209,1372,1376,1647,2646`). `forcemantle` is a candidate route for a mantle hint. Its exact semantics are UNVERIFIED.
- **Hint brushes do nothing.** The editor `hint` tool has no collision and is not drawn (`ED:src/core/iw8_contents.cpp:72`). The converter has no hint-to-mantle path.
- The `ledge` key (canonical key 569 in `ED:data/generated/iw8_canonical_keys.tsv`) appears in no stock class listed in doc 10. Its use is UNVERIFIED.

### 3.4 Editor steps for 6.24 (doors and mantle)

**Doors, can do now**

1. Author a door as a `script_brushmodel` brush with a targetname. Add a `trigger_use_touch` with `target` pointing at it, or an animated `script_model` for the gate, following the stock wiring in 3.1.
2. Use the existing Script dock behaviours (Sliding door, Rotating door; ROADMAP 6.12). In-game behaviour is NOT VERIFIED (`ED:docs/ROADMAP.md:265`).

**Doors, needs a fork change or verified script**

3. A stock-matching door needs the script that Replay runs for these classnames. It is not in the stock cache. Either extract it from a stock zone with ACTS (not done here) or confirm the call pattern from a stock map.

**Mantle hints, can do now**

4. None. Mantles come from collision. Test ledges in the play preview (`ED:docs/ROADMAP.md:167`). The preview mirrors Replay constants but is not compared in game.

**Mantle hints, needs a fork change or verified script**

5. Test whether `forcemantle` with a hint trigger gives a usable mantle hint (UNVERIFIED). If Replay needs edge data, a fork change is needed to write edge lists from editor edges.

---

## 4. Unverified items

- Bit names and values: `0x4000`, `0x2000000` (nav mask), `0x20` (water candidate in `kReplayContents`), and `physicscontents_water`. Needs Replay's contents table.
- Meaning of `script_brushmodel` `spawnflags 1` and `script_model` `spawnflags 4`.
- How Replay opens stock doors (GSC not in the stock cache).
- Whether `traverse_jumpdown_*` strings negotiate, and how `Bot_AnimscriptToNegotiationType` maps animscripts (addresses are cited in `replay_navigation.h:31-34` but not verified).
- Whether a water contents hull drives swimming, or whether `allowswim` is needed.
- Whether a mantle needs edge data, and what `forcemantle` does.
- Nav modifier weight and flag semantics; whether `_ai_obstacle` brushes become obstacle bounds.
- In-game status of the water volume and the Script dock door behaviours (checklist items unchecked).

## 5. Files read

- `X:convert/iw3_iw8/convert.cpp` (lines 85, 1432-1436, 1542-1544, 1683-1706, 1894, 2665).
- `X:convert/iw3_iw8/iw3_fastfile.cpp` (lines 3668, 4097-4122, 6949-6999, 7118-7123, 7185-7186, 12825-12950).
- `X:convert/iw3_iw8/iw3_render_assets.cpp` (lines 2481-2535, 2907-2940, 6051-6066, 6968-7003).
- `X:convert/iw3_iw8/iw8_gsc_builtins.inc` (lines 1209, 1225-1226, 1372, 1376, 1647, 2646).
- `X:convert/iw7_iw8/export_effects.cpp` (line 2154).
- `X:games/iw8/common/replay_navigation.h` and `.cpp` (lines 16-17, 31-34, 68-71, 22-33, 40, 49, 94-119, 251-268, 633-640, 1635-1700, 1836-1859, 1951, 2312-2377, 2519-2541, 2621-2637).
- `X:games/iw8/common/replay_havok.h` (lines 13-22).
- `X:games/iw8/structs.hpp` (lines 11516-11592, 11838, 15796, 25993).
- `X:games/iw8/iw8_structs.h`, `X:games/iw8/iw8_map_structs.h`, `X:games/iw8/neutral/world.cpp`, `X:games/iw8/neutral/gameplay.cpp`, `X:games/iw8/dump.cpp`, `X:core/assets/world_data.hpp`.
- `ED:src/core/compile_iw8.cpp` (lines 301-303, 494-509, 1012-1014), `ED:src/core/iw8_contents.cpp` and `.h`, `ED:src/core/link_graph.cpp`, `ED:src/core/player_mantle.cpp`, `ED:src/core/collision_world.cpp`, `ED:src/qtapp/compile_panel.cpp` (lines 93, 529-552, 632), `ED:src/qtapp/main_window.cpp` (lines 1049, 1342), `ED:tools/gen_iw8_entity_defs.py`, `ED:tools/make_dev_showcase.py`.
- `ED:docs/ROADMAP.md` (lines 96, 167, 261, 262, 265, 276, 277), `ED:docs/TEST_CHECKLIST.md` (line 42), `ED:docs/ENTITY_REFERENCE.md` (line 1479), `ED:docs/research/10_iw8_entity_inventory.md`, `ED:docs/research/15_iw8_extraction_contract.md` (line 209), `ED:data/generated/iw8_canonical_keys.tsv`.
- Stock JSON (read only): `stock_cache\mp_shipment\mw19replay\mp_shipment\assets\map_ents\maps\mp\mp_shipment.d3dbsp.0.asset.json`, `stock_cache\...\script_strings.json`, and `ED:data\stock_dumps\mp_backlot2\...\mp_backlot2.d3dbsp.0.asset.json`.

Not updated: `ED:docs/PROGRESS.md`. This task was scoped to one new file.
