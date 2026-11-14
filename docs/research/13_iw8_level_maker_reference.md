# 13. Third-party "IW8 Level Maker" (IW8 Radiant Studio): reference review

Date: 2026-11-08. Scope: `%USERPROFILE%\Downloads\iw8 level maker` (215 files), read as extra reference only.

Method and constraints. Every file was treated as untrusted data. Nothing in the folder was run, built, opened, or imported. The three root "IW8 Radiant Studio" executables and the `dist-*` executables were not launched; only file-system metadata (size, timestamps, empty version resources) was read from them. No Python from the folder was imported. Stock data was checked with `python -I` against our own `data/stock_dumps`. The folder was not modified. The owner's "about 340 MB" was not re-summed.

Name clash: this project is "IW8 Radiant Studio" / `IW8LevelMaker`. It is not our "IW8 Radiant Suite" (`E:\IW8-Radiant`).

---

## 1. Summary

- **What it is.** A Win32 / C++20 map editor for IW8 (Replay 1.20). It has an integrated fastfile path that calls an IW8-ZoneTool build (`build-map`). All editor logic is in one file, `src/main.cpp` (2,981 lines, 306,626 bytes). It is not buildable from this folder alone: the `.vcxproj` links `..\IW8-zonetool\xmake-out\...\iw8-zonetool-static.lib` and `DirectXTex` headers from a sibling tree.
- **Target build.** Replay 1.20.4.7623265 is implied, not documented. The only evidence is the exe name `game_dx12_ship_replay.exe` and a folder name that contains the version string. No MD5 is recorded anywhere in the folder.
- **Overlap with our evidence.** The entity key ids are the same as ours (212, 709, 80, 652, 1072, 1070, 845). I confirmed all seven against the stock 1.20 `mp_shipment` dump. The five-zone fastfile layout, the `build-map` flags, and the sun `lighting.json` schema also agree.
- **Main conflicts.** (a) The entity text it writes is CRLF with no NUL terminator, and each entity is on one line. Stock 1.20 is LF, NUL-terminated, multi-line. (b) It writes spawn points as entity classnames; stock 1.20 keeps spawns in `spawnList`. (c) Its reflection "probe" is six solid BC6H faces, which does not match our probe model. (d) Its `.map` faces use the 5-number classic layout, not our iwmap 4 layout. (e) It omits the stock worldspawn lighting and CSM keys.
- **Data that is worth something.** The lighting JSON schema, the key-id usage, the procedural skybox generator (art only), and the BR loot CSVs (a different game mode and build; provenance unknown). None of the code's numeric limits (128 point lights, 4096 render surfaces) is sourced.
- **Recommendation.** Use it as a reference for the data shapes listed in section 5. Do not adopt its code, its entity layout, its probe/collision blobs, or its bundled `gsc-tool.exe`. There is no licence file in the folder, so ask the owner before reusing anything.

---

## 2. Architecture

### 2.1 Layout and build

| Item | Detail | Source |
|---|---|---|
| Source | One translation unit, `src/main.cpp`, 2,981 lines (306,626 bytes) | `src/main.cpp` |
| Solution | VS2022, v143, x64, C++20, Unicode, `Application`, `MultiThreadedDLL` | `IW8LevelMaker.vcxproj` |
| External deps | `..\IW8-zonetool\xmake-out\x64\{Debug,Release}\iw8-zonetool-static.lib`; `..\IW8-zonetool\src\zonetool\iw3\resources.rc`; DirectXTex headers under `../../IW8-zonetool/iw8 zonetool/src/thirdparty/` | `vcxproj`, `main.cpp:16-17` |
| Post-build | Copies `gsc-tool.exe`, `material_template.json`, `techset_template.json`, `br_tables\` next to the exe | `vcxproj` PostBuildEvent |
| Build outputs in folder | `build-check\`, `x64\Debug\`, `x64\Release\`, `IW8LevelMaker\x64\...` (obj, pdb, `.ilk` of 76.6 MB, tlog) | folder listing |

### 2.2 UI toolkit and renderer

- **UI.** Raw Win32: `CreateWindowExW`, custom `WM_PAINT`, owner-drawn buttons, `GetOpenFileNameW`. No Dear ImGui, no Qt. UI strings are in French. GDI+ for images, Direct2D for a 2D geometry cache (`D2DGeometryKey`), WIC (`windowscodecs.lib`) for image input.
- **Renderer.** One D3D11 device (`D3D11CreateDevice`, `main.cpp:2271`). HLSL `vs_4_0` / `ps_4_0`, compiled at run time with `D3DCompile` (`main.cpp:2281-2282`). Orthographic orbit 3D view and a 2D top view. Terrain is a procedural heightfield. Textures are generated procedurally (tiles, mortar patterns, hashes at `main.cpp:2941-2951`).
- **Comparison with ours.** Ours is Qt Widgets plus D3D11 with offscreen views (`ARCHITECTURE.md` §3, §6). The toolkits do not overlap. Both use a single D3D11 device.

### 2.3 File formats read and written

| Format | Direction | Notes | Source |
|---|---|---|---|
| `*.iw8map` (`IW8LEVEL n`) | read/write | Own line-based project format. Reader accepts versions 1 to 11 (`main.cpp:386-387`). Writer emits `IW8LEVEL 11` (`save/mp_shatterbay.iw8map` is v11, `save/mp_test.iw8map` is v10). | `main.cpp:386-387`, `save/` |
| `*.map` (classic Radiant) | read (Ctrl+I, axis-aligned) / write | Entities as `{ "classname" ... }`; brush faces as `( x y z ) ( ... ) ( ... ) MAT 0 0 0 1 1` (5 numeric values after the material). No `iwmap 4` header. | `main.cpp:471-486`, `save/mp_test.map` |
| Entity text (`*_iw8_ents.txt`, `*.d3dbsp.ents`) | write | `{ 212 "classname" 709 "x y z" 80 "p y r" [1072 "targetname"] [652 "model"] }`, one entity per line, CRLF (text-mode write), no NUL. | `main.cpp:486`, `save/mp_test/mp_test_iw8_ents.txt` |
| `*.lighting.json` | write | Schema 1: `intensity`, `color`, `direction`, `up`, `sun_primary_light_index`, `primary_lights[]`. | `main.cpp:644-650`, `save/mp_test/maps/mp/mp_test.d3dbsp.lighting.json` |
| `*.bounds.json` | write | `{"schema":1,"min":[..],"max":[..]}` | `save/mp_test.bounds.json` |
| Point-light metadata | write | `{"schema":1,"note":"Editor metadata; not a native Replay light asset pool","lights":[]}` | `save/mp_test/mp_test_point_lights.json` |
| `*.collision.native` (`MWCOLL08`) | write | Own hull list (count 0xDA = 218 in the test map, matching the build log). Intermediate for `--collision`. | `main.cpp` collision writer, `save/mp_test.collision.native` |
| Probe blocks `*_probe0.bc6h`, `*_probe_array.bc6h` | write | Six 4x4 BC6H blocks (96 bytes) of one ambient colour, plus one block (16 bytes). | `main.cpp:633-641` |
| Atlases `*_sky_atlas.rgba8`, `*_surface_atlas.rgba8` | write | Raw RGBA8: 1024x768 (sky) and 1024x1024 (surface), exact sizes. | `save/mp_test/maps/mp/` |
| `*.d3dbsp.render.json` | write | Surfaces with positions, UVs, packed normals, colours, indices. 5.7 MB for the test map. | `save/mp_test/maps/mp/` |
| `*.d3dbsp.material.json`, `*.techset.json` | write | Built from `assets/zonetool/*_template.json`; `replaceOnce` injects `"nativeReplayFixture": true` into the techset. Constant buffers as hex strings. | `main.cpp` (template loader), `save/mp_test/maps/mp/` |
| `*.fbx` | read | Own FBX parser, including compressed arrays (`main.cpp:705-754`). Prefabs are voxelised into brushes. | `main.cpp:705-754`, `README.md` |
| Images | read | PNG, JPG, BMP, TIF through WIC / GDI+ (`main.cpp:1782`). | `main.cpp:1782` |
| Settings `%LOCALAPPDATA%\IW8LevelMaker\settings.ini` | read/write | Magic `IW8LM_SETTINGS`, version 1 or 2. Stores the ZoneTool exe, Replay exe, texture folder. | `main.cpp:189-190` (reader); writer emits `IW8LM_SETTINGS 2` |

### 2.4 Build path (editor to fastfile)

1. `exportBundle` writes a dump folder: `maps/mp/<id>.d3dbsp.*`, `<id>_iw8_ents.txt`, collision blob, atlases, JSON.
2. Integrated path: `IW8ZoneToolBuildMap(dump, id, out, replay, acts, collision, log, battleRoyale, gsc-tool, brGround, brCrates)` (extern "C", `main.cpp:44`, called `main.cpp:691`). The log is `zonetool-build.log`.
3. Standalone path: `save/mp_test/build-fastfile.cmd` calls `iw8-zonetool.exe build-map <dump> mp_test -o <out> --replay <exe> --collision <file> --no-navigation`. That exe is at `D:\games\wz_moding\IW8-zonetool\...`, which is not in the folder.
4. Output: five zones (`<id>`, `srv_<id>`, `eng_<id>`, `ww_<id>`, `techsets_<id>`) plus `map.json`. The four small zones in the test output are 172-byte stubs.
5. Battle Royale option (`battleRoyaleEnabled`): adds `mwbr` GSC (compiled with `gsc-tool.exe`), two `mwbr_corner` script_structs, two `ac130_flight_path` points, and the BR loot CSVs.

### 2.5 What `save/` contains

| Path | Contents | Notes |
|---|---|---|
| `mp_test.map` (555 KB), `mp_test.iw8map` (106 KB), `mp_shatterbay.iw8map` (78 KB) | Editor project files for two test maps | `mp_test.iw8map` is v10; shatterbay is v11. Both contain absolute paths to a texture under a user profile. |
| `mp_test.bounds.json`, `mp_test.collision.native` (98 KB) | Bounds (BR-style corners `-1050.76..1048.19`), collision blob | The collision blob is duplicated in `mp_test\`. |
| `mp_test\mp_test_iw8_ents.txt` (653 B, 9 lines) | Entity input to build-map | Three `mp_tdm_spawn*` lines, `script_model`, two `ac130_flight_path`, two `mwbr_corner`, `worldspawn`. |
| `mp_test\build-fastfile.cmd` | Build script with absolute `D:\` paths | Calls an external exe (contradicts README, see section 6). |
| `mp_test\zonetool-build.log`, `sky-validation.log` | Build logs | "passes structural checks", "published validated map output". Surfaces: 5,166 triangles; collision 218 hulls + 2,304 triangles; navmesh 1,763 areas; 448 pathnodes; 283 BR loot points. Asset stage reports 0 materials, 0 images, 0 models. |
| `mp_test\.br-scripts\mp_test\mp_test.gsc` (19.6 KB), `compiled\iw8\mp_test.gscbin` | Map script with BR support | Compiled by `GSC Tool 1.6.2.364 (xensik)`. |
| `mp_test\fastfile-br-check\` | Five zones plus `map.json` (`mp_test.ff` 9.7 MB, `techsets_mp_test.ff` 47 KB, four 172 B stubs) | The test build output. |
| `mp_test\maps\mp\` | `render.json` (5.7 MB), `gpulightgrid.native` (13.1 MB), `material.json`, `techset.json` (87 KB), `lighting.json`, `bounds.json`, probe blocks, atlases | The bulk of the save data. |

### 2.6 `dist-*` folders and root executables

- `dist\IW8LevelMaker.exe` (180,736 B), `dist-cubemap\`, `dist-explorer\`, `dist-verify\` (each 209,920 B). Each folder holds only the exe. There are no logs, notes, or sources.
- The name `dist-cubemap` suggests a probe-capture build. The source does not contain a cubemap capture: `main.cpp:633` says editor maps have no captured cubemap. So this variant is not matched by the current source. Its purpose is UNVERIFIABLE without running or reversing it, which was not done.
- Root: `IW8 Radiant Studio BR Plane.exe` (5.45 MB), `BR Resource Fix.exe` (5.45 MB), `Open Fixed.exe` (5.19 MB), and `gsc-tool.exe` (23.2 MB). The three IW8 Radiant exes have no version resource. Their relation to `main.cpp` is UNVERIFIABLE.

---

## 3. Data catalogue

### 3.1 `br_tables/` (Battle Royale loot, 14 CSV files)

| Table | Rows (lines) | Content | Build claim | Reuse trust |
|---|---|---|---|---|
| `loot_item_defs.csv` | 230 | Tagged rows: `item` (116), `zone` (62), `depends` (45), `raritydef` (7). Each row has 34 comma fields. Zone rows map a zone to a loot set and a cache set (e.g. `map_airfield`, `map_torez`, `map_quarry`, `map_dam`, `map_stadium`, `map_tvstation`, `map_gulag`). Also `dev_br_mechanics_{alcohol,armor,medical}`. | None in the files. | Low. BR-era content. The POI names look like a Verdansk-era BR map set, but that is an inference, not evidence. |
| `loot_set_global.csv` (108), `loot_set_global_namedLocation.csv` (106), `loot_set_plunder.csv` (109), `loot_set_super.csv` (108) | line counts | Chance and `req` rows: points 60%, clusters 76% (or 50% / 60% in the named-location set). Ammo 330, health 100, lethal 70, tactical 70, killstreak 0. | None. | Low. |
| `loot_set_cache_contents_{base,lege,mission_asn,mission_dom,mission_scav1,mission_scav2,mission_scav3,rare}.csv` | 96 to 102 | Cache contents by tier, with `req` lines and comments. | None. | Low. |
| `loot_cache_set_global.csv` | 5 | `req` and `chance` for `br_loot_cache`. | None. | Low. |

- `br_tables/mp/` and `br_tables/br_tables/mp/` are byte-identical (hash-checked for all 14 files). Copies also appear under `build-check\` and `x64\`.
- These tables are Battle Royale data. They are a different game mode from our 1.20 MP target, and the build they came from is unknown. They must not be used as evidence for MP 1.20.

### 3.2 `assets/`

| File | Size and content | Notes |
|---|---|---|
| `skybox_{clear,night,studio,sunset}.bmp` | 1024x768 24-bit BMP each (2,359,350 B). A 4x3 atlas of six 256 px faces. | Generated by `tools/generate_skybox.py`, procedural clouds and gradients. Art only. Not captured from the game. |
| `zonetool/material_template.json` | 1,277 B. `source` `tw/rubber_trim_black_baseboard_01`, `techset` `tw/mw120r_graybox_v1`. | Hex `constants`, `bufferIndices`. Field names not provided. |
| `zonetool/techset_template.json` | 87,193 B. Techniques with `header` hex, shader stubs. | Same naming family (`tw/`, `mw120r`) as the mw120rproxy pipeline (`03_build_pipeline_interface.md`). |
| `assets/assets/zonetool/*` | Duplicate copies of both templates. | Redundant. |

### 3.3 `tools/generate_skybox.py` (4,095 B)

- Writes the four skybox BMPs. Palettes: four presets (clear, sunset, night, studio). Sun direction `(0.32, 0.24, 0.92)` for clear and `(-0.48, 0.22, 0.85)` for sunset. The clear-preset sun matches the test map's `lighting.json` direction.
- The face-to-direction mapping (`face_dir`) and the atlas tile positions are self-contained and reproducible.
- Trust: Low as a source of game values (artistic output). Licence unknown.

### 3.4 `previews/`

- `IW8_Sky_Atlas_Preview.png` (1024x768) and `IW8_Texture_Atlas_Preview.png` (512x256), per the PNG headers. Previews of generated assets. No game data.

### 3.5 Test map data (`save/mp_test/`)

- Surfaces use one imported JPG (a generated image). The material source name comes from the template (`tw/rubber_trim_black_baseboard_01`). The asset stage reports 0 materials, 0 images, 0 models, so the build carries no stock art.
- Warning in the build log: no `--acts` and no `--replay` fixture copy, so the stock surface-FX table and CODCaster images are not copied.
- `mp_test.d3dbsp.gpulightgrid.native` (13,116,056 B): tetrahedral light grid, format not documented in the folder. UNVERIFIABLE layout.

### 3.6 Entity keys, classnames, and constants in code

- **Key ids written:** 212 (classname), 709 (origin), 80 (angles), 1072 (targetname), 652 (model). Not written: 1070 (target), 845 (script_noteworthy).
- **Classnames written:** `worldspawn`, `mp_tdm_spawn`, `mp_tdm_spawn_allies_start`, `mp_tdm_spawn_axis_start`, `script_model` (required), `script_struct` (`mwbr_corner`, `ac130_flight_path`), `light` (editor-only, with radius). `mp_dm_spawn` is remapped to `mp_tdm_spawn` (`main.cpp`, classname rewrite).
- **Lighting constants:** sun colour presets `(1,1,1)`, `(1,.68,.48)`, `(.32,.43,.8)`, `(.78,.82,.88)`; sun direction presets; ambient probe `0.18` per channel (`main.cpp:633-645`).
- **Limits asserted in code:** 128 point lights per map (`main.cpp:646`); 4096 render surfaces (`exportBundle`). Neither carries a source.
- **Struct layouts, offsets, RVAs, hook addresses, fastfile offsets:** none. The hex literals in `main.cpp` are hash and PRNG constants, colour masks, and BC6H code. This project has no address data to cross-check against `05_runtime_evidence_1_20.md`.

### 3.7 Stock 1.20 data in our folder (for comparison, re-verified 2026-11-08)

- `data/stock_dumps/mp_shipment`: entity string 62,892 bytes, LF only, one NUL at the end, 611 entity blocks, `spawnsCount` 611.
- `data/stock_dumps/mp_backlot2`: entity string 295,431 bytes, 3,234 entity blocks, `spawnsCount` 430.
- `data/iw8_entity_classes.json`: 35 classes, 3,845 entity records, 39 spawn names summing to 1,041. These match `10_iw8_entity_inventory.md`.
- The JSON has no per-zone entity counts (only zone membership). The per-zone split in doc 10 was checked against the raw dumps, not the JSON.

---

## 4. Cross-check table

Verdicts: **AGREES** (both sides consistent), **CONFLICTS** (the sides differ), **UNVERIFIABLE** (cannot be decided from the evidence available). Build target: 1.20 unless marked otherwise.

| # | Claim (project) | Our evidence | Verdict | Build target | Notes |
|---|---|---|---|---|---|
| C1 | Key ids 212 classname, 709 origin, 80 angles, 652 model, 1072 targetname (`main.cpp:486`; `mp_test_iw8_ents.txt`) | `ARCHITECTURE.md` §4: 212, 709, 80, 652, 1070, 1072, 845 | **AGREES** | 1.20 | Confirmed in stock `mp_shipment` entity text: 212 x611, 709 x604, 80 x192, 652 x207, 1072 x311, 1070 x58, 845 x25. |
| C2 | Entity text is CRLF, no NUL (`mp_test_iw8_ents.txt`: 653 B, 9 CR, 9 LF, 0 NUL; written in text mode at `main.cpp:486`) | `ARCHITECTURE.md` §4: "LF line endings, NUL-terminated". Stock `mp_shipment`: LF 4,339, NUL 1 at end, length = declared count | **CONFLICTS** on the on-disk input | 1.20 | Whether `build-map` normalises CRLF and adds the NUL is UNVERIFIABLE: ZoneTool source is not in the folder. |
| C3 | One entity per line: `{ 212 "worldspawn" }` | Stock: `{` on its own line, one key per line, `}` on its own line (`mp_shipment` begins `{\n2703 "1 0 0 0"\n...`) | **CONFLICTS** (layout) | 1.20 | Whether Replay parses single-line blocks is UNVERIFIABLE. The project's build logs report structural checks passed, not in-game behaviour. |
| C4 | Spawns written as entity classnames `mp_tdm_spawn*` with origin and angles | `ARCHITECTURE.md` §4: "Spawn points are not entities" (`spawnList`, 40-byte records). Stock: none of the 35 stock classes is a spawn name; spawns counted via `spawnList` (611 and 430 verified) | **CONFLICTS** (entity text). Whether `build-map` moves them into `spawnList` is UNVERIFIABLE | 1.20 | `zonetool-build.log`: "direct map source: ... 3 player spawns", which suggests a conversion step exists. |
| C5 | Worldspawn written with classname only; no `csm_*`, `forwardplus`, `sunlight`, `suncolor`, `sundirection`, `skyrotation`, precomputed-sky keys | Stock worldspawn (JSON, both maps) carries `csm_enable`, `csm_max_resolution`, `csm_sample_size`, `forwardplus`, `highresgpulightgrid`, `precomputed_sky_illumination_*`, `skyrotation`, `sunlight`, `suncolor`, `sundirection`, `sunisprimarylight`, `_primarylightcount` | **CONFLICTS** (omission) | 1.20 | Effect in game UNVERIFIABLE. Relevant to `09_iw8_render_features.md` §7. |
| C6 | Sun as a primary light: `sun_primary_light_index: 1`, `primary_lights[1].type = 1`, schema 1 (`mp_test.d3dbsp.lighting.json`) | `09` §7: sun is one primary-light index (`GfxWorld.lastSunPrimaryLightIndex`). `08` §5, §7: IW3 path reads schema-1 `.lighting.json` with intensity, color, direction, up | **AGREES** (concept and format) | 1.20 (format via x64-zt IW3 path) | Replay-side effect of the values is UNVERIFIABLE. |
| C7 | Sun direction written as a unit vector `(0.32, 0.24, 0.9165)` in `lighting.json`; no `sundirection` key | Stock `sundirection` values are angle-like triples (`-20.261 -15.4 0`, `-44.5 162 0`) (JSON) | **UNVERIFIABLE** which convention the worldspawn key uses; the two forms differ | 1.20 | The project avoids the key. Not resolved by our docs. |
| C8 | Sun colour presets `(1,1,1)`, `(1,.68,.48)`, `(.32,.43,.8)`, `(.78,.82,.88)` (`main.cpp:644`) | Stock `suncolor` examples `1 0.9764 0.945`, `0.992 1 0.996` (JSON). `08` §7: intensity calibration is "not an established physical conversion" | **UNVERIFIABLE** (artistic presets, no calibration source) | 1.20 | Do not reuse as calibrated values. |
| C9 | Point lights capped at 128 per map (`main.cpp:646`, "Replay local-light preview/build limit") | `09` §6: light-volume array is `min(primaryLightCount, 256)`; Frontier's 159 lights overran 144 slots; reserve at least 256 | **UNVERIFIABLE** (128 has no source). No direct contradiction: 128 is more conservative than the 256-slot rule | 1.20 | Keep our 256-slot rule. |
| C10 | Probe = six solid BC6H faces of an ambient colour; "editor maps have no captured cubemap" (`main.cpp:633-641`) | `09` §3: reflection probes are oriented boxes with priority and feather, octahedron variants, relighting data. `08` §3: "Not IW3 cubemap points" | **CONFLICTS** (concept). Whether Replay reads a six-face cube for a probe is UNVERIFIABLE | 1.20 | The probe here is a placeholder. |
| C11 | Light grid: 58,788 authored probes converted to 314,160 native tetrahedra, 68x70x22 voxels of 32 units (`zonetool-build.log`) | `09` §4: `GfxGpuLightGrid` is tetrahedral with SH probes | **AGREES** on structure (tetrahedral with probes). Counts and voxel size UNVERIFIABLE | 1.20 | |
| C12 | Point lights are editor metadata, "not a native Replay light asset pool" (`mp_test_point_lights.json`) | `09` §1: lights are records (`ComPrimaryLight`, `GfxLight`), not entities | **AGREES** | 1.20 | Stock has no `light` classname (JSON). |
| C13 | Five zones plus `map.json`; "passes structural checks"; "published validated map output" (`fastfile-br-check\`, logs) | `08` §5: `<map>.ff`, `techsets_`, `eng_`, `ww_`, `srv_`, `map.json`; same log phrases | **AGREES** | 1.20 | |
| C14 | `build-map <dump> <map> -o <out> --replay <exe> --collision <file> --no-navigation` (`build-fastfile.cmd`) | `03` §1: `build-map <dump> <map> [-o] [opts]`; collision by `--collision` plus `--replay`. `08` §5: x64-zt uses `--navigation 0` | **AGREES** on the flags in `03`. `--no-navigation` is not in our docs: UNVERIFIABLE | 1.20 | `03` identifies `build-map` with the mw120rproxy iw8-zonetool line. The `D:\` binary is not in the folder, so its identity is UNVERIFIABLE. |
| C15 | Replay exe `game_dx12_ship_replay.exe`; folder "Call of Duty Modern Warfare (1.20.4.7623265)" | `ARCHITECTURE.md` §1: exe name and MD5 `1c238fe327f2ecc3b0db924c5b425439` | **AGREES** on name and version string. The hash is not recorded in the project: UNVERIFIABLE | 1.20 | |
| C16 | Collision blob `MWCOLL08`, 218 hulls (header 0xDA, log "baked 218 hulls") | `08` §3: `col_map` (`clipMap_t`) with Havok TAG0 tagfiles; `05` §9: Havok shape-list functions | **UNVERIFIABLE** (project-private intermediate; no Replay-side parser in the folder). Internally consistent | 1.20 | Do not treat as a Replay format. |
| C17 | Material and techset written as JSON from templates, with `nativeReplayFixture: true` (`mp_test.d3dbsp.material.json`) | `08` §3: `material` 120 B, `techset` = `MaterialTechniqueSet`. No sample in our docs | **UNVERIFIABLE** | 1.20 | The `tw/` and `mw120r` names point to the mw120rproxy lineage (`03`). |
| C18 | No addresses, RVAs, struct offsets, or hook targets in source | `05` gives RVAs for 1.20 | **N/A** (no overlap) | 1.20 | |
| C19 | Stock names `script_model`, `script_struct`, `worldspawn` exist and are used | Stock JSON lists all three | **AGREES** | 1.20 | |
| C20 | BR script: "Stock BR's C130 setup looks for two ordered script_structs" (`mwbr`), `mwbr_corner`, `ac130_flight_path` | None in our data (no BR dumps) | **UNVERIFIABLE** | BR / Warzone (not 1.20 MP) | Keep as a lead only. |
| C21 | BR loot CSVs (`br_tables\mp\*.csv`) | None in our data | **UNVERIFIABLE**. Build not stated; zone names suggest a BR-era map set | BR (build unknown) | Do not import as MP data. |
| C22 | Render-surface limit 4096 (`exportBundle`) | None | **UNVERIFIABLE** | 1.20 | |
| C23 | Sky: six 256 px faces in a 1024x768 atlas, procedural (`generate_skybox.py`) | `08` §8: sky box is six 32,768-unit faces at reversed depth | **UNVERIFIABLE** (no unit overlap; the project's sky is an image, not geometry) | 1.20 | |
| C24 | Map source `.map`: faces `MAT 0 0 0 1 1` (5 numbers, no lightmap material) | `ARCHITECTURE.md` §5: per-face layout is material, 6 numbers, lightmap material, 6 numbers (iwmap 4) | **CONFLICTS** (different dialect) | n/a | Our source format is iwmap 4; this is classic Radiant. |
| C25 | Replay `game_dx12_ship_replay.exe` 1.20 used by `build-fastfile.cmd` | `ARCHITECTURE.md` §1 | **AGREES** (see C15) | 1.20 | |

### 4.1 Checks made on our own evidence in this pass

- Stock `mp_shipment` and `mp_backlot2`: entity block counts (611, 3,234), byte lengths (62,892, 295,431), `spawnsCount` (611, 430) all match `10_iw8_entity_inventory.md` and `ARCHITECTURE.md` §4.
- JSON totals (35 classes, 3,845 records, 39 spawn names, 1,041 spawns) match doc 10.
- `data/iw8_entity_classes.json` has parser artefacts. For example, `id:0` has example values such as `all_modes" "1` and `dom" "0`. These are key/value fragments, not values. Clean them before reuse.
- The JSON says `id:0` appears on `script_struct` (2) and `worldspawn` (1). Doc 10 says 3 occurrences in `mp_backlot2`. The JSON cannot resolve the zone split. UNVERIFIABLE.

---

## 5. Recommendations

Trust scale: **High** (checked against stock data or code), **Medium** (consistent with our docs, not tested in game), **Low** (single source or artistic), **None** (contradicts evidence or unsourced).

| # | Item | Action | Trust | Why |
|---|---|---|---|---|
| R1 | Entity key-id set (212, 709, 80, 652, 1070, 1072, 845) | Keep as is. Cite the stock confirmation in `ARCHITECTURE.md` §4 | High | C1 confirmed in stock bytes |
| R2 | `lighting.json` schema 1 (`intensity`, `color`, `direction`, `up`, `sun_primary_light_index`, `primary_lights[]`) | Adopt as an export shape for the sun / primary-light description | Medium | Matches x64-zt's IW3 reader (`08` §7). The direction convention is unverified in game (C7) |
| R3 | Stock-zone JSON (classes, spawn names, counts) | Reuse after cleaning the parser artefacts (`id:0` example values) and adding per-zone counts | High for counts; Medium for key/value examples | Verified in section 4.1 |
| R4 | Skybox generator and BMP atlas layout | Optional placeholder art tool. Do not use its colours as calibrated values | Low | Artistic; licence unknown |
| R5 | UX ideas from `README.md`: 2D/3D viewport split, grid snap, below-floor entity flag, `.map` import, FBX voxelisation | Note as concepts. Do not port code | Medium | Described in `README.md`; code not deep-reviewed |
| R6 | BR loot CSVs and `mwbr` script | Do not import now. Keep as a lead for a future BR track | Low | BR-era, build unknown, no evidence in our data |
| R7 | One-line entity blocks, CRLF output, spawns as entity classnames | Avoid. Keep our layout (C2, C3, C4) | None | Conflicts with stock 1.20 |
| R8 | Six solid BC6H probe faces | Avoid as a real probe. Use an oriented-box probe record per `09` §3 | None | Conflicts with `09` §3 (C10) |
| R9 | 128 point-light cap and 4096 render-surface cap | Avoid as facts. Use the 256-slot rule (`09` §6). Ask the owner for their source | None | Unsourced (C9, C22) |
| R10 | `MWCOLL08` collision blob | Avoid as a format. Use the Havok / clipMap path (`08` §3) | None | Project-private intermediate (C16) |
| R11 | Classic `.map` face layout | Avoid for our source. Keep iwmap 4 (`ARCHITECTURE.md` §5) | None | Different dialect (C24) |
| R12 | `gsc-tool.exe` (23 MB) | Do not vendor. Generate token data locally, as `ARCHITECTURE.md` §7 requires | None | GPLv3 binary in the folder; our policy forbids distributing it |
| R13 | Root executables and `dist-*` executables | Do not run. If they must be assessed, build from source in an isolated checkout first | n/a | Untrusted binaries; `dist-cubemap` does not match the source |
| R14 | Sibling-tree dependencies (`..\IW8-zonetool`, DirectXTex headers) | Do not copy the layout | n/a | Not reproducible from the folder |

Questions to put to the owner before any reuse:
1. Which licence applies? The folder has none.
2. What is the source of the 128-light and 4096-surface limits?
3. Which `iw8-zonetool` binary and hash was used, and does `build-map` normalise CRLF and add the NUL?
4. Are spawn entity lines converted into `spawnList` by the ZoneTool? Has any spawn placement been confirmed in game?
5. What build do the BR tables come from?
6. Can the absolute paths and the author's profile path in the saved projects be scrubbed?

---

## 6. Red flags

- **No licence** in the folder. Reuse needs owner permission.
- **Bundled GPLv3 binary** (`gsc-tool.exe`), contrary to our ARCHITECTURE §7 policy.
- **Not reproducible from the folder.** The build needs a sibling `IW8-zonetool` tree, DirectXTex headers from it, and `D:\games\...` paths.
- **Absolute and personal paths** in `build-fastfile.cmd` and in saved projects (`.iw8map`, `.map`), including a texture path under a user profile.
- **README is stale or wrong in places:**
  - README says `.iw8map` up to version 8 is accepted. Code accepts 1 to 11 and writes 11 (`main.cpp:386-387`).
  - README says no external `iw8-zonetool.exe` is launched. `build-fastfile.cmd` launches one.
- **Size and hygiene.** Build junk (obj, pdb, a 76.6 MB `.ilk`, tlog) and duplicate outputs (`build-check\`, `x64\`, three copies of the BR tables and templates) make up most of the 340 MB. Single 2,981-line source file with long lines.
- **Claims with no source:** 128 point lights, 4096 render surfaces, "Replay" limits, "stock BR" behaviour.
- **Unverifiable executables:** three IW8 Radiant exes and four `dist-*` exes with no version resource and no build notes.
- **Test map is not stock content.** It uses an imported generated JPG and no stock art. Its "validated" output has not been shown to load in game (the logs check structure only).
- **Entity text not in stock form** (C2, C3, C4). This is the strongest technical concern for anyone reusing its output.

---

## 7. Limits of this review

- Not run, built, or loaded in game. The Replay exe was not opened and its MD5 was not computed.
- The ZoneTool sources are not in the folder, so conversions inside `build-map` (CRLF normalisation, spawn routing, NUL) could not be checked.
- The `dist-*` and root executables were not analysed beyond file metadata.
- BR claims are checked only for internal consistency, because our data contains no BR build.
