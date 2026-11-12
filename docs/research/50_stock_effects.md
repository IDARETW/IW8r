# 50. Stock effect inspector: route, schema, and what a preview needs

Scope: roadmap 8.9 (stock effect inspector: emitters, images, bounds, looping or one-shot) and the first step of 8.1 (FX preview). Read-only research plus the core reader `src/core/vfx_info.h/.cpp` (namespace `iw8r`) and `tests/vfx_info_tests.cpp`. Nothing was built except `iw8r_tests`. ACTS was not run: the exports already on disk were enough (section 5). Paths relative to `E:\IW8-Radiant` unless absolute. "x64-zt" is `E:\mw124\x64-zt`; "ACTS" is `E:\mw124\atian-cod-tools`.

## 1. Answer: the route

**Chosen route: (a), a structured ACTS export of the `vfx` pool.** ACTS already has the type and the editor already runs it for materials and images. One structured file per effect carries the whole `ParticleSystemDef` graph, and the reader parses it. Options (b) and (c) are not sources for the inspector:

- (b) x64-zt `replay_vfx.h/.cpp` is the writer (source graph to zone). It names the same four reference kinds the inspector needs (materials, models, light defs, child effects) but does not read stock records, so it adds no data source.
- (c) `data\asset_catalog\iw8_common_vfx.tsv` is only a name list (section 4). It gives names and zones, not contents.

### 1.1 ACTS asset type

- The `vfx` pool is in the MW19 type table at `E:\mw124\atian-cod-tools\src\core\acts\tools\mw19\mw19.cpp:55` (name list) and `:580` (pool id `0x2c`, comment `// 2c`). The export confirms pool id 44 (`"pool_id":44`).
- It is a structured type. `docs\mw19\fastfile.md` (around lines 40-50) says the other 92 enabled types write a versioned structured `.asset.json`. `vfx` is one of them.
- Schema: `E:\mw124\atian-cod-tools\config\data\mw19\schema.json:461-467` (pool `vfx`, element size 128, root `ParticleSystemDef`). The struct definition is at `schema.json:17671` (`ParticleSystemDef`, size 128).

### 1.2 Command line

The editor's own ACTS call is `E:\IW8-Radiant\src\qtapp\material_cache.cpp:394-397`:

```
acts --noUpdater fastfile -r mw19replay -g <replay exe> [--oodle <oo2core_7_win64.dll>] [--xpak <each>] -a <type> -n <names,csv> -l -o <dir> <zone.ff>
```

For effects, change only `-a` to `vfx`:

```
acts --noUpdater fastfile -r mw19replay -g "E:/IW8/Builds/1.20-replay/game_dx12_ship_replay.exe" --oodle "E:/IW8/Builds/1.20-replay/oo2core_7_win64.dll" -a vfx -n "<effect name>" -l -o "E:/IW8-Radiant/out/vfx_probe/<run>" "E:/IW8/Builds/1.20-replay/zone/common_mp.ff"
```

Points to check before relying on it:

- **Environment.** The x64-zt stock exporter sets `ACTS_NATIVE_BYTES=1` and `ACTS_REPLAY_STRUCTURED=1` for the child process (`E:\mw124\x64-zt\src\games\iw8\assets\replay_stock.cpp:360-366`). The export on disk has `$native` keys, so it was made that way. The editor's `material_cache.cpp` sets neither (a grep finds no `ACTS_` or `Environment` there). A vfx run from the editor needs both variables set on its `QProcess`. This is not tested.
- **Name form.** `-n` takes exact names (`docs\mw19\fastfile.md:19`). The catalog form has no `.vfx` suffix (`vfx/core/mp/core/vfx_hp_chev_grey`), while the exported `name.string` has it (`vfx/core/mp/core/vfx_hp_chev_grey.vfx`). Which form `-n` matches is UNVERIFIED. Try the catalog form first, then the suffixed form.
- **Output path.** `-o <dir>` gives `<dir>/mw19replay/<zone>/assets/vfx/<name path>.<n>.asset.json` and an `assets.jsonl` index. The x64-zt cache layout is in `docs\research\18_x64zt_stock_assets.md` section 3.2 (lines 85-110). The material export has the same root in `docs\research\16_acts_asset_exports.md` (format `mw19-asset-json-v1`). The vfx format string is `mw19-asset-json` with `format_version` 1, so the reader accepts any `mw19-asset-json*` prefix.
- **Exit/log.** The editor treats a run as good when the process exits 0 and `assets.jsonl` exists. The x64-zt exporter additionally requires `complete.txt` (`replay_stock.cpp`, see doc 18 section 3.2).

## 2. Export format (what the reader uses)

Root (`json_lite` object): `format`, `format_version`, `profile` (`replay-1.20`), `pool` (`vfx`), `pool_id` (44), `root_type` (`ParticleSystemDef`), `asset`.

`asset.fields` (the loaded `ParticleSystemDef`, field names as in `E:\mw124\x64-zt\src\games\iw8\iw8_focus_structs.h:947-970`):

| Field | Used for | Sample (`vfx_hp_chev_grey`) |
|---|---|---|
| `name.string` | `VfxInfo::name` | `vfx/core/mp/core/vfx_hp_chev_grey.vfx` |
| `numEmitters` | `emitterCount` (fallback: `emitterDefs.values` length) | 4 |
| `emitterDefs.values[i]` (ParticleEmitterDef, `iw8_focus_structs.h:916-943`) | `flags`, `emitterLife {min,max}`, `emitterDelay {min,max}`, `particleLife {min,max}` | emitter 0: `particleLife` 2.0, `emitterLife` 0 |
| `flags` (emitter) | `looping` (bits `0x8`, `0x10`) | 4194304 (`0x400000`, no loop bit) |
| `preRollMSec`, `drawFrustumCullRadius`, `updateFrustumCullRadius`, `sunDistance` | not in `VfxInfo` yet (bounds, see 3.3) | -1.0, -1.0, 100000 |

Reference fields (found by a walk over `fields`, because they sit inside module payloads at varying depth):

- An object with `"asset_reference": true`, `"type"`, `"name"`. The name may start with `,`, which marks a shared name from another zone (the structured-JSON example in `docs\research\18_x64zt_stock_assets.md`, near line 78). The reader strips it.
  - `"type":"Material"` → `materials` (758 refs over the 440 exports).
  - `"type":"XModel"` → `models` (328).
  - `"type":"ParticleSystemDef"` → `childEffects` (128).
  - `PhysicsFXPipeline` (206), `PhysicsFXShape` (206), `GfxLightDef` (51), `VectorField` (9) are real references, but `VfxInfo` has no list for them. They are skipped on purpose.
- Sounds are not asset references. In `PARTICLE_MODULE_INIT_SOUND` the payload is `m_linkedAssetList.assetList.values[i]`, with `active_member` = `"sound"` and `value.string` = the alias (for example `shell_eject_20mm`). The reader collects those strings. 234 INIT_SOUND modules in the 440 exports.

Module payloads are not decoded. The reader only walks them for the references above. Decoding them is the step-8.1 work (section 3.4).

Looping and life (the heuristic):

- `looping` is true if any emitter has flag `0x8` (`PARTICLE_EMITTER_DEF_FLAG_INFINITE_PARTICLE_LIFE`) or `0x10` (`..._LOOP_INFINITE_PARTICLES`). These names and values come from the IW7 header `E:\mw124\x64-zt\src\games\iw7\structs.hpp:8729-8736`. The Replay 1.20 flag values are NOT CONFIRMED, but the data is consistent with them: across the 440 exports, 11 effects have one of these bits, and they are the looping ones (`vfx/iw8/veh/scriptables/vfx_veh_headlight_victor40_left`, `..._right`, `vfx_veh_policelight_victor40`, `vfx_veh_taillight_victor40_left`, and their `_nolight` variants).
- `emitterLife.max == 0` does NOT mean looping. 1168 of 1251 emitters have zero emitter life, and most of them are one-shot impacts that end when their particles die.
- `lifeSeconds` = max over emitters of `emitterDelay.max + (emitterLife.max > 0 ? emitterLife.max : particleLife.max)`, or `-1` when looping. It is an estimate: child effects and the preroll are not added.

## 3. What an inspector can show, and what it needs

### 3.1 Available now (`VfxInfo`)

- Name, emitter count, looping or one-shot, life estimate.
- Material, model, sound and child-effect dependencies, unique, in export order.

### 3.2 Available in the export but not in `VfxInfo`

- Per emitter: state count, particle spawn rate and burst count, particle count max, `randomSeed`, spawn shape range, fade distance (`fadeOutMaxDistance`), emitter flags (the raw bits, for a table).
- Per module: module type name (for example `PARTICLE_MODULE_INIT_SPAWN` 1257 modules, `PARTICLE_MODULE_INIT_MODEL` 237, `PARTICLE_MODULE_PHYSICS_RAY_CAST` 412), which shows what each emitter does.
- Physics and light references: PhysicsFXPipeline/Shape, GfxLightDef, VectorField (these matter for the inspector's dependency list).

### 3.3 Bounds

- The effect-level radii are `drawFrustumCullRadius` and `updateFrustumCullRadius`. The sample has -1.0 for both, which probably means "not set" (UNVERIFIED). `sunDistance` is also an effect-level value.
- Per emitter there is `spawnFrustumCullRadius` (`iw8_focus_structs.h:931`) and the spawn shape range (`particleSpawnShapeRange`, `:940`).
- No axis-aligned bounding box is stored. The radii are the closest thing to bounds, so the inspector can show them as-is with a note on their unknown meaning.
- To show them, `VfxInfo` needs four new members (`drawFrustumCullRadius`, `updateFrustumCullRadius`, `fadeOutMaxDistance` (max over emitters), and a per-emitter list). That is a follow-up change, not part of this step.

### 3.4 What a simple preview (8.1) would need

1. Decode the emitter and module payloads, not only the references. The fork's reader `E:\mw124\x64-zt\src\games\iw8\neutral\vfx.cpp` already decodes them (for example `read_particle_float_range` at `:394`, the `m_loopCount` read at `:561`). Port the part a preview needs, or call the same decoder on the export bytes.
2. Load the referenced materials (for the sprite texture) and, where the module says so, the models. The material and image path is the existing material cache (`src\qtapp\material_cache.cpp`).
3. A draw path. The viewport (`src\viewport`) renders meshes and not particles, so a billboard pass is new work. The minimum is spawn-rate and life per emitter, billboards at the origin, colour and size graphs from the modules.
4. Skip the physics modules (`PHYSICS_RAY_CAST`, `PHYSICS_LIGHT`) and child effects in the first preview. They are not needed to see the effect.

## 4. The effect catalog (`data\asset_catalog\iw8_common_vfx.tsv`)

- 1,020 effect rows, plus 2 comment lines. Format: `name<TAB>zones`. Zones seen: `common_mp`, `common_base_mp`, `common_core_mp`. It was listed by ACTS on 2026-11-09 (header comment, lines 1-2).
- Names have no `.vfx` suffix (line 70: `vfx/core/mp/core/vfx_hp_chev_grey	common_core_mp`). The export name has the suffix. Use the catalog to pick an effect and its zone, then the export for the contents.
- The catalog says which zone to pass to ACTS. It does not say what the effect contains.

## 5. Exports already on disk (read-only)

- `C:\Users\user\AppData\Local\IW8 Radiant Suite\IW8 Radiant Suite\x64zt_output\_stock_cache\mp_shipment-0507CF8E24D3\` is an ACTS stock export of `mp_shipment.ff` with types `vfx,physicsfxshape,surfacefx,lightdef` (`complete.txt`, key `stock-export-1|mp_shipment.ff|187760776|134284587247023686|vfx,physicsfxshape,surfacefx,lightdef`). `acts.log` reports 10478 assets loaded, 454 tested, 0 failed.
- The vfx files are at `mw19replay\mp_shipment\assets\vfx\vfx\...` (440 `*.asset.json`, 0 parse errors).
- Survey of the 440 (done with a read-only script, `E:\IW8-Radiant\out\vfx_probe\survey.py`, `survey2.py`):
  - `numEmitters`: 0 (3 files), 1 (97), 2 (221), 3 (36), 4 (23), 5-19 (59), 23 (1).
  - Emitters: 1251. Emitter life zero on 1168. Looping flag on 11 effects.
  - Reference types: as in section 2.
- The export is therefore available for every effect in the mp_shipment cache, so the inspector can read it without running ACTS. An ACTS run is needed only for an effect that is not in the cache (see 1.2).

## 6. Core reader (what was built, and what is tested)

- `src\core\vfx_info.h`: `struct VfxInfo` with the fields the step asked for (`name`, `materials`, `models`, `sounds`, `childEffects`, `emitterCount`, `looping`, `lifeSeconds`). `parseVfxExport(text, out, err)` and `readVfxExport(path, out, err)`.
- `src\core\vfx_info.cpp`: uses `json_lite` (`src\core\json_lite.h`). It rejects any input that is not an `mw19-asset-json*` export with `root_type` `ParticleSystemDef`.
- `tests\vfx_info_tests.cpp`, 6 tests:
  - Synthetic one-shot effect: fields, dedup of a shared and an unshared material name, inline sound, model, child effect, and that a PhysicsFXShape reference does not leak into the lists.
  - Synthetic looping effect (flag `0x8`): `looping` true, `lifeSeconds` -1.
  - `numEmitters` absent: falls back to the row count.
  - Rejects: non-JSON, an array root, a wrong format, a wrong `root_type`, and a missing name.
  - `readVfxExport` on a file in the test temp directory, and on a missing file.
  - Real export (optional): reads the file named by `IW8R_VFX_SAMPLE`. Without the variable it prints a skip note and passes.

### 6.1 Test status

- Built with `xmake build iw8r_tests`. Run: `bin\release\iw8r_tests.exe`, exit 0, "94295 checks, 0 failures, 705 tests" without the variable.
- Run with `IW8R_VFX_SAMPLE` set to `vfx_hp_chev_grey.vfx.33.asset.json`: "94300 checks, 0 failures, 705 tests". The real-export line printed: `vfx/core/mp/core/vfx_hp_chev_grey.vfx emitters=4 looping=0 life=2.00 materials=2 models=0 sounds=0 children=0`.
- **Tested on real data:** one real export (`vfx_hp_chev_grey`, a one-shot UI chevron), only the fields listed above. The looping rule was checked against the 440 files with a Python survey, not with the C++ reader. The C++ reader has not been run on a looping real export.
- **Not tested:** the flag names for Replay 1.20 (IW7 names), the `lifeSeconds` estimate against the game's actual run time, `-n` name matching for `vfx` (section 1.2), and an ACTS run from the editor (no run was done).

## 7. Open items

- Confirm the Replay 1.20 emitter flag bits for infinite life and loop (run one looping effect in game, or read the replay exe's flag table).
- Confirm `-n` name matching for `vfx`, and add `ACTS_NATIVE_BYTES` and `ACTS_REPLAY_STRUCTURED` to the editor's process environment if the inspector runs ACTS itself.
- Extend `VfxInfo` with the radii and emitter table (section 3.2-3.3) for the 8.9 UI.
- `docs\PROGRESS.md` was not updated: this step's rules limit file edits to the three files above.
