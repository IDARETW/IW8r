# 82. Decal volume producer in the converter fork (roadmap 5.8, converter half)

Date: 2026-11-11. Scope: the converter half of research 80: the x64-zt fork now reads `world.decal_volumes` from an
extraction and writes decal volumes into the built zones. The editor half (writing that key) is separate work.
Nothing was tested in game. The game was not started. The shared checkout `E:\mw124\x64-zt` and its live
`zonetool.exe` were not changed (only `git worktree add` metadata was added to its `.git` folder).

Status words: **VERIFIED OFFLINE** = shown by a build, a dump or a tool run in this pass. **INFERRED** = follows from
other evidence, reason given. **NOT VERIFIED** = needs the game. Guesses are marked as guesses.

## 0. Short answers

| Question | Answer |
|---|---|
| Where is the work | Branch `iw8radiant/decal-volumes` of the fork, worktree `C:\zsw-iw8r\decals`, three commits on top of `iw8radiant/build-extraction` (062ec6b4): `7f83c551`, `3308d13e`, `8ff480fa`. Not merged, not pushed |
| Does the worktree build | Yes. `C:\zsw-iw8r\decals\build\bin\x64\Release\zonetool.exe` (section 2) |
| Map without decals | Byte-identical to the live zonetool: 5 of 5 zones, on two extractions, and also when `decal_volumes` is an empty array or holds only bad entries (section 5.1) |
| One floor decal (research 80, section 7, steps 1 and 2) | Counts 1, 0, 1, 1; collection (0, 1, 0); `decalVisDataCount` 1; opacity word 0x00FFFF00; UV words 0x00003C00 0x3C008000 0; info word 0x80BFF800; one class-6 Umbra object. All as research 80 predicted (section 5.2) |
| ACTS on the decal build | All five zones load, 0 failed (section 5.2) |
| Deviation from the research 80 section 6 schema | None. The code reads exactly those keys. Five of them are optional with the defaults of section 3 |
| Correction to research 80 | The stock material `i/vfx_decal_surface_scorch_1` is owned by `techsets_common.ff`, not by `common_mp` / `common_base_mp` / `common_core_mp` (those only name it). See section 4 |

## 1. What was implemented

Files are relative to `src\` of the fork.

| File | What |
|---|---|
| `games\iw8\common\replay_render.h` | `kDecalVolumesPerCollection` (16), `struct DecalVolume`, `Mesh::decalVolumes`, `Mesh::decalVolumeMaterials`, and the five function declarations below |
| `games\iw8\common\replay_render.cpp` | `Load` reads the render-mesh keys `decalVolumeMaterials` and `decalVolumes` (line 1469). Packing: `DecalVolumeQuaternion`, `PackDecalVolumeQuaternion`, `PackUnsignedFloat`, `PackHalfPair`, `PackDecalVolume`, `DecalVolumeCollectionBounds` (lines 3218 to 3350). Writing: `DecalVolumeCollectionCount`, `StampDecalVolumes`, `EmitDecalVolumeTables`, `StampTransientDecalVolumes`, `EmitTransientDecalVolumes` (lines 3353 to 3440). `BuildUmbraTome` adds one object `0x60000000 + collection` per collection (line 3519) |
| `games\iw8\assets\write_maps.cpp` | `emitGfxMapBody` calls the five functions in load order (lines 477, 601, 617, 666) and reserves the zero-filled visibility words in stream 4 |
| `games\iw8\assets\replay_stock_library.h/.cpp` | `iw8::stock::CommonDecalVolumeMaterials(zoneFolder, names)`: which of the names are Materials with a decal volume definition in an always-loaded zone |
| `convert\iw3_iw8\iw3_render_assets.cpp` | `SourceDecalVolume`, `ReadDecalVolume`, `ReadDecalVolumes` (line 5242): read and validate `world.decal_volumes`. In `PrepareRenderAssets`: the material check next to the volumetrics reader (line 5875), the effect-material pass also builds decal materials (line 8568), the final material numbering (line 8829) |
| `convert\iw3_iw8\iw3_render_assets.h` | `RenderPlan::decalVolumes`, `RenderPlan::decalVolumeMaterials` |
| `convert\iw3_iw8\iw3_fastfile.cpp` | `BuildRender` copies both into the render mesh JSON (line 6156) |

What the writer does with each entry (research 80, sections 2 and 5.1):

- Packs it into the 44-byte record. Engine structs are used by name (`GfxStaticDecalVolumePacked`,
  `GfxDecalVolumeCollection`, `GfxDecalVolumeTransientData`, `GfxWorldDraw`, `GfxWorldDpvsStatic` from the fork's
  generated `structs.hpp`), not raw offsets.
- Groups entries into collections: runs of up to 16 in file order, all in transient zone 0. Bounds = union of the
  boxes (from the unrounded input values).
- Writes in `GfxWorldDraw`: material count, mask count 0, decal count, collection count, the Material pointer table
  (name references), the collection table, the zeroed indirection table.
- Writes in the transient zone: the 96-byte block, the records, the collection bounds, the uids.
- Writes `dpvs.decalVisDataCount` and its pointer together with the collection count, and one class-6 Umbra object
  per collection.

The three safety rules of research 80:

1. **Collection count and visibility data go together.** `StampDecalVolumes` writes the counts, the three table
   pointers, `decalVisDataCount` and the `decalVisData` pointer under one condition. VERIFIED OFFLINE: the dump of
   the built world shows the 4-byte runtime allocation exactly when the count is above zero.
2. **Every decal material has its decal volume material.** A stock name is accepted only when the converter finds the
   Material, with a non-null `decalVolumeMaterial`, in a zone that is always loaded (section 4). A material from the
   extraction is accepted only when the effect-decal builder makes its definition. The render-mesh loader refuses any
   other name. A decal whose material fails is left out with a warning.
3. **No `decal_volumes`, same bytes.** Every new code path is skipped when the list is empty. VERIFIED OFFLINE
   (section 5.1).

## 2. The worktree and how it builds

- `git worktree add C:\zsw-iw8r\decals -b iw8radiant/decal-volumes iw8radiant/build-extraction` (run from
  `E:\mw124\x64-zt`).
- Dependencies: the 13 submodule folders under `deps\` are directory junctions to `E:\mw124\x64-zt\deps\<name>`
  (the same idea as the symlinks in `C:\zsw3\_main\deps`). Nothing was downloaded. Because of the junctions, plain
  `git status` fails in the worktree; use `git status --ignore-submodules=all`.
- Solution: `tools\premake5.exe --games=iw3,iw7,iw8 vs2022` (the same game set as the live build's
  `enabled_games.hpp`). `generate.bat` was not used because it runs `git submodule update`.
- Build: `"C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" build\x64-zt.sln
  -p:Configuration=Release -p:Platform=x64 -m -v:minimal -nologo`. A full build took about 10 minutes (not timed exactly); `build\` is 2.5 GB.
- For a map without decals the exe built this way gives the same zones as the live exe (section 5.1), so the
  worktree build is a fair stand-in for the live one.

## 3. The input schema, exactly as the code reads it

`world.decal_volumes` in `extracted\maps\mp\<map>.d3dbsp.replay-world.json`. Optional. If the key is absent or the
array is empty, nothing is written. If it is not an array: one warning, no decals.

| Key | Type | Required | Accepted range | Default |
|---|---|---|---|---|
| `center` | 3 numbers | yes | each within -262144 .. 262144 | none |
| `axes` | 3 arrays of 3 numbers | yes | Rows: depth axis (into the surface), width axis, height axis. Each row a unit vector and the rows at right angles (squared length within 0.01 of 1, dot products within 0.01 of 0), right handed (depth x width = height) | none |
| `half_size` | 3 numbers | yes | each above 0 and below 65536. A value below 2^-14 is raised to 2^-14 | none |
| `material` | string | yes | 1 to 255 characters, no leading comma. Not usable: `..`, `:`, a backslash, a leading `/` | none |
| `opacity` | number | no | 0 .. 1 | 1 |
| `feather` | number | no | 0 .. 1 | 0 |
| `draw_order` | whole number | no | -4 .. 3 | 0 |
| `uv_matrix` | 4 numbers (m00, m01, m10, m11) | no | each within -65504 .. 65504 | 1, 0, 0, 1 |
| `uv_offset` | 2 numbers | no | each within -65504 .. 65504 | 0, 0 |

- Other keys are ignored.
- An entry that breaks a rule is left out with a warning that names the entry index and the reason. The build goes
  on. VERIFIED OFFLINE with 25 bad entries (section 5.4).
- Limits: the first 32768 good entries are kept (2048 collections of 16, the most Replay takes per frame) and at
  most 2047 different material names (the index has 11 bits; 0x7FF means none). More are left out with a warning.
- One thing the code cannot catch: a number too large for JSON (for example `1e999`) makes the whole world file fail
  to parse. That is the existing parser's behaviour for every key, not new.

Stored precision (format limits, not choices): half sizes keep 6, 6 and 5 mantissa bits (steps of about 1.6, 1.6 and
3.1 percent); the rotation keeps 10, 10 and 9 bits (a straight-down box decodes to an axis of (0, -0.003, -1));
opacity and feather are bytes; the UV values are half floats.

**How `material` is resolved**

1. If the extraction (or a converter search path) holds `materials\<name>.json`, the material is built by the
   existing effect-decal builder. The file must be in that form: `_game` "iw3", `_type` "material", `_version` 1,
   `techniqueSet` matching `[mw]c_l_sm_b0c0(n0)?(s0)?`, `textureAtlas` 1 x 1, `cameraRegion` "decal", `stateFlags` 52
   or 53, 1 to 3 `textures` with `semantic` `colorMap` (required), `normalMap`, `specularMap`, and `constants` with
   `colorTint` (`envMapParms` is allowed, any other constant is refused). The images must load. If any of this
   fails, the decals with that material are left out with a warning.
   Result: a carrier Material `elcq/mw120r_fx_<hash>` and a `decalvolumematerial` `i/mw120r_fx_decal_<hash>` in
   the techsets zone, three images in the map zone.
2. Otherwise the name is taken as a stock Material. It is accepted only if section 4's check finds it. This needs
   `--replay` (the editor passes it). Without it, stock decals are left out with a warning.

## 4. Stock decal materials: where they really live

Research 80 took the zone list of `i/vfx_decal_surface_scorch_1` from `data\asset_catalog\iw8_materials.tsv`. That
catalog comes from load traces and also lists zones that only **name** a material. VERIFIED OFFLINE with the fork's
`dumpasset`: the Material is not in `common_mp`, `common_base_mp`, `common_core_mp` or `code_post_gfx`. It is in
`techsets_common.ff`.

So `CommonDecalVolumeMaterials` loads, with the fork's own Replay reader, `techsets_common`,
`techsets_common_core_mp`, `techsets_common_base_mp`, `techsets_common_mp` and `code_post_gfx` (together under 25 MB,
about one second, and only when a decal names a stock material) and accepts a name when one of them owns the
Material with a non-null `decalVolumeMaterial`. `techsets_common_br_mp` is left out (Warzone; research 51 could not
confirm that multiplayer loads it).

That these zones are loaded with a custom map: INFERRED (research 51 lists `techsets_common*` and `code_*` as always
loaded). Extra evidence: every map built with `--acts` (the editor passes it) already names `i/vfx_decal_surface_scorch_1` and 11 other `i/`
decal materials as name-only references (the stock surface-effect table), with or without decal volumes.

Names that pass today (89, VERIFIED OFFLINE by a build with one decal per catalog candidate; full list in
`C:\zsw-iw8r\decals-scratch\stock_decal_materials.txt`):

- `i/mtl_default_vol_decal`, `i/gfx_crater_blacktop`, `i/gfx_crater_dirt`
- `i/vfx_decal_surface_scorch_1` .. `_3`, `i/vfx_decal_surface_concrete_1` .. `_4`, `i/vfx_decal_surface_dirt_3`,
  `_4`, `i/vfx_decal_surface_glass_2`, `i/vfx_decal_surface_glass_nopen_1` .. `_3`
- `i/vfx_decal_crater_1`, `i/vfx_decal_crater_dirt_1`, `i/vfx_decal_oil_pool_1`, `i/vfx_decal_spotfire_sheet_1`,
  `i/vfx_decal_water_ripple_1`, `i/vfx_decal_footstep_dirt_1`, `i/vfx_decal_footstep_sand_1`,
  `i/vfx_decal_ice_crack_grow_1`, `i/vfx_decal_ice_crack_grow_long_1` .. `_3`
- 51 bullet impact decals `i/vfx_decal_imp_*` (bark, cloth, concrete, default, dirt, drywall, kevlar, metal, plaster,
  woodhrd)
- 9 blood decals `i/vfx_fluid_blood_*_dec`, and `i/vfx_misc_tracker_print_1_decal`, `..._night`

Map-art decals (`i/decal_*`, for example `i/decal_rust_01`) are refused: their zones are not loaded with a custom
map. Carrying them (Material + definition + images copied from stock) is not in this version.

## 5. Offline checks and their numbers

All runs are in `C:\zsw-iw8r\decals-scratch`. Inputs are copies of
`E:\IW8-Radiant\out\c-stairs\map\iw8build\mp_iw8r_stairs\extraction` (taken 2026-11-11 20:18; the editor half had
already written the one test decal into it) and of the fixture `E:\IW8-Radiant\tests\data\iw8build\mp_iw8_sample_r`.
Command line: the editor's (`buildextraction ... --map --replay --output --metadata --acts --gsc_tool
--iw3_mode_assets off`) plus `--stock_cache <a copy of the editor's stock cache>` and, for dumps, `--dump 1`.

### 5.1 A map without decals is byte-identical (VERIFIED OFFLINE)

| Input | Worktree exe against live exe |
|---|---|
| mp_iw8r_stairs, `decal_volumes` key removed | 5 of 5 zones identical (`mp_` 128,840,612 bytes, sha256 eb84b79e...a86889), `map.json` identical |
| mp_iw8r_stairs, `decal_volumes: []` | 5 of 5 identical to the same live zones |
| mp_iw8r_stairs, 25 entries that are all refused | 5 of 5 identical to the same live zones |
| fixture mp_iw8_sample_r (no key) | 5 of 5 zones and `map.json` identical |

The live exe was only run, with its output in the scratch folder.

### 5.2 One floor decal with the stock material (research 80, section 7, steps 1 and 2) (VERIFIED OFFLINE)

Input entry: centre (0, 0, 4), axes `[[0,0,-1],[0,1,0],[1,0,0]]`, half size (4, 32, 32), material
`i/vfx_decal_surface_scorch_1`, opacity 1, feather 0, draw order 0, identity UV.

Decoded from the x64-zt dump of the built `mp_iw8r_stairs.ff` (`tools\decode_built.py`, the research 80 rules):

| What | Built value | Expected |
|---|---|---|
| GfxWorld +0x740: version, 3 reserved | 0, 0, 0, 0 | 0 |
| GfxWorld +0x750: materials, masks, decals, collections | 1, 0, 1, 1 | 1, 0, 1, 1 |
| Material table | `i/vfx_decal_surface_scorch_1` | same |
| Collection | (zone 0, count 1, first 0) | (0, 1, 0) |
| Indirection table | one zero u32 | zero, filled at load |
| `decalVisDataCount` (+0x3FAC) and its allocation | 1; 4 bytes zero-filled in stream 4 | 1 |
| Transient block | count 1, firstCollection 0, collectionCount 1, GPU buffer wrapper zero | same |
| Record: centre | 0, 0, 4 | same |
| Record: quaternion word | 0xCFFFFDFF (decodes to depth axis (0, -0.003, -1)) | straight down |
| Record: half-size word | 0xA0280440 (4, 32, 32) | 4, 32, 32 |
| Record: opacity word | 0x00FFFF00 | 0x00FFFF00 |
| Record: UV words | 0x00003C00 0x3C008000 0x00000000 | same |
| Record: info word | 0x80BFF800 | 0x80BFF800 |
| Record: reserved | 0 | 0 |
| Collection bounds | midpoint (0, 0, 4), half size (32, 32, 4) | the box |
| Uid | 0x9E3779B1 | any distinct value |
| Umbra tome | 27 objects to 28; one object 0x60000000; 2320 to 2368 bytes | +1 per collection |

Other facts from the same build:

- The record-by-record difference between the dump with the decal and the dump without it is exactly: 5 header
  values in GfxWorld (+0x750, +0x758, +0x75C, +0x3FAC, tome size), the material table, the collection, the
  indirection table, the runtime allocation, the tome, and in the transient zone the 96-byte block, the 44-byte
  record, the 24-byte bounds and the 4-byte uid. Nothing else changed in the map zone. `eng_`, `ww_` and `srv_` are
  byte-identical. `techsets_` differs only in the position of one name-only reference in its asset list.
- Zone data grows by 236 bytes = 188 (tables and records) + 48 (tome). The Material reference costs no bytes, so
  the final writer points the table at the top-level name reference (INFERRED from the size).
- The fork's own reader loads the built zone and reads it to the last byte (its `validate:` lines and the dump).
  This is the check that the stream order matches the game's loader, which research 80 asked for.
- ACTS `--test`, per zone, loaded / tested / failed: `mp_` 1122 / 27 / 0, `techsets_` 320 / 4 / 0, `eng_` 9 / 2 / 0,
  `ww_` 2 / 1 / 0, `srv_` 0 / 0 / 0. The same numbers as the build without the decal.
- `tools\verify_map_output.py` runs clean on the package. It lists only materials the map carries, so a name
  reference does not show there.
- Build time is unchanged (about 20 seconds for this map).

### 5.3 The packer against the 46 stock records (VERIFIED OFFLINE)

The 46 mp_shipment records were decoded with the research 80 rules, written as 46 `decal_volumes` entries, built, and
the built records compared with the stock ones (`tools\stock46.py`):

| Field | Identical |
|---|---|
| Centre | 46 of 46 |
| Half-size word | 46 of 46 |
| Quaternion word | 32 of 46; the other 14 are the ties research 80 describes. All 46 decode to the same rotation (largest axis difference 0.0027) |
| Opacity bytes 2 and 3 | 46 of 46 (bytes 0 and 1 are always written as 0 and 255) |
| UV values | 46 of 46. UV words 41 of 46: five records differ only in the sign of a zero |
| Draw order and running map order | 46 of 46 |

The same build shows more than one collection: 3 collections (16, 16, 14), `decalVisDataCount` 1, three class-6
objects. A further build with 89 materials gave 6 collections and loaded the same way.

About the zero sign: stock stores the identity matrix as 1, 0, -0, 1 (word 2 = 0x3C008000). The packer writes a zero
m10 as -0 so the common identity case is bit-equal to stock. For a 180 degree turn stock has +0 there. The value is
the same.

### 5.4 Bad entries and a mapper's own picture (VERIFIED OFFLINE)

- 25 bad entries (missing or wrong `center`, zero / negative / too large `half_size`, left-handed and non-unit
  `axes`, empty / missing / non-string / comma / path-like `material`, an unknown stock name, `opacity` 1.5,
  `feather` -0.1, `draw_order` 4 and 0.5, short `uv_matrix`, text in `uv_offset`, an entry that is not an object):
  25 warnings, build passes, zones identical to the build without decals.
- A material file in the effect-decal form (`materials\iw8r_decal_test.json`, colour map = an existing image of the
  extraction) plus the stock decal: 2 decals, 2 materials, one collection. The techsets zone gains the Material
  `elcq/mw120r_fx_871fb4892f8e9522` (technique set `,null`, pointing at its definition) and the
  `decalvolumematerial` `i/mw120r_fx_decal_871fb4892f8e9522` (flags 1100, three channels); the map zone gains three
  images. Second record: opacity word 0x4080FF00 (0.5, feather 0.25), UV words 0x3C000000 0x0000BC00 0x34003800
  (a quarter turn, offset 0.5, 0.25), info word 0xE1BFF801 (material 1, map order 1, draw order 3). ACTS: `mp_`
  1126 / 27 / 0, `techsets_` 326 / 5 / 0.
- In the same build a material file that is not a decal (`iw8r/grey`) and a map-zone decal name (`i/decal_rust_01`)
  were each refused with a warning.

## 6. What is not verified

Nothing here was seen in game. The owner decides if and when they test.

- **That a decal draws at all.** The data is in the form the loader takes and the visibility bit has its Umbra
  object, the same mechanism the fork already uses for volumetric boxes. NOT VERIFIED.
- **That the 89 stock names resolve in a custom map.** INFERRED (section 4). If one does not, Replay would use a
  Material without a definition; research 80 section 3 says the index is then garbage. The safest first test is
  `i/vfx_decal_surface_scorch_1`, which every built map already names.
- **The look of a mapper's own picture.** The builder is the effect-decal one; its tint is multiplied by the map's
  light unit scale as for effect decals. NOT VERIFIED.
- **Which way the picture faces** (u and v directions and signs), **the opacity curve**, the meaning of the record
  bits research 80 lists as inferred. The bit layouts are still "checked on stock data", not read in code.
- **What ACTS covers.** It loads every asset through Replay's loader code and reports none failed. It does not show
  that the decal fields are used correctly afterwards.
- **Cost of many decals.** Every collection is always visible (each Umbra object spans the scene, as all the fork's
  objects do). Collections are runs in file order, not groups of near decals.

## 7. Left out, and neutral values written

| Item | Written | Why |
|---|---|---|
| Masks | mask count 0, null mask table, mask index 0x7FF in every record | Research 80: second version; no mask asset writer |
| Normal blend mode | 2 (no override) | The value of 35 of 46 stock records; not in the schema |
| Blend-map edges (opacity bytes 0, 1) | 0 and 255 | The curve is not settled (research 80) |
| Quaternion bit 29, record `reserved`, `decalVolumeVersion`, three reserved words | 0 | Stock values; purpose unknown |
| `firstCollection` | 0 | One transient zone |
| Uids | `(index + 1) * 2654435761` (distinct) | Only the stock editor's live link reads them (INFERRED in research 80) |
| Spatial grouping of collections | runs of 16 in file order | First version, as designed |
| Carrying a stock map-art decal (`i/decal_*`) | refused | Needs a copy of the stock definition and images |
| A fixture test inside the fork | none | The fork has no test harness in the repository; the checks are the scratch scripts |

## 8. Steps to adopt it

Do this when the owner is not in the middle of a test, because step 2 replaces the live exe.

1. In `E:\mw124\x64-zt` (on `iw8radiant/build-extraction`): `git merge --ff-only iw8radiant/decal-volumes`. The base
   has not moved (062ec6b4), so this is a fast-forward of three commits.
2. Rebuild there: `"C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe"
   build\x64-zt.sln -p:Configuration=Release -p:Platform=x64 -m -v:minimal -nologo`.
3. For each map that has decals, delete `<map folder>\iw8build\<id>\zones.key` so the editor builds its zones again
   (or use Compile > Force Full Rebuild).
4. Read the compile log: `iw3: N decal volumes, M decal volume materials (...)` confirms what went in; every
   left-out decal has a `[warn]` line with the reason.
5. Afterwards the worktree can go: `git worktree remove C:\zsw-iw8r\decals` (2.5 GB of build output) and the scratch
   folder `C:\zsw-iw8r\decals-scratch` (0.9 GB).

Notes for the editor half:

- A custom-library decal needs its material file at `extracted\materials\<name>.json` in the form of section 3, and
  its images in the extraction. A name without a file is treated as a stock name.
- Offer only the 89 names of section 4 as stock decals. Any other stock name is left out by the converter.
- The first in-game look, if the owner chooses to do one: the map `E:\IW8-Radiant\out\c-stairs` already holds the
  test decal at (0, 0, 4).

## 9. Scratch contents

`C:\zsw-iw8r\decals-scratch` (0.9 GB after cleanup):

- `tools\`: `build.sh` (one build), `decode_built.py` (decoder for a built dump), `diff_dumps.py` (record
  difference of two dumps), `stock46.py` (the 46-record round trip), `acts_test.ps1`, the scripts that made the
  input variants, and the patch scripts used to edit the fork.
- `inputs\`: the world JSON of every variant (decal, nodecal, empty, bad, custom, stock46, survey).
- `src\extraction_orig`: the untouched copy of the c-stairs extraction.
- `out\`: the zones of `mine_decal`, `mine_custom`, `mine_nodecal` and `live_nodecal`; `zone_sha256.txt` and
  `zone_sha256_final.txt` hold the hashes of every build.
- `run\<name>\`: `zonetool.log`, `decoded.json`, and for the main builds `dump_kept\` (the dumped GfxWorld and
  transient zone).
- `bin\`: `zonetool_base.exe` (the branch before the change) and `zonetool_decals.exe` (after).
