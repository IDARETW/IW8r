# 16. ACTS asset exports: code_post_gfx (IW8 1.20 Replay)

Status: complete for materials, images and xmodels of `code_post_gfx.ff`. Output is under `E:\IW8-Radiant\data\stock_assets\code_post_gfx\`. The game folder was only read.

## Inputs

| Item | Value |
| --- | --- |
| ACTS binary | `E:\mw124\atian-cod-tools\build\bin\Release\acts.exe` (dev-3.3.0, built 2026-11-08 11:01) |
| Target | MW2019 Replay 1.20.4.7623265, profile `replay-1.20` |
| Zone | `E:\IW8\Builds\1.20-replay\zone\code_post_gfx.ff` (22,921,352 bytes) |
| Patch | `code_post_gfx.fp` (7,012,680 bytes), applied automatically |
| XFile version | input `0xfd5` (4053), final `0xff7` (4087), `applied_patches` = the `.fp` above |
| Game exe / Oodle | `E:\IW8\Builds\1.20-replay\game_dx12_ship_replay.exe`, `oo2core_7_win64.dll` |

## Commands

Final export (the output on disk comes from this command, on one line):

```
E:\mw124\atian-cod-tools\build\bin\Release\acts.exe --noUpdater fastfile -r mw19replay -g "E:/IW8/Builds/1.20-replay/game_dx12_ship_replay.exe" --oodle "E:/IW8/Builds/1.20-replay/oo2core_7_win64.dll" --xpak "E:/IW8/Builds/1.20-replay/zone/pak_launch.xpak" --geometry -a material,image,xmodel,xmodelsurfs -o "E:/IW8-Radiant/data/stock_assets/code_post_gfx" "E:/IW8/Builds/1.20-replay/zone/code_post_gfx.ff"
```

Index generation (reads only the ACTS output, writes only `index.json`):

```
python -I E:\IW8-Radiant\tools\acts_asset_index.py
```

The script is `E:\IW8-Radiant\tools\acts_asset_index.py`. It takes `--root`, `--zone`, `--build` and `--out`. It defaults to the code_post_gfx output.

Bounded checks, all with `-a material,image,xmodel,xmodelsurfs --geometry`:

- A first full run passed 14 populated zone XPaks (`pak_magma_parts` through `pak_launch_tu22`). Only `pak_launch.xpak` supplied a payload. The other 13 were not needed.
- A full run with no `--xpak` (output to a scratch directory) returned exit 127 with 2 unavailable xmodelsurfs, which is why `pak_launch.xpak` is required.
- A `--test` run without XPaks (one sample per type) showed 1 unavailable, the same key.

The final export uses only `pak_launch.xpak`. The first run's assets matched it by name, type, status and CRC, apart from the address difference described under Problems.

## Timings and sizes

| Step | Wall time | Result |
| --- | --- | --- |
| Final export (`pak_launch.xpak` only) | 10.8 s | 6,012 assets loaded, 696 exported, 0 failed, 0 unavailable, `complete` and `success` true |
| First export (14 XPaks) | 12.8 s | same counts |
| Full export, no XPaks (scratch) | 12.9 s | 2 unavailable (xmodelsurfs), exit 127 |
| `acts_asset_index.py` | 0.5 s | index.json 202,935 bytes |

Output sizes under `mw19replay\code_post_gfx\assets\`:

| Type | Files | Size |
| --- | --- | --- |
| image (DDS) | 305 | 9.4 MB |
| material (JSON, 371 files in prefix subfolders) | 371 | 1.7 MB |
| xmodel (JSON) | 12 | 220 KB |
| xmodelsurfs (shared `.bin` and `.geometry.glb`) | 8 + 8 | 158 KB |

Total on disk: about 12 MB, plus `index.json` (200 KB), `assets.jsonl`, `manifest.json` and `script_strings.json`. The 3 GB limit was not close. E: had only about 5.3 GB free before the run, so this matters for larger zones.

## Counts

| Type | Selected | Exported | Unavailable |
| --- | --- | --- | --- |
| material | 371 | 371 | 0 |
| image | 305 | 305 | 0 |
| xmodel | 12 | 12 | 0 |
| xmodelsurfs | 8 | 8 (each with a GLB) | 0 |

Unavailable payloads without XPaks: 2 xmodelsurfs keys, both served by `pak_launch.xpak`:

| Model | Key | Expected bytes | Served from |
| --- | --- | --- | --- |
| axis_guide_createfx_lod010v50 | 1851094410253438006 | 8,672 | `zone\pak_launch.xpak` |
| axis_guide_createfx_rot_lod010v50 | 347681101733777984 | 27,952 | `zone\pak_launch.xpak` |

Images need no XPak here. Every image is resident in the `.ff`, so 0 image payloads came from an XPak. Image counts with and without XPaks are both 305 ok.

The models have these LOD0 geometry counts (base surfaces only): 1,744 vertices and 850 triangles across the 8 GLBs. Every xmodelsurfs entry has `numsurfs` 1.

Materials and images referenced across the zone:

- 371 materials: 314 have no texture entries. 57 have 1 to 5 entries (90 entries in total).
- All 90 texture references resolve to an exported image or to a `$` placeholder. Zero refs are missing from the export.
- 18 materials have a colour texture (see the albedo rule). 39 textured materials have none. Among the 18, 9 unique colour files are used: `white`, `white&defaultspec`, `black_8x8&white`, `default_camo_color&defaultspec`, `refobj_white_solid&defaultspec`, `shadow&defaultspec`, `transparent_invisible&defaultspec`, `vfx_axis_rgb&defaultspec`, and the fallback `missing_fx`.

## Formats

### Images (DDS)

All 305 images are DDS with a DX10 header. The DXGI formats were counted by the index script from the headers:

| DXGI format | Count |
| --- | --- |
| BC7_UNORM_SRGB | 214 |
| BC1_UNORM_SRGB | 36 |
| R8G8B8A8_UNORM_SRGB | 20 |
| R8G8B8A8_UNORM | 10 |
| BC7_UNORM | 8 |
| BC4_UNORM | 6 |
| BC1_UNORM | 3 |
| BC3_UNORM_SRGB | 3 |
| BC5_UNORM | 2 |
| R32G32B32A32_FLOAT | 1 |
| R32_FLOAT | 1 |
| R32_UINT | 1 |

Every image has a width and height in its header. The journal and `index.json` record the file, format, size, mip count and payload byte count. Two engine images are 1x1 (`$black_uint`, `$black`). `ref_image` is 128x256 with the float format.

### Materials (`mw19-asset-json-v1`)

Each material is a file `assets/material/<prefix>/<name>.<n>.asset.json`. The journal `file` field is the authoritative path, since the folders are split by name prefix.

The JSON root is `{format, format_version, profile, pool, pool_id, root_type, asset}`, where `asset.fields` holds the loaded `Material`:

- `fields.name.string`: material name, for example `mo/vfx_debug_axis_guide`.
- `fields.techniqueSet.name`: technique set (techset) name. Every material has one.
- `fields.materialType.name`: material type. 300 are `MTL_TYPE_DEFAULT`. Others are `MTL_TYPE_MODEL`, `MTL_TYPE_SUPERTERRAIN*` and `MTL_TYPE_MODEL_VERTCOL_SELFVIS`, plus some unnamed numeric values.
- `fields.drawSurf.fields.surfType`: 0 for all 371. This field does not separate the surface types here.
- `fields.textureTable.values[]`: `{index, image}`. `image.name` is the image's name, in the form `name&suffix~hash` or `$name`. `index` is a small one-byte slot code (see the albedo rule). `textureCount` equals the number of entries.
- `fields.constantTable`, `constantBufferTable`: shader constants and PS constant data (not used here).

### xmodel and xmodelsurfs

`assets/xmodel/<name>.<n>.asset.json` has `fields`:

- `name`, `numLods`, `numsurfs`, `radius`, `bounds.midPoint.v`, `bounds.halfSize.v`.
- `lodInfo[]`: `dist`, `numsurfs`, `modelSurfsStaging.name` (the xmodelsurfs asset name), and `flags`.
- `materialHandles.values[].name`: material names, one per model here.
- Bones and physics fields are present but not used.

`assets/xmodelsurfs/<name>.<n>.shared.bin` is the raw surface buffer. `<...>.shared.bin.geometry.glb` is the GLB sidecar with one mesh and one primitive, using POSITION, NORMAL, TEXCOORD_0 and indices. Positions are in inches, right-handed Z-up. The node carries rotation (-90 degrees about X) and scale 0.0254. The GLB includes no materials, skeleton, skinning, morphs or subdivision. All 8 GLBs parsed and validated (magic, length, chunk type).

The xmodel bounds match the GLB POSITION min/max (within float precision), so `bounds` in the index is in the same units as the GLB source data.

## index.json

`E:\IW8-Radiant\data\stock_assets\code_post_gfx\index.json` has the requested top-level keys: `zone`, `build`, `materials`, `images`, `models`. It adds `source`, `bounds_units`, `albedo_rule` and `counts`. Each material also adds `material_type`, `surface_type` and `asset_file`. Each model adds `lods`, `radius` and `asset_file`. `counts.dxgi_format_counts` and `image_failures` (empty) are included.

- Materials: `images` maps `slot_<index>` to the image name for each texture entry. The slot is the raw index, not a resolved semantic.
- Images: `file`, `format`, `width`, `height`, `mips`, `bytes`, plus `cube` and `array_size` when they apply.
- Models: `glb` is the LOD0 GLB path (relative to the zone root), or null for the 4 `tag_ik_target_*` models, which have no surfaces. `materials` lists material names. `bounds` is `[mins, maxs]` from `midPoint ± halfSize`.

All paths are relative to `E:\IW8-Radiant\data\stock_assets\code_post_gfx\`.

## Albedo rule

The material JSON does not name texture semantics. The `index` byte is not a field in the schema's `TextureSemantic` enum (`TS_COLOR_MAP` is 2, a different value space). The rule therefore uses the slot index, as observed in the data:

- Slot 0 holds colour data: `default_camo_color`, `white`, `shadow`, `black_8x8&white`, `vfx_axis_rgb`, `refobj_white_solid`. Used as albedo.
- Slot 9 holds normal data: `default_camo_normal`, `identitynormalmap`. Not used as albedo.
- Slots 18, 27, 45, 59, 68 and 158 hold effect, placeholder or UI images.

**Rule:** colour = the texture entry with `index == 0` whose image is a real exported image, meaning its name does not start with `$`. Otherwise `color_image_file` is null. No other slot is guessed.

**Result:** 18 of 57 textured materials have a colour texture. All 18 slot-0 entries are real images, so no slot-0 entry was lost to placeholder names (`materials_slot0_placeholder_only` is 0). The other 39 textured materials and all 314 untextured materials get null.

The rule is heuristic. It is not verified against the techset's sampler table, which was not exported. Some albedo images are generic: `white`, `missing_fx` and `shadow`. Treat them as fallbacks, not real artwork.

## Problems and limits

1. Streamed payloads. Two xmodelsurfs buffers need `pak_launch.xpak`. Without it they are unavailable and the run exits 127. The first run used 14 XPaks needlessly. The final command uses only `pak_launch.xpak`, and the outputs match.
2. Runtime addresses. Material and xmodel JSON include `address` fields that vary between runs (pointer values from the loader). Their payload CRCs differ between runs (371 materials and 12 xmodels), but with `address` removed the materials are identical (371 of 371 compared). Do not use `payload_crc32` on these two types as a stable identity. Image and xmodelsurfs CRCs match across runs.
3. Texture semantics are not named. The slot `index` values are kept as raw numbers. A full mapping needs the techset texture table or the schema's semantic names.
4. Surface type and per-surface materials. `drawSurf.surfType` is 0 for every material. The xmodel exposes one `materialHandles` entry per model, and each model has one surface, so surface-to-material mapping is not separately shown. The GLB has no materials.
5. LODs. Each model has one LOD with surfaces (LOD0). Higher LODs are empty here. `dist` is kept.
6. Disk. About 5.3 GB was free on E: before the run. The output is 12 MB, so this is not a problem for this zone, but larger zones may exceed it.
7. Stale `build\` directories are not used. The binary is the freshly built `build\bin\Release\acts.exe`.
8. An earlier run in `E:\mw124-conversion-builds\zt-neutral-image-20261027` reported 2 unavailable keys without XPaks. This run confirms the same two keys and shows that `pak_launch.xpak` serves them.
