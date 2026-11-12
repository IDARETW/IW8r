# 04 - IW8 stock asset access for the Replay 1.20 map editor

Target: MW2019 "Replay" 1.20.4.7623265, `E:\IW8\Builds\1.20-replay\game_dx12_ship_replay.exe` (98,035,200 bytes, present).
Oodle DLL: `E:\IW8\Builds\1.20-replay\oo2core_7_win64.dll` (present).

Method: read-only survey. No executable was built or run. Binaries were only checked with `grep -c` for CLI strings. Claims below cite file:line where a source line was read. Anything not directly observed is marked UNKNOWN.

## Summary

1. **Only one tool reads stock Replay 1.20 fastfiles across the asset families the editor needs: ACTS (`atian-cod-tools`, `fastfile -r mw19replay`).** It loads all 111 enabled Replay types and writes DDS (images), structured `.asset.json` (materials, xmodels, FX, map entities, etc.), GLB (xmodel base surfaces with `--geometry`), and SAB (sound banks). Samples of its output are on disk.
2. **The mw19 viewers do not add a reader.** Both are web frontends that shell out to that same ACTS binary (`extractions.py:246`, `:335-364`).
3. **For writing, no single tool covers everything.** ACTS can only write 8 pool types, unsigned. `iw8-zonetool-native` writes image, material, xmodel and xmodelsurfs from dumps, plus the map family. `mw120rproxy/iw8-zonetool` writes Replay 0x0FF7 for a wider set (rawfile, localize, stringtable, netconststrings, scriptfile, physicsasset, col_map, com_map, mapents, native_material, and more) from a neutral JSON bundle.
4. **Sound and FX writers do not exist in any surveyed tool** (UNKNOWN for generic FX; sound writes limited to hash lists).
5. **Not usable for Replay 1.20:** `E:\IW8\Cascview` is the 1.64 game folder with its game data removed (no CASCView binary); `tools\ff_grep.py` points at 1.64; the `joelrau_*` repos have no IW8 support.

## 1. Stock fastfile inventory (Replay 1.20 zone folder)

Folder: `E:\IW8\Builds\1.20-replay\zone` (PowerShell `Get-ChildItem -Recurse`, run 2026-11-08).

| Item | Value |
|---|---|
| Total files | 10,996 (91,627,614,766 bytes, about 85 GiB) |
| `.ff` files | 9,646 total, 10,605,382,294 bytes (9.88 GiB) |
| `.ff` at zone root | 9,450; the rest are language/region subfolders (`english` 58, `worldwide` 57, `china` 15, `saudiarabia` 15, `japan` 15, `korea` 15, plus single-file locale folders) |
| Other extensions | `.xpak` 584, `.sabs` 441, `.fp` 317 (patch files), `.json` 6, `.rar` 1 (a 687 MB archive of the game exe, not a zone) |
| Largest `.ff` | `mp_donetsk_cg_ls_tr.ff` 1,022,394,312 B; `common_mp.ff` 746,619,016 B; `mp_donetsk.ff` 364,593,288 B |
| 10 example names (alphabetical) | `_dw_bot_styles_mp_0155601d9bc14b2c.ff`, `_dw_bot_styles_mp_3204f8ea76ff4385.ff`, `_dw_bot_styles_mp_56124a7c5634a296.ff`, `_dw_bot_styles_mp_7313eaf03ef99d31.ff`, `_dw_bot_styles_mp_dc7a04c95e57c55a.ff`, `_dw_bot_styles_mp_dcf9bf280f3c1149.ff`, `_dw_custom_operators_31f863513486abfc.ff`, `_dw_custom_operators_a55437fc15603b1f.ff`, `_dw_custom_operators_bef9d4dc33019086.ff`, `_dw_custom_operators_c687e1d0e5337738.ff` |
| Named stock zones (also present) | `common_mp.ff`, `mp_donetsk.ff`, `mp_br_quarry.ff`, `techsets_iw8_cw_raygun.ff`, `mp_donetsk_cg_ls_tr.ff` |

Verified fastfile version: the ACTS manifest for `eng_mp_shipment` (`_stock_cache\eng_mp_shipment-C4931AF36064\...\manifest.json`) reports `"xfile_version": 4087` (0xFF7) and `"profile": "replay-1.20"`. The 0xFF7 value matches `atian-cod-tools\docs\mw19\fastfile.md`.

Note: `E:\IW8\Builds\1.20-replay\replay-compiler-frontend-dw-review\zone` is a separate zone tree and was not counted.

## 2. Discovery TSV

File: `E:\mw124-conversion-builds\discovery-zt-107.iw8-assets.tsv` (713,655 bytes, ASCII, tab-separated).

- Columns (4): `zone`, `type`, `name`, `origin`.
- Rows: 5,925 data rows (5,926 lines including the header). Every row has 4 fields.
- Zones: `mp_discovery` 5,005; `techsets_mp_discovery` 909; `eng_mp_discovery` 9; `ww_mp_discovery` 2.
- Asset types present (32): xmodelsurfs 2,102; material 1,280; image 1,017; vfx 449; xmodel 382; physicsasset 340; techset 100; vertexshader 78; decalvolumematerial 76; net_const_strings 41; pixelshader 15; physicsfxshape 8; localize 8; physicsfxpipeline 5; lightdef 5; rawfile 3; then 1 each of vectorfield, tacgraph, surfacefx, streamkey, soundbank, scriptfile, navmesh, map_ents, impactfx, glass_map, gfx_map, fx_map, edgelist, com_map, col_map, aipaths.
- Origins: 1,120 "generated material variant"; 267 "stock effect of surface-fx table void (copied from mp_shipment)"; 131 "IW3 effect"; 41 "level string tables"; the rest are "name-only reference to the common-zone asset".
- This is a manifest of a converted discovery zone, not a stock inventory. Its type column also uses `net_const_strings`, which differs from the pool name `netconststrings`.

5 example rows:
```
mp_discovery	image	mw120r/mp_discovery_0_f0344dc6c9add064	texture of material w/mw120r_mp_discovery (generated variant)
mp_discovery	image	mw120r/mp_discovery_1_e6fd95191728ea40	texture of material w/mw120r_mp_discovery (generated variant)
mp_discovery	image	mw120r/mp_discovery_2_483be94c63ab661d	texture of material w/mw120r_mp_discovery (generated variant)
mp_discovery	image	mw120r/mp_discovery_sun_depth	texture of material w/mw120r_mp_discovery (generated variant)
mp_discovery	image	mw120r/mp_discovery_lightmap_primary	texture of material w/mw120r_mp_discovery (generated variant)
```

## 3. Tool-by-tool survey

Read = can produce data from stock Replay 1.20 `.ff` files. Write = can emit a fastfile or asset file for Replay 1.20. "Sample" lists output already on disk.

### 3.1 atian-cod-tools (ACTS) - the reader

- Path: `E:\mw124\atian-cod-tools`. Language: C++ (CMake, ANTLR grammar). Fork: IDARETW `codex/mw2019-complete`, per the viewer READMEs.
- License: `E:\mw124\atian-cod-tools\LICENSE.md` - GPL-3 or MIT (dual).
- Built binary: `E:\mw124\atian-cod-tools\build\bin\Release\acts.exe` (599,552 B, Sep 17). Handlers are in `build\bin\Release\acts-common.dll`: the string `mw19replay` appears 3 times there and 0 times in `acts.exe` itself (grep check only).
- Target claim: "Modern Warfare 2019 (1.20-replay)", `docs/mw19/fastfile.md`. Also claims unversioned MW19 fastfiles (`mw19`, `mw19pools`).
- READ from stock Replay 1.20 `.ff`:
  - All 111 enabled types (`README.md` "MW2019 Replay additions in this fork", `docs/mw19/fastfile.md`).
  - Images to DDS (DX10 header), with resident pixels.
  - Materials, xmodel, xmodelsurfs, FX, sound, map entity and world types as structured `.asset.json`.
  - Xmodel base surfaces to GLB with `--geometry` (`docs/mw19/fastfile.md` "Export files"; positions, normals, UVs, triangles, no LODs or textures).
  - Sound banks as SAB bytes (`soundbank`, `soundbanktransient`); `mw19_sound.cpp`, `mw19_sound_cli.cpp` exist.
  - Requires the Replay exe (`-g`) and Oodle DLL (`--oodle`); `--xpak` for streamed payloads.
  - Validation: Shipment consumed 385,140,167 serialized bytes and loaded 10,478 assets; 122 samples across 56 types with zero failures (`docs/mw19/parser-validation.md`, "Bounded stock checks").
- WRITE: fastfile linker `fastfilelinker` (`linker.md`), `IW8`/`MW19` linker names, `linker_mw19.cpp:24` (class `IW8Linker`), `:155` registration. Only 8 pool types: rawfile, luafile, ttf, scriptfile (GSCBIN input), localize, stringtable, netconststrings, soundbanklist. Unsigned `IWffc100`, stored or LZ4. "Live unsigned-fastfile acceptance... remain unfinished" (`linker.md`). No image, model, material or world writer.
- Entry points / CLI:
  - Export: `acts.exe --noUpdater fastfile -r mw19replay -g <game.exe> --oodle <dll> [--xpak X] [--geometry] [-a type,type] [-n names] [--test --limit-per-pool N] -o <outdir> <zone.ff>` (`fastfile.md`, viewer `extractions.py:246-253`).
  - Handler registration: `src/core/acts/tools/fastfile/handlers/handler_game_mw19_replay.cpp:761` (name "Modern Warfare 2019 1.20 Replay"); requires `-g` at `:770`; output root `mw19replay/<zone>` at `:874`.
  - Pool dumper (process/XPak, live game): `mw19pools` at `src/core/acts/tools/mw19/mw19_pools.cpp:76`, ADD_TOOL at `:604`.
  - Linker: `src/core/acts/tools/fastfile/linkers/linker_mw19.cpp:24`.
- Sample output on disk (ACTS exports made with this tool):
  - `E:\mw124-conversion-builds\_stock_ship_images\mw19replay\mp_shipment\assets\image\` - 220 DDS files, 34,556,846 B (e.g. `$outdoor.0.dds`, `al_qatala_buckle_big_a_black_thermalmap.23.dds`).
  - `E:\mw124-conversion-builds\_stock_backlot_xmodels\mw19replay\mp_backlot2\assets\xmodel\` - 1,059 `.asset.json` (e.g. `automotive_truck_loading_ramp_short.0.asset.json`, format `mw19-asset-json`, profile `replay-1.20`).
  - `E:\mw124-conversion-builds\_stock_cache\mp_shipment-01F60FCAD157\...\assets\xmodelsurfs\` - 2 JSON files; no GLB anywhere in the build folder (UNKNOWN whether `--geometry` GLBs were ever generated for stock).
  - `E:\mw124-conversion-builds\_ents_stock\hideout\...\map_ents\` (mp_hideout) and `_ents_nk\...\map_ents\` (mp_nuketown) - `map_ents`, `script_strings.json`, `manifest.json`.
  - `E:\mw124-conversion-builds\_vfx_nk\mw19replay\mp_nuketown\assets\vfx\` - 409 `.asset.json` VFX records.
  - `E:\mw124-conversion-builds\_stock_ship_nav\...\` - `aipaths`, `navmesh`, `tacgraph` JSON.
  - `E:\mw124-conversion-builds\_stock_backlot_glass\...\` - `glass_map`, `fx_map`, `physicsfxshape` JSON.
  - `E:\mw124-conversion-builds\output_ff\mw19replay\eng_mp_shipment\assets\soundbank\mp_shipment.english.10.sabl` - 1 sound bank file (extension `.sabl`, not `.sab`; format UNKNOWN).
  - Material JSON: UNKNOWN (no `assets\material` folder found in the sample trees).

### 3.2 mw19-fastfile-viewer (Node/Vite frontend + Python service)

- Path: `E:\mw124\mw19-fastfile-viewer`. Language: JavaScript (Three.js, fflate, texture2ddecoder-wasm) and Python 3.12+ (`scripts/`). Serves at 127.0.0.1:48120.
- License: none at repo root. `THIRD_PARTY_NOTICES.md` only (MIT/Apache deps).
- Built binary: none (no exe). Built frontend: `E:\mw124\mw19-fastfile-viewer\dist\index.html` (13,151 B) and `dist\wasm\`.
- Target claim: "MW2019 1.20 Replay" (`README.md`).
- READ: all of the above through ACTS `-r mw19replay`. It displays DDS, GLB/OBJ, material-linked base-color and cutout textures, supported audio, GSC source, JSON and hex. Scene reconstruction covers `gfx_map` and `gfx_map_trzone` (`scripts/scenes.py`).
- WRITE: none to fastfiles. Writes only extraction caches and preview files.
- Entry points: `scripts/extractions.py:245-253` (ACTS command builder, `--geometry`), `:335-340` (material-only `-a material`), `:353-364` (image `-a image -n`); `scripts/materials.py:22` `catalogue()`; `scripts/scenes.py:243` `save_scenes()`; `scripts/serve.py:361` `main()`.
- Sample output on disk: none in `dist` or `.local` (`.local\` holds a STATUS.md and two PNG screenshots). The STATUS note says `code_pre_gfx.ff` produced 43 assets (41 rawfile, 1 keyvaluepairs, 1 dlogschema). Cache path D:\MW19FastfileViewerCache is absent on this machine.

### 3.3 mw19-replay-fastfile-viewer ("IW8 FF Explorer")

- Path: `E:\mw124\mw19-replay-fastfile-viewer`. Same stack and same ACTS dependency as 3.2. Its README title is "IW8 FF Explorer"; the body is the same.
- License: none at repo root (`THIRD_PARTY_NOTICES.md` only). Built binary: none. Built frontend: `dist\index.html` (13,151 B).
- Target: "Modern Warfare 2019 1.20 Replay". READ and WRITE as in 3.2. Command builder: `scripts/extractions.py:245-253`. Entry: `scripts/serve.py:361`. Also has `scripts/configure-replay.ps1`, `start-local.ps1`.
- Sample output: `.local\desktop-mp-shipment-scene.png` and `mobile-mp-shipment-scene.png` (screenshots only). The viewer's own extraction outputs are not in the repo.

### 3.4 iw8zonewriter (standalone IW8 1.24 .ff writer)

- Path: `E:\mw124\iw8zonewriter`. Language: C++ (`build.bat`, VS18). License: none.
- Built binary: none. Only `main.obj` (168,284 B) is present. The exe is not on disk.
- Target claim: "MW2019 (1.24)" (`README.md`). Conflicts with the 1.20 target of the other repos; UNKNOWN which build the owner intends to load.
- READ: none. Only container header decoding in documentation.
- WRITE: one asset type, `rawfile` (name-only branding stub, `main.cpp:75` `ASSET_TYPE_RAWFILE`). Emits a 245-byte `mp_test_load.ff` in the `IWffa100` v11 container. Its own README says the in-game load test is still open ("REMAINING for M-B1").
- Docs: `ZONE_LOAD_MODEL.md` (dev.i64 blueprint: tagged pointer sentinels -1/-2/-3, 11 XFile blocks).
- Sample output: none apart from the stub format in the README.

### 3.5 iw8-zonetool (root, IW3 to IW8 converter prototype)

- Path: `E:\mw124\iw8-zonetool`. Language: C++ (C++20, XMake or `build.bat`) plus Python helpers in `tools\` (`import_map.py`). License: none.
- Built binary: `E:\mw124\iw8-zonetool\iw8-zonetool.exe` (891,904 B, Sep 9) and `xmake-out\x64\Release\iw8-zonetool.exe` (Sep 9/10 builds). Verified CLI strings `fromdump`, `formats` present.
- Target claim: `README.md` says "MW2019 1.20 Replay ... local 1.20 Replay / mw120rproxy custom-map pipeline". `STATUS.md` (2026-06-14) also says the 1.24 container format is used.
- READ from stock IW8: **no**. `src\main.cpp:223-227` `cmd_read()` refuses: "raw IW3 fastfile input is not supported". The IW3 reader (`src\common\ff_io.cpp`, `iw3_parse`) reads CoD4 `IWffu100` only. `inspect` reads IW8 headers only.
- READ other: IW3/IW4/IW5 dumps and Quake/Source/OBJ/glTF maps (`README.md` "Supported inputs").
- WRITE: IW8 zones for map families from IW3 dumps (`src\iw8\asset_material.cpp`, `asset_xmodel.cpp`, `asset_image.cpp`, `asset_maps.cpp`). Writes the 4 zones (`mp_test`, `eng_mp_test`, `srv_mp_test`, `ww_mp_test`) with Oodle or stored bodies (`STATUS.md`).
- CLI: `iw8-zonetool import <src> <mapid> -o <new dir> [--replay exe] [--graybox]`; `iw8-zonetool formats`; `fromdump`; `inspect`; `validate-package` (dispatch `src\main.cpp:604-612`; `src\import_cli.cpp:43`).
- Sample output: `E:\mw124\iw8-zonetool\_mp_test_zone.bin` (39,707,014 B, decompressed IW3 `mp_test.ff`), `_mp_test_raw.bin`, `out_*` folders (not inspected in detail). Replay output sample: UNKNOWN.

### 3.6 iw8-zonetool-native (Replay 1.20 writer, dump input)

- Path: `E:\mw124\iw8-zonetool-native`. Language: C++ (single x64 exe). License: "no project license" (`README.md`). Third party: `THIRD_PARTY.md` (nlohmann/json, MIT).
- Built binary: `E:\mw124\iw8-zonetool-native\xmake-out\x64\Release\iw8-zonetool.exe` (1,138,176 B, Sep 10).
- Target claim: "1.20.4 Replay executable used by mw120rproxy" (`README.md`).
- READ from stock: **no**. Input is a prepared dump folder (`docs\INPUT_FORMAT.md`): `dumpsrc\read_xmodel.cpp`, `read_material.cpp`, `read_image.cpp`, `read_xsurface.cpp` parse dumps only. No `.ff` reader.
- WRITE (Replay 1.20 fastfile output, 5 files per map):
  - `addImageAsset` - `src\zonetool\iw8\write_image.cpp:140` (GfxImage, `ASSET_TYPE_IMAGE`).
  - `writeMaterial` - `src\zonetool\iw8\write_material.cpp:21` (`ASSET_TYPE_MATERIAL`).
  - `addXModelFromDump` - `src\zonetool\iw8\write_xmodel.cpp:153` (`ASSET_TYPE_XMODEL`).
  - `writeXModelSurfsFromDump` - `src\zonetool\iw8\write_xsurface.cpp:163` (`ASSET_TYPE_XMODELSURFS`).
  - Map family: `emitGfxMapBody` `src\zonetool\iw8\write_maps.cpp:62`, `emitGlassMapBody` `:34`; com_map, col_map, map_ents (`map_zone.h`, `maps_write.h`); Havok collision via `--replay` (`replay_havok.cpp`); lightgrid, impact data (`replay_lightgrid.cpp`, `replay_impact.cpp`).
  - Other ASSET_TYPE references (techset, lightdef, impactfx, fx_map, surfacefx) appear only in other files; whether they are emitted is UNKNOWN.
- CLI: `iw8-zonetool.exe build-map <dump> <mp_id> -o <new dir> [--replay exe] [--collision] [--footsteps] [--sun-intensity-scale N]`; `validate-package`; `inspect` (`src\zonetool\main.cpp:686-694`). Output: `mp_x.ff`, `srv_`, `eng_`, `ww_`, `techsets_` (exactly 5 files).
- Sample output: none found in the repo (`out` and `xmake-out` only). UNKNOWN.

### 3.7 mw120rproxy / iw8-zonetool (largest Replay 1.20 writer and the stock copier)

- Path: `E:\mw124\mw120rproxy\iw8-zonetool` (the `src` folder the user named is `E:\mw124\mw120rproxy\iw8-zonetool\src`). Language: C++ (XMake), with PowerShell scripts. License: none found. `THIRD_PARTY.md`.
- Built binary: `E:\mw124\mw120rproxy\iw8-zonetool\xmake-out\x64\Release\iw8-zonetool.exe` (14,365,696 B, Oct 26). A Debug build also exists. Verified CLI strings `decompress-iw8` and `convert-assets`.
- Target claim: "IW8 asset conversion tool targeting ... build 1.20 Replay" (`README.md`), with a neutral-bundle contract (`docs/ASSET_CONVERSION.md`).
- READ from stock Replay 1.20:
  - Container only: `inspect-iw8` (`src\zonetool\main.cpp:2958`) reads the header. `decompress-iw8` (`:2962`) decompresses stored, LZ4 or Oodle blocks for profile 0x0FF7 (`src\zonetool\iw8\mw19_fastfile.cpp:686` `decodeMw19Fastfile`, `:851` `decompressMw19Fastfile`). It does not parse assets (`docs/ASSET_CONVERSION.md`: "asset-body decoding is not" implemented for 0x0FF7).
  - Stock asset data via ACTS: `src\zonetool\iw8\replay_stock.cpp` `Copier` (class `replay_stock.h:26`, constructor `replay_stock.cpp:1135`). It runs ACTS exports of `mp_shipment` and its `eng_`/`ww_` companions into a cache, then reads xmodel, the `void` surface-effect table with its effects, physics shapes and models, and CODCaster images. `RegisterSurfaceEffects` is at `:1399`, `RegisterEnglishImages` at `:1444`.
  - Odin 1.69 and other source-build readers (`mw19_*_native.cpp`, `fastfile_profile.cpp`) are not Replay 1.20 readers; they are used for the Odin/1.31 frontend (`ASSET_CONVERSION.md` profile table).
- WRITE (Replay 0x0FF7, from a neutral JSON bundle via `convert-assets`, `ASSET_CONVERSION.md` "Build Replay 1.20 assets"): `rawfile`, `localize`, `image2d`, `resident_image`, `decal_volume_mask`, `stream_tree_key`, `image_reference`, `scriptfile` (GSCBIN), `stringtable`, `netconststrings`, `physicsasset`, `glass_map`, `col_map`, `comworld` (`com_map`), `mapents`, `gfx_map_trzone_geometry`, `native_material`, `decal_volume_material`, `keyvaluepairs`, plus several `native_*_4289` families (Odin 1.69 source).
- CLI: `iw8-zonetool.exe build-map <dump> <mp_id>` (`main.cpp:2925`); `build-iw3` (`:2938`); `convert-assets <assets.json> -o <dir>` (see doc).
- Sample output on disk: `E:\mw124\mw120rproxy\iw8-zonetool\dump\assets\soundbank` (directory exists, 0 files). UNKNOWN otherwise.

### 3.8 mjkzy_x64zt_iw8 (IW8 in-process dumper, x64-zt fork)

- Path: `E:\mw124\mjkzy_x64zt_iw8`. Language: C++ (premake5, SharedLib `zonetool-iw8`, `src\zonetool-iw8.lua`). License: `LICENSE` GPL-3.
- Built binary: none. Only `tools\premake5.exe` present. `generate.bat` copies the output to a "MW IW8 CLIENTS\1.20" folder, which suggests a 1.20 target (UNKNOWN unverified).
- Target claim: `README.md` lists IW6, S1, T7, H1, H2, IW7 only, so the IW8 support is in code only. The IW8 code is `src\zonetool\game\iw8\` and `src\zonetool\component\iw8\`.
- READ: only from a running game. It hooks the engine's zone loader (`component\iw8\zonetool.cpp:37` calls `load_init_zones_stub`; `component\iw8\fastfiles.cpp`). Asset types with dumpers: `zonetool\iw8\assets\` - aipaths, clipmap, rawfile, scriptabledef, scriptfile, xanim, xmodel, xmodel_detail_collision, xsurface (`xsurface.cpp:404` `dump_subdiv`).
- WRITE: no fastfile writer. Its `write_*` functions write dump records.
- Sample output: none on disk.

### 3.9 joelrau_x64zt and joelrau_zonetool_x64 (no IW8 support)

- Paths: `E:\mw124\joelrau_x64zt`, `E:\mw124\joelrau_zonetool_x64`. Language: C++ (premake5). License: GPL-3 (`LICENSE`).
- Built binary: none found at top level.
- Target: `joelrau_x64zt` converts IW6, S1, H1, H2, T7 (dump only) and IW7 (`README.md`). `joelrau_zonetool_x64` dumps CoD4 through MW3 only (`README.md`).
- IW8 mention: `joelrau_x64zt\src\zonetool\game\shared.cpp:433` (`Vec3UnpackUnitVec_IW8`, one helper). No IW8 read or write.
- READ/WRITE for IW8: none.

### 3.10 mw120rproxy readers and related folders (listed in the request)

- `E:\mw124\mw120rproxy\iw8-zonetool\src\common\` - `ff_io.cpp`, `zlib`, `thirdparty\directxtex` (BC decoding), `thirdparty\lz4`.
- `E:\mw124\mw120rproxy\iw8-zonetool\src\zonetool\iw8\` - `mw19_fastfile.cpp` (IWC decode, see 3.7), `fastfile_profile.cpp` (XFile profile table), `replay_stock.cpp` (ACTS-based copier, see 3.7), `mw19_*.cpp` (writers and Odin-1.69 readers).
- `E:\mw124\mw120rproxy\iw8-zonetool\src\zonetool\dumpsrc\` - `read_*` source dump readers only.
- `E:\mw124\mw120rproxy\iw8-zonetool\src\zonetool\convert\` - neutral conversion (material, xmodel, xsurface, image, map entity instances).
- `E:\mw124\mw120rproxy\iw8-zonetool\.codex-link-dryrun.txt`, `.tmp_native.out` - scratch logs (not inspected).

### 3.11 E:\IW8\Cascview (not a tool; 1.64 game folder)

- Path: `E:\IW8\Cascview`. Subfolders: `1.64` (game install, data removed) and `work` (empty).
- No CASCView binary here. The `1.64` folder holds `ModernWarfare_joiner.exe`, `bootstrapper.exe`, and `GAME_DATA_REMOVED.txt`. That note says the 1.64 game data was removed on 2026-09-27 and points to a CASCView restore of `1.64.5.13113888`.
- `1.64\.proxy\` has dumps from an earlier game build: `assetdump` (2,544 files, 86,936,016 B), `stringtables` (1,802 files), `camos` (1,227 files, 180,936,081 B), `reticles` (3), `images` (2). These are 1.64 data, not Replay 1.20. Do not use them for the Replay target.
- `1.64\xpak_cache\`, `main\`, `english\`, `worldwide\` folders exist (not inspected).

### 3.12 tools folder (prior research and helpers)

- `E:\mw124\tools\ff_tools_research.md` - prior research (summary of Greyhound, ModernModellingWarfare, Cordycep, ZoneTool). Its conclusions are external and were not re-verified here.
- `E:\mw124\tools\ff_grep.py` - decompresses one IW8 `.ff` with Oodle and greps the bytes for names (`decompress_ff` at line 61). Its `GAME` path (line 39) is `E:\Games\IW8\1.64.5.13113888\...`, so it targets 1.64, not Replay 1.20.
- `E:\mw124\tools\ida_*.py`, `*_out.txt` - IDA scripts and outputs for the game exe. Not asset tools.

## 4. Recommended reuse path for the editor

The editor should shell out to ACTS for reads (`acts.exe --noUpdater fastfile -r mw19replay -g <exe> --oodle <dll> ...`, the same pattern as the viewers) and use the Replay 1.20 writers in `iw8-zonetool-native` and `mw120rproxy\iw8-zonetool` for output.

| Asset type | Best reader (single tool) | Evidence | Best writer for custom content | Evidence / caveat |
|---|---|---|---|---|
| Materials | **ACTS** `-a material` (`atian-cod-tools`) | Viewer command `extractions.py:335-340`; structured `material` pool (`fastfile.md`). No material JSON sample on disk. | `iw8-zonetool-native` `writeMaterial` (`write_material.cpp:21`) from dump; or `mw120rproxy` `native_material` bundle | Material reader sample UNKNOWN; in-game load UNKNOWN |
| Images | **ACTS** `-a image` (DDS) | 220 stock DDS on disk (`_stock_ship_images`); DDS with resident pixels (`fastfile.md`) | `iw8-zonetool-native` `addImageAsset` (`write_image.cpp:140`); `mw120rproxy` `image2d` / `resident_image` | Strong on reader; writers need dumps or bundle |
| XModels (metadata) | **ACTS** `xmodel` structured JSON | 1,059 xmodel `.asset.json` (`_stock_backlot_xmodels`) | `iw8-zonetool-native` `addXModelFromDump` (`write_xmodel.cpp:153`) | Strong |
| XModel geometry (preview) | **ACTS** `xmodelsurfs --geometry` (GLB) | Doc claims base-surface GLB; viewer consumes it (`scripts/scenes.py`). Only 2 xmodelsurfs JSON on disk, no GLB | `iw8-zonetool-native` `writeXModelSurfsFromDump` (`write_xsurface.cpp:163`) | GLB sample UNKNOWN on disk; it has no LODs or textures |
| FX (vfx, impactfx, surfacefx, fx_map) | **ACTS** structured JSON | 409 VFX `.asset.json` (`_vfx_nk`); fx_map (`_stock_backlot_glass`) | None general. Native writes bullet-impact data (`replay_impact.cpp`); `mw120rproxy` `void` surface-effect closure via `Copier` | No generic FX writer: UNKNOWN |
| Sounds | **ACTS** `soundbank` (SAB) + `soundbanklist` | One `.sabl` file on disk (`output_ff`); doc says SAB | Writer: `soundbanklist` hash list only (`linker.md`) | No sound writer for real audio: confirmed gap |
| Stock map entities (map_ents, com_map, gfx_map) | **ACTS** `map_ents`, `com_map`, `gfx_map` | `_ents_stock\hideout` (mp_hideout), `_ents_nk` (mp_nuketown) with `script_strings.json` | `mw120rproxy` `mapents` / `comworld` / `col_map` writers (`ASSET_CONVERSION.md` table); `iw8-zonetool-native` `build-map` for the map family | Replay 0x0FF7 MapEnts/ComWorld writers exist per doc; in-game load UNKNOWN |

Why ACTS wins as the reader: it is the only surveyed tool that reads Replay 1.20 fastfiles by name and type across the families above, and it has on-disk output for most of them. The copier in `mw120rproxy` does the same job for a small subset (surface effects, xmodel, images) and is itself built on ACTS exports.

Recommended writer split (UNKNOWN until tested in game): use `iw8-zonetool-native` for images, materials, xmodels and map-family output (its 5-file `build-map` package is the most complete custom-map path), and `mw120rproxy` `convert-assets` for rawfile, localize, stringtable, netconststrings, scriptfile and physicsasset records. The ACTS linker is the simplest for those eight types but writes unsigned `IWffc100` containers that are not verified to load on the game.

Operational notes for the editor:
- ACTS needs the exact Replay exe and `oo2core_7_win64.dll` from `E:\IW8\Builds\1.20-replay`. Shipment needs `--xpak` pointing at the 14 populated `.xpak` files in the zone folder for streamed images, models and stream keys (`fastfile.md`).
- Exports of a full zone are large: `mp_donetsk_cg_ls_tr.ff` is 1.0 GB. Use `-a <types>` and `-n <names>` filters, and `--test --limit-per-pool` for previews (`fastfile.md`).
- Partial output is normal. The manifest `complete` and `success` flags must be checked (`fastfile.md`).

## 5. UNKNOWN / not verified

- Whether ACTS `--geometry` GLBs were generated for stock Replay assets (none on disk).
- Format of stock material JSON in ACTS output (no sample on disk).
- Whether `.sabl` (on-disk sound bank) and the documented SAB output are the same format.
- Whether the iw8zonewriter 1.24 target and the 1.20 target of the other repos are the same build; the 1.24 header differs from 0x0FF7.
- Whether any writer output (ACTS linker, native `build-map`, `mw120rproxy` `convert-assets`) loads on the in-game 1.20 client. Every repo says this is unfinished or untested; none was tested here.
- Whether a generic FX or sound writer exists in any repo: none found.
- Whether mjkzy_x64zt_iw8 targets exactly build 1.20 (path string only).
- Whether `E:\IW8\Builds\1.20-replay\zone` ACTS exports for `common_mp.ff` and `mp_donetsk.ff` were ever run (not in caches on disk).
- Sample output of iw8-zonetool-native and root iw8-zonetool Replay builds: not inspected.
- Whether any Greyhound or Cordycep claims in `tools\ff_tools_research.md` still hold in 2026: UNKNOWN (not re-verified).
