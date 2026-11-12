# 18. Stock Replay assets by name in an x64-zt buildextraction

Scope: can an editor-made extraction (the `buildextraction` route, MW2019 Replay 1.20 target) use stock Replay materials and xmodels by name instead of a grey placeholder? Read-only. Nothing was built, run, committed or switched. Code is read with `git show iw8-conversion:<path>` from `E:\mw124\x64-zt` (branch `iw8-conversion`), and ACTS with `git show codex/mw2019-complete:<path>` from `E:\mw124\atian-cod-tools`. Run output was read from `E:\IW8-Radiant\out\...`. Line numbers are from those revisions. Paths are relative to `src/` unless absolute.

## 0. Corrections to the premise and to docs 15 and 17

- **The ACTS lines are not in `out\m5\sample\build.log`.** That log has only a cache-hit timing line (`build.log:89`, "package/stock exports 3.5s"). The ten `stock: exported ... with ACTS` lines are in `E:\IW8-Radiant\out\x64zt-extraction-test\run.log:85-97`, plus the `void` closure summary at `run.log:98`. Doc 17 line 99 is right; the m5 log is not a source for them.
- **Doc 15 §4.1 says stock world technique sets are used "only when `world.replay_stock_world` is true".** Not quite. `stockOnlyWorld` also triggers `buildStockWorldMaterials` for shadow-only casters and distortion materials (`iw3_render_assets.cpp:7229-7231`, `:7814-7815`, `:7862-7863`).
- **Doc 15 §4.1 says name-only references are "used only for `vfxReferences`".** Not quite. The `void` closure registers shared (comma-prefixed) names (`replay_stock.cpp:1421-1424`, `:1435-1478`), and the FX distortion alias registers a stock name (`iw3_render_assets.cpp:8509`, written at `replay_render.cpp:3128-3136`).
- The `void` closure already contains **144 shared material names** in the `mp_shipment` export (see §3.1). So stock materials by name are not absent from the current build. They are just not used for world surfaces.
- There is **no grey placeholder for world-surface materials**. A missing surface material throws (`iw3_render_assets.cpp:2559-2562`). The grey comes from elsewhere: the `mc/lambert1` and `mc/gfx_missing_fx` stub (`:2550-2557`, a white colour), the `$gray` image (`:1305-1306`), and `$gray` in texture slots of stock-library materials (`:7451-7452`). Which one you saw is UNKNOWN; check the materials JSON in your extraction.

## 1. Summary table

"Now" means the code at `iw8-conversion` as read. "Name-only" means the zone records a header with `,name`, and the game resolves it against a loaded common zone at load time. The converter does not check the name against the zone.

| Asset type (referenced from) | Stock-resolvable now? | Mechanism | file:line |
|---|---|---|---|
| Material named by an FX effect (`stockmaterial`, shared `,name` in the `void` closure) | **Yes** (name-only) | `vfxReferences` / closure name-only refs | `iw3_fastfile.cpp:9387-9388`, `:12277-12278`; `convert.cpp:1797-1802`; `replay_stock.cpp:1007-1013`, `:1441-1446` |
| Material of the FX heat-haze / shock-ring alias (`elcq/vfx_dist_heat_1`, `..._shimmer_blast_1`) | **Yes** (name-only) | `plan.stockMaterials` → name-only in both zones | `iw3_render_assets.cpp:8497-8511` (insert at `:8509`); `iw3_fastfile.cpp:6789`; `replay_render.cpp:1292-1303`, `:3128-3136` |
| **World-surface material** (`material` on a BSP surface) | **No** | `ReadMaterial` needs `materials/<name>.json`; only `mc/lambert1` and `mc/gfx_missing_fx` are stubs | `iw3_render_assets.cpp:1954-1979` (`FindMaterial`), `:2539-2562` (`ReadMaterial`), `:5324` (call site) |
| Stock world material for a surface (`tw/shadowcaster`, `twc/...`, `tw/...`) | **Partly**: fixed names only, source JSON still required per surface | in-process `iw8::stock::Library` (no ACTS) | `replay_stock_library.cpp:91-102` (ctor), `:201-252` (`Material`); `iw3_render_assets.cpp:7421-7422`, `:7471`, `:7523`, `:7567`, `:7613`, `:7716` |
| XModel from an FX effect (`stockxmodel`) | **Yes** (name-only) | `vfxReferences` | `iw3_fastfile.cpp:9391-9392`, `:12281-12282`; `convert.cpp:1797-1802` |
| XModel in the `void` closure, local to `mp_shipment` | **Yes** (copied, reproducible models only) | ACTS export `-a xmodel -n` and re-write | `replay_stock.cpp:1264-1270`, `:1290-1335` |
| XModel in the `void` closure, not reproducible | **Yes** (name-only from `ww_common*`) | `kWorldwideCommon` loop | `replay_stock.cpp:27-28`, `:1345-1358` |
| **XModel named by `script_model` / `trigger_radius`** | **No** | `ReadSourceModel` needs `xmodel/<name>.json`; the extraction route throws | `iw3_fastfile.cpp:2963-2968` (read), `:635-640` (no common fallback for extraction), `:10698`, `:10798-10816` (gate and throw); `maps.cpp:472-495` (names) |
| XModelSurfs, VFX, PhysicsFXShape, LightDef, SurfaceFX, Image in the `void` closure | **Yes**, only for the `void` table closure and CODCaster images | ACTS export of `mp_shipment` / `ww_mp_shipment` / `eng_mp_shipment` | `replay_stock.cpp:57-78` (`Pool`), `:1168-1169`, `:1394-1400` |
| Image named by a world material | **No** (no stock path) | `LoadImage` reads `images/<name>.dds` or `.iwi` from the extraction and search paths | `iw3_render_assets.cpp:1274-1310` |
| Sound role (`--style iw8`) | Alias by role, not by name | ledger `sound:stock` | `iw3_fastfile.cpp:1971-1972`, `:12412`, `:12472` |

## 2. Where stock-by-name is decided, and the rules

### 2.1 Copier (ACTS-backed) for the `void` table

- Constants: source map `mp_shipment` (`replay_stock.cpp:19`), table `void` (`:20`), worldwide common zones `ww_common`, `ww_common_mp`, `ww_common_base_mp`, `ww_common_core_mp` (`:27-28`), export format tag `stock-export-1` (`:30`).
- Pools exported by ACTS (`Pool`, `:57-78`): `vfx`, `physicsfxshape`, `lightdef`, `xmodel`, `xmodelsurfs`, `surfacefx`, `image`. **`material` is not in `Pool`**, so the copier cannot look up a material file.
- Asset types it accepts as references (`AssetType`, `:175-192`): `Material`, `XModel`, `XModelSurfs`, `ParticleSystemDef`, `PhysicsFXPipeline`, `PhysicsFXShape`, `PhysicsAsset`, `VectorField`, `GfxLightDef`. Anything else throws (`:52`).
- Decision rule for each reference (`Copier::Copier`, `replay_stock.cpp:1157`):
  - **Comma prefix** (`ReadReference`, `:89-98`) means the shipped zone does not own it. A shared ref is looked up in `ww_mp_shipment` only (`:56-57`). If not found, it becomes a **name-only reference** (`:88-93`, `:108-111`, `:115-123`).
  - **No comma** means the asset is local to `mp_shipment`. It is looked up in `mp_shipment`, then `ww_mp_shipment` (`:52-55`). Effects and shapes and lights are copied. Models are copied if `ParseModel` can reproduce them (`:1264-1270`, `:1290-1305`). A **local material** or pipeline or vector field or physics asset throws (`:1271-1278`), because the copier does not carry those.
  - A model that `ParseModel` cannot reproduce ("uses detailCollision" in the log, `run.log:88-90`) is looked up in all four `ww_common*` zones (`:1345-1358`). If found, a name-only reference is made. If not found, it throws.
- The ACTS export is run once per zone and pool (`RunExport`, `:294-418`). Calls: `mp_shipment` with `vfx,physicsfxshape,surfacefx,lightdef` (`:1168`); `ww_mp_shipment` with `vfx,physicsfxshape,lightdef` (`:1169`); models in `mp_shipment` or `ww_mp_shipment` (`:1295`); surfaces (`:1319`); `ww_common*` (`:1348`); CODCaster images in `eng_mp_shipment` and `ww_mp_shipment` (`:1394`, `:1400`).

### 2.2 Replay in-process library (no ACTS)

- `iw8::stock::Library(zoneFolder, map = "mp_shipment")` (`replay_stock_library.h:16-31`) loads `zoneFolder/techsets_<map>.ff` and `zoneFolder/code_post_gfx.ff` through the zone loader (`replay_stock_library.cpp:91-102`), then indexes techsets and materials by name (`:56-`).
- `Library::Material(name)` returns a Replay material JSON (techset name, textures, constant table, buffers, layers) or throws `no stock Replay material <name>` (`:201-252`). It is the only route here that returns a **full** stock material by name.
- Names used in code are hard-coded: `tw/shadowcaster` and `tw/plastic_pvc_wire_cable_sheet_01_ge128` (`iw3_render_assets.cpp:7421-7422`), `twc/dub_water_wall_waves_dist` (`:7471`), `tw/uk_periph_emissive_light` (`:7523`), `twc/paint_lines_01_thick_yellow_clean` (`:7613`), and the layered `twc4/...` set (`:7716`). A second library uses map `common` (`:8524`), which loads `techsets_common.ff`.
- Output of a stock material is a **generated** material per source name: `w/mw120r_<map>_stock_<id>` (`:7376`, `addMaterial` in the `buildStockWorldMaterials` lambda at `:7233-`). The surface still needs its source material JSON (see §2.3).

### 2.3 World surfaces (BSP materials)

- Every world-surface material name goes through `ReadMaterial` (`iw3_render_assets.cpp:5322-5324`). `ReadMaterial` (`:2539-2563`) calls `FindMaterial` (`:1954-1979`), which searches `<extraction>/materials/<name>.json`, then `materials/<name without mc/>.json` for `mc/` names, then each `--search-path` root in the same form. If nothing is found, it returns the `mc/lambert1` / `mc/gfx_missing_fx` stub (`:2550-2557`) or throws (`:2559-2562`).
- `HasStaticModelSurface` (`:5254-5260`) also calls `ReadMaterial`.
- `FindMaterial` never looks in Replay's `zone` folder. The `replayZones` argument of `PrepareRenderAssets` (`:5277`) is used only for the library above.
- A surface's `material` index is `worldMaterialIndex` or `stockWorldMaterialIndex` into `plan.additionalMaterials` (`iw3_fastfile.cpp:3927-3941`, `:6477`). A stock surface needs a **native lightmap** (`StockLightmapUv`, `iw3_fastfile.cpp:3984-3996`, throws `a stock Replay world surface needs a lightmap`).

### 2.4 XModels named by entities

- Names come from `convert::iw3ReplayEntityModels` (`src/games/iw8/dump/maps.cpp:472-495`): `script_model` and `trigger_radius` only. `misc_model` is excluded (`:483-489` comment).
- `ReadSourceModel` (`iw3_fastfile.cpp:2963-2968`) reads `<root>/xmodel/<name>.json` and throws if absent. It is called for every kept entity model (`:11461`, `:11467`, `:11486`, `:11503`) and for script models (`:11383-11387` filter, precache only if present).
- In extraction mode `external = true` and `cached = true` (`:10695-10698`), so `CopyExtractedEntityModels` (`:10799-10806`, guarded by `!cached`) does not run. `FindIw3CommonFastfile` returns `{}` for an extraction (`:638-639`). So the throw at `:10809-10816` ("IW3 entity model assets are absent from the map fastfile ... add the IW3 installation root") is the outcome for a missing `script_model` xmodel.
- Not gated by `cached`: FX dependency models (`:11131`) and the small-glass shard check (`:11182`). Both use `--search-path` roots, not Replay zones.

### 2.5 FX-driven name-only references

- FX JSON types `stockmaterial` and `stockxmodel` are parsed at `iw3_fastfile.cpp:9387-9392` and turned into `vfxReferences` at `:12277-12283`. The converter writes them with `RegisterNameReference` (`convert.cpp:1797-1802`). For materials the techset zone also gets one (`:1801-1802`).
- `RegisterNameReference` (`replay_stock.cpp:1421-1424`) calls `RegisterReference` (`:1007-1027`). For a material it calls `replayrender::RegisterMaterialReference` (`replay_render.cpp:3054-3061`). For other types it writes a zeroed stub header plus the string `,name` (`:1016-1025`).
- The FX distortion alias (`iw3_render_assets.cpp:8497-8511`) puts a stock name in `plan.stockMaterials` (`:8509`). The output field is `stockMaterials` (`iw3_fastfile.cpp:6789`). The converter reads it back in `replay_render.cpp:1292-1303` and registers name-only material references in both zones (`:3128-3136`).

## 3. Evidence from the x64zt run

### 3.1 Stock material references already in the build

In `E:\IW8-Radiant\out\x64zt-extraction-test\output\_stock_cache\mp_shipment-0507CF8E24D3\mw19replay\mp_shipment\assets\` (the export of `vfx,physicsfxshape,surfacefx,lightdef`), the structured JSON holds shared references in this form:

```
"type":"Material","asset_reference":true,"name":",elcq/vfx_ui_chevron_2_ds"
```

Counts of unique shared names across that folder: **144 Material**, **43 XModel**, **48 ParticleSystemDef**, **6 PhysicsFXShape**. The `void` table itself references only 31 `ParticleSystemDef` names (`surfacefx/void.453.asset.json`). The material and model names come from the effects in its closure. The log's total of 164 name-only references (`run.log:98`) is consistent with this, but the log does not break it down by type (UNKNOWN exact split).

These are FX materials. None of them is a BSP surface material.

### 3.2 Cache layout and ACTS invocation (what the converter consumes)

Command, built in `RunExport` (`replay_stock.cpp:159-181`):

```
acts.exe --noUpdater fastfile -r mw19replay -g <game_dx12_ship_replay.exe>
         [--oodle <replay>\oo2core_7_win64.dll]           (only if present)
         -a <types,csv>  [-n <name1,name2,...>]           (-n only when names are given)
         -l -o <cacheDir> <zone .ff>
```

Environment set for the child process (`:201-202`, cleared after `:206-207`): `ACTS_NATIVE_BYTES=1`, `ACTS_REPLAY_STRUCTURED=1`. Timeout 15 minutes (`:211`). Success requires exit 0 and `mw19replay/<zone>/assets.jsonl` (`:222`). Log to `<cacheDir>/acts.log` (`:186`). Output layout (`:310-312`, `:340`, `:392-399`):

```
<cache>/<zone>-<FNV-hash12>/
    complete.txt                       (the request string, written on success)
    acts.log
    mw19replay/<zone>/
        assets.jsonl                   (one JSON row per asset: type, name, status, file, format "mw19-asset-json-v1")
        assets/<pool>/<name>.<n>.asset.json
        script_strings.json            (when present)
```

The hash key is FNV-1a over `stock-export-1|<zone file>|<size>|<mtime>|<types>|<names>` (`:145-151`, `:308-310`). Real examples: `_stock_cache\mp_shipment-0507CF8E24D3`, `ww_common-094305DD029C`, `eng_mp_shipment-C4931AF36064`.

ACTS side (`codex/mw2019-complete`):

- `-r mw19replay`, `-g`, `--oodle`, `-a`, `-n`, `-o` are documented in `docs/mw19/fastfile.md:17-19` and `:44-48` (`-a` types, `-n` exact names, output `mw19replay/<zone>/assets/<type>/`). `material` is one of the 92 structured types (`fastfile.md:47`).
- `-n` is parsed at `src/core/acts/tools/fastfile/fastfile_handlers.cpp:221`. `-a` at `:347`. `-o` at `:301`. `-l` appears at `fastfile_handlers.cpp:201` as `--linker` (a writer option that takes a value) and at `src/core/acts/main.cpp:102` as `--log`. The copier uses it as a bare flag and the export runs, so it must be the log flag in the reader context. Exact meaning of `-l` in the reader: UNKNOWN (not traced further).

### 3.3 Zones searched, and which options control them

Fixed in code. No option lists zones or names.

| Zone | Used for | file:line |
|---|---|---|
| `<replay exe dir>\zone\` (`--replay`) | root of all stock lookups | `convert.cpp:2511`; `replay_stock.cpp:1161` |
| `mp_shipment.ff` | `void` closure, FX, local models | `replay_stock.cpp:1162`, `:1168` |
| `worldwide\ww_mp_shipment.ff` | shared refs, worldwide copies, CODCaster images | `replay_stock.cpp:1163`, `:1169`, `:1400` |
| `english\eng_mp_shipment.ff` | English CODCaster image | `replay_stock.cpp:1164`, `:1394` |
| `worldwide\ww_common{,_mp,_base_mp,_core_mp}.ff` | models that cannot be copied | `replay_stock.cpp:27-28`, `:1346-1348` |
| `techsets_mp_shipment.ff` + `code_post_gfx.ff` | `Library` (default map) | `replay_stock_library.cpp:93-94`; `replay_stock_library.h:21` |
| `techsets_common.ff` + `code_post_gfx.ff` | `Library(..., "common")` | `iw3_render_assets.cpp:8524` |

Options that exist: `--replay` (sets the zone folder, `convert.cpp:2511`), `--acts` or env `IW8_ACTS` (`:1553-1556`, `:2678`), `--stock_cache` (default `<output parent>\_stock_cache`, `:1566-1568`, `:2679`), `--search_path` (adds an extracted-asset root for `materials/`, `xmodel/`, `images/` only, `:2527-2529`, `:2681-2683`). There is no option for a stock-name list, a zone list, or stock materials on world surfaces. The JSON flags are `world.replay_stock_world` (`iw3_render_assets.cpp:6241`, `:7814`) and per-material `replayStockOnly` (`:2702`), which only affect the generated stock world materials.

## 4. Smallest change to support stock materials and models (NOT APPLIED)

The questions have different answers, so two changes.

### 4.1 Models named by `script_model` (smallest; name-only, no copy)

Idea: a missing `script_model` xmodel that exists in a loaded common zone becomes a name-only XModel reference. That is the mechanism already used for FX `stockxmodel` (§2.5). Nothing needs copying at build time, and no ACTS run is needed, only a presence check.

Sketch (a diff, not applied):

```diff
--- iw3_fastfile.cpp  (PrepareFastfile, after line 10806)
+    // Extraction route: a name that a worldwide-common zone owns is a name-only XModel reference.
+    std::vector<std::string> stockEntityModels;
+    if (external && !missingEntityModels.empty())
+    {
+        for (const auto &name : missingEntityModels)
+            if (StockXModelExists(options.replayZones, name))      // new: in-process replay::load_zone
+                stockEntityModels.push_back(name);                 // or ACTS -a xmodel -n, as RunExport does
+        std::erase_if(missingEntityModels, [&](const std::string &n) {
+            return std::find(stockEntityModels.begin(), stockEntityModels.end(), n) != stockEntityModels.end(); });
+    }
     if (!missingEntityModels.empty()) { ... throw ... }           // unchanged for the rest

--- iw3_fastfile.cpp  (keptEntityModelNames, :11461-11503)
-    sourceEntityModels.models = ParallelMap<...>(keptEntityModelNames.size(), ...ReadSourceModel...)
+    // skip stockEntityModels: they are written as name-only references, not read from xmodel/<name>.json
+    std::erase_if(keptEntityModelNames, [&](const std::string &n) { return stockEntityModels.contains(n); });

--- convert.cpp  (after the vfxReferences loop, :1797-1802)
+    for (const auto &name : prepared->stockEntityModels)
+        iw8::stock::RegisterNameReference(writer, ASSET_TYPE_XMODEL, name);
```

Requirements and unknowns:

- The entity writer must accept a model that has no `ReadSourceModel` result (bounds, LODs, collision). UNKNOWN which entity-record fields need them, and whether the placement needs a converted model record. Check how `script_model` placements are written before you rely on this.
- `StockXModelExists` must use `options.replayZones` and the four `ww_common*` zones. It does not exist yet. UNKNOWN whether `replay::load_zone` (already used at `replay_stock_library.cpp:98`) can index xmodels in those zones without the copier's ACTS path.
- The precache filter at `iw3_fastfile.cpp:11383-11387` should keep stock names, or they lose `precachemodel`. UNKNOWN whether the map script needs them precached.

### 4.2 Materials named by world surfaces (larger; UNKNOWN feasibility)

Idea: keep the surface's material as an additional-material entry whose name is the stock name, with no `materialDefinition`, and point `stockWorldMaterialIndex` at it. The FX distortion alias (`iw3_render_assets.cpp:8497-8511`) and the stock-world path (`:7376`) both use this pattern.

Sketch (a diff, not applied):

```diff
--- iw3_render_assets.cpp  (ReadMaterial, :2559)
-        throw std::runtime_error("missing IW3 material '" + name + ...);
+        if (!replayZones.empty() && StockMaterialExists(replayZones, name))   // new; Library::Has(name)
+        {
+            SourceMaterial stock;                 // stock name-only; no textures are read
+            stock.name = name;
+            stock.replayStockName = name;         // new field
+            return stock;
+        }
+        throw std::runtime_error(...);

--- iw3_render_assets.cpp  (buildStockWorldMaterials, after addMaterial, :7376)
+        for (const SourceMaterial &source : materials)
+            if (!source.replayStockName.empty() && neededWorldMaterials.contains(source.name))
+            {
+                plan.additionalMaterials.push_back({{"schema", 1}, {"material", source.replayStockName}});  // no materialDefinition
+                plan.stockMaterials.insert(source.replayStockName);                                           // :8509 pattern
+                plan.materials.at(source.name).stockWorldMaterialIndex = unsigned(plan.additionalMaterials.size());
+            }

--- replay_render.cpp  (RegisterMapMaterials, :3110-3125)
+    // an additional material without a definition is a name-only reference (both zones)
```

Requirements and unknowns (why this is larger):

- The draw path for a stock surface uses layout 1 and needs a **native lightmap** (`iw3_fastfile.cpp:6465-6477`, `StockLightmapUv` `:3984-3996`). So only lit surfaces of maps with `world.lightmaps` can use it. UNKNOWN whether the surface's lightmap UVs are right for a foreign stock techset.
- The stock material's techset must accept the world vertex layout and the native lightmap defines. Known only for `tw/` and `twc/` families (the library's zone). UNKNOWN whether `tm2co/` and other families load from `techsets_mp_shipment.ff`. The zone that holds a named material is UNKNOWN; you may need a `Library(replayZones, "common_mp")` (`techsets_common_mp.ff`, which exists in the Replay zone folder).
- The mesh parser for `additionalMaterials` (`replay_render.cpp`, near `:1290-1305`; exact lines UNKNOWN) may require `materialDefinition`. It must accept the name-only form.
- The name-only material's textures are the game's own, read from the common zone at load time. The converter reads none of them. UNKNOWN whether the game resolves the material's image references when the material comes from a common zone.
- `FindMaterial` (`:1954`) would still run first. Only a missing file triggers the new path, so existing extractions are unchanged.

ACTS would be needed only to confirm presence without a loader: `acts.exe ... -a material -n <name>` on `common_mp.ff` or `ww_common_mp.ff`. For that, `RunExport` (`replay_stock.cpp:414-415`) would need an exception for `material` as it has for `xmodel`, and `Pool` (`:57-78`) would need `material`. The in-process `Library` is simpler and needs neither.

## 5. UNKNOWN list

- Whether `tm2co/me_curb_stone_01` or any other BSP-surface material name lives in `techsets_mp_shipment.ff`, `techsets_common_mp.ff`, `code_post_gfx.ff` or another zone. The Replay zones are Oodle-compressed; a grep for the name finds nothing, so this needs the zone loader or ACTS.
- Whether the Replay techsets for non-`tw`/`twc` families are compatible with the converter's layout 1 and native lightmap path.
- The entity writer's requirements for a `script_model` with no source model (§4.1).
- The exact `-l` meaning in the ACTS reader (§3.2).
- The source of the grey you observed (§0).
- The split of the 164 name-only references by type (§3.1).

## 6. What was not done

- Nothing was built or run. zonetool and ACTS were not executed. `E:\mw124\x64-zt` and `E:\mw124\atian-cod-tools` were read through `git show` and `git grep` only.
- No file was changed except this one. Scratch copies of the source went to the session scratchpad.
