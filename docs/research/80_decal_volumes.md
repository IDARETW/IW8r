# 80. Decal volumes (roadmap 5.8): where they live, the record layout, and a producer plan

Date: 2026-11-11. Scope: roadmap row 5.8 "Decal volumes (box entities + decal volume materials)". Analysis and design
only: no source file was changed, nothing was built, the game was not started. This file is the only file written.

A **decal volume** is an oriented box placed in the map. The game projects a picture (the decal material) along one
axis of the box onto every surface inside it: paint, logos, stains, puddles, bullet-hole clusters. It is not geometry;
the lit pixel shaders blend it into the colour and normal of whatever they draw.

Status words used below:

- **VERIFIED IN CODE**: read in the stock exe (address given) or in the converter fork source (file:line given).
- **CHECKED ON STOCK DATA**: a sub-class of INFERRED. The rule could not be read in the 1.20 exe, but it decodes all
  46 stock mp_shipment records to sensible values and reproduces stored cross-check values. Strong, but not code.
- **INFERRED**: follows from other evidence; the reason is given.
- **UNKNOWN**: not determined; what would settle it is given.

## 0. Sources and method

| Short name | What it is |
|---|---|
| exe | `E:\IW8\Builds\1.20-replay\game_dx12_ship_replay.exe` (98,035,200 bytes, image base 0x140000000). The IDA MCP tools did not respond in this session ("IDA Assist is not reachable", three tries), so the file was read directly and disassembled with capstone (read only). Addresses are the same virtual addresses IDA shows. The fork's own notes name the same addresses for the same functions (Umbra consumer 0x18CCA80, visibility clears 0x18EC6F1), so it is the same build. |
| fork | `E:\mw124\x64-zt`, branch `iw8radiant/build-extraction`, head 062ec6b4 (2026-11-11). Read only. `S:` = `src\games\iw8\structs.hpp`, `A:` = `src\games\iw8\zone\asset_streams.cpp`, `W:` = `src\games\iw8\assets\write_maps.cpp`, `RR:` = `src\games\iw8\common\replay_render.cpp`, `R:` = `src\convert\iw3_iw8\iw3_render_assets.cpp`, `P:` = `src\convert\iw3_iw8\iw3_fastfile.cpp`, `L:` = `src\games\iw8\zone\replay_map_layout.h`, `H:` = `src\convert\iw3_iw8\resources\map_surface_realtime.hlsl`. |
| dump | `E:\IW8-Radiant\out\m2-x64zt\dump\mp_shipment` (x64-zt dump of stock mp_shipment). Its `.iw8` files are the fork's "IW8D" record streams (`src\games\iw8\zone\streams.hpp:345-425`); they were parsed with a small script. |
| ref | `E:\mw124\tools\dvar_search_web\src\combined\game-test-src-cleaned\code_source\src\`: a cleaned decompile of a **different, later development build**, with names. Files `gfx_d3d\r_decal_volume.cpp`, `gfx_d3d\r_decal_volume_load_obj.cpp`, `qcommon\radiant_remote.cpp`. It is not the 1.20 exe, so nothing read only there is marked VERIFIED IN CODE. It was used as a hypothesis and then tested against the dump and the exe. |
| editor | `E:\IW8-Radiant\src\core\compile_iw8.cpp` (`E:`), `data\iw8_entity_defs.json`. |

Scratch scripts (parser, decoder, disassembly listings) are in
`%USERPROFILE%\AppData\Local\Temp\claude\2b25dc13d65a86cc\f955f107-10f4-450a-a1df-e62aedc1d809\scratchpad\r80`
(`iw8d.py`, `decode.py`, `decoded.txt`, `roundtrip.py`, `pe.py`, `dis_1860_1890.txt`). The decode rules are repeated in
section 2 so this document does not depend on them.

Why the bit layouts cannot be VERIFIED IN CODE in the 1.20 exe: the exe never decodes a packed record on the CPU. It
uploads the 44-byte array unchanged to a GPU buffer (exe 0x14187D750: element size 0x2C, name "decal volumes buffer")
and compute shaders named `cs_decal_volume_generate.hlsl/...` (strings at exe 0x14258D8C0-0x14258DA60) unpack it. Those
shaders are `computeshader` assets in `code_post_gfx.ff`, not in the exe and not in any dump on disk. The constant the
ref build uses to unpack on the CPU (0.0027675412, the 9-bit quaternion scale) does not exist in the 1.20 exe
(searched). What would upgrade the layout to VERIFIED: dumping the `computeshader` pool of `code_post_gfx` and
disassembling `cs_decal_volume_generate_draw_data` (section 8, item 1).

## 1. Where decal volumes live (question 1)

They live in **GfxWorld** (asset `gfx_map`, the render world) and in the map's **GfxWorldTransientZone** (asset
`gfx_map_trzone`, the streamed part of the render world). MapEnts has none: decal volumes are not entities at run time.

| Part | Where | Size | Status |
|---|---|---|---|
| Counts: `decalVolumeVersion`, 3 reserved, `decalVolumeMaterialCount`, `decalVolumeMaskCount`, `mapDecalVolumeCount`, `decalVolumeCollectionCount` | GfxWorld +0x740 .. +0x75C (GfxWorldDraw +0xF8 .. +0x114) | 8 x u32 | VERIFIED IN CODE: `S:12892-12899`, asserts `S:24044-24051`; exe reads GfxWorldDraw +0x108 / +0x10C at 0x14187D7A0 and +0x114 at 0x140D9697C |
| `decalVolumeMaterials` | GfxWorld +0x760 | array of `Material*` (8 bytes each) | VERIFIED IN CODE: `S:12412`, `A:3845-3853`; exe 0x14187D7B0 |
| `decalVolumeMasks` | GfxWorld +0x768 | array of `GfxDecalVolumeMask*` (8 bytes each) | VERIFIED IN CODE: `S:12417`, `A:3855-3863`; exe 0x14187D7B7 |
| `decalVolumeCollections` | GfxWorld +0x770 | array of 8-byte records (1.1) | VERIFIED IN CODE: `S:12422`, `A:3865`; exe 0x141881ADC |
| `materialAndMaskIndirection` | GfxWorld +0x778 | u32 x (materials + masks). Zero in the file; filled at load | VERIFIED IN CODE: `A:3866`; exe 0x14187D7E0-0x14187D831 |
| `materialAndMaskIndirectionBuffer` | GfxWorld +0x780 | 64-byte runtime GPU buffer wrapper, zero in the file | VERIFIED IN CODE: exe 0x14187D84D (name string 0x1425452C0) |
| `dpvs.decalVisDataCount`, `dpvs.decalVisData` | GfxWorld +0x3FAC, +0x41D8 | u32 count of 32-bit words; the words are run-time visibility bits, one bit per collection | VERIFIED IN CODE: `S:24286`, `S:24292`, `L:40`, `L:47`; exe 0x14187EAC5 |
| `decalVolumes` (pointer to the per-zone block) | GfxWorldTransientZone +0x138 | pointer to a 96-byte `GfxDecalVolumeTransientData` | VERIFIED IN CODE: `S:12813`, `S:23998`, `A:4828` |
| The packed records, the collection bounds, the uids | inside `GfxDecalVolumeTransientData` (1.2) | see 1.2 | VERIFIED IN CODE: `S:12780-12789`, `A:4787-4792`; exe loader 0x140D90B90 |

A **collection** is a group of decal volumes that are near each other. The game decides visibility per collection, not
per decal.

**mp_shipment numbers** (read from the dump, header at GfxWorld +0x740): version 0, reserved 0 / 0 / 0, **15
materials, 16 masks, 46 decal volumes, 7 collections**, one transient zone. Collections (zone, count, first): (0,11,0)
(0,1,11) (0,14,12) (0,15,26) (0,2,41) (0,2,43) (0,1,45); the counts add up to 46. bspVersion 243.

### 1.1 GfxDecalVolumeCollection (8 bytes)

| Offset | Size | Field | Status |
|---|---|---|---|
| 0x0 | u16 | `transientZoneIndex`: which transient zone holds the records | VERIFIED IN CODE: `S:23755`; exe 0x14187EB4B (tests this zone's bit in the frame's "zones drawn" set) |
| 0x2 | u16 | `decalCount` | VERIFIED IN CODE: `S:23756`; exe 0x141881B2B |
| 0x4 | u32 | `firstZoneDecal`: index of the first record in that zone's array | VERIFIED IN CODE: `S:23757`; exe 0x141881B45 (packs `first + i` with `min(16, remaining) << 24`) |

### 1.2 GfxDecalVolumeTransientData (96 bytes, one per transient zone)

| Offset | Size | Field | Status |
|---|---|---|---|
| 0x00 | u32 | `decalVolumeCount` | VERIFIED IN CODE: `S:23973`; exe 0x140D90C0E (`count * 0x2C` bytes loaded), 0x14187D754 |
| 0x04 | u16 | `firstCollection`: index of this zone's first collection in the world table | Offset VERIFIED IN CODE (`S:23974`); meaning INFERRED (ref uses it only for debug boxes; mp_shipment: 0) |
| 0x06 | u16 | `collectionCount` | VERIFIED IN CODE: `S:23975`; exe 0x140D90C94 (count of `collectionBounds`) |
| 0x08 | ptr | `decalVolumes`: array of 44-byte packed records | VERIFIED IN CODE: `S:23976`; exe 0x140D90BB8, 0x14187D76E |
| 0x10 | 64 | `decalVolumesBuffer`: runtime GPU buffer wrapper, zero in the file | VERIFIED IN CODE: `S:23977`; exe 0x14187D757 |
| 0x50 | ptr | `collectionBounds`: one `Bounds` (midpoint xyz, half size xyz; 24 bytes) per collection | VERIFIED IN CODE: `S:23978`; exe 0x140D90C6D. No reader found in 1.20 run-time code (INFERRED: used when the stock visibility data was built) |
| 0x58 | ptr | `decalVolumeUids`: one u32 per record | VERIFIED IN CODE: `S:23979`; exe 0x140D90CAB. Meaning INFERRED: an id the stock editor's live link uses to find a decal (ref `R_DecalVolumes_GetUID`); mp_shipment holds 46 distinct hash-like values |

## 2. The 44-byte record, GfxStaticDecalVolumePacked (question 1)

Top-level offsets and sizes are VERIFIED IN CODE (`S:12764-12773`, asserts `S:23962-23969`; exe 0x14187D779 stride
0x2C). The meaning and bit layout of each field is in the table. "ref" names the function of the other build that
states the rule; the status says how the 46 mp_shipment records agree.

| Offset | Size | Field | Layout and meaning | Status |
|---|---|---|---|---|
| 0x00 | 3 x f32 | `obbCenter` | Box centre, world units, plain floats | CHECKED ON STOCK DATA: all 7 stored collection bounds are reproduced from the decoded centres and boxes (worst difference 0.74 units; the rest is rotation rounding) |
| 0x0C | u32 | `obbQuat` | Box rotation as a quaternion (x, y, z, w) with its largest part dropped. bits 30-31: index of the dropped part (0 = x .. 3 = w). bits 0-9: first kept part, `v = raw * 0.001382418 - 0.70710677`. bits 10-19: second kept part, same scale. bits 20-28: third kept part, `v = raw * 0.0027675412 - 0.70710677`. The dropped part is `+sqrt(1 - sum of squares)`. Kept parts stay in x, y, z, w order | CHECKED ON STOCK DATA (ref `R_DecalVolumes_UnpackOBB`, `r_decal_volume_load_obj.cpp:568-644`): every record decodes to a rotation whose thin box axis is a clean floor or wall direction, and the bounds check above passes |
| 0x0C | bit 29 | (inside `obbQuat`) | Not used | 0 in all 46 stock records; ref never reads it. Purpose UNKNOWN; write 0 |
| 0x10 | u32 | `obbHalfSize` | Three half sizes packed as R11G11B10F (unsigned small floats: 5-bit exponent with bias 15, then 6, 6 and 5 mantissa bits). bits 0-10: half size along box axis 0 (the projection depth). bits 11-21: along axis 1. bits 22-31: along axis 2 | CHECKED ON STOCK DATA (ref `Vec3PackR11G11B10F`): decodes to round numbers (1, 32, 32; 4, 80, 36; ...); re-encoding gives the identical word for 46 of 46 |
| 0x14 | u32 | `opacityParams` | Four bytes, each `value * 255`. byte 0: blend-map edge 0. byte 1: blend-map edge 1. byte 2: blend-map scale (acts as the decal's top opacity). byte 3: depth feather, 0 to 1 (0 = hard edge at the front and back of the box). The mask value m becomes `scale * (m - edge0) / (edge1 - edge0)`, clamped | INFERRED from ref (`R_DecalVolumes_PackOpacityParams`, `:475-493`; `R_DecalVolumes_PrecalculateBlendMapAdjust`, `:538-560`). Stock is consistent with four independent bytes: 10 of 46 are the defaults 0, 255, 255, 0; byte 0 is 0 in 42 of 46. But 4 stock records have byte 0 = byte 1 (for example 51, 51, 102, 255), which the ref's live-update path would reject as "edge 1 must be greater than edge 0". So the exact curve is not settled (a hard threshold is the likely reading). The first version should write bytes 0 and 1 as 0 and 255 and vary only bytes 2 and 3 |
| 0x18 | 3 x u32 | `uvMatrixAndOffset` | Six half floats (IEEE 16-bit), low half first: matrix m00, m01, m10, m11, then offset u, offset v. Picture coordinate = matrix x box coordinate + offset. Identity = 1, 0, 0, 1, 0, 0 | CHECKED ON STOCK DATA (ref `R_DecalVolumes_PackUVTransform`, `:501-531`): 29 of 46 are the identity, the rest are clean 90 or 180 degree turns or tilings such as 3.78 x 0.078 on long caution stripes |
| 0x24 | u32 | `packedMaterialInfo` | bits 0-10: index into the world's `decalVolumeMaterials` (0x7FF = none). bits 11-21: index into `decalVolumeMasks` (0x7FF = no mask). bits 22-23: normal blend mode (0 blend, 1 add, 2 no override). bits 24-28: "map draw order", a running number modulo 32. bits 29-31: user draw order + 4 (4 = default, range -4 .. +3) | Material bits: VERIFIED IN CODE by analogy: the exe packs the same field for bullet marks as `(index & 0x7FF) \| 0x3FF800` at 0x14187E8B5-0x14187E8BF (0x3FF800 = "no mask" in bits 11-21). Static indices are map-local and go through the indirection table: INFERRED (ref `R_DecalVolumes_GetMaterialInfo`, `r_decal_volume.cpp:10634-10701`), CHECKED ON STOCK DATA: material index always < 15, mask index < 16 or 0x7FF (4 records), and the pairs make sense (blood pool + splatter mask, logo + logo mask). Bits 22-31: INFERRED from ref (`:10698-10700`, debug text `:9905`); stock: bits 22-23 are 2 in 35, 1 in 9, 0 in 2 records; bits 24-28 run 0, 1, 2 .. 31, 0 .. 13 in record order; bits 29-31 are 2 to 7 |
| 0x28 | u32 | `reserved` | 0 | 0 in all 46. Purpose UNKNOWN; write 0 |

Field count for the report: the 7 top-level offsets are VERIFIED IN CODE. Of 19 sub-fields: 1 VERIFIED IN CODE by
analogy (material index bits); 9 CHECKED ON STOCK DATA (centre, the three kept quaternion parts and the dropped
index, half size, UV matrix, UV offset, mask index); 7 INFERRED from the ref build with stock agreement only (the four
opacity bytes, normal blend mode, map draw order, user draw order); 2 UNKNOWN in purpose and 0 in stock (quaternion
bit 29, `reserved`).

**Box axes** (CHECKED ON STOCK DATA): box axis 0 is the projection direction and points **into** the surface. 13
records project straight down; the 7 at floor height (asphalt patches, pothole, puddle, four blood pools) decode to
axis 0 = (0, 0, -1) with a half depth of 1 to 2.5 units. Half sizes 1 and 2 are the picture's half width and half
height. Which of axis 1 / axis 2 is "u" and "v", and their signs: INFERRED from ref
(`MatrixForViewerOrthogonalTransposed`, `r_decal_volume_shared.h:53`: u along minus axis 1, v along axis 2, depth
along axis 0). UNKNOWN until seen in game with an asymmetric picture.

No fade distance is stored per decal. The draw distance is the vision field `decalVolumeDrawDistance` (index 207,
default 5000; string at exe 0x142388BF0; research 31 section 3.9). No per-decal colour either: tint belongs to the
material.

## 3. Game code that reads them (question 2)

All VERIFIED IN CODE in the exe unless marked.

| Address | What it does |
|---|---|
| 0x140D96946-0x140D96A14 (inside the loader the fork calls `Load_GfxWorldDraw D96710`) | Loads `decalVolumeCollections` (`collectionCount * 8` bytes), `materialAndMaskIndirection` (`materials + masks` u32), then calls 0x14187D790 |
| 0x14187D790 | Fills the indirection table. For each world decal material, `Material +0x58` (`decalVolumeMaterial`) is turned into a pool index by 0x140F5F260 (`(ptr - poolBase) / 104`); for each mask, 0x140F5F240 (`(ptr - poolBase) / 16`), stored after the materials. Then it creates the GPU buffer "materialAndMaskIndirectionBuffer". **There is no null check**: a world decal material whose `decalVolumeMaterial` is null gives a garbage index |
| 0x140D90B90 | Loads `GfxDecalVolumeTransientData`: the 44-byte array, a call to 0x14187D750, then `collectionBounds` and `decalVolumeUids` |
| 0x14187D750 | Creates the GPU buffer "decal volumes buffer": element size 44, count = `decalVolumeCount`, initial data = the packed array as stored |
| 0x1418EC6D7-0x1418EC6F1 | At client start: clears `dpvs.decalVisData` for `(decalVolumeCollectionCount + 31) / 32` words. No pointer check: a non-zero collection count with a null `decalVisData` crashes here (the fork notes the same at `L:32-35`) |
| 0x1418CCA80 (0x1418CCBD8-0x1418CCC1B) | Visibility. Each object the Umbra query returns carries a class in bits 28-30 of its id. Class 6 (`0x60000000 + n`) sets bit n of `dpvs.decalVisData`, most significant bit first; n is a **collection index**. (Umbra is the stock occlusion library; its baked data is the "tome".) |
| 0x14187EA10-0x14187EC25 | Per frame: walks the set bits of `dpvs.decalVisData`; a collection is taken when its `transientZoneIndex` is in the frame's drawn-zones set; at most 0x800 collections. Two dvar checks at the top (INFERRED: `r_decalVolumes`, `r_decalVolumesStatic`). **1.20 has no other path**: a collection whose bit is never set is never drawn (the ref build has a fallback behind `r_decalVolumesUmbraVisibility`; the 1.20 function does not) |
| 0x141881940-0x141881BBF | Turns the collected collections into work groups of up to 16 records (`first + i`, `count << 24`), at most 0x1000 groups, counted per transient zone |
| GPU: `cs_decal_volume_generate.hlsl/cs_decal_volume_cull`, `..._generate_draw_data`, `..._generate_cluster_data` (names at exe 0x14258D8C0-0x14258DA60) | Unpack the records, cull, and write 256-byte "draw data" records and per-screen-cluster lists. NOT read (not in the exe) |
| Pixel shaders | The lit world shaders read the cluster lists and the draw data and blend colour and normal. The fork ported that read into its own world shader: `H:786-878` (its comment says it was ported from the stock forward+ pixel shader), called at `H:1332` |
| 0x14187E790 | Bullet-mark decal volumes (dynamic, 76-byte records). Not map data; listed because it confirms the material bits |

## 4. What a decal volume material is (question 3)

Three assets work together. Stock example `i/decal_rust_01`, read from the ACTS dump
`E:\IW8-Radiant\out\agent_techset\all\mw19replay\techsets_mp_emporium\assets\material\i\decal_rust_01.1302.asset.json`
and the matching `decalvolumematerial` file (tool output, so "read in a dump", not in code):

1. A **Material** named `i/<name>` that is only a carrier: `materialType` MTL_TYPE_INFO_CARRIER (0x800000),
   `cameraRegion` 3, `sortKey` 30, `contents` 1, **no textures, no constants**, technique set reference `,null`, and
   `decalVolumeMaterial` pointing at asset 2. The world's `decalVolumeMaterials` table lists these Materials (dump: 15
   names, `i/decal_rust_01` .. `i/decal_logo_05`).
2. A **GfxDecalVolumeMaterial** (asset type 111, `decalvolumematerial`, 104 bytes; `S:3463-3477`, asserts
   `S:17842-17855`; pool of 1536, `atian-cod-tools\docs\mw19\pools.md:125`) with the same name. It holds six image
   slots `channels[6]`, `flags`, `colorTint`, `alphaDissolveParms`, `emissiveScale`, a packed displacement scale and
   bias (two half floats), displacement cutoff distance and falloff, a packed temperature pair, atlas rows and columns.
   Read from the 17 mp_shipment dumps:
   - channel 0 = a `<colour>&<specular>` image, channel 1 = a `<normal>&<gloss>` image, channel 2 = the plain colour
     image (its alpha is the decal's own shape), channels 3-5 empty in all 17.
   - `flags`: bit 2 (4) is set exactly when channel 0 is present, bit 3 (8) when channel 1 is, bit 6 (0x40) when
     channel 2 is; 0x400 always; bit 0 on painted and logo decals. Bits 2, 3 and 6: INFERRED from 17 of 17 assets and
     the fork's shader comment (`H:792-793`). Bits 0 and 0x400: UNKNOWN.
   - everything else is the same in all 17: tint 1, 1, 1 (or a paint colour), dissolve 0, emissive 1, displacement
     (2, -1), cutoff 750, falloff 0.004, temperature (-1, 0), atlas 1 x 1 (one is 4 x 1).
3. Optionally a **GfxDecalVolumeMask** (asset type 112, `decalvolumemask`, 16 bytes: name + one image
   `<name>~atlasbc4`; `S:9935`; pool of 384). A mask is a grey picture that shapes one placement (a stencil, a splatter
   outline). A placement without a mask uses index 0x7FF.

The game copies the images into shared decal atlases at run time (strings `$decal_volume_primary_channel` ..
`$decal_volume_bc7_temp`, exe 0x142584D60). No technique set and no shader belong to the material.

**The fork can already write assets 1 and 2.** `RR:2818-2866` (`RegisterDecalVolumeDefinition`) writes a
`decalvolumematerial` with exactly the stock defaults above (`replay_render.h:63-76`), and `RR:2689`, `RR:2733-2743`
write the Material's `decalVolumeMaterial` pointer. `R:8533-8577` builds the definition from a colour, normal and
specular picture for IW3 effect decals (`i/mw120r_fx_decal_<id>`, flags 1100 = 0x44C). VERIFIED IN CODE (fork). That
the result draws in game for effect decals is stated by the fork's shader comment (`H:786-788`); not checked here. The
fork has **no writer for a mask asset** in this route.

**Reference by name or carry?** The same rule as for any asset (research 51):

- A stock decal Material in an always-loaded zone can be named. From `data\asset_catalog\iw8_materials.tsv`: about 25
  `i/` materials sit in `common_mp`, `common_base_mp`, `common_core_mp` or `code_post_gfx`, for example
  `i/vfx_decal_surface_scorch_1`, `i/vfx_decal_surface_scorch_2`, `i/vfx_fluid_blood_splat_m01_dec`,
  `i/vfx_decal_oil_pool_1`, `i/gfx_crater_dirt`, `i/mtl_default_vol_decal`. INFERRED that a name reference to one of
  them resolves in a custom map (research 51; the fork writes such references as `,name`, `RR:2698`, `RR:3164`).
- The map-art decals (`i/decal_*`, 196 names in the catalog) live in map zones (`mp_*`, `techsets_mp_*`). Those zones
  are not loaded with a custom map, so these must be carried: Material + definition + images. mp_shipment itself
  shows the split is normal: its world table names `i/decal_blood_pool_01` and `i/decal_stencil_al_asad`, whose
  definitions are not among the 17 in `mp_shipment.ff`.
- A mapper's own picture: built by the fork's existing effect-decal builder (above). Nothing new on the material side.

## 5. What the converter fork does today (question 4)

- **It writes nothing.** The GfxWorld writer is `W:` (hand-written, in load order). No line stamps GfxWorld +0x740 ..
  +0x780, +0x3FAC or +0x41D8, and the transient zone block (`W:604-614`, `RR:3177-3187`) leaves +0x138 zero. So the
  built world has 0 materials, 0 masks, 0 decals, 0 collections and null pointers. VERIFIED IN CODE (fork). That is
  safe: every reader in section 3 is gated by a count.
- Correction to research 46 section 3a: `W:365-368` does **not** leave the decal sort range zeroed. It writes the
  stock table 7, 14, 17, 35, 24, 36, 41, 1, 6, 7, 17, 18, 34, 35, 40, which is exactly mp_shipment's (dump, GfxWorld
  +0x3C). The comment there explains why zeros would be wrong.
- The layout constants already exist and are unused: `L:40` (`DecalVisDataCount`), `L:47` (`DecalVisData`).
- The neutral schema round-trips the arrays (`src\games\iw8\neutral\gfxworld.cpp`), but the build-extraction route
  does not go through it (research 46).

### 5.1 Where a producer goes, mirroring the volumetric boxes (the nearest existing feature)

| Step | Volumetric boxes today | Decal producer (new) |
|---|---|---|
| Read the extraction key | `R:5682-5731` reads `world.volumetrics` | Read `world.decal_volumes` next to it |
| Carry to the render mesh JSON | `P:6155` | Add `decal_volumes` and the resolved material names |
| Parse and validate | `RR:1406-1465` into `Mesh::volumetrics` (`replay_render.h:288`) | New `Mesh::decalVolumes` and `Mesh::decalMaterials`; refuse bad entries the way `RR:1436-1441` does |
| Stamp the world header | `W:468-476` | +0x750 materials, +0x754 masks (0), +0x758 decals, +0x75C collections; pointers +0x760, +0x770, +0x778 = "follows"; +0x3FAC = `(collections + 31) >> 5`; +0x41D8 = "follows" |
| Emit the world arrays | `RR:3118` (`EmitVolumetrics`), called at `W:669` | In `Load_GfxWorldDraw` order (`A:3845-3866`), **before** the transient zone block that starts at `W:597-602`: the Material pointer array (alignment 8) with one material reference each (as `EmitSurfaces` does, `RR:3160-3168`), the collection array (alignment 4), the zeroed indirection array (alignment 4, `materials + masks` u32) |
| Emit the per-zone block | (none) | Stamp transient +0x138 = "follows" beside `W:612`. Inside the zone body after `replaylightgrid::Emit` (`W:661`), in the order of `A:4816-4832`: the 96-byte block (count, firstCollection 0, collectionCount, three pointers), then `A:4787-4792`: the 44-byte records, the bounds, the uids (alignment 4 each). The stream pushes must be matched to exe 0x140D90B90 (it treats the records pointer specially at 0x140D90BC1-0x140D90C28) |
| Zero-filled visibility words | `W:749`, `W:763` (stream 4, 128-byte aligned) | The same for `decalVisDataCount` words; `A:4192` gives the place (right after `volumetricVisData`) |
| Umbra objects | `RR:3233-3234`: `0x30000000 + n` per volumetric | `0x60000000 + collection` per collection, counted in `objectCount` (`RR:3208-3210`). Every object already spans the scene (`RR:3289-3290`), so each collection's bit is always set |

Fields the producer must fill. Per decal: centre (3 floats), quaternion word, half-size word, opacity word (default
0x00FFFF00), three UV words (default 0x00003C00, 0x3C008000, 0), material info word, reserved 0. Per collection: zone
0, count, first, and one `Bounds`. Per zone: the uids (any distinct u32, for example a hash of the index). World:
`decalVolumeVersion` 0, as stock.

Packing rules (the inverse of section 2). `roundtrip.py` re-encodes the stock words with them: half size 46 of 46
identical; quaternion 32 of 46 identical, and the other 14 are ties between two equally large parts, which decode to
the same rotation.

- Quaternion: turn the box axes (rows: depth axis, width axis, height axis) into a quaternion x, y, z, w; pick the
  part with the largest magnitude; flip all four signs if it is negative; store its index in bits 30-31; store the
  other three, in order, as `round((v + 0.70710677) / 0.001382418)` (10 bits, twice) and
  `round((v + 0.70710677) / 0.0027675412)` (9 bits).
- Half size: `e = floor(log2(x))`, mantissa `round((x / 2^e - 1) * 64)` (`* 32` for the third), carry into the
  exponent when the mantissa overflows, field = `(e + 15) << mantissaBits | mantissa`. x must be above 0 and below
  65536; precision is about 1.5 percent (3 percent for the third).
- Material info: `material | 0x7FF << 11 | blend << 22 | (index & 31) << 24 | (drawOrder + 4) << 29`.

Size of the fork work: **medium, roughly 300 to 450 lines** in 5 files (`iw3_render_assets.cpp`, `iw3_fastfile.cpp`,
`replay_render.cpp`, `replay_render.h`, `write_maps.cpp`), plus a fixture test. No new asset type, no shader, no
unknown struct. The material side reuses `R:8533-8577` (own pictures) or a name reference (stock). Masks are left out
of the first version.

Feasibility: **yes**. Every container field is verified in the loader, the record layout is confirmed on 46 stock
records, and visibility uses a mechanism the fork already uses for four other object classes.

## 6. Editor design (question 5)

One box, one material, the same shape as the existing volumetric box (`E:2314-2362`,
`data\iw8_entity_defs.json:332`).

**Entity class `iw8r_decal`** (point entity, `"kind": "point"`, `buildAccepted` true):

| Key | Type | Default | Meaning |
|---|---|---|---|
| `origin` | vec3 | required | Box centre |
| `angles` | vec3 | `0 0 0` | Pitch yaw roll. The entity's forward (X) axis is the projection direction; it must point into the surface |
| `size` | vec3 | `8 64 64` | Depth, width, height in map units (full sizes; each above 0) |
| `material` | string | required | A custom-library material (the mapper's picture), or a stock `i/...` name from an always-loaded zone |
| `opacity` | float | `1` | 0 to 1; written as the blend-map scale (byte 2) |
| `feather` | float | `0` | 0 to 1; soft fade toward the front and back of the box (byte 3) |
| `draw_order` | int | `0` | -4 to 3; higher draws over lower where decals overlap |
| `uv_rotate`, `uv_scale`, `uv_shift` | float, vec2, vec2 | `0`, `1 1`, `0 0` | Optional picture turn, tiling and shift; written as the 2 x 2 matrix and the offset |

Left out of the first version: masks, normal blend mode (write 2 = no override, the value in 35 of 46 stock records),
blend-map edges (write 0 and 255). For reference only, the stock editor's own key names (ref
`radiant_remote.cpp:2987-3290`, another build): `modeloverridematerial`, `blendMapImageName`,
`normalBlendModeOverride` (blend / add / no_override), `edgeFeatherZ`, `blendMapAdjustEdge0`, `blendMapAdjustEdge1`,
`blendMapAdjustScale`, `explicitUVShift`, `explicitUVScale`, `explicitUVRotateDeg`, `uvAutoTiling`, and `decalsize` or
`size_min` / `size_max` with `origin` and `angles`.

**What `compile_iw8.cpp` writes**: a loop beside the volume loop at `E:2319`, a skip beside `E:2040` (so the class does
not reach the entity list), and the key appended beside `volumesKey` at `E:2362-2368`:

```
"decal_volumes": [
  { "center": [x, y, z],
    "axes": [[fx, fy, fz], [lx, ly, lz], [ux, uy, uz]],   // anglesToAxis(angles): depth, width, height axis
    "half_size": [depth / 2, width / 2, height / 2],
    "material": "name",
    "opacity": 1.0, "feather": 0.0, "draw_order": 0,
    "uv_matrix": [1, 0, 0, 1], "uv_offset": [0, 0] } ]
```

The editor validates before writing (finite numbers, sizes above 0, opacity and feather 0 to 1, draw order -4 to 3, at
most 2047 distinct materials) and leaves a bad entity out with a warning, as it does for volumes, so a bad decal cannot
stop the build. For a custom-library material it also writes the material file in the form the fork's effect-decal
reader accepts (`R:1996-2070`, `R:2141-2186`; example in research 31 section 3.9): `_game` iw3, `_type` material,
`_version` 1, `techniqueSet` `mc_l_sm_b0c0`, `cameraRegion` decal, `stateFlags` 52, atlas 1 x 1, textures `colorMap`
(plus optional `normalMap`, `specularMap`), constant `colorTint`. The colour picture's alpha is the decal's shape.

**What the converter reads**: the key above. It packs each entry (section 5.1), groups decals into collections (first
version: runs of up to 16 in file order; bounds = union of the boxes), resolves each distinct `material` to a Material
that has a decal definition (a name reference, or one it builds), and numbers them 0 .. n-1 for bits 0-10.

In the editor's own viewport the box is an editor-only marker (a wire box plus an arrow along the projection axis); it
never affects game-like rendering.

## 7. The smallest experiment (question 6)

No play test is needed to start. Steps 1 and 2 are offline.

1. **Fork only, no editor change.** Add the `decal_volumes` reader, packer, writer, visibility words and class-6 Umbra
   objects (section 5.1). Take the test fixture extraction (`tests\data`, never `samples\iw8`) and add by hand to its
   `world.json` one decal: centre on an opaque brush floor near a spawn, axes `[[0,0,-1],[0,1,0],[1,0,0]]`, half size
   `[4, 32, 32]`, material `i/vfx_decal_surface_scorch_1` written as a **name reference** (the catalog lists it in
   `common_mp`, `common_base_mp` and `common_core_mp`). This needs no material builder at all.
2. **Prove it offline.** Build with the exe copy in `out\audit_bin`, dump the built zone with x64-zt (the way
   `out\m2-x64zt\dump\mp_shipment` was made) and run the section 2 decoder on it. Expect: counts 1, 0, 1, 1 at GfxWorld
   +0x750; one collection (0, 1, 0); `decalVisDataCount` 1; one 44-byte record that decodes back to the input, with
   opacity word 0x00FFFF00, UV words 0x00003C00 0x3C008000 0, and info word
   `0 | 0x7FF << 11 | 2 << 22 | 0 << 24 | 4 << 29` = 0x80BFF800. Also run `tools\verify_map_output.py` and read the
   zonetool log.
3. **In game, if the owner chooses to.** Load the map and look at the spot: a scorch mark on the floor. A second map
   with a custom picture of the letter "F" settles the u and v directions.

What could go wrong, most likely first:

| Risk | Why | Guard |
|---|---|---|
| Nothing draws | 1.20 draws a collection only when its `decalVisData` bit is set (exe 0x14187EAC5), and the bit comes only from a class-6 Umbra object (exe 0x1418CCBD8) | Step 1 adds the objects; step 2 checks that the tome's object count grew by the collection count |
| Crash at client start | A collection count above 0 with a null `decalVisData` (exe 0x1418EC6F1) | Stamp the count and the pointer together, as `W:472-476` does for volumetrics |
| Wrong picture or crash at load | A world decal Material without a `decalVolumeMaterial` gives a garbage pool index (exe 0x14187D7E3, no null check) | The converter refuses a decal whose material has no definition |
| The stock material name does not resolve | The named material must be in a zone that is loaded with a custom map (research 51). Its zone list comes from the catalog, not from a load test | Fall back to `i/mtl_default_vol_decal` (`code_post_gfx`) or to a built material |
| It draws on some surfaces only | Only shaders that read the decal clusters show decals: stock lit shaders, and the fork's world shader for its opaque kinds 0 and 3 (`H:1332`); not alpha-tested, blended or sky surfaces. Static models can opt out (flag 32, `S:12064`) | Put the test decal on a plain opaque brush floor |
| The picture is mirrored or turned | The u and v directions are INFERRED | Step 3 with the "F" picture, then fix the axes in the converter |
| The 1.20 layout differs from the ref build | The bit rules come from another build | Already checked on 46 stock 1.20 records; bspVersion 243 is past every decal format change in the fork's enum (`S:11845-11901`: versions 183, 184, 187, 238) |
| Load stream mismatch | The records pointer has special handling at exe 0x140D90BC1 | The step 2 dump round trip fails loudly if the stream order is wrong |
| Custom pictures look wrong | The effect-decal builder's images must suit the decal atlases | Use the stock material first (step 1); custom pictures are the second step |

## 8. Open items

1. Read the compute shader `cs_decal_volume_generate_draw_data` (pool 12 `computeshader`, zone `code_post_gfx`) to move
   the section 2 bit layouts from "checked on stock data" to "verified in code". Needs a dump of that pool. Not
   blocking.
2. The u and v directions and signs (the in-game "F" test).
3. The meaning of material `flags` bits 0 and 0x400, quaternion bit 29, the record's `reserved`, and
   `decalVolumeVersion`. All can be written the way stock has them.
4. Masks: a `decalvolumemask` writer (16 bytes plus one BC4 atlas image) for stencils. Second version.
5. Whether `collectionBounds` is read anywhere at run time in 1.20 (no reader found; write it correctly anyway).
6. The classname the stock editor uses for a decal volume entity (not needed).

## 9. Corrections to earlier research

- Research 46 sections 3a and 3d: "`write_maps.cpp:365-368` leaves the decal sort range zeroed" is wrong (section 5).
- Research 46 section 3c and item 11 of its UNVERIFIED list ("packed bit layouts UNKNOWN"): answered in section 2.
- Research 46 item 12 ("is a non-empty collections table needed?"): yes. Visibility is per collection and 1.20 has no
  other path (section 3).
- Research 46 section 3c ("needs a six-channel definition, not a custom library material"): the definition is built
  by code the fork already has (`R:8533-8577`); the editor only has to write the material file in the effect-decal
  form.
- Research 09 section 2: the structs listed there are right, but the placement records are not in GfxWorld itself;
  they are in the transient zone's `GfxDecalVolumeTransientData`.
