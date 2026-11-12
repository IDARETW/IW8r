# 43. CoD4 port wizard (roadmap 11.9): routes, converter facts, remap and report design

Scope: roadmap 11.9, "a CoD4 `.map` or built map through the fork's iw3 to iw8 converter, with a material and model remap table and a report of what did not carry over".
Method: read-only. Fork read: `E:\mw124\x64-zt`, branch `iw8radiant/build-extraction` (as checked out). Editor read: `E:\IW8-Radiant`. The converter was not run, the editor was not built or run, and no file was changed except this one. Line numbers were read on 2026-11-10. Several line numbers in `15_iw8_extraction_contract.md` and `17_x64zt_buildextraction.md` are older than the current fork (for example `PrepareFastfile` is now `iw3_fastfile.cpp:10622`, not 10603), so cite the numbers below, not those.

Tags: **UNVERIFIED** marks anything that was read but not run, or inferred, or not found in the code.

---

## 0. Summary

1. Two routes exist and they start from different things. **Route A** (a built CoD4 `.ff`) already runs end to end in the fork: `dumpzone <zone> --target iw8` extracts the zone with the OpenAssetTools Unlinker, converts it, and writes the five IW8 zones, `map.json` and two ledgers. **Route B** (a CoD4 source `.map`) has no converter path in the fork. It goes through the editor's own compiler (`compile_iw8`), which writes an extraction folder, and then the fork's `buildextraction`. The editor already runs `buildextraction` for its own maps (`src/qtapp/compile_panel.cpp:389-407`).
2. The converter converts the CoD4 world, collision, materials, textures, models, entities, scripts, effects, sounds, vision (fog and grading) and light grid. It keeps CoD4's own content and generates matching Replay technique sets. It maps to stock Replay content only in a few hard-coded places: effects by name, FX distortion materials, some sounds, and the two IW3 placeholder materials.
3. There is **no user-supplied remap table** in the converter. The only user-controlled path overrides are search roots (`--search_path`, `IW3_GAME_ROOT`, `COD4_ROOT`).
4. The ledger is the source for "what did not carry over": `<out>.iw3-assets.tsv` gives the fate of each source asset with a reason, and `<out>.iw8-assets.tsv` gives the origin of each output asset.
5. Route B has three blockers that a wizard must handle: a CoD4 `.map` carries no baked light (its faces name a placeholder `lightmap_gray`), material names resolve only to editor library entries (anything else becomes grey), and model names resolve only to custom OBJ files or to stock models the editor exports.
6. Recommended design (section 7): a Port Wizard that builds the remap as a pass over the extraction folder (both routes can use this, with no fork change for route B), plus one proposed fork option, `--remap <json>`, for route A. Neither the option nor the wizard exists yet.

---

## 1. The two routes

| | Route A: built CoD4 map | Route B: CoD4 source `.map` |
|---|---|---|
| Starts from | `zone\english\<map>.ff` in the CoD4 install (the converter also reads `<map>_load.ff` and `common_mp`) | `map_source\<map>.map` plus `map_source\prefabs\**.map` |
| Who reads the source | OpenAssetTools Unlinker (external exe), run by the converter | Editor `compile_iw8` (native, `src/core/compile_iw8.cpp`) |
| Fork entry point | `zonetool -game iw3 -path <CoD4> dumpzone <zone> --target iw8 ...` (`src/cli/zone_commands.cpp:173`, `convert_zone` at `:102-127`) | `zonetool -game iw8 -path <Replay> buildextraction <extraction> --map <id> --replay <exe> ...` (`src/cli/zone_commands.cpp:309-357`) |
| Lighting | CoD4's lightmaps, light grid and reflection probes come from the compiled BSP (`iw3_fastfile.cpp:12967`, `iw3_lightgrid.h`) | None baked. No light grid, so no GPU grid and no default (`15_iw8_extraction_contract.md` section 5) |
| Textures | CoD4 IWi images, exported as DDS by the Unlinker (`iw3_fastfile.cpp:475-486`) | None unless the mapper supplies them (`.iw8mat.json` library, doc 21) |
| Materials | CoD4 material JSON from the Unlinker, one generated technique set per material (`iw3_render_assets.cpp:2539`) | Grey (`iw8r/grey`) unless a library material matches (`compile_iw8.cpp:1454-1462`) |
| Status | Runs. The fork's own docs record conversions (`docs/IW8_CONVERSION.md`, timings for IW3 Nuketown) | The editor's own maps run this route. A CoD4 `.map` through it is not tested (UNVERIFIED) |

---

## 2. Route A: built CoD4 map through the fork converter

### 2.1 Command line

From `docs/IW8_CONVERSION.md` (Commands and options table), with the options the wizard needs:

```
zonetool.exe -game iw3 -path "<CoD4 folder>" dumpzone <zone name> --target iw8 --map mp_<id> --style iw8 ^
  --unlinker "<Unlinker.exe>" --gsc_tool "<gsc-tool.exe>" --replay "<game_dx12_ship_replay.exe>" ^
  --metadata "<map.json>" --acts "<acts.exe>" --iw3_mode_assets 0
```

Rules, from the code:

- The option parser runs only after `--target <game>`, as `--name value` pairs (`src/cli/zone_commands.cpp:173-200`). Options written before `--target` are not read.
- `dumpzone` has no `--output`. The output folder is always the literal `output` relative to the working directory (`zone_commands.cpp:107`), and the zones go to `output\<map>\` (`src/convert/iw3_iw8/convert.cpp:2656`). The wizard must set the working directory to its build folder.
- The zone argument is resolved by `game_files::find_zone` inside the CoD4 install (`zone_commands.cpp` `require_zone`). Passing an absolute `.ff` path is **UNVERIFIED**; pass the zone name.
- Map id: at most 15 lower-case characters with an `mp_` prefix (`convert.cpp:131-139`, `requireMapId`).
- `--iw3_mode_assets 0` is what the editor passes for its own builds (`compile_panel.cpp:401-402`). Without it the converter adds IW3 gametype props to the Domination flags and fails when those models are absent (the editor's comment).

### 2.2 The chain

1. `dump_zone` sees the converter has `convert_zone` and calls it (`zone_commands.cpp:204-212`). `convert_zone` sets `source_game_path` to the CoD4 folder and `output_folder` to `output` (`:102-127`).
2. `convert_map` (`convert.cpp:2586`) turns the options into `Args`. The map id defaults to the zone stem (`:2597-2603`). `--unlinker` goes to `args.unlinkerExecutable` (`:2648`). With no extraction, the CoD4 folder is added as a source root (`convert.cpp:2653-2654`).
3. `buildIw3` (`convert.cpp:2461`) calls `PrepareFastfile` (`iw3_fastfile.cpp:10622`).
4. The Unlinker is found by `FindUnlinker` (`iw3_fastfile.cpp:390`): environment variables `IW8_ZONETOOL_UNLINKER` or `MW120R_UNLINKER`, then candidate paths beside the exe, then `PATH`. It runs through `RunUnlinker` (`:475`) with `--no-color --image-format DDS --model-format OBJ -o <dir>` (plus `--include-assets` for the supplemental runs).
5. The unlinked folder is cached as `output\.iw3-extract-cache\<hash>\` (`ExtractionCacheDirectory`, `:10132`). The hash covers the Unlinker, the `.ff` and its `_load.ff`, `common_mp` and the search paths (size and mtime). A `complete` file marks a finished cache (`:10829`).
6. The converter reads the world JSON (`ReadWorldJson`, `:2332`), collision JSON, comworld JSON, `.ents`, materials, images and xmodels, then runs `buildMap` (`convert.cpp:2157`).
7. `buildMap` writes five zones and `map.json`: `<map>.ff`, `techsets_<map>.ff`, `eng_<map>.ff`, `ww_<map>.ff`, `srv_<map>.ff` (`convert.cpp:2045`, `:2072`, `:2120`). The package check requires exactly five `.ff` files and `map.json` (`convert.cpp:622`).
8. Both ledgers are written (section 3).

### 2.3 What converts, by layer (fork facts)

| Layer | What converts | Where | Notes |
|---|---|---|---|
| Collision | Brush hulls and Havok shapes, native by default; glass becomes panes; ladders and stairs | `iw3_fastfile.cpp:10678` (collision JSON), `iw8::havok` via `--havok native\|native-check\|exe` (`convert.cpp:2612-2625`) | `docs/IW8_CONVERSION.md` IW3 maps |
| World | Surfaces, lightmaps decoded from the GfxWorld export, reflection probes, static models, sun, sky, heightfield | `ReadWorldJson` `iw3_fastfile.cpp:2332`; ledger rows in `iw3_fastfile.cpp:1950-1990` | Lightmaps: "decoded from the GfxWorld export" (audit origin text) |
| Light grid | Static-model lighting from the exported grid, sun visibility | `PrepareNativeLightGrid` `iw3_fastfile.cpp:12967`, `SampleLightGridAmbientCubes` `iw3_lightgrid.h` | |
| Materials | One generated technique set per CoD4 material, from its colour, normal and specular maps and state bits | `ReadMaterial` `iw3_render_assets.cpp:2539`; `FindMaterial` `:1954` | `techniqueSet` is required and matched by name tags (alpha, blend, shadow caster), no technique-set file is opened (`:2577`, `:2629-2657`) |
| Images | DDS from the extraction; loose `.iwi` from source roots as a fallback | `LoadImage` `iw3_render_assets.cpp:1272`; `DecodeIwi` `:1124` (IWi version 6) | `$white`, `$black`, `$identitynormalmap`, `$water_specular` are built in (`:1277-1285`) |
| Models | Geometry, LODs, skeletons, collision LOD, vehicle parts; static models get an XModelDetailCollision | `ReadSourceModel` `iw3_fastfile.cpp:2960` | Unplaced models are left out ("never placed ... referenced", ledger) |
| Entities | Spawns, script models, brush models, triggers, glass prefabs, Domination flags, createfx | `.ents` transforms in `PrepareFastfile` (doc 15 section 3.1) and the entity converter | Class and key whitelists (doc 15 sections 3.2-3.3) |
| Scripts | The map's own GSC translated into one IW8 unit with an `iw3compat` layer; IW3-only builtins become `iw3compat_noop()`; compiled with gsc-tool (`-m comp -g iw8 -s pc`) | `iw3_scripts.h` `Translate` (`iw3_scripts.cpp:793`), `Compile` | Translation notes are returned with the script |
| Effects | Converted effect graphs named `vfx/mw120r/<map>/<name>`; script `loadfx` calls go to the converted effect, else to a stock effect | `ConvertFx` `iw3_fastfile.cpp:9539`, `BuildFxAliases` `:9645`; script mapping `:12477-12519` | Stock fallbacks in section 2.4 |
| Sounds | Aliases the scripts play, gathered into one always-loaded map bank; audio from the extraction or the IWD archives | `iw3_sound.h` `Convert` | Unused aliases are left out |
| Vision and grading | Fog from the `.vision` raw file; film grading as a CLUT; Replay vision file | `iw3_fastfile.cpp:10882-10904`, `iw3_vision.h` `BuildReplayVision` | `--style iw3` keeps IW3's look; `--style iw8` uses Replay's fog, scattering and exposure (`iw3_vision.h`) |
| Stock content | Replay's void surface table, IES profiles, CODCaster images, map intro chyron, from the user's own Replay install | `iw8::stock::Copier`, `convert.cpp:1549-1570` | Needs `--acts`. Without it the converter warns (`:1558`) |

Left out by design (`docs/IW8_CONVERSION.md`, IW3 maps): weapons and menus, player animations and character types, weapon pickups (`weapon_<weapon>_mp`), tool surfaces (nodraw, caulk), most unused models and aliases.

### 2.4 Materials and models: converted, or mapped to stock names?

**Converted.** CoD4 textures, materials and models come into the output as their own assets. The stock mapping is limited to these cases:

- **Stock technique sets with converted textures.** When the world JSON sets `replay_stock_world`, or when some material has no converted equivalent (`stockOnlyWorld`), the converter builds `w/mw120r_<map>_stock_<id>` materials on Replay's own world technique sets from `iw8::stock::Library` (read from the Replay install's `zone` folder). The key is read at `iw3_render_assets.cpp:6217` and `:7765-7768`; the stock-material builder is at `:7209`. Whether the Unlinker output for CoD4 sets `replay_stock_world` is **UNVERIFIED**. No code in the fork writes it.
- **FX distortion** (heat haze, shock rings) become `elcq/vfx_dist_heat_1` or `elcq/vfx_dist_heat_shimmer_blast_1` under `--style iw8` (`iw3_render_assets.cpp:8440-8462`, `plan.stockMaterials`).
- **Effects by name.** `--style iw8` uses an exact table of 14 CoD4 effect names to stock effects (`iw3_scripts.cpp:765-780`, `Iw8StyleEffectFor` at `:756`). Otherwise `StockEffectFor` (`:740-753`) picks a stock effect by keyword (glass, explo, fire, smoke), and the fallback is `vfx/iw8_mp/equipment/flashbang/vfx_spark_crack_npop`.
- **Sounds by stock alias.** Ledger fate "stock" rows (`iw3_fastfile.cpp:1968`), `sound:stock` uses (`:12462-12465`), and `<map>.stock_sounds.txt` (`:12364-12381`). The producer of `stock_sounds.txt` is **UNVERIFIED**.
- **Placeholder materials.** `mc/lambert1` and `mc/gfx_missing_fx` get a built-in white stub when no file exists (`iw3_render_assets.cpp:2550-2557`).

Models have **no CoD4-to-stock name mapping** in the fork. A CoD4 model name that no zone of the install holds is left out (`docs/IW8_CONVERSION.md` Limits).

### 2.5 Every remap-like mechanism in the fork

Searches used: `remap`, `replace`, `substitut`, `alias`, `stock`, `override` across `src/convert/iw3_iw8`.

| Mechanism | Where | Scope | Editable by a user? |
|---|---|---|---|
| Exact effect name to stock effect (14 entries) | `iw3_scripts.cpp:765-780` | Hard-coded | No |
| Keyword effect name to stock effect | `iw3_scripts.cpp:740-753` | Hard-coded | No |
| FX distortion to stock refraction material | `iw3_render_assets.cpp:8440-8462` | Hard-coded | No |
| Per-map FX material aliases | `plan.fxMaterialAliases` `iw3_render_assets.cpp:8442`, `:8463`, `:8621`; `iw3_fastfile.cpp:11961` | Generated | No |
| Per-map effect aliases | `BuildFxAliases` `iw3_fastfile.cpp:9645-9700` (`vfx/mw120r/<map>/<name>`) | Generated | No |
| Sound alias to stock alias | `iw3_fastfile.cpp:12364-12381`, ledger `sound:stock` | From a file in the extraction | UNVERIFIED producer |
| Entity keys and classes | `docs/15` sections 3.2-3.3 (`maps.cpp` table in the IW8 dump) | Closed lists | No |
| Known source asset types | `IsKnownIw3SourceAssetType` `iw3_fastfile.cpp:1671-1682` (33 names; an unknown type throws) | Closed list | No |
| Source search roots | `--search_path`, `IW3_GAME_ROOT`, `COD4_ROOT` (`convert.cpp:2526-2550`); `FindMaterial` also strips `mc/` (`iw3_render_assets.cpp:1954-1966`) | Path only, not name | Yes |
| Lighting match to shipped Replay maps | `MatchIw8Lighting` `iw3_vision.h` | Computed | No |
| Prefab material swap (editor side) | `src/core/prefab.cpp:292-300` (`iw8r_mat:<from>`) | Per prefab instance | Yes (editor) |
| Stock model export (editor side) | `src/core/compile_iw8.h:46` `stockModels`, `stockModelBuildName` (`compile_iw8.cpp:447`) | Editor | Yes (editor) |

**Conclusion:** there is no name-to-name remap file in the converter. Adding one means a new option (section 7.5).

### 2.6 CoD4 raw files are not directly usable

- `raw\images`: 6,561 `.iwi` files. The first bytes of a sample are `IWi\x06`, format `0x0b` (DXT1). The decoder accepts IWi version 6 with formats 1-5 and 11-13 (`iw3_render_assets.cpp:1124-1140`), so DXT1 images are readable.
- `raw\materials`: 4,355 files. The sampled file is binary, not JSON (its first bytes are not text). The converter reads only JSON material files (`ReadJson` in `FindMaterial` and `ReadMaterial`). So CoD4 materials must come through the Unlinker. Whether all 4,355 are binary is **UNVERIFIED** (one sample checked).
- `raw\xmodel`: 4,372 entries. Format not checked (**UNVERIFIED**).

---

## 3. The ledger: "what did not carry over"

Two TSV files are written beside the output folder (`output\<map>.*`).

**Input ledger, `<map>.iw3-assets.tsv`** (source side). Header: `zone  type  name  fate  detail` (`iw3_fastfile.cpp:2115`). Each row is written at `:2124` by `AuditSourceAssetDeclarations` (`iw3_fastfile.cpp:1752`), which gets its file name from `iw3::ledger::SetReport` (`convert.cpp:2554-2555`).

- The declarations come from OpenAssetTools' `zone_source` (`*.zone` files). The audit throws if that folder is missing or empty (`iw3_fastfile.cpp:1760-1781`).
- The consumed set is what the readers record with `ledger::Use`: `LoadImage` (`iw3_render_assets.cpp:1276`), `ReadMaterial` (`:2000`, `:2543`), xmodels (`iw3_fastfile.cpp:2964`), physics presets (`:3370`), xanims (`:11278`), sounds (`:12462`), sound stock (`:12465`), soundcurves and loadedsounds (`iw3_sound.cpp:1029`, `:1810`), and the FX, raw file and light-def selections (`:1858-1868`).
- Fates (from `classify`, `iw3_fastfile.cpp` about `:1865-2100`): `converted`, `stock`, `shared-unused`, `reference`, `n/a`, `unused`. A row that matches none becomes `UNEXPLAINED`, which the converter logs.
- Each fate has a plain-language detail. Examples: "IW3 shared zone ... only what the map uses converts"; "IW3 player character (mptype): Replay players are its own operators"; "vehicle part ..."; "no converted surface, model or effect uses it"; "IW3 killstreak/vehicle system sound".

**Output ledger, `<map>.iw8-assets.tsv`** (IW8 side). Header: `zone  type  name  origin` (`convert.cpp:1494-1520`, `writeOutputLedger`). Written at `convert.cpp:2126`. Each row's origin comes from `outputLedgerRows` (`convert.cpp:1339`), with origin text per asset type (for example "world root, exported whole by the Unlinker", "stock IES light", "lightmap: decoded from the GfxWorld export"). A row with no origin is written as `UNEXPLAINED` and logged with examples (`:1506-1519`).

The converter logs a count per zone and type: `iw3: ledger ...` (`iw3_fastfile.cpp:2135-2145`) and `build-map: output ledger: N asset(s)`.

For the wizard's report: take the input ledger's non-`converted` rows as "did not carry over" with their detail, the output ledger for origin, and any `UNEXPLAINED` rows as "needs review".

---

## 4. Route B: CoD4 source `.map` through the editor

### 4.1 Format compatibility

- The header of every CoD4 map is `iwmap 4` (CRLF on most files, LF on some), and `docs/research/02_iwmap4_format.md` section 1 covers the layer lines. Checked this session on `mp_backlot_geo.map`: face lines are `( x y z ) ( x y z ) ( x y z ) <material> <sx> <sy> <rot> <ox> <oy> <flags> lightmap_gray 16384 16384 0 0 0 0`. The `lightmap_gray` token is a placeholder, not baked light.
- The editor reads this format losslessly (`src/core/map_text.h:1`; the face suffix is kept as text, `map_text.h:35`).
- CoD4 uses `misc_prefab` with `"model" "prefabs/<path>.map"` relative to `map_source\` (doc 02 section 5). The editor expands prefabs before compiling (`compile_iw8.cpp:864-874`, `src/core/prefab.h` and `prefab.cpp`). Prefab resolution is **UNVERIFIED** on the CoD4 tree (only the grammar was checked by doc 02).

### 4.2 The pipeline in the editor

1. Parse the `.map`; expand `misc_prefab` (`compile_iw8.cpp:864-874`).
2. Write the extraction (`compile_iw8.cpp` header comment for the file list): `complete`, `skeleton/complete`, `extracted/maps/mp/<id>.d3dbsp.replay-world.json`, `.replay-collision.json`, `.replay-comworld.json`, `.ents`, `extracted/materials/<name>.json`, `extracted/images/*.dds`, `extracted/xmodel/*.json`. The editor also writes an empty `extracted/zone_source/<zone>.zone` per zone (`compile_iw8.cpp:1776`), which satisfies the audit in section 3.
3. Run `zonetool -game iw8 -path <Replay> buildextraction <extraction> --map <id> --replay <exe> --metadata <json> --acts <acts.exe> --gsc_tool <gsc> --iw3_mode_assets off --output <folder>` (`compile_panel.cpp:389-407`).

### 4.3 Entity classes

Counts from the CoD4 files on disk (grep of `"classname"`; read only):

| Class | `mp_backlot_geo.map` (1.66 MB) | `mp_test.map` | Editor behaviour (`compile_iw8.cpp`) |
|---|---:|---:|---|
| `misc_model` | 2,222 | 24 | Placement from `world.models` if the model is a custom OBJ or an exported stock model. Otherwise skipped (`:1248-1293`, comment "Stock names are skipped") |
| `misc_prefab` | 759 | 4 | Expanded before compile (`:864-874`) |
| `func_group` | 78 | 0 | Geometry only (`:933`, `:941`) |
| `dyn_model` | 74 | 0 | Not in the accepted list (`:487-510`). Left out with a warning (`:1367-1368`) |
| `script_origin` | 41 | 0 | Accepted |
| `reflection_probe` | 34 | 3 | Becomes `world.json` probes, at most 256 (`:1180-1240`) |
| `mp_tdm_spawn` | 18 | 9 | Accepted |
| `mp_dom_spawn` | 18 | 0 | Accepted |
| `mp_dm_spawn` | 18 | 0 | Accepted |
| `light` | 13 | 0 | Primary lights in comworld (`:569-643`) |
| `mp_tdm_spawn_allies_start` | 12 | 1 | Accepted |
| `mp_sd_spawn_defender` | 12 | 0 | Accepted |
| `info_player_start` | 0 | 1 | Accepted by the editor's list; the converter does not require it (doc 15 section 3.4) |
| `worldspawn` | 1 (per file) | 1 | Geometry |

Also from `mp_backlot.map` (the top file, 12 KB): `light` 35, `misc_prefab` 19, `info_null` 13, `worldspawn` 1.

The accepted list is the converter's closed list, copied into the editor (`compile_iw8.cpp:487-510`). Other classes are counted in `unacceptedClasses` and logged, and are not written (`:1367-1368`). Mode spawns are completed from other spawns by the converter (doc 15 section 3.4).

### 4.4 Materials and textures

- A surface's material is `materialFor(editorMaterial)` (`compile_iw8.cpp:1454-1462`). If the mapper's library (`MaterialLibrary`, `src/core/custom_materials.h`) has the name, the surface uses that material (`extracted/materials/<name>.json` with DDS images). Otherwise it is `iw8r/grey`. The number of distinct grey materials is logged (`compile_iw8.cpp:1116`).
- CoD4 materials such as `ch_tile_floor05` (from `mp_test.map`) have no library entry, so they compile grey unless a library entry is written. The CoD4 images are IWi and the editor has no IWi reader, so they cannot be used directly (the converter's DecodeIwi is in the fork, not the editor).
- Tool materials (`caulk`, `nodraw`) on the editor side: **UNVERIFIED** how `compile_iw8` treats them (not read).

### 4.5 Lighting

A CoD4 `.map` has no lightmaps, no light grid and no probe data. Those come from the compiled BSP (`d3dbsp`) that `cod4rad` writes, which the source map does not contain. Doc 15 section 5 states the result: with no light grid the editor and the converter write no GPU grid, and surfaces are unlit or baked-atlas-only. In-game lighting result is **NOT-VERIFIED** (doc 15 section 5).

---

## 5. What does not carry over (consolidated)

| Item | Route A (built `.ff`) | Route B (source `.map`) | Evidence |
|---|---|---|---|
| CoD4 lightmaps and light grid | Carries (world and static-model lighting) | Lost; grey or unlit | `iw3_fastfile.cpp:12967`; doc 15 section 5 |
| Reflection probes | Carries (from the BSP) | Carry from `reflection_probe` entities (max 256) | `compile_iw8.cpp:1180-1240` |
| CoD4 textures | Carry as DDS | Lost unless the mapper supplies PNG/TGA/JPG or DDS through the library | `iw3_render_assets.cpp:1272`; `custom_materials.h` |
| CoD4 material shaders | Replaced by a generated Replay technique set (shader math reproduced for IW3, not the IW3 shader binary) | Grey | `iw3_render_assets.cpp:2577`, `:2629-2657`; `IW8_CONVERSION.md` Materials |
| Tool surfaces (caulk, nodraw, sky shadow) | Draw nothing | Draw nothing (UNVERIFIED for route B) | `IW8_CONVERSION.md` Materials |
| Models | Carry if placed; unplaced models omitted | Carry only with a custom OBJ or a remap to an exported stock model; else skipped with a warning | `iw3_fastfile.cpp:2960`; `compile_iw8.cpp:1248-1293` |
| `dyn_model` entities | Carry only if the converter's dynamic-entity path reads that class (`sourceDynamicEntities`, `iw3_fastfile.cpp`; **UNVERIFIED** for `dyn_model`) | Left out | `compile_iw8.cpp:1367-1368` |
| Gametype props (Domination flags) | Kept unless `--iw3_mode_assets 0` | Not produced | `compile_panel.cpp:401-402` |
| Weapon placements (`weapon_<weapon>_mp`) | Left out | Not produced | `IW8_CONVERSION.md` Mod content |
| Scripts | Translated; IW3-only builtins become no-ops; some calls left out (named in the log) | None (the editor writes its own map script, `compile_iw8.cpp`) | `iw3_scripts.cpp:793`; `IW8_CONVERSION.md` Scripts |
| Sounds | Aliases the scripts play, in one map bank | None unless the mapper adds sounds | `iw3_sound.h` |
| Effects | Converted graphs, or stock effects by name | None (no CoD4 FX reader on route B) | `iw3_fastfile.cpp:12477-12519` |
| Fog and grading (`.vision`) | Converted | Replay defaults (UNVERIFIED) | `iw3_fastfile.cpp:10882-10904` |
| Bot navigation | Generated (`--navigation 0` leaves it out) | Editor-specific (UNVERIFIED) | `convert.cpp` options |
| Triggers, spawns, objectives | Carry (mode spawns completed from others) | Carry for accepted classes | doc 15 section 3 |
| Entities not in the converter's list | Left out (`isReplayEntity`) | Left out with a warning | `compile_iw8.cpp:1367-1368` |

---

## 6. Existing outputs on disk (not opened)

- `data\asset_catalog\iw8_stock_maps.tsv` (96 lines: 3 comment lines, a header, and 93 map rows, per `docs/research/35_stock_map_catalog.md`). The rows for `mp_discovery` (line 36) and `mp_nuketown` (line 71) show a main `.ff` under `mods\mw120r\maps\` or under `.proxy\backups\mw120r-map-20261017-173632-156\`. Their notes say "taken from mod folder ... (no copy in zone/)". Provenance is only partly established. `docs/IW8_CONVERSION.md` and `AGENTS.md` (line 181) treat Nuketown as an IW3 map converted by x64-zt, so `mp_nuketown` is probably a conversion (**UNVERIFIED**). `mp_discovery` provenance is **UNVERIFIED**.
- `docs/research/06_test_assets.md` (line 147) records 15 converted IW3 packages in `maps_parked` (for example `mp_aerodrome`, `mp_agroprom`, `mp_arnhem`, `mp_broadcast`, `mp_crash_253`, `mp_dr_sm64`, `mp_greenband`).
- The per-map `<map>.iw3-assets.tsv` and `<map>.iw8-assets.tsv` files from those runs are the best reference for the report format. Their location was not opened.

---

## 7. Port Wizard design

### 7.1 Inputs

| Input | Route A | Route B | Required |
|---|---|---|---|
| Source | CoD4 install folder; zone name (for example `mp_backlot`) | `map_source\<name>.map` | Yes |
| Target map id | `mp_<name>`, at most 15 lower-case characters | same | Yes |
| Style | `iw3` or `iw8` (default `iw8` for a port) | same | Yes |
| Replay install and exe | `--replay` (zone folder for the stock library) | same | Yes |
| Metadata | title, description, compass rotation, `exposureAdjust` | same | Yes (the wizard writes `map.json` input) |
| ACTS exe | `--acts` (stock copier, validation) | same | Recommended |
| gsc-tool | `--gsc_tool` (map script compile) | same | Recommended |
| Unlinker | `--unlinker` (or `IW8_ZONETOOL_UNLINKER`) | not needed | Route A only |
| Mapper library | `.iw8mat.json` roots and image files (for remapped textures) | same | Optional |
| Remap table | section 7.3 | same | Optional (auto-proposed) |

### 7.2 Steps

1. **Pick source.** Route A: zone name in the CoD4 install. Route B: a `.map` file. The wizard checks the file exists and, for route B, that `map_source\prefabs\` resolves each `misc_prefab` (**UNVERIFIED** on the CoD4 tree).
2. **Scan.** Route B: parse the `.map` with the editor's reader and list every material token, `misc_model` name, entity class and effect or sound name. Route A: the scan needs the Unlinker output, so run the export first (no export-only switch exists for IW3, section 7.5 proposes one). The scan gives the list of materials, models, classes and effect names to remap.
3. **Propose the remap.** For each CoD4 material: the mapper's library entry (if a name matches), else `grey`, else `stock` (a Replay world material, when chosen), else `drop`. For each model: custom OBJ, exported stock model (by an explicit name the mapper picks), or `drop`. For each class: `keep`, `drop`, or a named accepted class. Effects and sounds: stock name or `drop`.
4. **Review.** The mapper edits the table in a list view. Any row marked `drop` or `grey` is shown with the count of uses (from the scan), so the mapper sees the cost.
5. **Build.** Route B: apply the remap to the document (material tokens and `misc_model` model names rewritten in the in-memory document, the same way `prefab.cpp:292-300` applies `iw8r_mat`), then compile to an extraction and run `buildextraction` (`compile_panel.cpp:389-407`). Route A: run `dumpzone` (section 2.1) with `--remap` (proposed, 7.5), or, without that option, rewrite the extraction's `materials/`, `xmodel/` and `.ents` names by hand (UNVERIFIED for route A because the cache key is internal).
6. **Report.** Section 7.4.
7. **Validate.** Check for five `.ff` files and `map.json` (`convert.cpp:622`). ACTS validation runs when `--acts` is set (the editor's `CompilePanel` already does this, `compile_panel.cpp` Validate stage).

### 7.3 Remap table format (proposed)

A JSON file the wizard writes beside the project, so the mapper can edit it and re-run:

```json
{
  "schema": 1,
  "source": {"game": "cod4", "kind": "map", "name": "mp_test", "path": "map_source/mp_test.map"},
  "target": {"map": "mp_port_test", "style": "iw8"},
  "materials": [
    {"from": "ch_tile_floor05", "to": {"kind": "library", "name": "custom/ch_tile_floor05"}, "uses": 412},
    {"from": "caulk", "to": {"kind": "drop"}},
    {"from": "wc/glass_test", "to": {"kind": "grey"}, "note": "no library entry"}
  ],
  "models": [
    {"from": "com_cardboardbox05", "to": {"kind": "custom", "obj": "models/cardboard.obj"}},
    {"from": "foliage_shrub_brownmid", "to": {"kind": "stock", "name": "<stock model name>"}},
    {"from": "body_complete_sp_spetsnaz", "to": {"kind": "drop"}}
  ],
  "classes": [
    {"from": "dyn_model", "to": {"kind": "drop"}},
    {"from": "misc_model", "to": {"kind": "keep"}}
  ],
  "effects": [{"from": "fire/firelp_small_pm", "to": {"kind": "stock", "name": "vfx/..."}}],
  "sounds": [{"from": "<alias>", "to": {"kind": "drop"}}]
}
```

Rules:

- Names are matched case-insensitively, as the editor's `MaterialLibrary::find` does (`custom_materials.h`).
- `kind` is one of `library`, `custom`, `stock`, `grey`, `drop`, `keep`. `stock` names must exist in the Replay install (the wizard checks them against the install's catalogue, `iw8_stock_maps.tsv` and the stock model list, **UNVERIFIED** how the model list is built).
- Unlisted names keep their default (grey or drop), and the report lists them.

### 7.4 Report contents

1. **Header:** source, target id, style, Replay and converter versions (the fork commit and the editor build), date, and the remap file's hash.
2. **Source fates** (from the input ledger, `<map>.iw3-assets.tsv`; route A only): counts per fate and type, and the full list of non-`converted` rows with their detail text.
3. **Output origins** (from `<map>.iw8-assets.tsv`), and the count of `UNEXPLAINED` rows with examples.
4. **Remap applied:** each material, model, class, effect and sound: its target, and the number of uses.
5. **Dropped and greyed items** (route B): the compile's own warnings (`compile_iw8.cpp:941-950`, `:1116`, `:1248-1293`, `:1367-1368`), with counts.
6. **Lighting:** "no CoD4 light data carried" (route B) or "carried from the BSP" (route A), and the reflection probe count.
7. **Script notes:** the translation notes (`iw3_scripts.h` `Translation::notes`) and the gsc-tool result.
8. **Not checked:** in-game load, lighting result, and any item marked UNVERIFIED in this document.

The report is a plain-text file and an HTML summary (or a list view in the editor). Keep the two TSV files beside the output folder, as the converter does.

### 7.5 Fork entry points

**Existing, used as they are:**

- Route A: `zonetool -game iw3 -path <CoD4> dumpzone <zone> --target iw8 ...` (`zone_commands.cpp:173-212`, `convert.cpp:2706`).
- Route B: `zonetool -game iw8 -path <Replay> buildextraction <extraction> ...` (`zone_commands.cpp:309-357`, `convert.cpp:2711`), already called by `compile_panel.cpp:389-407`.

**Proposed fork changes (none exists; each is UNVERIFIED as a design until built):**

1. `--remap <json>` option on `dumpzone` (route A). Read in `convert_map` (`convert.cpp:2586`), passed in `iw3::ImportOptions` (`iw3_fastfile.h`), and applied in `FindMaterial` and `ReadMaterial` (`iw3_render_assets.cpp:1954`, `:2539`), `ReadSourceModel` (`iw3_fastfile.cpp:2960`), the `.ents` transform, and the effect name lookups (`iw3_scripts.cpp:740-791`). Materials and models would be matched by source name, and the target name is used in place of the source name.
2. `--export_only` for the IW3 route (the wizard's scan needs the unlinked folder before the conversion). The IW7 route has `IW7_EXPORT_ONLY=1` (`docs/IW8_CONVERSION.md` Pipeline), so this is a small addition in `PrepareFastfile` (`iw3_fastfile.cpp:10622`). No IW3 export-only switch exists (grep of `EXPORT_ONLY` in `src/convert/iw3_iw8` found none).
3. `--report <path>` to write the wizard's report rows straight from the ledgers. Optional: the wizard can read the TSV files itself.

**Without any fork change (route B):** the wizard can apply the remap to the editor's document before compile, and to the extraction after compile (rewrite `extracted/materials/*.json` names, `extracted/xmodel/*.json` names and the `model` keys in `.ents`). The editor's writer is the one doing the work, so the extraction is known-good by construction (doc 15).

### 7.6 Risks

- A CoD4 `.map` may use features the editor's parser or compile has not been tested on (`patchDef`, `brushDef`, prefab chains): doc 02 found none in its corpus, but the CoD4 tree was only checked for grammar.
- The editor's compile treats some classes as "left out" without an error, so a port can build and still miss entities. The report must show the skipped counts.
- Route A's `dumpzone` needs CoD4's `_load.ff` and `common_mp`. If the wizard runs in a CoD4 install without them, the converter fails (UNVERIFIED).
- Route A writes into `output\` under the working directory, which is shared across runs. Two wizard runs in the same folder would collide (the x64-zt checkout notes say other sessions use the same tree, so the wizard must use its own folder).

---

## 8. UNVERIFIED list

1. `dumpzone` accepts a zone name only; an absolute `.ff` path is UNVERIFIED (`require_zone` / `game_files::find_zone` not read in full).
2. Whether the Unlinker output for CoD4 sets `world.replay_stock_world` (section 2.4).
3. The producer of `<map>.stock_sounds.txt` (`iw3_fastfile.cpp:12364-12381`).
4. Whether all 4,355 CoD4 `raw\materials` files are binary (one checked, section 2.6), and the format of `raw\xmodel` (not checked).
5. CoD4 prefab resolution on the full tree (only doc 02's grammar check exists).
6. How `compile_iw8` treats `caulk` and `nodraw` surfaces.
7. The fog, grading and bot navigation for a route B map (defaults not read).
8. The stock model list the editor exports (`stockModels` source, `compile_iw8.h:46`).
9. Whether `mp_discovery` and `mp_nuketown` are converted IW3 maps (section 6).
10. Route A with the Unlinker in its default location: the editor's doc 03 says the vendored OpenAssetTools exists at `tools\_vendor\OpenAssetTools\...`; not re-checked here.
11. Whether an in-game load of a route A or route B map passes (not tested; the editor's doc 17 test room loaded only in the converter's structural check).

---

## 9. Notes for the docs

- `docs/research/03_build_pipeline_interface.md` describes an older pipeline with `build-iw3` and `build-map` commands. Those commands are not in the fork's command table (`src/cli/commands.cpp`, ten commands: `inspect`, `dumpzone`, `dumpasset`, `verifyzone`, `listzone`, `buildzone`, `buildextraction`, `refreshzone`, `patchzone`, `generatecsv`). Use `dumpzone` and `buildextraction` (sections 2 and 4).
- `docs/research/15_iw8_extraction_contract.md` section 6.3 says a skeleton `complete` file is required for an external extraction. The current fork gates that check on `!external` (`iw3_fastfile.cpp:10840`), so the requirement is gone for `buildextraction`. Its line numbers are stale (section 0 note).
- `docs/PROGRESS.md` was not updated. The task said to create only this file.
