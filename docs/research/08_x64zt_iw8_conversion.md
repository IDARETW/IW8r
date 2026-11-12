# 08. x64-zt: IW8 (Replay 1.20) tooling, dump formats and the map extraction contract

Scope: the IW8 side of the x64-zt fork only. IW3 and IW7 are covered in section 9 for completeness. Read-only research. Nothing was built, converted, or run in the game.

Sources:
- Repo: `E:\mw124\x64-zt`, remote `fork` = `https://github.com/IDARETW/x64-zt.git`, branch `iw8-conversion`, HEAD `a9f39fc` ("Write IW8 fastfiles Oodle Kraken compressed", committed 2026-11-08 09:21:30 -0500). The working tree is clean.
- `build\bin\x64\Release\zonetool.exe`: 19,341,312 bytes, mtime 2026-11-08 09:17. This is about four minutes before HEAD's commit time, so the binary may not contain `a9f39fc`. UNKNOWN until rebuilt.
- Replay install: `E:\IW8\Builds\1.20-replay\game_dx12_ship_replay.exe`, 98,035,200 bytes, MD5 `1c238fe327f2ecc3b0db924c5b425439` (matches the owner's value). Its `zone\` folder holds 10,281 files.

---

## 1. Build (IW8 only)

Prerequisites: Windows, Visual Studio 2022 (MSBuild found through `vswhere`), git submodules.

```bat
:: from E:\mw124\x64-zt
generate.bat --games=iw8 --no-convert
"<MSBuild.exe>" build\x64-zt.sln -p:Configuration=Release -p:Platform=x64 -m -v:minimal -nologo
```

The same steps run in one command with `tools\build.bat iw8 --no-convert` (`tools/build.bat:7-13`).

- `generate.bat` runs `git submodule update --init --recursive`, then `tools\premake5 %* vs2022` (`generate.bat:2-3`). `--games` takes `all` (default), or a list such as `iw8`. `--copy-to=<folder>` copies the exe after the build.
- Converter projects `convert-<a>-<b>` build only when both games are selected (`AGENTS.md`, Build section). An IW8-only build needs no converter. `convert-iw7-iw8` also needs IW3 (`--games=iw3,iw7,iw8`).
- Output: `build\bin\x64\Release\zonetool.exe`. The existing build also holds `game-iw8.lib`, `core.lib`, `common.lib`, and `convert-iw3-iw8.lib`/`convert-iw7-iw8.lib`.

## 2. IW8 command line and setup

### Game folder

Resolution order (`src/cli/settings.cpp`, `select_game_path`):
1. `-path <folder>`, else `zonetool.json` `paths.iw8`.
2. The exe folder, if it contains `zone\`.
3. The Steam default (`game.steam_folder`, empty for IW8, so an error without (1) or (2)).

Example `zonetool.json` next to the exe:

```json
{ "game": "iw8", "paths": { "iw8": "E:/IW8/Builds/1.20-replay" } }
```

The IW8 Replay folder also needs `oo2core` for fastfile compression. The settings code points the Oodle lookup at the game folder (`settings.cpp`, `load_settings`).

### Commands (`src/cli/commands.cpp`)

| Command | Behaviour on IW8 |
|---|---|
| `zonetool.exe -game iw8 -path "E:/IW8/Builds/1.20-replay" dumpzone <zone>` | Writes one `.iw8` per asset to `dump\<zone>\` (cwd-relative, `filesystem.cpp:419`), plus `dump\<zone>\<zone>.csv`. |
| `dumpzone <zone> --target <game> [--opt val]...` | Converter path. For IW8 source, only converters from iw8 apply; none is in the IW8-only build. |
| `dumpasset <type> <name> <zone>` | Writes one asset to `dump\assets\<pool>\<name>.iw8` (`dump.cpp:3827-3870`, `set_dump_folder("assets")`). Type names are the CSV names, e.g. `material`, `image`, `xmodel`, `gfx_map`. |
| `verifyzone <zone>` / `verifyzone --all` | Reads every asset and checks the stream is consumed exactly. `IW8_REWRITE=<mode>` adds a round-trip check (section 3). |
| `inspect <zone>` / `inspect --all` | Container header and block data. |
| `listzone <zone>` / `--all` | Writes `asset_listing\iw8\<zone>.zone`. |
| `buildzone <zone>` | Reads `zone_source\<zone>.csv` (`build.cpp:88`) and asset files found through the search paths `zonetool\<zone>\` then `zonetool\` (`filesystem.cpp:334-335`). Writes `<game folder>\zone\<zone>.ff` (`build.cpp:363`, `get_output_path` returns the zone folder). It replaces a same-name zone. Back up stock zones first. |
| `refreshzone <deployed> <rebuilt> [out]` | Replaces assets in a deployed map folder from a rebuilt one. Used for live refresh (commit `6be57cd`). |
| `generatecsv <map> [sp] [--path <dir>]` | Generates a build CSV. |

Flags: `-game`, `-path`, `-target_path`, `-no_cache`. The README also lists `-dds` and `-dump_streamed_image`, but no code in the IW8 path reads them (grep of `src/cli`, `src/core`, `src/games/iw8` found nothing). Treat them as unavailable.

Environment variables used by the IW8 code:
- `IW8_REWRITE`: `dump`, `fresh`, `havok`, `convex`, `compound`, `fxshape`, `mesh`, `smodel`, `physics`, `physicscounts`, `compare`, `neutral`; the default is a plain rewrite (`dump.cpp:3899-3950`).
- `IW8_READ_TRACE=<file>`: one `A <type> <name>` line per asset during `verifyzone`.
- `NEUTRAL_OUT=<folder>`: writes neutral JSON during `verifyzone`. Only xanim is written (`dump.cpp:888-893`).
- `IW8_ZONETOOL_KEEP_SCRATCH`, `IW8_SCRIPTS_ONLY`, `IW8_GSC_TOOL`: converter-side, see `IW8_CONVERSION.md`.

### Round trip per asset type

Facts from code:
- The IW8 format table has 117 rows (`src/games/iw8/zone/asset_streams.cpp:10801-11042`). Six rows are null: `iesprofile`, `ui_map`, `aitype`, `mptype`, `character`, `xmodelalias`. The other 111 have `load`, `write`, `dump` and `parse` functions. This matches `tools/iw8_format.json` (111 assets).
- Each traversal is one template run by four streams (`zone/streams.hpp`), so `read()`, `write()`, `dump()` and `parse()` cannot disagree by construction.
- Six types use shared files instead of `.iw8` (`asset_files.cpp:300-314`): `localize` (`localizedstrings/<file>.str`), `rawfile` (raw path), `scriptfile` (`<name>.gscbin`), `stringtable`, `luafile`, `ttf`. A dump writes the shared file only when it holds the asset exactly. Builds read the shared file first.

Documented proof (`docs/IW8_CONVERSION.md`, Verification table):
- Each zone round-trips through `IW8_REWRITE=dump` (`verifyzone --all`).
- `dumpzone X`, `buildzone X`, `verifyzone X`, then `dumpzone` the built zone gives equal dumps, except pointer values.
- Good round-trip zones named by the docs: `mp_shipment`, `common_mp`.

Per-type proof: the docs do not list each type. The neutral families below have adapter gates: image, xanim, light_def and grading_clut, decal_volume_material, stream_key, xmodel and xmodel_surfs, rumble, text (`docs/NEUTRAL_ASSETS.md`). Ordinary `material` and `techset` are marked "open" in that document, so they are not neutral-proven.

Practical answer for a builder: dump, then build, is documented for whole zones. It is not documented per type. Each type needs `IW8_REWRITE=dump` plus a `buildzone` check on its own zone.

## 3. On-disk dump formats

### Container (`.iw8`)

Written by `finish()` in `src/games/iw8/zone/streams.hpp:382-415`. Header bytes, confirmed on real files:

```
offset 0  u32 magic    0x44385749  "IW8D"  (49 57 38 44)
offset 4  u32 version  1
offset 8  u32 type     asset type index (ASSET_TYPE_* order in asset_streams.cpp)
offset 12 u32 count    number of records
then per record: u32 kind (null, data, string, asset, script_string, runtime, temp, offset),
                 value, length-prefixed bytes (data, string, asset, script_string), or delta (offset)
```

Real examples (type index from table order):

| File | type | records | Note |
|---|---|---|---|
| `dump/assets/image/compass_map_mp_shipment.iw8` (699,413 B) | 0x13 = 19 `image` | 5 | Header 232 bytes, pixel data inline |
| `dump/assets/material/compass_map_mp_shipment.iw8` (888 B) | 0x0B = 11 `material` | 15 | Techset and image refs |
| `dump/assets/map_ents/maps/mp/mp_iw7_scorch.d3dbsp.iw8` | 0x1D = 29 `map_ents` | 2242 | Entity text verbatim (section 4) |

Sample files are in `E:\mw124\mw120rproxy\scratch\frontier-script-20261025\dump\assets\` (dump produced 2026-10-25 by the mw120rproxy pipeline, not by this checkout).

Folder names: `dump\<zone>\<pool>\<name>.iw8`, where `<pool>` is the asset format name (`iw8_file_path`, `asset_files.cpp:323-326`). The zone CSV lists `<pool>,<name>` for owned assets and `<pool>,,<name>` for references (`dump.cpp:3765-3776`).

Format rules: `dump_stream` writes header bytes as data, pointers as asset records or offsets, and inline arrays as data records. The record-kind enum width is not checked in this pass.

### Per-type content (field order from `asset_streams.cpp`)

- `gfx_map` (GfxWorld, 17,808 B header): `stream_gfxworld`. Surfaces, static models, cells, DPVS planes, lightmaps and probes, with image and material refs. Field-level dumps not read in this pass (UNKNOWN for exact order beyond the traversal).
- `col_map` (clipMap_t, 248 B): `stream_clipmap_t`. Brushes and planes. Havok data is stored as TAG0 tagfile bytes; `IW8_REWRITE=havok`, `physics`, `convex`, `mesh`, `compound` check their round trip (`IW8_CONVERSION.md`, Verification).
- `physicsasset`, `physicslibrary`, `physicsdebugdata`, `physicsfxshape`: Havok 2018.2 objects. Shape lists and one-body assets, plus `XModelDetailCollision` (40 B).
- `material` (120 B): techset ref, image refs, constants, sampler states. Stored as binary `.iw8`. Not JSON. The JSON material reader in `src/games/iw8/dump/read_material.cpp` reads an IW5-style source dump for conversion, not IW8 output.
- `image` (232 B): `stream_gfximage` (`asset_streams.cpp:2255-2267`). The packed atlas, fallback and resident pixel data are inline. Resident data is written only when flag bit 6 is zero. Not DDS. The DDS and `.iwi` readers in `dump/read_image.cpp` are for converting into IW8.
- `xmodel` (688 B) and `xmodelsurfs` (96 B): LOD and surface refs, bones, physics refs. Surfaces hold streamed vertex data. Neutral adapters exist (`neutral/xmodel.cpp`, `neutral/xmodel_surfs.cpp`), but `dumpasset` does not write JSON.
- `com_map` (ComWorld, 168 B): primary lights (`ComPrimaryLight`), change lists, and light structures. Neutral `light_def` adapter.
- `fx_map` (FxWorld, 15,568 B): effect definitions by ref, with `fx_map_trzone` (160 B) transient zones. `vfx` (ParticleSystemDef, 128 B) and `particlesimanimation` (120 B) for elements.
- `map_ents` (MapEnts, 1032 B): section 4.

Materials and models as a browser source: use the `.iw8` files and the readers. There is no JSON or DDS output from `dumpasset` for these types.

## 4. MapEnts (`map_ents`) dump format

Source: `stream_mapents`, `asset_streams.cpp:3486-3530`; struct `MapEnts`, `src/games/iw8/structs.hpp:10929`.

Ordered members in the dump:
1. `name` (string).
2. `entityString`: `numEntityChars` bytes of raw entity text. Stored verbatim, as in the `.iw8` sample. Syntax is the standard CoD entity text: `{ "key" "value" ... }` blocks, one per entity.
3. `trigger` (`MapTriggers`): counts and arrays of `TriggerModel`, `TriggerHull`, `TriggerSlab`, `TriggerWinding`, `TriggerWindingPoint`.
4. `clientTrigger` (`ClientTriggers`) and `clientTriggerBlend`: client-side audio and blend volumes.
5. `spawnList` (`SpawnPointRecordList`): `spawnsCount` (u16) then records. Each `SpawnPointEntityRecord` holds `index` (u16), `name`, `target`, `script_noteworthy` (script strings), `origin` (vec3), `angles` (vec3).
6. `splineList` (`SplineRecordList`): `splineCount` (u16), then `SplinePointRecordList` with `splinePointCount` (u16), `splineLength` (float), and points. Each `SplinePointEntityRecord` holds `splineId`, `splineNodeId`, `splineNodeLabel`, `targetname`, `target`, `string`, `speed`, `splineNodeTension`, `origin`, and more fields.
7. `havokEntsShapeData` (blob, `havokEntsShapeDataSize` bytes).
8. `cmodels` (`cmodel_t`: bounds, radius, `PhysicsAsset*`, and more), `edgeLists` (`MapEdgeList` asset refs), `clientModels`, dynamic-entity counts and lists, and spatial partition trees.

Example content: the scorch `.iw8` contains 547 `spawn` strings, 90 `spawn_allies_start`, 44 `spawn_attacker`, and so on, as plain ASCII. A tool can read the spawn and trigger text out of the dump without the zone parser.

The neutral schema is `src/core/assets/mapents_data.hpp` (the `variable_type_names` table is GSC variable types, not entity keys). The native-to-neutral mapping is `src/games/iw8/neutral/mapents.cpp`. Its spec is `tools/neutral_families/mapents.json`. Neutral mapents are not on the `NEUTRAL_OUT` writer list, so no JSON appears by default.

Parser helpers in the code: `src/games/iw8/dump/maps.cpp:92` `parseEntities()` splits the entity text with the regex `\{([^{}]*)\}` (`maps.cpp:159`). This is the IW5-era reader used when converting maps.

## 5. The map extraction (the IW8 map builder's input)

This is the layout that an IW3 or IW7 front end writes and that the IW8 map pipeline reads. A native IW8 brush compiler would target this layout directly.

### Contract

- Folder with a `complete` marker file. `PrepareFastfile` requires `cacheDirectory/complete` (`src/convert/iw3_iw8/iw3_fastfile.cpp:10427-10450`). The IW7 front end writes it through `write_marker` (`src/convert/iw7_iw8/convert.cpp`). The IW7 sample in section 5.3 has no `complete` file, so it is incomplete.
- Subfolders: `extracted/` (the data), `scripts/` (gscbin, decompiled, log), `skeleton/` (xmodel, xmodel_export).
- Exactly one `*.replay-world.json`, one `*.replay-collision.json` and one `*.replay-comworld.json` must exist under `extracted/`. `FindSingleExport` enforces this (`iw3_fastfile.cpp:10470-10472`). The map name is the world file's stem minus `.d3dbsp` and `.replay-world.json`.

### Files (from `E:\mw124-conversion-builds\zslK\mp_iw7_frontier\output\.iw7-extract\mp_frontier\`, IW7 front end, written 2026-11-07)

| Path under `extracted/` | Size (sample) | Role |
|---|---|---|
| `maps/mp/<map>.d3dbsp.replay-world.json` | 290 MB | Surfaces: `surfaces[]` with `material`, `lightmap`, `reflection_probe`, and `vertices[]` (position, normal, tangent, binormal_sign, uv, lightmap_uv, color) |
| `maps/mp/<map>.d3dbsp.replay-collision.json` | 24.7 MB | `brushes[]`: `mins`, `maxs`, `contents`, `surface_type`, `surface_flags`, `planes[[a,b,c,d]]`, `side_materials`, `axial_materials`, `ladder_planes` |
| `maps/mp/<map>.d3dbsp.replay-comworld.json` | 128 KB | `ies_profiles[]` (definition, profile, texels), `primary_lights[]`, `in_use`, `name` |
| `maps/mp/<map>.d3dbsp.lightgrid.json` | 638 B | Metadata for `.colors.bin`, `.entries.bin`, `.lightgrid.raw.bin`, `.rows.bin`, `.sh.bin` (`schema: 1`, `row_axis`, `col_axis`, `mins`, `maxs`, `entry_count`, `sun_primary_light_index`) |
| `maps/clienttriggers.json` | 27 KB | `map`, `triggers[]` (audio, hulls with halfSize/midPoint/slabs, origin, priority, scriptDelay, type), `blend_nodes`, `pa_speakers` |
| `maps/createfx/`, `maps/createart/` | | Script-side effect and art placements |
| `sun/<map>.sun` | 468 B | Sun and flare settings as `r_*` key-value lines |
| `xmodel/<name>.replay.json` | 0.2 to 2.3 MB each | One model: geometry, LODs, bones, physics |
| `images/*.dds`, `images/*.iwi` | | Images (1,096 in the sample) |
| `materials/*.material.json`, `techsets/*.techset.json`, `techniques/`, `replay_shaders/`, `shader_bin/` | | Material and shader sources |
| `vision/`, `lights/`, `fx/`, `impactfx/`, `physic/`, `sound/`, `soundaliases/`, `mp/mapstable.csv`, `zone_source/` | | Vision (fog, grading), lights, effects, physics, audio |

Other keys seen in the IW3 converter (`convert.cpp`, `iw3_fastfile.cpp`): `<map>_iw8_ents.txt`, `<map>.lightgrid.json`, `<map>.lighting.json`, `<map>.render.json`, `<map>.vision`, `<map>.d3dbsp.bounds.json`, `map.json`, `.gscbin`. The entity text file `<map>_iw8_ents.txt` is read from the dump folder or its parent (`convert.cpp:2207-2208`). No producer for it exists in `src/`. Its producer is UNKNOWN.

Output of a build (`docs/IW8_CONVERSION.md:16-19`): `output/<map>/` with `<map>.ff`, `techsets_<map>.ff`, `eng_<map>.ff`, `ww_<map>.ff`, `srv_<map>.ff`, and `map.json`. Two ledgers sit beside the folder: `<map>.iw3-assets.tsv` and `<map>.iw8-assets.tsv`. Build logs confirm the same five zones and the phrases "passes structural checks" and "published validated map output".

### Entry points (C++)

Call path:
1. `zonetool::convert::iw3_iw8::convert_extraction(zone_path, extraction, conversion)` (`src/convert/iw3_iw8/convert.cpp:2711-2715`). It calls `convert_map(zone_path, conversion, extraction)`.
2. `convert_map` (`convert.cpp:2586`) builds an `Args` with `command = "build-iw3"` and the zone path as positional. It sets `ImportOptions.extraction` (`convert.cpp:94`, `2506`).
3. `PrepareFastfile(const ImportOptions&)` (`iw3_fastfile.cpp:10427`). With an external extraction it reads the folder in place and does not copy it (`iw3_fastfile.cpp:10455-10461`). It rejects an input that is not a readable `.ff` file (`iw3_fastfile.cpp:10432`).
4. The map pipeline in `src/convert/iw3_iw8/` runs layer by layer (clipmap, comworld, gfxworld, mapents, xmodel, xanim, material, image, lightdef, fx, impactfx, sound, loadedsound, soundcurve, rawfile; `IW8_CONVERSION.md:49-50`).
5. The IW8 zone writer in `src/games/iw8/zone/writer.cpp` and `maps_write.h` writes the zones. ACTS then validates them.

For a native editor: it must still pass a readable `.ff` path as `zone_path`, because step 3 checks it. Whether the editor can avoid a real `.ff` is UNKNOWN without changing that check.

### Options (`docs/IW8_CONVERSION.md:23-43`)

`--map`, `--style iw3|iw8`, `--replay`, `--metadata <json>`, `--acts`, `--stock_cache`, `--havok native|native-check|exe`, `--detail_collision`, `--bc7_images`, `--writer x64-zt|iw8-zonetool`, `--dump 1`, `--navigation 0`, `--iw3_mode_assets`. Options reach the converter as the `conversion.options` string map (`zone_commands.cpp`, `convert_zone`).

### Limits relevant to a native compiler

`IW8_CONVERSION.md` lists one- to two-sentence facts per feature, most of them engine rules (light volume capacity, sky depth, reactive motion). They are not rewritten here. The useful points for a builder are: lights need at least 256 primary slots (`IW8_CONVERSION.md` Engine facts, light-volume capacity); the sky box is six 32768-unit faces (sky depth); compressed meshes drop zero-area triangles (compressed meshes).

## 6. Launch and install

- `buildzone` writes into the game's `zone\` folder (section 2). The converter writes to `output\` instead. Copy `output\<map>\*.ff` into `E:\IW8\Builds\1.20-replay\zone\` to install. The docs do not name this copy step; treat it as UNKNOWN.
- Launch: `E:\IW8\Builds\1.20-replay\game_dx12_ship_replay.exe`. `AGENTS.md` names the in-game `map <name>` and `devmap <name>` commands. Launcher scripts exist in the Replay folder (`Run_Optimized_Replay.cmd`, `Run_Recompiled_Replay.cmd`, `Run_Replay_DW.cmd`); they were not read.
- Crash dumps: `%LOCALAPPDATA%\CrashDumps` and `minidumps\` in the game folder (`IW8_CONVERSION.md`, Engine facts, crash dumps).

## 7. In-game evidence

No in-game result was found for any x64-zt conversion. All recorded results are build-time:

| Map | Log (under `E:\mw124-conversion-builds\`) | Last entry | Result |
|---|---|---|---|
| mp_nuketown | `x64zt_r\nuketown_iw8-r5.log`, `-r6.log`, `-r7.log` | 2026-10-21 09:54, 10-21 21:14, 10-22 00:18 | "published validated map output"; ACTS structural pass |
| mp_discovery_8 | `x64zt_r\discovery_iw8-r5.log`, `-r6.log`, `-r7.log` | 2026-10-21 09:58, 10-21 21:16, 10-22 00:20 | Same |
| mp_broadcast_8 (broadcast2) | `x64zt_s\broadcast2.log` | 2026-10-22 07:44 | Same |
| agroprom | `x64zt_s\agroprom.log` | 2026-10-22 06:43 | Same |
| arnhem | `x64zt_s\arnhem.log` | 2026-10-22 07:41 | Same |
| greenband | `x64zt_s\greenband.log` | 2026-10-22 07:08 | Same |
| gulag | `x64zt_s\gulag.log` | 2026-10-22 07:03 | Same |
| aerodrome | `x64zt_s\aerodrome.log` | 2026-10-23 06:59 | No "published" line: not produced |

- Duration: the logs carry phase timings but no total. The IW8 docs give warm-cache times (Nuketown ~29 s for IW3, Carnage ~50 s for IW7), not measured here.
- The output folders named in the logs (for example `x64zt_r\output\mp_nuketown`) no longer exist on disk.
- No zone named after a converted map is in the Replay `zone\` folder (name check for nuketown, broadcast, aerodrome, greenband, agroprom, arnhem, gulag, discovery found only `dw_gulag_nav_*` and `common_br_mp`).
- Status for every map above: ACTS (build-time loader) only. In-game load: NOT-VERIFIED.

## 8. Using x64-zt as an IW8 asset reader

Recommended pipeline for a material or model browser:
1. `dumpzone <zone>` on a stock zone (for example `mp_shipment` or `common_mp`), or `dumpasset <type> <name> <zone>` for one asset.
2. Read `dump\<zone>\<pool>\<name>.iw8` (or `dump\assets\<pool>\...`) with a reader that follows the header in section 3. `parse()` is the reference, but it lives in the zonetool code (`zone/streams.hpp`, `parse_stream`).
3. Shared-file types (strings, scripts, localize) are plain text files, not `.iw8`.
4. Images need pixel decoding from the `.iw8` record. The DDS path is not wired in the IW8 dump.

The current IW8 readers do not write JSON or DDS. A browser that wants JSON or DDS needs its own exporter on top of the `.iw8` reader or the neutral families.

## 9. IW3 and IW7 (one paragraph)

The IW3 and IW7 front ends write the extraction in section 5. The IW3 side needs OpenAssetTools' Unlinker, found at `E:\mw124\mw120rproxy\mw120rproxy\tools\_vendor\oat-release\Unlinker.exe`. ACTS is at `E:\mw124\atian-cod-tools\build\bin\Release\acts.exe`. No built `gsc-tool.exe` is on disk. Source is in `E:\mw124\x64-zt\deps\gsc-tool`. The archives are `E:\mw124\sharables\mw2019-asset-tools-retrieved\05-iw8-script-compiler\gsc-tool-1.6.2-windows-x64.zip` and `...\16-replay-workstation-dependencies\gsc-tool-1.5.1-windows-x64.zip` (not extracted or run). The IW7 path reads the game folder's zones and paks directly (`IW8_CONVERSION.md`, IW7 maps). The owner has scoped IW3 and IW7 out of the editor, so none of this is needed for it.

## 10. Comparison with `E:\mw124\mw120rproxy\iw8-zonetool` (not examined)

Not examined in this pass. The only link found is `--writer iw8-zonetool` in x64-zt (`IW8_CONVERSION.md:36`), which lays out zones the way that tool does so the two outputs can be compared byte by byte.

## 11. UNKNOWNs

1. Whether `zonetool.exe` at 09:17 includes commit `a9f39fc` (09:21). Rebuild and compare.
2. Per-type dump-to-build round trip for each of the 111 streamed types. Only whole-zone claims are documented.
3. Exact field widths of the record `kind` enum in `.iw8` dumps (`streams.hpp:396`).
4. Field-level layout of `gfx_map`, `com_map`, `fx_map` dumps beyond the traversal order.
5. Producer of `<map>_iw8_ents.txt` (read at `convert.cpp:2207`, not written in `src/`).
6. Whether `convert_extraction` callers can supply a non-`.ff` `zone_path` (`iw3_fastfile.cpp:10432` requires a readable `.ff`).
7. Whether the IW7 extraction sample is complete (no `complete` marker).
8. The step that copies `dump\<zone>\` into `zonetool\<zone>\` before `buildzone`. The docs and the search paths disagree, and no step is written down.
9. Whether `-dds` and `-dump_streamed_image` ever worked in v2 (they are in the README, but no code reads them).
10. Install and launch steps for converter output (copy target and the `map` command). Not verified in game.
11. Any in-game load of an x64-zt map: NOT-VERIFIED for every map in section 7.
12. Whether `refreshzone` (commit `6be57cd`) is used by the editor.
