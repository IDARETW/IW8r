# 22. Custom models: OBJ import to the x64-zt extraction layout

Scope: how a mapper's own model reaches MW2019 Replay 1.20 through the x64-zt `buildextraction` route, the exact files `ReadSourceModel` reads, the worked example written by `src/core/model_import.cpp`, and what is still open. Date: 2026-11-08. Paths are relative to `E:\mw124\x64-zt\` unless they start with `E:\IW8-Radiant`. Line numbers are from this checkout. Read-only on x64-zt: nothing there was edited or built.

## 0. Summary

1. A model is an extraction folder pair: `extracted/xmodel/<name>.json` (table: type, collision LOD, LOD distances) and `extracted/xmodel/<name>.replay.json` (geometry: one surface per material per LOD). Each surface names a material, and that material is `extracted/materials/<name>.json`. No other files are needed for a `script_model` (no `.xanim`, no physics preset, no skeleton; the bone is a fallback).
2. `iw8r::importObj` / `iw8r::writeExtractionModel` (`src/core/model_import.h/.cpp`) produce exactly those files from an OBJ. Verified end to end: `zonetool.exe ... buildextraction` on a compiled sample room with a cube added exits 0 and publishes `mp_iw8r_cmod` (section 7).
3. Winding is the one convention that conflicts inside x64-zt. Models are read as clockwise seen from outside; world surfaces are aligned counter-clockwise. The writer follows the model rule. Confirm in game (section 8, item 1).
4. The compiler still drops `model` from `script_model` (`src/core/compile_iw8.cpp:809-818`). The e2e added the key in the extraction copy by hand. Keeping it needs a change in the compiler and exporter, which another agent owns right now.

## 1. What x64-zt reads, with citations

### 1.1 Entry points

| What | Where |
|---|---|
| `ReadSourceModel(root, modelName, placedStaticBindPose, skeletonRoot)` | `src/convert/iw3_iw8/iw3_fastfile.cpp:2960-3068` |
| `ReadRawGeometry` (`.replay.json`) | `iw3_fastfile.cpp:2292-2324` |
| `RawSurfaceFrom` (one surface) | `iw3_fastfile.cpp:2238-2290` |
| `ReadWorldSurface` (vertex and index checks, normal and tangent handling) | `iw3_fastfile.cpp:2400-2450` |
| LOD distance mapping, minimum draw distance, LOD padding | `iw3_fastfile.cpp:2667-2676`, `:3020-3030`, `:3069-3108` |
| Entity model names (`script_model`, `trigger_radius`, not `*N`) | `src/games/iw8/dump/maps.cpp:472-495` (`iw3ReplayEntityModels`) |
| Entity model load (kept entity models) | `iw3_fastfile.cpp:11403-11411` |
| Unscripted `script_model` promoted to a static placement | `iw3_fastfile.cpp:4339-4390` (`TakeUnscriptedScriptModels`), `:11395` |
| Static placements (`world.models[]`) | `iw3_fastfile.cpp:3735-3825` |
| `FindMaterial`, `ReadMaterial` (`materials/<name>.json`) | `src/convert/iw3_iw8/iw3_render_assets.cpp:2539-2562`; doc 18 §2.3 |
| Collision LOD (`collLod`) | `iw3_fastfile.cpp:2991-2995`, `:4663-4665`, `:12775-12815` |

### 1.2 Model table: `xmodel/<name>.json`

Read by `ReadSourceModel`:

- `type`: must be `"rigid"` on the static path. `"animated"` is accepted only with a bind pose and a skeleton (`iw3_fastfile.cpp:2967-2976`).
- `lods`: a non-empty array of at most 6 objects, each with `distance` (`:2971-2973`, `:3021-3024`). Other keys are ignored.
- `collLod`: optional, default `-1` (no collision). Must be `< lods.size()` (`:2991-2993`). LOD `collLod`'s surfaces become the static-model collision (`:12775-12815`).
- `reactiveMotion`: optional (`:2980-2990`). Not used.

### 1.3 Geometry: `xmodel/<name>.replay.json`

Required by `ReadSourceModel` (`:2996-3004`): the file must exist. Schema 1 and the name must match. The LOD count must equal the table's (`:3002-3004`).

Top level:

| Key | Required | Notes |
|---|---|---|
| `schema` | yes (must be 1) | `:3002`. Read with default 0, so absence fails. |
| `name` | yes | must equal the model name (`:3002`, `at("name")` at `:2301`). |
| `lods` | yes | array; LOD `i` pairs with table LOD `i` (`:3028`). |
| `bone_count` | no | `1` with no `root_bone` means "one bone, no bind pose"; it also suppresses the skeleton warning (`:3016-3017`). |
| `root_bone` | no | `{name, quat[4], trans[3], trans_weight}`; only with `bone_count` 1 (`:3006-3015`). Not needed. |
| `contents` | no | default `1` (`:3005`). |

Each `lods[i]`:

- `distance` (double). Must equal the table's `distance` after `static_cast<float>` (`:3024`, `:3029`): the two files must agree exactly. Values must be finite and non-decreasing (`:3025-3026`).
- `surfaces`: array. Empty throws (`:3030`, `:3066`).

Each surface (`RawSurfaceFrom`, `:2238-2290`):

| Key | Required | Notes |
|---|---|---|
| `material` | yes | string, `materials/<name>.json` (section 1.4). |
| `reflection_probe` | yes | `at()` (`:2243`). Range-checked only on world surfaces (`:2412-2414`); for a model use `0`. |
| `deformed` | yes for the static path | `ReadSourceModel` throws when it is absent (`:3038-3039`). `false` for a rigid model. `true` requires a bind pose and throws otherwise (`:3040-3041`). |
| `vertices` | yes, non-empty | `:2248`, `:2415`. |
| `indices` | yes, multiple of 3 | `:2415`, `:2420-2425`. Each index must be `< vertices.size()`. |
| `lightmap` | no | default `-1` (`:2242`). Ignored for models. |
| `shadow_lights` | no | `[uint]` (`:2244-2247`). |

Each vertex (`:2255-2284`):

| Key | Required | Notes |
|---|---|---|
| `position` | yes | `[x,y,z]`, floats (`FloatVector` throws on non-finite, `:2347-2352`). |
| `normal` | yes | normalised by the reader (`:2425`). |
| `tangent` | yes | a zero tangent is replaced by a cross product (`:2427-2430`). |
| `binormal_sign` | yes | `|v| < 0.5` throws; otherwise stored as +1 or -1 (`:2431-2434`). |
| `uv` | yes | `[u,v]`. |
| `lightmap_uv` | yes | `[u,v]`, `[0,0]` for models. |
| `color` | no | `[r,g,b,a]` ints 0..255 (`:2265-2270`). Missing means the default colour. |
| `layer_uvs`, `layer_transforms` | no | stock layered surfaces only (`:2271-2283`). Not used. |

Values are plain floats, not packed. The IW7 exporter unpacks its 10-bit normals before writing (`src/convert/iw7_iw8/export_models.cpp:40-53`).

### 1.4 Material: `materials/<name>.json`

Doc 15 §4.2 and doc 18 §2.3 already cover it. For a model surface the name is `materials/<name>.json` with `/` allowed (`materials/room/grey.json` in the contract example). The existing grey placeholder is `out\m5\sample\extraction\extracted\materials\iw8r\grey.json`:

```json
{"techniqueSet":"iw8r_grey","textures":[{"name":"colorMap","semantic":"colorMap","image":"$white"},{"name":"normalMap","semantic":"normalMap","image":"$identitynormalmap"}]}
```

Built-in names `$white` and `$identitynormalmap` need no image file (doc 15 §4.2). The e2e build generated a material variant from it (section 7).

### 1.5 Limits

| Limit | Value | Where |
|---|---|---|
| LODs per model | 1 to 6 | `:2971-2973` |
| Surfaces per model, all LODs together | 255 | `:3078-3080` ("Replay static models index at most 255 surfaces across all LODs") |
| Vertices per surface | at most 65535 | `:4729-4731` (static-model path) |
| Triangles per surface | at most 65535 | `:4729-4732` |
| Surface `reflection_probe` | at most 65535; not range-checked for models | `:2412-2414` |
| Minimum LOD draw distance | 4000 | `:2671`, `:3071-3072` |
| LOD distance scale | `distance * 2.0` (IW3 to Replay), capped at 1000000 | `:2667`, `:2673-2677` |
| Padding | short LOD lists are padded to 6 with copies of the lowest LOD | `:3073-3108` |

### 1.6 Winding, UV, tangents: conflicts and decisions

- Models: `iw3_fastfile.cpp:3061-3062` ("IW3 and Replay XSurfaces use the same clockwise winding. World-surface normal alignment would turn these native model faces inside out."). `:5150-5151` ("IW3 model triangles are clockwise seen from outside"). So the front face of a model triangle has `cross(b-a, c-a)` pointing away from its normal.
- World surfaces: `AlignWinding` (`:2482-2497`, applied at `:3681`) makes `cross(b-a, c-a)` follow the normal. That is counter-clockwise seen from outside.
- Doc 15 §7's room example uses the world rule. It is marked NOT-VERIFIED there. The writer uses the model rule (section 8, item 1).
- UV: no flip is applied by the reader. The writer flips V (`v' = 1 - v_obj`) because OBJ puts V = 0 at the bottom and D3D at the top. This is an assumption (section 8, item 2).
- Tangent: the reader takes it as given. The writer computes it from the UV gradient and sets `binormal_sign` so that `binormal = cross(normal, tangent) * sign` follows the UV bitangent.

### 1.7 Scale, axes, bounds

- No bounds field exists in either file. The converter computes model bounds from the vertices (`iw3_fastfile.cpp:4670` onwards, `replaybounds::Accumulator`). `ImportedModel::bounds` is only for the editor.
- Units are Radiant units (inches). Radiant is Z-up (`src/core/geometry.h:2`). The writer maps OBJ Y-up to Z-up with `(x, y, z) -> (x, -z, y)` when `zUp` is true (a rotation, so winding is kept). `scale` multiplies positions.

### 1.8 Reference writer: the IW7 exporter

`src/convert/iw7_iw8/export_models.cpp` writes both files for IW7 models: `write_model` (`:400-500`) builds the table (`collLod` 0, `lods[].distance`, `type` "rigid", `rootBoneName`) and the geometry (`schema` 1, `name`, `bone_count`, `root_bone` when it exists, `contents`, `lods[].surfaces[]` with `deformed`, `material`, `reflection_probe`, `vertices`, `indices`). The geometry structs are at `:19-43`. Vertex fields are `export.hpp:375-386`. The IW7 table also writes `lods[].file` (an OBJ path), which the reader ignores. The writer here leaves it out.

## 2. Files written by `writeExtractionModel`

For model `cube_model`, with `extractedDir` = the `extracted` folder:

```
extracted/xmodel/cube_model.json           table
extracted/xmodel/cube_model.replay.json    geometry
extracted/materials/<prefix><mat>.json     one grey material per surface material (an existing file is kept)
```

`extracted/xmodel/cube_model.json` (one LOD):

```json
{"collLod":0,"flags":0,"lods":[{"distance":0}],"rootBoneName":"","type":"rigid"}
```

`extracted/xmodel/cube_model.replay.json` (structure; `[...]` abbreviated):

```json
{"schema":1,"name":"cube_model","bone_count":1,"lods":[
  {"distance":0,"surfaces":[
    {"material":"iw8r/grey","reflection_probe":0,"deformed":false,
     "vertices":[{"position":[x,y,z],"normal":[x,y,z],"tangent":[x,y,z],"binormal_sign":-1,"uv":[u,v],"lightmap_uv":[0,0]}, ...],
     "indices":[0,2,1, ...]}
  ]}
]}
```

`lods` repeats the surface list once per entry in `lodDistances`. The LOD distance is the same value in both files.

## 3. Worked example: a 1-surface cube

Input: `cube_model.obj`, a cube of half-size 8 in Y-up OBJ units. It has 6 quads (24 corners, each face with its own vertices, UVs and normal), one `usemtl grey`, and no `mtllib` (`out\custom-model-test\cube_model\cube_model.obj`).

Call: `importObj(...)` then `writeExtractionModel(m, "<extracted>", {prefix "iw8r/", scale 1, zUp true, lodDistances {0}})`.

Result:

- 1 mesh, material `grey`, 24 vertices, 12 triangles (`importObj` output, see the driver run in section 7).
- `xmodel/cube_model.json`: `lods` has one entry, `distance` 0, `collLod` 0.
- `xmodel/cube_model.replay.json`: one surface, material `iw8r/grey`, `deformed` false, 24 vertices, 36 indices.
- Position mapping: OBJ `(-8, -8, 8)` becomes Radiant `(-8, -8, -8)`, because the OBJ +Z face is Radiant -Y. Normal `(0,0,1)` becomes `(0,-1,0)`.
- Tangent `(1,0,0)` with `binormal_sign` -1 on the -Y face: the UV bitangent runs toward `-z`, and `cross((0,-1,0),(1,0,0)) = (0,0,1)`, so the sign is -1.
- The converter pads the single LOD to 6 copies with distances `3995` to `4000` (`:3073-3108`, the e2e log shows `lods=6`). The mapper sees one model. It only pays for the extra copies in the zone.

The written files are in `E:\IW8-Radiant\out\custom-model-test\cube\extracted\xmodel\` (written by the unit test; the material file there was overwritten by the keep-existing check). The e2e copy with the grey material is `E:\IW8-Radiant\out\custom-model-test\e2e_cmod\extracted\xmodel\`.

## 4. The implementation

Files (new): `src/core/model_import.h`, `src/core/model_import.cpp`, `tests/model_import_tests.cpp`. Nothing else in the repo was touched. `xmake.lua` globs `src/core/*.cpp` and `tests/*.cpp`, so no build edit was needed.

API (`namespace iw8r`):

- `importObj(path, ImportedModel&, err)`: `v`, `vt`, `vn`, `f` (fan triangulation, negative relative indices), `usemtl` (one mesh per material), `mtllib` read for `map_Kd` hints only, `o`/`g`/`s` ignored. Vertices are deduplicated per (material, v, vt, vn). A missing `vn` is generated from area-weighted face normals; a missing `vt` gives `(0,0)`. Each condition adds a warning.
- `importGltf(...)`: returns false with an error that says to export OBJ. Not implemented.
- `writeExtractionModel(m, extractedDir, opt, err)`: validates the name (`[A-Za-z0-9_-]`), scale (> 0), LOD count (1 to 6), distances (finite, non-decreasing), the 255-surface budget, and 65535 per surface. Writes the three file kinds from section 2.

Writer decisions, each covered by the unit tests:

- Triangles are emitted clockwise seen from outside (last two corners swapped). Test: `model_import_write_extraction_cube` checks that every triangle's `cross(b-a, c-a)` opposes its vertex normal.
- V is flipped. Test: the `uv` values in the cube test.
- Y-up to Z-up. Test: `model_import_write_axis_and_scale`, a non-symmetric triangle (OBJ `(0,1,0)` maps to `(0,0,1)`) and `zUp = false` and `scale` cases.
- `collLod` is 0 (LOD 0 collides). Not configurable yet.
- `materials/<prefix><name>.json` is written with the grey content; an existing file is kept, so a hand-made material is never overwritten.

## 5. Tests

`tests/model_import_tests.cpp`, 7 test cases:

| Test | Checks |
|---|---|
| `model_import_obj_quad_tri_usemtl` | quad plus triangle, `usemtl`, index and normal and UV values, map_Kd warning |
| `model_import_obj_negative_indices` | relative indices, generated normals |
| `model_import_obj_generated_normals` | no `vn`: generated `(0,0,1)`, warning; no `vt`: UV zero |
| `model_import_obj_errors` | missing file, bad index names `file:line`, no faces, glTF refused |
| `model_import_write_extraction_cube` | file set, table, geometry keys, 24 vertices and 36 indices, Z-up mapping, unit normals, tangents, winding, grey material, existing material kept |
| `model_import_write_lods_and_limits` | two LODs, distances, non-decreasing check, name check (`../escape` refused), 255 budget, scale |
| `model_import_write_axis_and_scale` | axis change, `zUp = false`, scale |

Full suite (`bin\release\iw8r_tests.exe`, log `E:\IW8-Radiant\out\custom-model-test\suite.log`): `16528 checks, 6 failures, 152 tests`. All 7 model import tests pass. The 6 failures are in `iw8_sample_room_edit_save_reopen` (`tests\core_tests.cpp:484`), not in this work. See section 8, item 4.

## 6. Driver

`out\custom-model-test\driver\import_driver.cpp` is a throwaway driver (not in the repo). Build it with `out\custom-model-test\driver\build_driver.bat` (MSVC BuildTools 2022 `vcvars64.bat`; the xmake build did not need it). Usage: `import_driver.exe <obj> <extracted_dir> [materialPrefix] [scale] [zUp]`. The CLI (`src/cli`) has no import command yet, and adding one was out of scope.

## 7. End to end (Step 3)

1. Compile the sample room with the CLI (exit 0; `E:\IW8-Radiant\out\custom-model-test\e2e`):
   `bin\release\iw8r.exe --keys data\generated\iw8_canonical_keys.tsv compile tests\data\iw8_sample_room.map -o out\custom-model-test\e2e --map mp_iw8r_cmod`. Warnings are the expected ones (stock model keys left out, fewer than 2 spawns).
2. Copy to `e2e_cmod`, import the cube: `import_driver.exe cube_model\cube_model.obj e2e_cmod\extracted iw8r/ 1 1` (`model 'cube_model': 1 mesh(es)`, `verts=24 tris=12`).
3. Append to `e2e_cmod\extracted\maps\mp\mp_iw8r_cmod.d3dbsp.ents`:
   ```
   {
   "classname" "script_model"
   "model" "cube_model"
   "origin" "0 0 8"
   "angles" "0 0 0"
   }
   ```
4. Build (exit 0; log `E:\IW8-Radiant\out\custom-model-test\buildextraction_run1.log`):
   `zonetool.exe -game iw8 -path E:/IW8/Builds/1.20-replay buildextraction E:/IW8-Radiant/out/custom-model-test/e2e_cmod --map mp_iw8r_cmod --replay E:/IW8/Builds/1.20-replay/game_dx12_ship_replay.exe --acts E:/mw124/atian-cod-tools/build/bin/Release/acts.exe --output "C:/Users/user/AppData/Local/IW8 Radiant Suite/IW8 Radiant Suite/x64zt_output"`.

What the log shows:

- `iw3: placed 1 unscripted script_model(s) through the static-model path`. The `script_model` was promoted to a static placement (not an entity model).
- `writeXModel: registered 'mw120r/mp_iw8r_cmod/smodel_0' (surfs=6 lods=6 bones=1)`. Six LODs, one surface each (the padding in section 3).
- `xsurface(dump)` wrote `smodel_0_lod0` to `lod5`, each with 24 verts and 12 tris.
- `collision: built native PhysicsAsset 'mw120r/mp_iw8r_cmod/smodel_0' (8 vertices, 12 triangles ...)`. The collision LOD is the cube's LOD 0.
- Output ledger `...\x64zt_output\mp_iw8r_cmod.iw8-assets.tsv`: `xmodel`, six `xmodelsurfs`, `physicsasset`, `xmodeldetailcollision`, and a generated material variant `w/mw120r_mp_iw8r_cmod_model_<hash>`, all from `IW3 model cube_model`.
- `validate: ... passes structural checks` for all five zones. Published at `...\x64zt_output\mp_iw8r_cmod\`. The result files are `mp_iw8r_cmod.ff` (584 KB), `eng_`, `srv_`, `techsets_` and `ww_` zones.

Not verified: the model's appearance in game (not run), its winding and UV orientation, and that the zone loads in Replay. Doc 17 has the route's status for earlier maps.

Warnings in the same run that are not from this work: no light grid (`:42`, `:61`), no `.colmap` or `.comworld` in the prepared folder (`:71-72`, hence no comworld collision for the map), no compass image, no `--gsc-tool`.

## 8. Open questions and what is still missing

1. **Winding (highest risk).** The writer emits models clockwise seen from outside, per `iw3_fastfile.cpp:3061` and `:5150`. The doc 15 §7 example and the world alignment use the opposite. Check in game, or compare a stock model's index winding with `xmodelsurfs` from ACTS (`docs\research\04` lists the dumps). If faces are inside out, flip the swap in `model_import.cpp` (search "Swap the last two corners").
2. **V flip.** The writer assumes D3D's top-left UV origin. Confirm with a textured model.
3. **Reflection probe.** Model surfaces use `reflection_probe` 0. The reader does not range-check it for models. Probably unused, but not confirmed.
4. **Suite failures (not this work).** `iw8_sample_room_edit_save_reopen` (`tests\core_tests.cpp:484`) expects the pillar material `tm2co/ee_trim_beam_airport_concrete`. At 13:43:23 the working copy `samples\iw8\iw8_sample_room.map` was edited by another agent (`twc4/65n_30n` on those faces). The test reads `g_iw8Sample`, which resolves to `samples\iw8`, so it fails. Fix: point that test at the pristine fixture `tests\data\iw8_sample_room.map`. The coordinator owns that call. The model tests pass.
5. **Compiler keeps the model key.** `compile_iw8.cpp:809-818` strips `model` from every `script_model` and `trigger_radius` whose name is not `*N`. The e2e added the key by hand. A real build needs: keep the key, and have the export write `xmodel/<name>.json` and `.replay.json` (from the mapper's OBJ, stored in the project) into the extraction. Owned by another agent right now.
6. **Collision.** Every model gets `collLod` 0 (solid). Needs a per-model option (none, or a box). Triangle collision from LOD 0 only.
7. **Textures.** `map_Kd` is a hint. A real texture needs `images/<name>.dds` (doc 15 §4.2) and a material that uses it. Not implemented.
8. **glTF.** `importGltf` returns false. A `.glb` path needs a glTF reader; the rest of the pipeline does not change.
9. **In-editor placement** (not started):
   - *Preview mesh.* `ImportedModel::meshes` has what is needed. The viewport (`src/viewport`) needs a mesh path for `script_model` with a non-stock `model` key, uploaded per model name. Stock names have no mesh, so they draw as a marker.
   - *Picking.* `ImportedModel::bounds` is in model space. Picking needs the placement transform (origin, `angles`, `modelscale`) and a ray test against the bounds, or triangles for exact picking. The model library needs an entry for each project model. Today `src/qtapp/asset_browser.cpp` lists stock names only.
   - *Placement.* `main_window.cpp:189` places a `script_model` with a `model` key. It would add the imported model to the list and copy the OBJ into the project.
10. **Static `misc_model`.** Not done. It needs `world.models[]` (`iw3_fastfile.cpp:3735-3825`: `model`, `origin`, `scale`, `axis`, `reflection_probe`, optional `mins`/`maxs`, `primary_light`, `model_flags`, `lighting_colors`, `lod_scale`). Not a `script_model`. Doc 15 §2.2 describes the world side.
