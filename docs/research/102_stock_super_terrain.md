# 102. Stock super-terrain (Replay 1.20): exporting `stterrain` and rebuilding the Warzone terrain mesh

**Why.** Research 100 found that Verdansk (`mp_donetsk`) and Quarry (`mp_br_quarry`) keep their ground in a
`stterrain` asset, which ACTS could not export (64 `pointer_extent_required` issues on the terrain node vertex data).
The editor needs that asset for the terrain mesh, the height maps and the GPU clutter (research 100 section 1).

**Verdict.**

1. **Fixed in the ACTS fork** (worktree `E:\mw124\acts-stt`, branch `stterrain-extents`, commit `8b5ee11d`, not
   merged and not deployed). Both zones now export `stterrain` with status `ok`, 0 issues, 0 failed, 0 unavailable.
2. Cause: `Replay::StMesh` embeds `Replay::StVertexData0` and `Replay::StMeshLayerMaskData` by their `Replay::` names.
   The rule lookup tries the native name and the name the record was reached by, and both are the `Replay::` name. So
   the game-test rules (keyed `StVertexData0` / `StMeshLayerMaskData`) never matched. Their counts were also wrong
   for Replay (both `4 * vertexCount`).
3. The terrain is **27 square surfaces**, each a quadtree of nodes. Each node holds an adaptive triangle mesh
   (about 1,000 vertices and 2,000 triangles). Only the coarse levels are in the zone. The fine levels are streamed
   from `pak_magma1.xpak`, one stream key per node. **Heights come from the per-surface height map** (an R16 image
   of 257² to 2049² texels), except on the root nodes, which also store a height per vertex.
4. Rebuilding a mesh is simple (section 4). The full-resolution ground is the height map grid (32 or 16 inch texels).
   The stock adaptive meshes are the node meshes with z taken from the height map.

**Labels.** VERIFIED = read in the Replay loader disassembly or checked against the exported data (numbers cited).
INFERRED = from names, value patterns or data statistics, not from a shader or a loader.

## 1. The export fix (VERIFIED)

Replay 1.20 loaders, disassembled from `game_dx12_ship_replay.exe` (read only):

| Loader | RVA | Extent of `data` (bytes) |
|---|---|---|
| `Load_StVertexData0` | `0xE1EB60` | `vertexCount (StMesh+0x120) * ((flags (StMesh+0x126) & 4) ? 4 : 8)` (`test byte [rcx+126h], bpl; cmovne eax, ebp; imul rdx, rax`) |
| `Load_StMeshLayerMaskData` | `0xE1E390` | `((flags & 0x10) ? triCount (+0x122) - cutoutTriCount (+0x124) : 0) * 4` |

x64-zt's generated reader (`src/games/iw8/zone/asset_streams.cpp` `stream_stvertexdata0` /
`stream_stmeshlayermaskdata`, from `tools/iw8_format.json`) says the same.

The change, in the ACTS worktree:

- `scripts/mw19/replay_rules_x64zt.py`: two Replay-keyed pointer rules, `Replay::StVertexData0.data` and
  `Replay::StMeshLayerMaskData.data`. Their counts use the expression ops the schema already has (`mul`, `select`,
  `and`, `sub`) over the enclosing `StMesh` context (`flags.packed`, `vertexCount`, `triCount`, `cutoutTriCount`).
- `config/data/mw19/schema.json`: regenerated with `scripts/mw19/generate_schema.py`. The only change is the two rules
  (+83 lines). Before the change, regenerating gave a byte-identical schema.

It is a data-only fix: the old `acts.exe` with the new `schema.json` writes the same `stterrain` JSON as the new
build (compared with addresses ignored).

### Results

| Zone | Before | After |
|---|---|---|
| `mp_donetsk.ff` `-a stterrain` | unavailable, 64 `pointer_extent_required` | ok, 0 issues; JSON 93,464,742 bytes, 4,531,205 nodes; about 10 s |
| `mp_br_quarry.ff` `-a stterrain` | unavailable | ok, 0 issues; JSON 93,528,079 bytes, 4,531,216 nodes |

Regressions (new build compared with the deployed one):
- `test_fastfile.py` passes with both exes.
- `verify_fastfile_bindings.py` passes (111 bindings).
- `test_schema.ps1` stops on the known stale check "Column-major CSV addressing" with both the old and the new schema.
- A full `mp_m_speedball` export gives 3,685 assets with 0 failed on both exes. All rows have the same status, sizes,
  node counts and issues. All 1,412 CRC differences are heap pointer values (`address` fields and 8-byte pointer
  words inside raw `bytes`), which also change between two runs of the same exe.

## 2. Asset layout (VERIFIED from the Verdansk export unless marked)

```
StTerrain: surfaceCount 27, surfaces[27] (StDiskTerrainSurface, 496 bytes each in Replay)
           maps[4] {mapResolution, paddedResolution}: 2049/2052, 1025/1028, 513/516, 257/260
           flattenedImages[668] (layer mask / material images), clutterSamplePoints (4096 float2)
StDiskTerrainSurface: objToWld {quat (all identity), origin}, halfWldSize, surfaceMapSetIndex -> maps[],
           surfaceMapWorldSize (inches per height texel), heightOffset, heightScale, tileVertXCount 33,
           highLODIndices (6144 u16 = the 33 x 33 grid's 2048 triangles), grid, layerMaterials, clutterLayers,
           clutterSampleBitmask (streamed, see 2.3), heightMap / cutoutMap / combinedAlbedoMap / combinedNogMap
StTerrainGrid: numLevels, levels[] (finest first). Level L is a width x height array of StTerrainNode, row-major
           (index = yIndex * width + xIndex). The last level is a single root node.
StTerrainNode: mesh (StMesh), wldBounds, compressBase, compressScale, layerMask (u32), xIndex, yIndex
StMesh:    vd0.data, layerMask.data, indices, streamKey, vertexCount, triCount, cutoutTriCount, flags
```

### 2.1 Surfaces

The 27 surfaces are squares of 8,192, 16,384, 32,768 or 65,536 inches. Together they cover x -65,536..98,304 and
y -53,248..77,824. They overlap: the four 65,536 periphery surfaces (s0, s2, s3, and s9/s10 east) surround the
smaller detailed ones, and s8 (offset origin) overlaps several. INFERRED: the overlap is resolved by cut-out triangles
and the `cutoutMap` (section 3.3).

Root heights per surface (stored z, world inches): city surfaces have a median of about -300 (origin z is -320, near
sea level), with ranges like s1 -364..392 and s11 -540..403. The periphery mountains reach s0 -967..14,969 and
s3 -876..8,746. These are plausible for Verdansk.

### 2.2 Node flags (`StMeshFlags.packed`)

Bits, from the game-test bitfield: 1 `clippedOut`, 2 `renderable`, 4 `gpuGenerate`, 8 `vertPos3d`, 16 `layerMask`.

| Flags seen | Where | Vertex data |
|---|---|---|
| 26 (renderable, vertPos3d, layerMask) | every root node | resident, 8 bytes/vertex with a stored height |
| 18, 2 | the next 3 to 4 coarse levels | resident, 8 bytes/vertex |
| 20 (gpuGenerate, layerMask) | finest level, most nodes | streamed, 4 bytes/vertex, 33 x 33 grid, no own indices |
| 16, 0, 1 | finer levels | streamed, 8 bytes/vertex with own indices (0/1: no layer mask; 1 = clipped out) |

Verdansk: **611 resident nodes** (629,275 vertices, 992,895 triangles; 5,034,200 vertex bytes, 5,957,370 index
bytes, 3,461,200 layer-mask bytes) and **31,183 streamed nodes**. All resident indices are below `vertexCount`, and
no resident triangle is degenerate. Quarry has the same structure (611 / 31,183) with its own names
(`mp_br_quarry_height_<s>`, ...).

### 2.3 Streamed data (VERIFIED on 4 nodes)

- Node mesh: stream key `mp_donetsk_s<surface>__terrain_l<level, 2 digits>_n<node index, 6 digits>`, exported with
  `acts ... -a streamkey -n <name> --xpak ...` (only the last `-n` counts, so use one run per name).
  The payload is three parts, each padded to 16 bytes, in this order: **indices** (`triCount * 3` u16; absent when
  flags & 4), **vertex data** (`vertexCount * 4 or 8`), **layer mask** (`(triCount - cutoutTriCount) * 4`; absent
  when !(flags & 16)). Checked sizes: l00_n000000 (flags 20) 12,560 bytes; l00_n000034 (16) 29,200; l01_n000000 (16)
  28,848; l00_n000035 (0) 9,920.
- Clutter bitmask: stream key `mp_donetsk_s<surface>__clutter_bitmask` holds `wordCount` u32 words (s1: 650,752 bytes =
  162,688 words). This is the `bitmask` that research 100's `parseStockClutterTerrain` needs. The resident pointer is
  null.

## 3. Vertex formats

Everything below is surface-local. World = local + `objToWld.origin`; the quaternions are all identity.

### 3.1 8-byte vertices (flags & 4 == 0)

| Bytes | Field |
|---|---|
| 0-1, 2-3 | u16 x, y: `local = compressBase + u / 65535 * compressScale` (VERIFIED: matches `wldBounds` exactly). `compressScale` is twice the node size, and a node uses half the u16 range. |
| 4-5, 6-7 when flags & 8 (root) | u16 z: `local z = heightOffset + z / 65535 * heightScale` (VERIFIED: within 2 inches median of the height map, section 3.4); u8 x, u8 y octahedral normal (INFERRED: `n = (b/255*2-1)`, z = `1-|x|-|y|`; median dot 0.98 with the height-map normal) |
| 4-5, 6-7 otherwise | u16 x, y **morph target** (INFERRED: values snap to the parent level's vertices, for example grid x 1 -> 0, 3 -> 4). The vertex height is **not stored**: sample the height map. |

The u16 uses round-to-nearest UNORM16: 0.5 encodes as 32,768, so it decodes 1/65535 of the scale too far, at most
2 inches on a root node and 0.25 inches at level 3. Snap to the node edge if seams matter.

### 3.2 4-byte vertices (flags & 4, `gpuGenerate`; streamed)

The vertex position is implicit: vertex `k` is grid point `(k % 33, k / 33)` of a 33 x 33 grid over the node's
`wldBounds` (32-inch spacing at level 0 on a 65,536 surface). Triangles are the surface's `highLODIndices`. The 4
bytes are u16 x, y, INFERRED to be the morph target in the same encoding as 3.1. Evidence: the bottom row and the
right column hold exact even grid points, and the interior values are the parent's irregular vertices. The heights
come from the height map.

### 3.3 Triangles and layer masks

- `indices` are `triCount * 3` u16. **Winding is clockwise seen from +Z** (all 30,795 triangles of s1 level 3 have
  a downward `(b-a)x(c-a)`). Flip them for a counter-clockwise front face.
- The first `triCount - cutoutTriCount` triangles have one u32 each in `layerMask.data`. INFERRED: it is a bitmask of
  the surface's material layers (`layerMaterials[]`, up to 29) that touch the triangle. It is mostly a subset of the
  node's `layerMask`: 61 of 865,300 resident triangles are not.
- The last `cutoutTriCount` triangles are INFERRED to be the alpha-tested ones, cut by `cutoutMap`. They have no
  layer mask entry, and they lie inside another surface's square more often than the others do (s2 root: 85% against
  29%). An opaque preview can draw only the first part.

### 3.4 Height map (VERIFIED)

- `heightMap` is the GfxImage `mp_donetsk_height_<s>`, exported with `-a image -n mp_donetsk_height_<s>`. It is a
  DDS in DX10 format 56 (R16_UNORM), `paddedResolution`² texels. The first `mapResolution`² texels are used, so
  ignore the padding on the right and bottom.
- `local z = heightOffset + H[ty][tx] / 65535 * heightScale`, with `tx = (local x + halfWldSize) / surfaceMapWorldSize`
  and `ty = (local y + halfWldSize) / surfaceMapWorldSize`. Texel (0, 0) is the surface's -x/-y corner, with no v
  flip, sampled bilinearly. Checked against the stored root heights: median error 2.0 / 0.3 / 0.7 / 0.3 inches on
  s0 (2049²), s1 (1025²), s8 (1025²) and s25 (257²). Other row orders and offsets do worse (2.0 against 4.4 or more
  on s0).
- `mapResolution - 1` texels span the surface: 64 x 1024 = 65,536 / 32 and 1024 x 16 = 16,384 / 16.
- `out/agent/stt/s1_heightmap_vs_level3.png`: hillshade of `mp_donetsk_height_1`, north up, with the s1 level-3 node
  triangles drawn over it. Mesh density follows the relief in the hillshade, which confirms the xy
  decode and the map orientation together.

## 4. How to rebuild a mesh

1. Export `stterrain` (`-a stterrain`) and the `mp_<map>_height_<s>` images (`-a image -n ...`), both with the `--xpak`
   list. ACTS hex-encodes the arrays (`encoding: hex-little-endian`, `bytes`).
2. **Full-resolution ground (simplest):** for each surface, make the `mapResolution`² grid of the height map, place
   texel `(i, j)` at `origin.xy - halfWldSize + (i, j) * surfaceMapWorldSize`, and set z from 3.4. Use two triangles
   per cell. Verdansk's 27 surfaces add up to about 33 million texels. Use a coarser stride or the quadtree levels
   for a viewport.
3. **Stock adaptive meshes:** pick a level and decode each node's vertices (3.1 or 3.2), taking z from the height map
   unless flags & 8. Use the node's `indices` (or `highLODIndices` for flags & 4), keep the first
   `triCount - cutoutTriCount` triangles for an opaque pass, and reverse the winding if needed. The resident coarse
   levels are ready in the zone. Finer levels need one stream-key read per node (2.3).
4. Surfaces overlap. Without the cutout pass, draw the smaller surfaces on top, or clip the large periphery surfaces to
   their uncovered area. The exact resolution is INFERRED (cutout triangles plus `cutoutMap`); it is not traced.

Reference code: `out/agent/stt/py/stterrain_mesh.py <stterrain.json> <surface> <height.dds> [--obj f] [--level N]`.
It checks the height placement against the root heights, then writes either the full height grid
(s25: 66,049 vertices, 131,072 triangles, z 1,253..1,380) or one level's resident node meshes (s1 level 3: 16,903
vertices, 30,795 triangles). `py/summ.py` prints the per-surface summary.

## 5. Open items

- The morph-target reading of bytes 4-7 (8-byte format) and of the 4-byte format, and the octahedral normal, are
  INFERRED. The terrain vertex shader was not disassembled.
- How surfaces overlap at run time (cutout triangles, `cutoutMap`, draw order).
- The layer-mask semantics and how `flattenedImages` and `layerMaskMapIdxs` give per-texel layer weights (texturing).
- The editor side: `world_cache.cpp` does not request `stterrain` yet. Wiring it in, and the streamed node meshes, are
  for the main session.
