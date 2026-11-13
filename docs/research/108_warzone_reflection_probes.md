# 108. Warzone reflection probes (Verdansk, Quarry): layout, streaming, selection and an editor recipe

Read-only research, 2026-11-13. Goal: make the stock-probe reader (`src/core/stock_probes.{h,cpp}`, cache
`probes.iw8rprb`, renderer `setStockProbes` / `stockProbeLight` in `src/viewport/render.cpp`) work on the Warzone maps.
Today it fails on both:
- mp_donetsk: "reflectionProbeCount 5620 does not match its records". The records are fine; the message comes from the
  `kMaxProbes = 4096` test in the same `if` (`stock_probes.cpp:32`, `:230-231`).
- mp_br_quarry: "has no reflectionProbeArrayImage_octahedron" (`stock_probes.cpp:236-237`). Warzone maps have no array
  image at all (section 1).

**Labels.** VERIFIED means read in decoded data, in shader bytecode or in machine code (cited). INFERRED means reasoned
from that evidence, or taken from the IW8 game-test decompile without a Replay check. Nothing was run in the game.

**Sources.**
- Data: the world cache exports of mp_donetsk (gfx_map JSON, 421,966,248 bytes; `gfx_map_trzone` of the main zone and
  97 cached streamed tiles) and mp_br_quarry. ACTS (`E:\mw124\atian-cod-tools\build\bin\Release\acts.exe`) decompressed
  the main Verdansk zone (`mp_donetsk.ff.dec`, 573,432,600 bytes, deleted after use) and all 749 tile zones
  `mp_donetsk_00001_tr` .. `_00749_tr` (one at a time, each `.dec` deleted at once). ACTS also exported shared probe
  images with the populated XPaks.
- Structs and zone stream order: `E:\mw124\x64-zt\src\games\iw8\structs.hpp:12316-12386, 12747-12762` ("S:n") and
  `zone\asset_streams.cpp:3746-3800, 4769-4784` ("Z:n").
- Replay CPU code: `E:\IW8\Builds\1.20-replay\game_dx12_ship_replay_dw.exe` (the recompiled Replay the owner runs) with
  its linker map, through `tools/dw_disasm.py` and capstone ("DW 0x..."). The IDA bridge was not reachable.
- GT = the IW8 game-test decompile `E:\mw124\mw164proxy\$dst\game-test-src-cleaned\code_source\src\gfx_d3d\r_reflection_probe.cpp`
  (not Replay; used for names and for logic not checked in DW, marked INFERRED).
- Shader: the world lit PS `01631f08...` of research 95 ("W:n", the `.asm` in `out/agent/shader_research/shw/...`).

## 0. Summary

- A Warzone map keeps **every** probe record in the main zone (Verdansk: 5,620 probes, 6,052 boxes), but the images in
  three places, chosen per probe by `probeImageIndirection` (VERIFIED on all 5,620):
  `zone = v >> 20`, `bucket = (v >> 16) & 0xF`, `element = v & 0xFFFF`.
  - `zone == 0xFFF`: a **shared** probe (1,849). Its image is the GfxImage `*reflection_probe_octahedron_<probe index>`,
    slot `element` of `reflectionProbeImages_octahedron`. It is a **streamed** image: the pixels are in the big XPaks,
    not in the `.ff` or the `.ff.dec`.
  - `zone == 0`: one of 3 probes of the main zone's own transient zone (the fallback and two map-sized boxes). The
    pixels are in the main `.ff.dec`.
  - otherwise: an **exclusive** probe (3,768 in 180 tiles) whose pixels are in tile `mp_donetsk_<zone %05d>_tr.ff`.
- Every image has the MP format: BC6H unsigned, 256 x 256, 6 levels, 87,360 bytes, octahedral in world space with the
  4-texel gutter, luminance-normalised (VERIFIED). Only the containers differ.
- A tile's `GfxReflectionProbeTransientData` holds (a) the list of box (instance) indexes that become active while the
  tile is loaded (its own probes' boxes plus boxes of shared probes) and (b) its exclusive images as texture arrays
  ("buckets") of 64/32/16/8/4/2/1 slices, the binary split of the exclusive count, in probe order (1 to 5 buckets on
  Verdansk, up to 79 probes in a tile; VERIFIED on every tile).
- The ACTS export of the bucket pixels is **wrong** (every bucket exported from the same address, first 416 bytes zero).
  Read the pixels from the tile's `.ff.dec`, where they follow the bucket records (section 2.3).
- Shading is the MP shader unchanged (research 95): boxes front to back with per-axis feather until the weight reaches
  1, box parallax, two levels. Warzone differs only in which boxes reach the per-view list: active tiles, the image
  streamed in, at most **96 per view**, sorted by `lodLevel` then priority, images copied into a 128-slot cache
  (section 3).
- Recipe (section 4): lift the 4,096 limit; accept "no array image" when the indirection is used; take zone-0 images from
  the main `.dec` and shared images through ACTS image export with XPaks (162 MB for Verdansk); take exclusive images in
  the existing per-tile decompress job; keep per-tile cache files; upload only a camera-local subset (fallback + nearest
  127 boxes, 11 MB of BC6H), never all 5,620 (491 MB, and over D3D11's 2,048-slice array limit).

## 1. The main zone (question 1)

### 1.1 Counts (VERIFIED, `out/agent/wzprobe/rpd_stats.py`)

`draw.reflectionProbeData` of the gfx_map export (key at byte 160,865,908; the object is 13.4 MB):

| Field | mp_donetsk | mp_br_quarry | ACTS form |
|---|---|---|---|
| reflectionProbeCount | 5,620 | 653 | |
| sharedReflectionProbeCount | 1,849 | 206 | |
| reflectionProbeImages / reflectionProbeArrayImage / ..._octahedron | null | null | |
| reflectionProbeImages_octahedron | 1,849 image names | 206 | `values` (names) |
| reflectionProbes (GfxReflectionProbe, 48 B, S:12316) | 5,620 | 653 | `values` |
| reflectionProbeInstanceCount / Instances (144 B, S:12348) | 6,052 | 733 | `values` |
| reflectionProbeSH (GfxSH3Vec 36 B, 4 per probe) | 22,480 | 2,612 | **hex `bytes`** on Verdansk, `values` on Quarry |
| probeRelightingCount, reflectionProbeGBufferImageCount | 0 | 0 | |

How 5,620 splits (VERIFIED):

| Kind | Probes | Boxes (instances) | Where the image is |
|---|---|---|---|
| shared (zone 0xFFF) | 1,849 | 1,963 | streamed GfxImage, XPak |
| zone 0 (main transient zone) | 3 (probes 5616-5618) | 3 (instances 0, 6049, 6050) | main `.ff.dec` |
| exclusive (zones 1-296) | 3,768 in 180 tiles (1 to 79 per tile, median 17) | 4,086 | tile `.ff.dec` |

Quarry: 206 shared, 3 in zone 0, 444 exclusive in 23 tiles (zones 10-212); the same rules hold on its records.

Rules checked on every Verdansk record:
- Shared probe `i` has `element` = its rank among shared probes (0..1848, in probe order), and slot `element` of
  `reflectionProbeImages_octahedron` is named `*reflection_probe_octahedron_<i>` (the name carries the probe index,
  not the slot: `_19, _21, _22, ...`).
- The exclusive probes of a zone, in probe order, fill bucket 0 element 0.., then bucket 1, ...; bucket sizes are the
  binary split of the zone's count, largest first (25 = 16 + 8 + 1). 0 of 181 zones break this. Histogram of bucket
  sizes: 1 x91, 2 x94, 4 x87, 8 x69, 16 x62, 32 x38, 64 x6.
- Each instance's `probeImageIndex` is the index of the probe that owns it (6,052 / 6,052), not an image slot. Every
  instance has exactly one owner. Instances per probe: 1 (5,289), 2 (240), 3 (83), 4 (6), 5 (2).
- Instance 0 is the fallback: probe 5617 (indirection 0x1, zone 0 bucket 0 element 1), half size 524,288, priority
  -FLT_MAX, flags 1. Quarry's fallback is probe 650, also 0x1.

Box fields on Verdansk: `lodLevel` 0 (5,562), 1 (488, all shared probes, mostly flags 1, median 705 x 430 x 798
units), 2 (2: the zone-0 boxes 6049/6050, 40,000-58,000 units); `flags` 0 (5,020) or 1 (1,032); priority -1 (3,523),
0 (1,175), 1 (963), 2 (328), a few fractional; feather (3,3,3) on 5,470, also (1,1,1), (0.1,3,0.1), (8,8,8), (1,0.1,3);
`expandProjectionNeg/Pos` non-zero on 108 boxes (up to 280); `probeRotation` identity on all. Lod-0 boxes are rooms:
median 254 x 137 x 254. MP maps have lod 0 only (shipment, backlot2, hideout).

### 1.2 Shared images: format and where the bytes are (VERIFIED)

- GfxImage header (ACTS structured export, and read in the main `.ff.dec` at `*reflection_probe_octahedron_19` =
  0x466676F): format 42 `GFX_PF_BC6H`, flags 0x20041 (0x40 = streamed), 256 x 256, depth 1, 1 element, 6 levels,
  totalSize 87,360, category IMG_CATEGORY_LIGHTMAP, `streamedPartCount` 1 with `levelCountAndSize` 0x155406
  (6 levels, 87,360 bytes) and a non-zero XPak key, pixels handle 0. In the `.dec` the next GfxImage follows the name
  directly: **no pixel bytes in the zone**.
- Without XPaks ACTS reports `package_key_missing` (exit 127). With every populated XPak (`--xpak` for each `.xpak`
  of at least 64 KiB, 28 files, as `MaterialCache` does) `-a image -n <names>` exports them: 460 names in one call in
  about 1.4 s, 0 failed. Output `assets/image/_encoded/<n>.<n>.dds` (names start with `*`, so the journal
  `assets.jsonl` maps name to file).
- DDS: 148-byte header (DX10, DXGI 95 BC6H_UF16, 256 x 256, 6 mips, array 1), then 87,360 bytes = level 0 (65,536),
  128 (16,384), 64 (4,096), 32 (1,024), 16 (256), 8 (64). This is exactly the per-probe block of `probes.iw8rprb`.
- The command line limit (32,767 characters) allows about 900 names of 33 characters per call; batches of 400-460 are
  safe. All 1,849 = 161.5 MB.

### 1.3 Zone-0 images in the main `.ff.dec` (VERIFIED)

The main zone's own `gfx_map_trzone` (`assets/gfx_map_trzone/mp_donetsk.0.asset.json`) has `reflectionProbes` =
{instances 3 [0, 6049, 6050], buckets 2 of [2, 1] slices, exclusive 3}. In the `.dec` its 24-byte header is at
0x8C109B5, the pixels at 0x8C10A03 .. 0x8C509C3 (262,080 bytes), laid out as in a tile (section 2.3).

### 1.4 Image convention (VERIFIED)

- Octahedral, world space, Z up, the probe's `angles` not applied, 4-texel gutter, as on MP (stock_probes.h:21-27):
  the gutter test (top rows r < 4 equal row 7 - r mirrored) passes on level 0 of every slice read (relative error
  0.001-0.028, BC6H noise) and on levels 1-2.
- Sun check against `sunDirection` (0.783, 0.364, 0.504): of 460 shared probes, 44 have their brightest texel within
  6 degrees of the sun and 16 at the sun mirrored below the horizon (a water glint, same azimuth). The zone-0 probes
  peak at (0.76, 0.37, -0.54), the glint. The 60 hits have 20 different yaws (45, 90, 135, 180, 270, 315, 360, odd
  ones like 65 and 175; `suncheck.py`), so the probe's angles are not applied to the image.
- Levels are prefiltered: level m+1 differs from a 2x box downsample of level m by 0.1-0.5 (relative), as on MP.
- Mean Rec. 709 luminance of level 0 (gutter skipped): shared 0.55-1.35, median 0.90 (460 probes); zone 0 and tile
  probes 0.70-1.20, median 0.93 (103 probes): luminance-normalised like MP, so the shader's relight by luma(Lr) applies.

## 2. The streamed tiles (question 2)

### 2.1 What GfxReflectionProbeTransientData holds (VERIFIED, all 749 tiles)

```
struct GfxReflectionProbeTransientData {          // S:12755, 24 bytes
    u16 reflectionProbeInstanceCount;              // box indexes this tile switches on
    u16 exclusiveProbeBucketCount;                 // popcount(exclusiveProbeCount)
    u32 exclusiveProbeCount;                       // probes whose image only this tile holds
    u16* reflectionProbeInstances;                 // ascending indexes into GfxWorld reflectionProbeInstances (6,052)
    GfxImageReflectionProbe* exclusiveProbeBuckets;
};
struct GfxImageReflectionProbe { u32 totalSize; u32 elementCount; u64 textureId; u8* pixels; };   // S:12747, 24 bytes
```

- 462 of the 749 tiles have the struct (tiles 1-584); 180 of them hold exclusive images; the other 282 only switch on
  boxes of shared probes (for example tile 66: 48 boxes, 0 buckets).
- The instance list of tile z = the boxes of tile z's own exclusive probes (each listed by its own tile only: 4,086 of
  4,086) plus boxes of shared probes. Each of the 1,963 shared-probe boxes is listed by 1 to 19 tiles (mean 2.5). No
  tile lists another tile's exclusive boxes. Every box outside zone 0 is listed by at least one tile. List sizes 1-139
  (median 7.5).
- `exclusiveProbeCount` and the bucket split equal what the main records give for that zone, on all 180 tiles.
- Tile name and zone index agree: `mp_donetsk_NNNNN_tr` has `transientZoneIndex` NNNNN (97 cached exports) and holds
  the probes whose indirection names zone NNNNN (all 180).
- The main zone also carries 281 stub transient zones (`mp_donetsk_00587_tr.1` .. `_00867_tr.281` in its own export)
  with `reflectionProbes` null.

### 2.2 How the game uses it (INFERRED from GT; the shapes are VERIFIED in DW)

- Load (`Load_ImageReflectionProbe`, DW 0x1419B48F0): each bucket becomes one texture array: width 0x100, height
  0x100, depth 1, `elementCount` slices (record +4), 6 levels, format 0x2A (BC6H), flags 0x20000 (VERIFIED DW).
- GT `R_ReflectionProbe_BuildInstanceListForView`: when a transient zone becomes visible, every instance in its list
  gets a reference count +1 and its bit in `reflectionProbeInstanceMask`; when it goes away, -1 and the bit clears at 0.
  Only masked instances are considered; the copy into the frame cache takes an exclusive image from
  `transientDrawContext.reflectionProbes[zone]->exclusiveProbeBuckets[bucket]`, slice `element`
  (GT `R_ReflectionProbe_CopyProbesToFrameCache`).
- So there is no spatial bucket structure: a bucket is only a texture array of a power-of-two size (INFERRED purpose:
  allocator-friendly slice counts).

### 2.3 Where a tile's images are (VERIFIED on tiles 1, 25, 26, 242 and the main zone)

In the tile's `.ff.dec` (ACTS `fastfile -d`, 0.2 s; median 1.5 MB, largest 40.7 MB), in stream order (Z:4769-4784):

```
+0                 GfxReflectionProbeTransientData (24 B): counts, then pointers -2 (follows), -2 (or 0 without buckets)
+24                u16 reflectionProbeInstances[instanceCount]
+24 + 2n           GfxImageReflectionProbe[bucketCount]: {totalSize = elementCount * 87360, elementCount, 0, -2}
+24 + 2n + 24b     bucket 0 pixels, bucket 1 pixels, ... contiguous, no padding (offsets are not even 8-aligned)
```

- Inside a bucket the layout is **level-major**, like the MP array image: level 0 of every slice (65,536 each), then
  level 1 of every slice, ... Level m of slice e is at `sum over l < m of (count * L[l]) + e * L[m]`, L = 65,536,
  16,384, 4,096, 1,024, 256, 64. Checked by the gutter test on levels 0-2 of every slice of tiles 1, 26, 242 and
  zone 0 (max error 0.028), and by the downsample test.
- Exclusive probe with (bucket b, element e) -> bucket b, slice e.
- Find the struct with an exact needle built from the tile's export: header (counts, -2, -2) + the u16 instance list
  (+ the bucket records). It matched exactly once in every tile (the all-tile scan used the counts from the main
  records and the same checks).

| Tile | `.ff.dec` bytes | Header at | Boxes (own + shared-probe) | Buckets | Exclusive | Pixel bytes |
|---|---|---|---|---|---|---|
| main zone (zone 0) | 573,432,600 | 0x8C109B5 | 3 | [2, 1] | 3 | 262,080 |
| mp_donetsk_00001_tr | 9,846,235 | 0x5CD52E | 35 (29 + 6) | [16, 8, 1] | 25 | 2,184,000 (0x5CD5D4..0x7E2914) |
| mp_donetsk_00026_tr | 29,092,006 | 0x1409060 | 96 | [64, 8, 2, 1] | 75 | 6,552,000 (0x1409198..0x1A48B58) |
| mp_donetsk_00242_tr | 26,228,639 | 0x1292254 | 91 | [64, 8, 4] | 76 | 6,639,360 (0x129236A..0x18E726A) |
| mp_donetsk_00066_tr | 9,398,313 | (export) | 48 (0 + 48) | none | 0 | 0 |

All 180 tiles together: 329,172,480 bytes of exclusive images. Smallest tiles with images hold one probe (tile 4:
1 box, 1 bucket, 87,360 bytes: a good test fixture).

### 2.4 The ACTS export of bucket pixels is unusable (VERIFIED symptom, INFERRED cause)

- Tile 1's export gives all three buckets the same `pixels.address` (0x22d11400150). Bucket 1's bytes equal bucket 0's
  first 698,880 bytes and bucket 2's equal its first 87,360. Compared with the `.dec`, the exported block is bucket 0's
  tail overwritten by bucket 1 and then bucket 2, and its first 416 bytes are zero. A single-bucket tile (25) also
  differs in its first 416 bytes.
- Cause (INFERRED from the game's loader as recorded in `atian-cod-tools/research/mw19/source_loaders.json`,
  `GfxReflectionProbeTransientData`): the pixels go to `AllocLoad_GfxImagePixelsData`, a temporary image buffer that
  `Load_ImageReflectionProbe` uploads at once and the next image reuses. An export after the load only sees what was
  last written there. Fixing ACTS would mean capturing the bytes during the load.

## 3. How the game picks probes for a pixel (question 3)

### 3.1 Per view, on the CPU

VERIFIED in DW:
- `R_ReflectionProbe_ProcessView` (0x1419B6F20): the list holds at most **0x60 = 96** instances when octahedral data is
  present (Warzone and MP), 0x55 = 85 for the old cube path (0x1419B6F99, 0x1419B7049).
- `R_ReflectionProbe_ProcessProbe` (0x1419B6CE0): skips a box with flags bit 0x80, reads `lodLevel` (+0x16), and calls
  `R_ReflectionProbe_IsProbeStreamedIn` (0x1419B6BB0). That returns true for any zone other than 0xFFF; for a shared
  probe it returns true when the texture is not a streamed backend id, or when `Stream_IsImageMipTailLoaded(image, 4)`
  says the image is in. A shared probe whose image is still streaming is left out of the view.
- `R_ReflectionProbe_CompareProbeInstancePriority` (0x1419B5890), the final sort: `levelOfDetail` ascending, then
  priority descending, then the world flag, then the index. So detail (lod 0) boxes blend first, lod-1 boxes after them,
  the lod-2 boxes and the fallback last.
- `R_ReflectionProbe_BuildInstanceDataFromInstanceList` (0x1419B4F70): flags bit 0 writes 0xFFFFFFFF as the probe
  position (0x1419B50AE-B5; the shader then reflects with plain R, **no parallax**); flags bit 2 marks a light-grid
  override box (0x1419B5057).

INFERRED from GT (same function names in DW):
- Candidates are the masked instances (2.2) that Umbra marks visible (`dpvs.reflectionProbeVisData`, one bit per
  instance; Verdansk `reflectionProbeVisDataCount` 190 = 6,052 / 32) or, without Umbra, that pass an OBB-frustum test.
- Slot 0 is always instance 0 (the fallback). On overflow the list keeps the higher lod and, within a lod, the nearer
  box (`R_ReflectionProbe_GetObbDepth`: centre along the camera axis minus the box's projected half extent), so far
  lod-0 boxes drop first.
- Images go into a 128-slot LRU frame cache (`UpdateCacheInternal<128,211>`, DW 0x1419B3F10): the shader's t124 array.
- GPU records: instance 68 bytes {probe position relative to the camera, u32 cache slot | half(relight scale) << 16,
  1/feather xyz, rotation quaternion, projection extents x/y/z = (-halfSize - expandNeg, halfSize + expandPos)}, OBB
  60 bytes {centre, x/y/z axis, halfSize}. Z-bins: 8,192 entries.

### 3.2 Per pixel, in the shader (VERIFIED, W:606-776)

- Cluster = depth slice (32 slices, a 4-segment piecewise scale `cb7[48]`, `cb7[49]`) x screen tile (`cb7[50].zw` =
  1 / tile size). A z-bin `t47[min(floor(depth) >> 5, 8191)]` (32-unit bins) packs the first and last probe index that
  can touch that depth; `t46[cluster * cb7[52].z + word]` is a bit mask of probes (32 per word). With 96 probes that is
  3 words (INFERRED).
- For each set bit, front to back in list order: `d = P - centre`; per axis `f_k = (halfSize_k - |d . axis_k|) * (1 /
  feather_k)`; `w = min(saturate(min_k f_k), remaining)`; skip when w = 0. Parallax against the expanded projection box
  unless position.x is 0xFFFFFFFF; optional quaternion rotation when w < 1; octahedral fold; two levels around 5 - 5 g';
  sample `t124[slot = word & 0xFFFF]`; `sum += c * luma(Lr) * w`; `remaining -= w`.
- So the Warzone pixel uses the **same box / priority / feather blend as MP** (research 95). The limits are the 96
  boxes per view (and therefore per cluster) and the 128 images; there is no per-pixel cap below that.

### 3.3 What the editor shader does differently today

`stockProbeLight` (render.cpp:277-329) and `setStockProbes` (render.cpp:1833-1973):
- sorts by priority only (render.cpp:1884); Warzone needs (lodLevel ascending, priority descending), fallback last;
- applies parallax to every box; boxes with flags bit 0 must use R (also on MP: shipment 4, hideout 11 boxes);
- uses one feather (the largest of three) on the smallest margin; the game is per axis (Verdansk has (0.1, 3, 0.1)
  boxes);
- ignores `expandProjectionNeg/Pos` (108 Verdansk boxes);
- uploads every probe into one array: impossible here (5,620 > 2,048 slices; 491 MB).

## 4. Editor recipe (question 4)

### 4.1 Records (`parseStockProbeRecords`)

1. `kMaxProbes` 4,096 -> 65,535 (instance and probe indexes are u16 in the data). Report "too many" separately from
   "does not match".
2. Array image optional: when `reflectionProbeArrayImage_octahedron` is null and `sharedReflectionProbeCount > 0` or
   any `probeImageIndirection != 0`, it is the streamed layout. Keep the shared names
   (`reflectionProbeImages_octahedron.values[k].name`) in the set.
3. `reflectionProbeSH`: accept hex `bytes` (36 bytes = 9 little-endian floats per GfxSH3Vec) as well as `values`.
4. Keep `probeImageIndirection` (already stored) and decode it as in section 0. Keep `lodLevel` and `flags` (already).
5. Memory: never decode all images (`decode = false`); 5,620 decoded probes would be 2.9 GB of halves. The renderer
   uploads BC6H directly.

### 4.2 Images

- **Zone 0** (always resident; includes the fallback): in the existing main-zone job, after ACTS `-d`. From
  `assets/gfx_map_trzone/<zone>.0.asset.json` take `reflectionProbes.values[0]` (counts, instance list, bucket
  `elementCount`s). Build the needle: `u16 instanceCount, u16 bucketCount, u32 exclusiveCount, u64 -2, u64 -2`, the u16
  instances, and one `{u32 count * 87360, u32 count, u64 0, u64 -2}` per bucket. Require exactly one hit. Read the
  buckets that follow and split them level-major (2.3).
- **Shared**: ACTS image export of the names, with every populated XPak, batches of at most about 450 names, into the
  job's scratch folder. Per DDS check DX10, DXGI 95, 256, 6 mips, 87,360 data bytes; copy the bytes after the 148-byte
  header. Verdansk: 1,849 images, 161.5 MB; Quarry: 206, 18.0 MB.
- **Exclusive**: in the per-tile decompress job (`WorldCache::queueTileLightmap`, world_cache.cpp:885, which already
  runs ACTS `-d` on a tile for its lightmap and light grid). Run it for a tile when its export's
  `reflectionProbes.exclusiveProbeCount > 0`, with the same needle from the tile export (never its pixel hex, 2.4). A
  tile whose export has no `reflectionProbes` or no buckets needs no decompress for probes.
- Which tiles a camera needs follows from the records alone: the zone of each nearby box's probe gives the tile name
  (`<map>_%05d_tr`), so tiles can be requested for their probes even before their geometry.

### 4.3 Cache layout (keeps `StockProbeSet` and v1)

Version 2 of `probes.iw8rprb` (v1 files stay readable and mean "K = P, identity"):

```
0   char[8] "IW8RPRB\1"
8   u32 version 2
12  u32 P            probe records (0 in a tile file)
16  u32 size 256
20  u32 mips 6
24  u32 B            boxes (0 in a tile file)
28  u32 format 42
32  u32 K            images in this file
36  u32 zone         0xFFFFFFFF = main file, else the tile's transient zone index
40  P x 208-byte probe records (v1; imageIndirection = the game's word), the box index lists, B x 140-byte boxes (v1)
    K x u32 probe index (ascending)
    tile file only: u32 N, N x u32 box index (the tile's reflectionProbeInstances, for the game's activation rule)
    K x 87,360 bytes BC6H, level 0 first per probe (v1 order)
```

- Main file `<zone>/probes.iw8rprb`: all records + K = shared + zone 0. Verdansk about 164 MB (records 2 MB), Quarry
  about 18.5 MB, MP maps unchanged in size.
- Tile file `<zone>/streamed/<tile>/probes.iw8rprb`: tile 1 2.2 MB, tile 26 6.6 MB, tile 242 6.6 MB; all 180 tiles
  329 MB, but only the tiles kept by the existing stream budget (`stock/streamCacheGB`) exist on disk. A "no probes"
  verdict file can stand for tiles with no buckets.
- Reading: `StockProbe::bc6h` stays empty until a file supplies the image; a merge adds a tile's K images by probe
  index. Disk is tight on C:: if 162 MB of shared images is too much, export them lazily per camera neighbourhood
  (one ACTS call for the missing names) into the same file (option, not measured).

### 4.4 Renderer: a camera-local subset

- Keep `setStockProbes` and its 128-box constant buffer (`kMaxProbeBoxes`, render.h:708); feed it a subset set built
  on the CPU whenever the camera moves 1,500 units (as the streamed tiles do) or a tile file arrives:
  1. the fallback box (instance 0);
  2. the nearest boxes by AABB distance to the camera, up to 127, whose image is loaded (shared: main file; exclusive:
     its tile file; zone 0: always);
  3. re-index their probes into at most 128 slices (11.2 MB BC6H) and their boxes into the subset.
- Sort the subset by (lodLevel ascending, priority descending), fallback last (no change on MP: lod 0 only).
- In `stockProbeLight`: no parallax for flags bit 0; feather per axis; projection extents with `expandProjection*`.
- Game-exact option: activate a box only while a cached tile that lists it is within the stream radius (2.1). The
  nearest-boxes rule is the simpler stand-in.

### 4.5 Budget (VERIFIED on the records, `budget.py`)

400 cameras at random probe origins of Verdansk (jittered 300 units), the fallback excluded:

| Boxes whose AABB is within | median | p90 | max | distinct probe images (median / p90 / max) |
|---|---|---|---|---|
| 0 units (containing the point) | 1 | 3 | 8 | 1 / 3 / 8 |
| 2,000 | 29 | 52 | 111 | 28 / 50 / 102 |
| 5,000 | 100 | 179 | 242 | 94 / 168 / 235 |
| 10,000 | 286 | 488 | 592 | 269 / 453 / 534 |
| 20,000 | 938 | 1,362 | 1,688 | 879 / 1,276 / 1,561 |

- The game shows at most 96 per view after frustum and Umbra culling. 127 nearest boxes cover everything within about
  2,000-3,000 units, which is where room-sized boxes (median 254 x 137 x 254) matter; the lod-1 and lod-2 boxes and the
  fallback cover the rest.
- At most 8 boxes contain one point: the editor's 4-candidate grid is enough for 90 %; 8 candidates would be exact.

### 4.6 Checks for an implementation

- Records: Verdansk 5,620 / 1,849 shared / 6,052 boxes / 22,480 SH (hex); zones: 3 in zone 0, 180 tiles; bucket split
  rule on all zones. Quarry 653 / 206 / 733.
- Fixture (tests use `tests/data`): a cut of tile 4's `.ff.dec` (one box, one bucket, 87,360 bytes) or of tile 1 around
  0x5CD52E plus its export header; expect the needle once, and the gutter test (rows 0-3 vs rows 7-0 mirrored, error <
  0.03) on every slice.
- Shared: `*reflection_probe_octahedron_19` decodes with the brightest texel at (0.76, 0.38, 0.52), 2 degrees from
  the sun (peak 6.9).
- Viewport: with Game Lighting on Verdansk, indoor rooms get their own probe (dark, warm) and open ground the lod-1 and
  zone-0 probes; compare with the owner's in-game shots (lit preview and game lighting).

## 5. Not established

- The original `game_dx12_ship_replay.exe` was not disassembled; DW (the recompiled Replay) was. Umbra visibility, the
  frustum test and the frame cache were read in GT only.
- Runtime values of `cb7[48..52]` (slice split, tile size, words per cluster).
- Flags bit 1 (0x2: skipped by GT's frustum path) and the high half of the slot word (relight scale; no relighting
  data on Warzone).
- Why the binary bucket split, and why tiles above 584 carry no probe data.
- Quarry's tile buckets were not decompressed (records only; INFERRED the same).
- Lazy shared-image export (4.3 option) was not timed; the full export was timed for 460 names only.
- In-game appearance (no run).

## 6. Reproduce

Scripts in `out/agent/wzprobe` (Python 3 + numpy; run with `python -I` unless the script imports `wzlib`). World cache
paths are arguments. Kept data: `donetsk_rpd.json` / `quarry_rpd.json` (the cut `reflectionProbeData` objects),
`alltiles.json` (per-tile header offset, instance list, buckets), `d1_b*.bin` (tile 1 buckets from the `.dec`),
`m0_b*.bin` (zone 0), `shared*.bin` + `img/` (three shared DDS). The decompressed zones were deleted.

| Script | What it does |
|---|---|
| `peek.py <file> <needle>` | windows around a byte string in a big file (mmap) |
| `cutobj.py <json> <key> <out>` | writes the balanced `{...}` after a key (used for `reflectionProbeData`) |
| `rpd_summary.py`, `rpd_stats.py <rpd.json>` | field shapes; indirection decode, bucket rule, instance ownership, box statistics |
| `tilescan.py <streamed dir>`, `tilecheck.py <rpd.json> <streamed dir>` | per cached tile export: counts; cross-check with the records |
| `tilebuckets.py <tile json> [prefix]` | the export's bucket records and pixel hex (shows the aliasing of 2.4) |
| `tilebuckets_dec.py <dec> <n> <b> <e> <prefix>` | exact-needle extraction of the buckets from a `.dec` |
| `scan_transient_probes.py <dec> [--check]` | signature scan without counts, plus the gutter test per slice (false positives where a tile has no buckets) |
| `alltiles.py <rpd.json> 1 749 alltiles.json`, `alltiles_report.py` | decompresses every tile in turn (into `dec/`, deleted at once) and summarises |
| `wzlib.py`, `layout_test.py`, `layout_test2.py` | BC6H decode via `tools/bc6dec.exe`, octahedral helpers; level-major vs slice-major tests |
| `imgstats.py <bucket.bin> <count> ...` | mean luminance, brightest texel, angle to the sun |
| `suncheck.py <rpd.json> <ACTS image export dir>` | brightest texel of exported shared probes against the sun, by probe yaw (1.4) |
| `budget.py <rpd.json>` | section 4.5 |
| `tools/bc6dec.c`, `tools/bc6scan.c` | built with `clang -O2` on `third_party/bcdec/bcdec.h`: decode one level; gutter error at every offset |

ACTS command lines (output roots kept short):
- Decompress: `acts --noUpdater fastfile -r mw19replay -g <replay exe> --oodle <oo2core dll> -d --noAssetDump -l -o <dir> <zone>.ff`.
  The main zone also writes `.fp.dec` and `.fp.patched.dec` (delete them; the patched one equals the `.ff.dec`).
- Shared images: `acts ... --xpak <each .xpak of 64 KiB or more> -a image -n "<name>,<name>,..." -l -o <dir> mp_donetsk.ff`;
  with `ACTS_REPLAY_STRUCTURED=1` it writes the GfxImage header JSON instead of the DDS.
- DW disassembly: `py -3 -I tools/dw_disasm.py <symbol>`; the function chunks past the map's symbol size were read with
  capstone over the same image.
