# 81. The heightfield (roadmap 5.25): what it is, who reads it, what a custom map gets

Scope: roadmap row 5.25, "Heightfield asset (fork `replay_heightfield`): find what the game uses it for and expose it".
Read-only research. Nothing was built, run in game, or edited. Only this file was written. Scratch scripts and ACTS
exports are in the session scratchpad (`...\scratchpad\r81`), not in the repo.

Status tags: **VERIFIED IN CODE** = read at the given address or file:line in this pass. **VERIFIED IN DATA** = decoded
from a stock or built file in this pass. **INFERRED** = follows from what was read, not proven. **UNKNOWN** = not found.
Every guess is marked as a guess.

Addresses are virtual addresses in the stock `game_dx12_ship_replay.exe` (image base 0x140000000).
**The IDA tools did not respond in this pass** ("IDA Assist is not reachable", four tries). All engine evidence was
read from the exe file on disk with a disassembler script (capstone, function bounds from the exe's exception table).
That gives instructions and cross-references, not decompiled code, and no names: function names below are my labels.

Terms: a **heightfield** here is a top-down picture of the map where each pixel stores the height of the highest solid
surface at that spot. A **techset** (technique set) is the set of shader programs a material draws with. A **code
constant** is a value the engine hands to a shader each frame. **Voxel tree** is a per-region lighting structure in the
render world (GfxWorld); only its role as an index matters here.

---

## 0. Answers in short

| # | Question | Short answer | Status |
|---|---|---|---|
| 1 | What is it | Not an asset pool. It is an array inside the render world: `GfxWorld.heightfieldCount` (+0x3D80) and `GfxWorld.heightfields` (+0x3D88), 96 bytes per entry: bounds, a 4x4 lookup matrix, and a pointer to an 8-bit image named `*heightmap0`. Stock mp_shipment has one entry with a real 2048 x 2048 image | VERIFIED IN CODE and DATA |
| 2 | Who reads it | Only the renderer, only to feed shaders. Two per-view functions: one binds the image as the global shader texture `outdoorTexture`, the other copies the matrix into the "outdoor lookup matrix" and sets one parameter vector. The stock shaders found that use them are rain effect materials (`..._od`). No collision, physics, AI, audio, minimap or decal code reads it | VERIFIED IN CODE (renderer), VERIFIED IN DATA (rain materials) |
| 3 | What an editor-built map gets today | A real heightfield. The converter builds it on every build from the map's solid world triangles plus the sun-shadow casters. Built map mp_iw8r_fix1: count 1, 256 x 256 image with real content, matrix in the stock form | VERIFIED IN CODE and DATA |
| 3b | Link to open symptoms (stone impacts, probes, killstreak aircraft) | None found | VERIFIED IN CODE for the readers found; see section 4.3 |
| 4 | Should the editor produce it | **Not needed as new editor work.** The converter already produces it and the editor's extraction already carries everything it needs. Its only use is hiding rain or snow under roofs. Optional small items are listed in section 5 | recommendation |
| 5 | Smallest experiment | Offline: export the built zone with ACTS and check count, size, matrix and one pixel under a roof (done here for mp_iw8r_fix1, except the roof pixel). In game, only if the owner chooses: two sheds and a stock rain effect (section 6) | - |

---

## 1. What the asset is

### 1.1 It is a member of GfxWorld, not a pool

- The ACTS pool list for Replay has no heightfield pool. The render world is pool 31 `gfx_map`, size 0x4590,
  struct `GfxWorld`, present in 9 stock map zones: `E:\mw124\atian-cod-tools\docs\mw19\pools.md:45`. VERIFIED IN CODE.
- The fork's name `replay_heightfield` is a source file, not an asset type:
  `E:\mw124\x64-zt\src\games\iw8\assets\replay_heightfield.h:12-19` ("GfxWorld heightfield (+0x3D80 count, +0x3D88
  GfxHeightfield[])"). VERIFIED IN CODE.
- Struct in the fork: `src\games\iw8\structs.hpp:13100-13105`, with size and offset asserts at `:24197-24200` and the
  GfxWorld offsets at `:24399-24400`. ACTS has the same three fields at `research\mw19\reference_types.hpp:5344-5349`
  (its `reference_layouts.txt:6000-6001` shows other GfxWorld offsets, 2488 / 2496: that file describes a different
  layout, not Replay 1.20). VERIFIED IN CODE.

### 1.2 Layout of `GfxHeightfield` (0x60 bytes)

| Offset | Size | Field | Meaning | Status |
|---|---|---|---|---|
| 0x00 | 12 | `bounds.midPoint` (3 floats) | centre of the covered box | VERIFIED IN DATA (stock values fit the matrix); no runtime reader found |
| 0x0C | 12 | `bounds.halfSize` (3 floats) | half extents. Only z (+0x14) is read at run time: `movss xmm1, [r8+rdx+0x14]` at 0x141977830, then doubled | +0x14 VERIFIED IN CODE; x, y VERIFIED IN DATA only |
| 0x18 | 64 | `lookupMatrix` (4 x 4 floats, row 3 is the translation) | world position to (u, v, normalised height). Copied as four 16-byte rows at 0x1419777F4-0x14197781D | VERIFIED IN CODE |
| 0x58 | 8 | `image` (GfxImage pointer) | the height picture. Loader loads an image pointer at +0x58 (0x140D932FC, call 0x140E300C0); read at 0x141979FA7 and 0x14197782B; its width (+0x24) and height (+0x26) are read at 0x14197783B / 0x141977843 | VERIFIED IN CODE |

Size 0x60: the loader allocates `count * 3 << 5` bytes and steps 0x60 per entry (0x140D932A9-0x140D9330C).
VERIFIED IN CODE.

GfxWorld fields around it (Replay offsets): `voxelTreeCount` +0x3D70, `voxelTree` +0x3D78, `heightfieldCount` +0x3D80,
`heightfields` +0x3D88, `precomputedSkyIllumination` +0x3D90 (two image pointers first: `detailMask` +0x3D90,
`heightMap` +0x3D98), `outdoorLookupMatrix` +0x3E98, `outdoorImage` +0x3ED8. Source: `structs.hpp:13376-13390`,
`:24399-24411`; the +0x3D70 / +0x3D78 / +0x3D90.. reads are in the functions of section 2. VERIFIED IN CODE.

### 1.3 The loader

- `Load_GfxWorld` (label from the fork, `replay_heightfield.cpp:23`), entry 0x140D92E80. Heightfield block at
  0x140D93259-0x140D9331D: if the pointer at +0x3D88 is set, align to 8, read `heightfieldCount` (+0x3D80) entries of
  0x60 bytes, and for each one resolve the image pointer at +0x58. VERIFIED IN CODE.
- Three more functions touch +0x3D80 / +0x3D88 in the same code area (0x140D9D5A0, 0x140DA2C80, 0x140DAF4D0). They
  look like the other passes of the zone loader over the same struct (guess: mark, unload, verify). INFERRED.

### 1.4 What stock maps carry

- mp_shipment asset list: `image,$outdoor` and `image,*heightmap0`:
  `E:\IW8-Radiant\out\m2-x64zt\dump\mp_shipment\mp_shipment.csv:5760-5761`. VERIFIED IN DATA.
- mp_shipment render world dump (`...\gfx_map\maps\mp\mp_shipment.d3dbsp.iw8`, body starts at file offset 0x21, checked
  by `bspVersion` 243 at body +0x10): `heightfieldCount` = 1, `voxelTreeCount` = 1. The one heightfield record is at
  file offset 0x3AAFB85: midPoint (-516, 2372, 480), halfSize (2501, 2365, 585), matrix diagonal (1/5002, -1/4730,
  1/1170), translation (0.6032, 1.0015, 0.0897, 1). That is exactly: u = (x - minX) / sizeX, v = (maxY - y) / sizeY,
  h = (z - minZ) / sizeZ. VERIFIED IN DATA.
- mp_shipment `*heightmap0` (`...\image\_heightmap0.iw8`, 4,194,617 bytes = 313 header + 2048 x 2048): format 1 (R8,
  one byte per pixel), flags 3, one mip level. 174 different pixel values, range 0 to 210; the most common are 26 to 28
  (the ground: 27/255 x 1170 - 105 is about z 19) and 49 is frequent (about z 120, container tops). So it holds real
  heights. About 2.4 world units per pixel. VERIFIED IN DATA.
- mp_shipment `$outdoor` (`...\image\$outdoor.iw8`): 16 x 16, format 3 (L8), every byte 0x16. A placeholder.
  VERIFIED IN DATA.
- Catalogue: `*heightmap0` and `$outdoor` appear in all 9 stock map zones traced (mp_aniyah_tac, mp_backlot2,
  mp_bm_tut, mp_emporium, mp_hideout, mp_m_king, mp_rust, mp_shipment, mp_village2):
  `data\asset_catalog\iw8_images.tsv:13` and `:24`. VERIFIED IN DATA.
- The size table `data\asset_catalog\iw8_streamed_images.tsv:13-20` has `*heightmap0` 4096 x 4096 and `*heightmap1` to
  `*heightmap7` at 1 x 1. That table merges 21 zones into one row per image name, so it does **not** say "1 x 1 in most
  maps": the 1 x 1 rows are seven other images. Which zone they come from is UNKNOWN. Guess: a large world with eight
  heightfields (one per voxel tree, see 2.1) where seven are empty. INFERRED.
- Only mp_shipment was decoded in this pass. "Every 6v6 map has one real heightfield" is INFERRED from the catalogue.

---

## 2. What reads it in the game

Method: every instruction in the exe that addresses +0x3D80 or +0x3D88 from a register was listed (6 and 9 hits). Four
functions are the loader passes of 1.3. Two are run-time readers. Both are per-view renderer setup functions that take
the same per-view block (called "view block" below) in rcx / rbx. Limit: an access through a pointer to the middle of
GfxWorld would not show in this scan; none was seen.

World pointer global: 0x150C77870 (set to 0 at world unload, 0x1418D9632). VERIFIED IN CODE.

### 2.1 Which heightfield a view uses: the voxel tree index

- The view block holds an index at +0x2D44. The only writer is 0x14197B4DE. It picks the first voxel tree whose box
  contains the camera (loop 0x14197B451-0x14197B4C7 over `world->voxelTree`, 0x70 bytes each); if none, the engine
  reports "No valid voxel trees.  Are there empty skyboxes in your map?" (string 0x142595B18). If `voxelTreeCount` is 0
  the write is skipped (0x14197B42C). VERIFIED IN CODE.
- Both readers use that same index into `heightfields`. So heightfields run parallel to voxel trees: one per region.
  In mp_shipment both counts are 1. INFERRED for the "parallel" reading (same index, no separate bound check).

### 2.2 Reader 1: bind the image as the shader texture `outdoorTexture`

Function 0x141979C00 (fills the view block's table of global shader textures).

- 0x141979F81-0x141979FAC: if `heightfieldCount != 0`, store `heightfields[index].image` at view block +0x2978.
  If the count is 0 the slot is not written. VERIFIED IN CODE.
- That table starts at view block +0x28F8 and has 42 texture slots: the loop at 0x1419A801F-0x1419A8075 walks 0xA8 / 4
  entries from +0x28F8. Slot (0x2978 - 0x28F8) / 8 = 16. The exe has the matching name table at 0x1454940D0 (75 names);
  name 16 is `outdoorTexture`. The neighbours fit: slots 17 to 20 are filled from world +0x3D98, +0x3D90, +0x3DA0,
  +0x3DA8 (0x141979FD7-0x14197A01D) and names 17 to 20 are `CTSkyLightHeightMap`, `CTSkyLightDetailMask`,
  `CTSkyLightSVDCoefficient`, `CTSkyLightSVDBasis`. VERIFIED IN CODE.
- **Absent or empty:** in that loop a null slot is replaced by the image at global 0x150C77888 (0x1419A803A,
  `cmove`), which is `$black` (registered at 0x1419390B7-0x1419390CD). So with no heightfield the shader samples
  height 0 everywhere, the bottom of the box. Guess at the visible result: everything counts as "under open sky" and
  rain is drawn indoors too. Fallback VERIFIED IN CODE; the visible result is INFERRED. Whether the slot is reset to
  null each frame is UNKNOWN.

These 42 textures are bound for every draw by name, not per technique. That is why techsets do not list the height
image among their arguments (2.4). INFERRED from the table and loop.

### 2.3 Reader 2: the lookup matrix and one parameter vector

Function 0x141977370 (per-view constants).

- 0x1419777C2-0x14197781D: if `heightfieldCount != 0`, copy `heightfields[index].lookupMatrix` into the global at
  0x15183F200 (64 bytes). VERIFIED IN CODE.
- That global is the engine's **outdoor lookup matrix**: at world load it is first filled from
  `GfxWorld.outdoorLookupMatrix` (+0x3E98; 0x1418D910D-0x1418D9129) and cleared at unload (0x1418D964D). So the
  heightfield matrix replaces the older `outdoorLookupMatrix` every frame. VERIFIED IN CODE.
- 0x141977824-0x14197787B: write four floats at view block +0x2000: x = (2 x halfSize.z) / max(`r_outdoorFeather`,
  1.0), y = 0, z = image width, w = image height. The dvar pointer is 0x15055A608, registered at
  0x14192F42C-0x14192F462 with default 8 and help text "Outdoor z-feathering value" (hashed name `LQKORRQMLR`,
  `E:\mw124\Dvars.txt:4874`). VERIFIED IN CODE. Guess at the meaning: the height range divided by a soft edge of 8
  units, so the shader can fade rain over 8 units below a roof edge. INFERRED.
- **Absent or empty:** the matrix global keeps `GfxWorld.outdoorLookupMatrix` and the vector is not written here.
  VERIFIED IN CODE.

### 2.4 Who uses the matrix: rain effect shaders

- Function 0x1419885E0 builds matrix code constants on demand. Constants 160 and up are matrices in pairs; the case
  for the pair 166 / 167 (branch at 0x1419886D8 to 0x141988785) multiplies an object matrix with the global
  0x15183F200 (0x1419887B3, 0x1419887D4). So code constant 166 is "this object's position to heightfield space".
  VERIFIED IN CODE. Earlier engine generations call this `WORLD_OUTDOOR_LOOKUP_MATRIX`
  (`x64-zt\src\games\iw7\structs.hpp:2091`); the name for IW8 is INFERRED.
- Stock data: 3,002 distinct techsets were exported with ACTS from 14 stock zones (code_post_gfx, techsets_common,
  techsets_common_mp, _common_core_mp, _common_br_mp, _common_base_mp, _global_mp, _global_stream_mp, _mp_shipment,
  _mp_hideout, _mp_piccadilly, _mp_br_quarry, _mp_donetsk, plus the emporium export in `out\agent_techset`). Exactly
  three bind code constant 166, all in `techsets_mp_shipment.ff`, all in the vertex shader stage:
  - `elcq/unlit_6_effect_bad_ta0_802_1422_0_1_0_0_0_10000005b_0_0_2_0_0`
  - `elcq/unlit_6_effect_bad_ta0_8480a_422_0_1_0_0_0_10000005b_0_0_2_0_0`
  - `elcq/unlit_6_effect_bad_tca_802_1402_0_1_0_0_0_10004005b_0_0_3_0_0`

  VERIFIED IN DATA. Many techsets in the map zones failed to export (for example 165 of 379 in shipment), so three is
  a lower bound.
- The materials that draw with them (same zone): `elcq/vfx_fluid_screen_drops_atl`, `elcq/vfx_rain_filler_scroll_a`,
  `elcq/vfx_rain_filler_scroll_b`, `elcq/vfx_vol_weather_rain_drop_sheet_norm_1_od`. VERIFIED IN DATA.
- Clean pair: `vfx_vol_weather_rain_drop_sheet_norm_1` uses techset `...tca_802_1002...` and its twin with the suffix
  `_od` uses `...tca_802_1402...`. The two techsets have identical code arguments except that the `_od` one adds code
  constant 166 in the vertex stage of both of its techniques (`TECHNIQUE_EMISSIVE`,
  `TECHNIQUE_BUILD_TRANS_SHADOWMAP`). In the 167 distinct `elcq/unlit_6_effect` techsets exported, bit 0x400 of the
  second name field is set exactly when constant 166 is bound (3 of 3, 0 of 164). VERIFIED IN DATA. Guess: `_od` means "outdoor" and bit
  0x400 is the outdoor option of the effect shader. INFERRED.

So the chain is: heightfield image and matrix -> `outdoorTexture` plus code constant 166 -> rain materials -> rain is
not drawn where the drop is below the stored height (under a roof). The last step is the shader's own code, which was
not read. INFERRED. It matches the dvar next to it: `r_outdoor`, "Prevents snow from going indoors"
(`Dvars.txt:4873`).

### 2.5 Consumers looked for and not found

| Candidate | Result | Status |
|---|---|---|
| Collision, physics, bullet traces | No reader. The physics "HeightField" strings in the exe (`hknpHeightFieldShape` and others) are the Havok physics library's own shape type and have no link to GfxWorld | VERIFIED IN CODE (no access to +0x3D88 outside section 2) |
| AI, bots, navigation, audio, minimap, decals, clutter, grass | No reader of the heightfield array outside the two renderer functions. The pixels live in a GPU image; no code path copies them back | VERIFIED IN CODE within the limit of the scan |
| `r_outdoor` (bool, "Prevents snow from going indoors") | Registered at 0x14192F416, pointer stored at 0x15055A600, and nothing in the exe reads that pointer | VERIFIED IN CODE (RIP-relative reference scan) |
| `GfxWorld.outdoorImage` (+0x3ED8, the `$outdoor` image) | No instruction addresses +0x3ED8. It is loaded and then unused, a leftover of the older outdoor system that the heightfield replaced | INFERRED (scan limit) |
| `r_showHeightmap`, `r_showHeightmapScale` | The names (plain and hashed, `LNQTKNKRRQ` / `NTRSRSLLKS`) are referenced only by two large name-list functions (0x140C20C30, 0x140C52C00), not by the renderer's dvar registration (0x14192A130); guess: not registered in this build. The debug material `show_heightmap` is still in `code_post_gfx` (`data\asset_catalog\iw8_materials.tsv:17567`) and has no working switch | VERIFIED IN CODE |
| Debug or other shaders that sample `outdoorTexture` without constant 166 | Cannot be listed: the texture is bound globally by name, so a shader that uses it leaves no trace in the techset arguments | UNKNOWN |

---

## 3. Images with similar names: related or not

| Image or setting | Related to the heightfield? | Evidence |
|---|---|---|
| `*heightmap0` | **Yes.** It is the heightfield's image | section 1.4; fork `replay_heightfield.h:29` |
| `*heightmap1` .. `*heightmap7` (1 x 1) | Yes by name: further heightfields of a world with more than one. Source zone UNKNOWN | INFERRED |
| `$outdoor` (16 x 16) | Same subject, older system. Stored in GfxWorld, no reader found. mp_shipment fills it with 22, the fork with 125 (copied from mp_hideout); the value does not matter if nothing reads it | section 2.5 |
| `ctskylight_heightmap` (2048 x 2048, `iw8_streamed_images.tsv:4118`) | **No.** It belongs to `GfxWorld.precomputedSkyIllumination.heightMap` (+0x3D98), a different field, bound as shader texture `CTSkyLightHeightMap` (slot 17) only when that struct's bounds are valid (0x141979FBF-0x14197A01D) | VERIFIED IN CODE for the field and slot; the image-to-field match is by name, INFERRED |
| `r_fogHeightMapBBoxMin / Max / Scale` ("current map volumetric heightfield") | **No.** The fog code takes its box from these dvars, not from GfxHeightfield (0x1419A9F7C-0x1419AA003). Defaults are about +/-64,000 units, the size of the battle royale map | VERIFIED IN CODE; which image it samples is UNKNOWN |
| `vfx_magma_heightfield`, `particle_magma_heightfield_bbox_*` | **No.** A separate image looked up by name at 0x141BB1040, shader texture `vfxMagmaHeightField` (slot 41). Battle royale effects | VERIFIED IN CODE |
| `stream_farAwayHeightmapsStreamingDistance`, `StDiskTerrainSurface.heightMap` | **No.** Terrain system (`structs.hpp:26650`) | INFERRED from names |
| Volumetric mask type `HEIGHTMAP` (research 09) | **No.** A per-volume fog mask image | research 09 |

---

## 4. What the converter writes for an editor-built map today

### 4.1 Code path

- `E:\mw124\x64-zt\src\convert\iw3_iw8\convert.cpp:1831-1847`: if the render mesh has solid triangles, build and
  register the heightfield and log `heightfield: NxN over T triangles`. VERIFIED IN CODE.
- Input 1: `renderMesh.solidTriangles`, nine floats per triangle of every world surface that is opaque, not sky, and
  not part of a brush model (`replay_render.cpp:2019-2031`; `replay_render.h:295-296`). Alpha-tested foliage counts as
  opaque; blended surfaces (glass, water) do not. VERIFIED IN CODE.
- Input 2: every vertex of `prepared->shadowScene.surfaces`, the sun-shadow casters, which adds the static models
  (`convert.cpp:1836-1842`). VERIFIED IN CODE.
- Box: `renderMesh.sceneBounds`, which leaves out sky surfaces (`replay_render.cpp:1876-1877`). VERIFIED IN CODE.
- Size: a power of two between 256 and 2048, the first that gives 8 world units per pixel or better
  (`replay_heightfield.cpp:39-48`). VERIFIED IN CODE.
- Rasterizer: highest height per pixel from triangle edges and interiors; pixels nothing covers get 0 (the bottom of
  the box); 8-bit result (`replay_heightfield.cpp:87-181`). VERIFIED IN CODE.
- Matrix: `replay_heightfield.cpp:183-197`, the same form as the stock mp_shipment record in 1.4. VERIFIED IN CODE and
  DATA.
- Written into the world: count 1 and the outdoor fields stamped at `write_maps.cpp:372-373`; the 96-byte record
  emitted after the light view frustums and before the brush models at `write_maps.cpp:679-680`
  (`replay_heightfield.cpp:207-224`). Images `*heightmap0` (R8) and `$outdoor` (16 x 16 of 125) registered before the
  world (`replay_heightfield.cpp:200-205`). VERIFIED IN CODE.
- The fork writes no voxel tree (`voxelTreeCount` stays 0; no code names it). VERIFIED IN CODE (search) and DATA (4.2).

### 4.2 Check on a built map

Built package `C:\Users\user\AppData\Local\IW8 Radiant Suite\IW8 Radiant Suite\x64zt_output\mp_iw8r_fix1\
mp_iw8r_fix1.ff`, exported read-only with ACTS (`-a image,gfx_map`, `ACTS_REPLAY_STRUCTURED=1`) into the scratchpad:

- Build log: `out\agent\mp_iw8r_fix1\build\compile-log.txt:214`: `heightfield: 256x256 over 76 triangles`.
- `gfx_map`: `heightfieldCount` 1; bounds midPoint (0, 0, 236), halfSize (288, 288, 268); matrix diagonal
  (1/576, -1/576, 1/536); `outdoorImage` = `$outdoor`; `voxelTreeCount` 0; 0 read errors.
- `*heightmap0`: 256 x 256, flags 3, one level, 65,536 bytes; pixel values 0 (outside), 0x0F (floor) and 0x4C (wall
  tops) seen, so it holds the map's real heights.

All VERIFIED IN DATA. With `voxelTreeCount` 0 the engine skips the index search, so the index stays at its initial
value; the build works in game, so that value is 0 (INFERRED: the index is not written anywhere else).

### 4.3 Does this explain a known symptom?

No. The heightfield only feeds the outdoor inputs of effect shaders.

- Bullet impacts that all look like stone: impact choice comes from surface types on collision (research 71), which
  never reads the heightfield. No link.
- Reflection probes with no visible change: `reflectionProbeTexture` is another slot (40) of the same global texture
  table, filled by other code. No link to the heightfield found.
- Killstreak aircraft and their cameras not drawn: no link found. Aircraft heights come from script (research 75).

One neighbour fact, not a finding about these symptoms: stock mp_shipment has `voxelTreeCount` 1 and editor-built maps
have 0. Other renderer code reads the voxel tree with the same index (for example 0x1419A9940 uses the tree's box and
falls back to world +0x84 when there is none). What a missing voxel tree costs was not studied here. UNKNOWN.

---

## 5. Should the editor produce it?

**Not needed, because the converter already builds a correct heightfield from the map on every build, and the game
uses it only to keep rain and snow effects out of roofed areas.** The editor's extraction already contains what the
builder needs (the world surfaces and the static models), so `compile_iw8.cpp` needs no new output. A separate height
grid sampled in the editor would duplicate `replay_heightfield.cpp` and could only disagree with the built world.

Row 5.25 can be closed as "explained; produced by the converter". Optional follow-ups, smallest first. None is needed
for a map without weather effects.

| # | Item | Why | Where | Size |
|---|---|---|---|---|
| a | Document it for mappers | What blocks rain today: opaque world brushes, patches and shadow-casting static models. What does not: sky, glass and other blended surfaces, brush models (doors, movers, brush visuals built as models), and guess: models with `no_shadow 1`, since they leave the sun-shadow caster list (research 54) | manual page | S |
| b | Finer pixels | Stock mp_shipment is about 2.4 units per pixel (2048 px over 5,002 units); the fork gives about 4.9 for the same box and never more than 2048 px. The roof-edge fade is 8 units, so 8 units per pixel can cut rain a pixel inside or outside a doorway. Fix: aim at about 2 to 4 units per pixel and allow 4096 (a 4096 image is 16 MB in the zone) | Converter: `replay_heightfield.cpp:41-48` (`Resolution`), called from `convert.cpp:1843`. Only if a per-map override is wanted: a worldspawn key read where `compile_iw8.cpp` reads the other worldspawn `iw8r_` keys (`compileIw8ExtractionFlat`, `:1386`, for example `:1415`) and carried in the extraction | S |
| c | Editor preview "rain cover" | A top view tint of what counts as covered, from the same rule as (a). Useful only once maps use rain | editor view code | M |
| d | Blocking rain with a brush that does not draw | There is no "weather clip". If wanted: let a tool material add triangles to `solidTriangles` only | `replay_render.cpp:2019-2031` and the extraction | M |

Before any of (b) to (d), the real gate is whether an editor map can use an outdoor rain effect at all: the `_od`
materials and their techsets are in `techsets_mp_shipment.ff`, a map zone, not a common zone. Whether the editor's
stock-material route (roadmap 4.15) can reference them is UNKNOWN and belongs to the FX work (research 67).

Height precision note: heights are 8-bit over the box height. mp_iw8r_fix1: 536 / 255 = 2.1 units per step; a map
with solid geometry 4,000 units tall gets 16 units per step, coarser than the 8-unit fade. Sky is already left out of
the box, so this only matters for very tall solid geometry. INFERRED.

---

## 6. Experiments

1. **Offline, no game (the proof used here).** Export the built zone and read the world:
   `acts.exe --noUpdater fastfile -r mw19replay -g <replay exe> --oodle <oo2core dll> -a image,gfx_map -l -o <scratch>
   <package>\<map>.ff` with `ACTS_REPLAY_STRUCTURED=1`. Pass when `heightfieldCount` is 1, the matrix equals
   (1/sizeX, -1/sizeY, 1/sizeZ) with translation (-minX/sizeX, maxY/sizeY, -minZ/sizeZ), and the `*heightmap0` pixel
   over a known roof brush decodes to that roof's top height (pixel / 255 x sizeZ + minZ, within one step) while a
   pixel over open ground decodes to the ground. Done in this pass for mp_iw8r_fix1 except the roof pixel (that map
   has no roofed room in the heightfield). This could become a check in `tools\verify_map_output.py`.
2. **In game, only if the owner chooses to test.** One map, two identical sheds under open sky: shed A with a normal
   worldspawn brush roof, shed B with the same roof as a `script_brushmodel`. Add a rain effect that uses an `_od`
   material (for example the one behind `vfx_vol_weather_rain_drop_sheet_norm_1_od`). Prediction: no rain inside A,
   rain inside B (a brush-model roof is not in the heightfield), rain everywhere outside. Needs the gate in section 5
   solved first.
3. **In game on stock, zero build.** On stock mp_shipment in the rain, step inside a container: the rain sheets stop
   at the roof line. Then `r_outdoorFeather 2000` (if the console accepts it; its flags are 0x40) should turn the hard
   roof line into a very wide fade, because the dvar divides the height range (2.3; the fade itself is a guess at
   the shader's use of that value). This shows the stock game uses
   the path at all; it proves nothing about the editor's output.

---

## 7. Method, limits, open items

- IDA was not available, so there is no decompiled code and no cross-check of function names. Disassembly was read
  with capstone from `E:\IW8\Builds\1.20-replay\game_dx12_ship_replay.exe` (read only; the game was not started).
  The fork's own address for `Load_GfxWorld` (0xD92E80) matched this file.
- Field-offset scan: finds `[register + offset]` operands inside functions listed in the exe's exception table.
  Reference scan: finds RIP-relative references. Accesses built from a pointer into the middle of a struct, or through
  computed addresses, are not found. Statements of the form "nothing else reads X" carry that limit.
- ACTS exports made in this pass (read-only on stock zones and one built package, output in the scratchpad): techsets
  of 13 stock zones, materials of `techsets_mp_shipment.ff`, images and world of `mp_iw8r_fix1.ff`.
- UNKNOWN: the shader code that turns `outdoorTexture` and constant 166 into "hide this drop"; other shaders that
  sample `outdoorTexture`; which stock zone carries `*heightmap1..7`; whether any stock 6v6 map other than mp_shipment
  ships rain materials with the outdoor option (not found in hideout or piccadilly, but their exports were partial);
  the cost of `voxelTreeCount` 0 in editor-built maps.
