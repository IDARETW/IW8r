# 23. Stock model preview cache (LOD0 GLB, Replay 1.20)

Status: working for `automotive_truck_loading_ramp_short`. `ModelCache` (`src/qtapp/model_cache.{h,cpp}`) resolves a stock
model name to its LOD0 surface buffer as a GLB with ACTS on demand, caches it under `AppLocalDataLocation\model_cache`,
and answers later lookups without ACTS. The GLB reader is `src/core/glb_mesh.{h,cpp}`. Test mode:
`IW8Radiant.exe --model-cache-test <model>[,<model>...] --out <dir>` (`src/qtapp/main.cpp`). The editor's 3D view is not
wired to it yet (per the brief, `MainWindow` and `scene.cpp` are unchanged).

The game folder was only read. ACTS wrote only to scratch and to the cache root under C:. ACTS was not rebuilt and the game
was not started. `samples\iw8` was not written.

## Results

Test: `automotive_truck_loading_ramp_short` (catalog zone `mp_backlot2`, LOD0 surfs `automotive_truck_loading_ramp_short_lod010v011`).

| Run | Wall time | ACTS runs | Notes |
| --- | --- | --- | --- |
| Cold (model_cache folder deleted), final build | 7,979 ms (process 8.1 s) | 2 | xmodel 4,256 ms, xmodelsurfs 3,616 ms (ACTS process time, zone load included) |
| Cold, earlier build (before the winding fix; same ACTS work) | 7,870 ms | 2 | xmodel 4,376 ms, xmodelsurfs 3,336 ms |
| Warm (same cache), final build | 114 ms (process 0.21 s) | 0 | index hit and the GLB file only |
| Negative: `does_not_exist_model_xyz` plus the sample | 103 ms (warm sample) | 0 | Unknown name cached as failed with `not listed in iw8_models.tsv`; exit code 1 |

Output: `E:\IW8-Radiant\out\model-cache-test\cold.txt` and `...\warm.txt` (the cold file was written on the final build),
`E:\IW8-Radiant\out\model-cache-test-neg\model-cache-test.txt` for the negative case.

Cache sizes: `glb\automotive_truck_loading_ramp_short.glb` 111,360 bytes, `index.json` about 0.9 KB. Two files, 111,934 bytes in
total. No scratch folder left behind.

Geometry of the LOD0 buffer (ACTS journal and reader): 2 surfaces (`lodInfo[0].numsurfs` 2), 2,563 vertices, 2,740 triangles.
The xmodel has 4 LODs (`numLods` 4, distances 500, 1100, 1800 and up); only LOD0 is cached.

## Commands that worked

Probe, one step at a time (these are the same commands `ModelCache` issues):

```
acts.exe --noUpdater fastfile -r mw19replay -g "E:/IW8/Builds/1.20-replay/game_dx12_ship_replay.exe"
  --oodle "E:/IW8/Builds/1.20-replay/oo2core_7_win64.dll"
  -a xmodel -n "automotive_truck_loading_ramp_short" -l -o <out> "E:/IW8/Builds/1.20-replay/zone/mp_backlot2.ff"
```
Exit 0, no XPak needed. The xmodel JSON gives `lodInfo[0].modelSurfsStaging.name` (the LOD0 surfs name) and `bounds`.

```
acts.exe --noUpdater fastfile -r mw19replay -g ... --oodle ... --xpak <each populated XPak> ...
  --geometry -a xmodelsurfs -n "automotive_truck_loading_ramp_short_lod010v011" -l -o <out>
  "E:/IW8/Builds/1.20-replay/zone/mp_backlot2.ff"
```
Exit 0. The GLB is `assets\xmodelsurfs\<name>.0.shared.bin.geometry.glb`, and the journal's `geometry_file` names it.

The same command with only `--xpak E:/IW8/Builds/1.20-replay/zone/pak_tu20.xpak` also exits 0, with the same payload (95,456
bytes, key 967594413225312942). The cache passes every populated zone XPak (size of at least 64 KiB, 28 files), as
`MaterialCache` does, because the key-to-archive mapping differs per model and the run time was the same (4.7 s each).

Without any XPak the xmodelsurfs run exits 127 with `package_key_missing` and status `unavailable`. Exit 127 means a payload
was missing, not that the run stopped; the journal per name is the authority.

## XPak and zone needs

| Step | Zone | XPak | Result |
| --- | --- | --- | --- |
| xmodel table | `mp_backlot2.ff` | none | ok |
| xmodelsurfs LOD0 | `mp_backlot2.ff` | none | unavailable (exit 127) |
| xmodelsurfs LOD0 | `mp_backlot2.ff` | `pak_tu20.xpak` only | ok |
| xmodelsurfs LOD0 | `mp_backlot2.ff` | all 28 populated | ok, served by `pak_tu20.xpak` |

Unlike the materials (doc 20), the model table and surfaces both sit in the catalog zone, so no `techsets_` candidate is needed for
this model. The cache still tries the catalog zones in order; a model listed for two zones falls through to the second on a
table failure.

## GLB axis and scale (verified)

The GLB JSON carries `extras.sourceCoordinates` = "right-handed Z-up, inches". Facts from the file and the xmodel JSON:

1. The node has `rotation` [-0.7071, 0, 0, 0.7071] (-90 degrees about X) and `scale` [0.0254, 0.0254, 0.0254].
2. The raw POSITION accessor min and max (per surface: -60.014 to 21.322 in X, -50.012 to 50.012 in Y, -3.274 to 2.993 in Z)
   equal the xmodel `bounds` (midPoint plus or minus halfSize) to 1.3e-5. So the accessor data is ACTS source space:
   Z-up, inches. The smallest extent is Z (6.3 in), which is consistent with a flat ramp standing on the ground in Z-up.
3. The glTF rule applies the node transform, so the output of a standard glTF reader is Y-up, metres: (x, y, z) in Z-up
   inches becomes (x, z, -y) * 0.0254 in glTF. The reader (`readGlb`, default options) returns exactly this.
4. The editor's `buildModelGeom` (`src/viewport/scene.cpp`) reads an `ImportedModel` as Y-up file coordinates and maps them with
   `(x, -z, y)`. So the loader multiplies by 1/0.0254 (inches) and keeps the glTF Y-up values. The editor then maps back to the
   raw Z-up inches. The test prints both: `bounds_radiant_inches` equals `xmodel_bounds_inches` (max abs diff 1.34e-5), and
   `bounds_gltf_yup_inches` is the Y-up inch form.

Winding (new finding): the ACTS triangles are clockwise seen from outside. The GLB normals point outward, and the geometric
normal of each triangle points against them: `winding_agree_raw=0/2740`. This is the IW3/IW7 model convention (docs/research/22
section 1.6). `ImportedModel` and `buildModelGeom` expect counter-clockwise (the OBJ and glTF rule, and their face normals use
`cross(b-a, c-a)`). `ModelCache::loadModel` swaps two indices per triangle, and the test then reports
`winding_agree_loaded=2740/2740`. Without this flip, a mesh drawn through the existing custom-model path shows inside-out faces.

## Reader (`src/core/glb_mesh.cpp`)

Supported: GLB container (JSON and BIN chunks), one buffer, node tree with matrix or TRS and parent chains, default scene (or
roots without scenes), meshes with POSITION, NORMAL and TEXCOORD_0 (float, or normalised integers), indices u8, u16 and u32.
Mirrored node transforms (negative determinant) reverse their triangles. Not supported, with an error or a warning: sparse
accessors, external buffers, more than one buffer, primitives other than TRIANGLES (skipped with a warning), morphs, skins.
A missing NORMAL becomes (0,0,1) and a missing TEXCOORD_0 becomes (0,0), both with a warning.

Tests: `tests\glb_mesh_tests.cpp` (8 tests) build GLBs in memory: the ACTS node transform and its inverse, u8/u16/u32
indices, the raw option, mirrored winding, missing attributes, rejected input, and reading a file by name.

## Failures and blockers seen

1. **C: full during the probes.** Mid-session `C:` reported 0 bytes free, which stopped a PowerShell and a Bash call and
   would have blocked the cache root (`C:\Users\...\AppData\Local\IW8 Radiant Suite`). Later `C:` had 15 GB free and the cache
   ran there. Nothing was deleted except `model_cache`, which this session created and emptied before the clean cold run.
   Large folders on C: at the time (sizes only, not touched): `C:\IW7`, `C:\IW8`, `C:\dwdb`, `C:\zsl*`, `C:\build`, `C:\tmp`,
   `C:\mw120rproxy-research`, and about 90 GB under `AppData\Local`. The cache lives on C: by design (brief), so C: needs headroom.
2. **E: free space.** E: had 28.9 GB free during this work (the brief said under 5 GB). The model cache is not on E:.
3. **Unrelated test failures.** One run of the suite showed three `lights_tests` failures in a file this work does not touch.
   After `iw8r_tests` was rebuilt from current sources the suite passed: 222 tests, 19,243 checks, 0 failures.
4. **Build error in my own test file.** `iw8r_tests` failed once because `parseGlb` had no default for `err`. Fixed in the header.

## Limits and not done

- Only one model was run through ACTS. The catalog has about 7,975 models (`data\asset_catalog\iw8_models.tsv`), not checked.
- LOD0 only. Materials are not bound: the xmodel lists 6 material handles for this model (`,mco/automotive_truck_loading_ramp_short`
  and accessories), and the GLB has no material indices. A mesh is one `ImportedMesh` per primitive with an empty material.
- The editor does not call `ModelCache` yet; `scene.cpp` still draws the OBJ path only.
- Not verified in game. The bounds and winding come from the file data.
- The cache does not seed from `data\stock_assets` (the code_post_gfx GLBs there are not referenced by this cache).

## Files

- `E:\IW8-Radiant\src\core\glb_mesh.h`, `E:\IW8-Radiant\src\core\glb_mesh.cpp`
- `E:\IW8-Radiant\tests\glb_mesh_tests.cpp`
- `E:\IW8-Radiant\src\qtapp\model_cache.h`, `E:\IW8-Radiant\src\qtapp\model_cache.cpp`
- `E:\IW8-Radiant\src\qtapp\main.cpp`: `--model-cache-test` (argument, one early branch, usage comment)
- Cache: `%USERPROFILE%\AppData\Local\IW8 Radiant Suite\IW8 Radiant Suite\model_cache\` (`index.json`, `glb\`)
- Test output: `E:\IW8-Radiant\out\model-cache-test\cold.txt`, `...\warm.txt`, `...\model-cache-test\model-cache-test.txt`;
  negative case `E:\IW8-Radiant\out\model-cache-test-neg\model-cache-test.txt`
- Probes (ACTS output, not part of the project): `C:\iw8mp\p1` to `C:\iw8mp\p4`
