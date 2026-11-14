# 110. The Umbra tome of stock maps: layout, decode and an offline visibility query

Read-only research, 2026-11-13. This is plan item 2.1 (`docs/PLAN_REMAINING.md`, Milestone 2). It decodes the Umbra 3
occlusion tome that every Replay 1.20 map carries (`GfxWorld.umbraTomeData`). The core reader is
`src/core/umbra_tome.{h,cpp}` and the tests are `tests/umbra_tome_tests.cpp`. Research 96 (lines 111-183) has the
earlier header work and the converter's conservative tome.

**Labels.**
- **VERIFIED** means one of two things. Either it was read in decoded data and checked on every stock zone below
  (the checks are in `UmbraTome::validate()` and the tests), or it was read in machine code (cited).
- **INFERRED** means reasoned from that evidence, without a machine-code or in-game check.
- Nothing was run in the game.

**Sources.**
- Data: the stock tomes cut out of the world cache's `gfx_map` exports
  (`%LOCALAPPDATA%\IW8 Radiant Suite\IW8 Radiant Suite\world_cache\<zone>\assets\gfx_map\...json`). There are 15
  zones: mp_shipment, mp_hideout, mp_aniyah_tac, mp_backlot2, mp_rust, mp_village2, mp_emporium, mp_m_speedball,
  mp_m_fork, mp_m_king, mp_m_speed, mp_frontend, mp_br_tut, mp_br_quarry and mp_donetsk (Verdansk's main zone, a
  60 MB tome).
- Struct names: the IW7 `Umbra::ImpTome` / `ImpTile` in x64-zt `src/games/iw7/structs.hpp:10-118`. Every offset
  matches.
- Converter: `BuildConservativeUmbraTome` / `validateUmbraTome` (`C:\zsw-iw8r\scriptables\src\games\iw8\common\replay_render.cpp:1014-1086`,
  `:3548-3694`).
- Machine code: `game_dx12_ship_replay_dw.exe` (the recompiled Replay) with its linker map, read through
  `tools/dw_disasm.py` and capstone ("DW 0x..."). Most Umbra symbol names in that map are descriptive names, not
  Umbra's own. The IDA bridge was not reachable, and the REA tool had no Ghidra configured.

## 0. Summary

- **What the tome is.** An Umbra 3 `ImpTome` (version `0xD6000014`, `m_crc32` = `0xD15AB1ED` "disabled" on every
  stock map). It is a set of 16-byte-aligned sections addressed by absolute byte offsets from the header (VERIFIED).
- **The tile tree.** A k-d tree splits the tome box at explicit split values. Each tree node can carry one tile.
  - **Leaf tiles** (flags 5) sit at tree leaves. Each holds cells, portals, a **view tree** (a k-d tree that splits at
    midpoints, point to cell) and a cell index list.
  - **LOD tiles** (flags 4) sit at inner nodes and are coarser copies of the same space, without a view tree.
  - All VERIFIED: the tree boxes equal the tile boxes on every zone.
- **Cells and portals.**
  - A cell has a portal range, an object-list range, a cluster and 16-bit bounds in tile quanta.
  - A portal is 16 bytes: face, flags, target tile and cell, plane, a rectangle and its own growth. Gate portals carry
    a world box instead.
  - Internal portals pair up both ways. So do fine (leaf to leaf) and coarse (leaf / LOD to LOD) portals. On every zone
    every non-gate portal has its reverse (VERIFIED).
- **Clusters.** Clusters group the cells of a leaf tile. Cluster portals join them into a global graph (VERIFIED
  layout, all reciprocal). The camera query does not use them.
- **Objects.**
  - Objects have bounds, near / far squared distances and one or more user IDs. The class is in bits 28-30 of the
    user ID: 0 surface, 1 static model, 3 volumetric, 4 primary light, 5 probe, 6 decal collection. Gates are class 2
    but appear only in the gate table.
  - Cells list their objects in bit-packed lists. On the `mp_m_*` maps an entry is a run (first, count).
  - Every object is listed by at least one cell (VERIFIED).
- **The query.** `UmbraTome::queryPoint` / `queryView` find the camera's cell (tile tree, then view tree). They walk
  the leaf-tile portal graph, clipping the view to each portal's screen rectangle (portals grown as the game grows
  them, gates open), and report the visible cells, objects and user IDs by class.
  - An all-direction query takes about 2-25 ms on the MP maps and about 70 ms on Verdansk.
  - From cell centres on the MP maps it keeps 19-83 % of the objects (distance culling off).
  - It is a stand-in for Umbra's rasterizing portal culler. Section 12 lists how it differs.

## 1. Where the tome lives

`GfxWorld.umbraTomeSize` / `umbraTomeData` (hex `"bytes"` in the ACTS export, next to `numUmbraGates` and
`umbraGates`). `extractUmbraTomeFromGfxMapJson` streams the export in 8 MB chunks and keeps only these bytes. With a warm file cache
it takes 0.1 s for shipment's 138 MB export and 0.7 s for Verdansk's 420 MB, and it never builds a DOM. `GfxWorld.umbraGates`
(stride 32: `objID`, `bounds` midPoint/halfSize, `closeDistance`) lists the gates whose user IDs the tome's gate table
holds (section 10).

## 2. Header (`Umbra::ImpTome`, 0x170 bytes, VERIFIED)

| Off | Field | Stock values / meaning |
|---|---|---|
| 0x00 | m_versionMagic | `0xD6000014`. The game reads the low 16 bits (`0x14`) to pick the tile layout (DW 0x142000116: `cmp cx, 0x14`) |
| 0x04 | m_crc32 | `0xD15AB1ED` on all stock tomes. The converter writes CRC32C over [8, size) |
| 0x08 | m_size | the byte size (checked) |
| 0x0C | m_lodBaseDistance | 128 (shipment), 256 (hideout) |
| 0x10 | m_flags | 0 on stock. The game tests bit 0 (DW 0x14200006E) |
| 0x14 / 0x20 | m_treeMin / m_treeMax | the tile-tree root box |
| 0x2C | m_tileTree | SerializedTreeData: `nodeCount << 5 \| mapWidth`, tree, map (0), numSplitValues (= nodes), splitValues |
| 0x40 | m_numObjects | |
| 0x44 / 0x48 | m_objBounds / m_objDistances | 24 / 32 bytes per object (section 9) |
| 0x4C / 0x50 | m_userIDStarts / m_userIDs | starts: numObjects + 1 u32 (CSR) when objects have several IDs, else 0 |
| 0x54 | m_listWidths | bits 0-4 object index width, bits 5-9 run-length width (section 9) |
| 0x58 / 0x5C | m_objectLists / m_objectListSize | bit-packed entries / entry count |
| 0x60 / 0x64 | m_clusterLists / size | 0 on all stock tomes |
| 0x68..0x78 | gates | m_numGates, m_gateIndexMap, m_gateVertices, m_numGateVertices (a float count), m_gateIndices |
| 0x7C..0x84 | m_numClusters, m_clusters, m_clusterPortals | section 8 |
| 0x88 | m_cellStarts | numTiles + 1 u32: global index of each tile's first cell |
| 0x8C / 0x90 | m_numLeafTiles / m_numTiles | tile-tree leaves / nodes (nodes = 2 leaves - 1) |
| 0x94 / 0x98 | m_bitsPerSlotPath / m_slotPaths | section 11 |
| 0x9C | m_tileLodLevels | float per node: -1 (no tile), 1, 2, 4, 8 |
| 0xA0 | m_tiles | u32 per node: byte offset of its ImpTile, 0 = none |
| 0xA4..0xB8 | matching trees, tome collections | all 0 on stock (`m_numTomes` 0) |
| 0xBC | m_computationString | e.g. `SO 24.0 SH 4.000 BF 100 F 0` on shipment, often empty |
| 0x13C..0x148 | depthmaps | all 0 |
| 0x14C | m_tilePortalExpands | float per node, equal to each tile's `m_portalExpand` |
| 0x150 / 0x15C | m_boundsMin / m_boundsMax | data bounds (tile `m_tileBounds` are clipped to them) |
| 0x168 | m_clusterCoordScale | cluster coordinates: treeMin + q x scale |

## 3. Serialized k-d trees and their rank directory (VERIFIED)

The tile tree and every view tree share one format:
- **The node codes.** Two bits per node in breadth-first order: 3 = leaf, 0/1/2 = split axis x/y/z.
- **The children.** The children of the k-th inner node (k = the number of inner nodes before it) are nodes 2k + 1
  (low side) and 2k + 2 (high side). A leaf's index is node minus k.
- **The rank directory.** It follows the code words, so k is a lookup. It holds the inner-node counts:
  - a u32 per 65,536-node block after the first;
  - a u16 per 256-node superblock that does not start a block (relative to its block, padded to 4 bytes);
  - a u8 per code word that does not start a superblock (relative to its superblock, padded to 4 bytes).
  - The u32 level only appears in trees over 65,536 nodes (for example the view trees of mp_village2 tiles 83 and 85).

`validate()` recomputes every rank directory on every zone. The tile tree splits at `m_splitValues[node]` (VERIFIED: all
2,283 shipment tile boxes equal their tree boxes). The view trees have no split values and split the tile's quantized
box (`m_tileBounds`, max + 1) at midpoints. VERIFIED in data with `vtree.py` on shipment, hideout, aniyah_tac and
speedball: every view leaf lies inside or overlaps the cells it maps to, none is disjoint.

## 4. Tiles (`Umbra::ImpTile`, 0x60 bytes, VERIFIED)

| Off | Field | Meaning |
|---|---|---|
| 0x00 / 0x0C | m_treeMin / m_treeMax | the tile-tree box |
| 0x18 | m_viewTree | `nodes << 5 \| mapWidth`, tree (tile-relative), map (tile-relative), 0 split values |
| 0x2C | m_sizeAndFlags | `byteSize << 8 \| flags`: 5 leaf tile (bit 0 = view tree), 4 LOD tile. The converter writes 3 |
| 0x30 | m_portalExpand | world units (4 / 8 / 16 / ... on stock) |
| 0x34 | m_numCellsAndClusters | cells (low 16), clusters (high 16; 0 on LOD tiles) |
| 0x38 / 0x3C / 0x40 | m_cells / m_portals / m_cellIndices | tile-relative offsets |
| 0x44 / 0x48 | scale / invScale | world = treeMin + q x scale. The game quantizes the camera as (p - min) x invScale (DW 0x142000250) |
| 0x4C | m_tileBounds[6] | u16 minX, maxX, minY, maxY, minZ, maxZ: the data box in tile quanta (inclusive), indexed by portal face (DW 0x142003D51) |

**Leaf and LOD tiles.**
- Leaf tiles hold the finest cells. On a few tree leaves there is no tile at all: empty space Umbra did not compute
  (hideout 125, backlot2 159).
- LOD tiles cover their whole subtree with fewer, coarser cells.
- `m_tileLodLevels` is 1, 2, 4 or 8 on leaf tiles and 2, 4 or 8 on LOD tiles (Verdansk: up to 32 on both). It is
  never 1 on an LOD tile.

## 5. Cells (36 bytes, VERIFIED unless marked)

| Off | Field |
|---|---|
| 0x00 / 0x04 | portal start (tile-local, contiguous) / count |
| 0x08 / 0x0C | object-list start / entry count |
| 0x10 | cluster (global), `0xFFFFFFFF` on LOD-tile cells |
| 0x14 | flags: `0x80000000` \| a one-hot value (0x10..0x2000) \| bit 0 on leaf-tile cells. INFERRED: a voxel size class |
| 0x18 | bounds: three u32 of `(first << 16) \| second` over minX, minY, minZ, maxX, maxY, maxZ (inclusive tile quanta) |

The bound order was found by brute force: it is the only one of 720 orders consistent with all 20,070 shipment internal
portals. A cell is a union of voxels, so its box can overlap other cells' boxes. A cell's object list holds the objects
that touch it, with some slack: 70 % of the entries overlap the cell box, and most of the rest lie within one voxel.

## 6. Portals (16 bytes)

| Word | Bits | Meaning | Evidence |
|---|---|---|---|
| d0 | 29-31 | face 0-5: axis = face / 2, even towards -axis, odd towards +axis | VERIFIED (data: plane = source min on even faces, max + 1 on odd faces; DW 0x142003D15) |
| d0 | 28 | leads outside the tome (target `0x1FFFFFF`) | VERIFIED (DW 0x142003BF1) |
| d0 | 27 | gate: the game asks the gate-state vector (call 0x141FFCDF0) and skips the plane test | VERIFIED (DW 0x142003BEC, 0x142003849) |
| d0 | 26 | coarse: a link that has an LOD tile on at least one side | VERIFIED in data (fine portals only join leaf tiles) |
| d0 | 25 | internal: the game tests `(d0 & 0x12000000) == 0x02000000` | VERIFIED (DW 0x14200382C) |
| d0 | 0-24 | target tile (external and gate portals). On plain internal portals: the growth in tile quanta | VERIFIED (DW 0x142003C0F `and 0x1ffffff`, 0x142003DB4) |
| d1 | 16-31 | target cell, local to the target tile | VERIFIED |
| d1 | 0-15 | internal: the plane in tile quanta. External: the growth in tile quanta (the plane is `m_tileBounds[face]`) | VERIFIED (DW 0x142003D43-0x142003D58) |
| d2 / d3 | | rectangle `(min << 16) \| max` on axes (axis + 1) % 3 and (axis + 2) % 3 | VERIFIED (DW 0x142003D20: `(1 << axis) & 3`) |
| d3 (gates) | 12-31 | first float of a 6-float world box in `m_gateVertices` | VERIFIED (DW 0x142003E06; hideout's 1,928 floats fill the section exactly) |

**The game's portal box.** The game builds a thin box: the rectangle and the plane, minus the growth on the low side
and plus the growth + 1 on the high side, in tile quanta, then scale + tile min (DW 0x142003DB4-0x142003DF2; the
constant `(1, 1, 1, 1)` is at DW 0x14246B850). The growth
is one-hot (0x80 .. 0x4000 quanta). On shipment tile 2181 (scale 1/128) the internal growth 0x200 equals its 4-unit
`m_portalExpand`.

**Portal graphs.**
- Internal portals pair with a reverse portal (opposite face, same two cells).
- Fine portals (d0 bit 26 clear) join leaf tiles to leaf tiles, with a reverse.
- Coarse portals join leaf tiles to LOD tiles, LOD tiles to LOD tiles and LOD tiles to leaf tiles, all with a reverse.
- `validate()` checks all three kinds of pairing on all nine zones below. It also checks that no fine portal touches
  an LOD tile and no coarse portal joins two leaf tiles.
- So the leaf tiles form one complete graph: the accurate graph. The LOD tiles plus coarse links form the far graph.
- **How the game picks between them.** The game follows a portal only into a tile whose bit is set in a per-query
  tile set (DW 0x142003CD7-0x142003CF2: the target index, optionally remapped, is tested in a bit array). The rule that
  fills that set (distance, `m_tileLodLevels`, `m_lodBaseDistance`, `r_umbraAccurateOcclusionThreshold`) is not traced:
  INFERRED that leaf tiles are used near the camera and LOD tiles beyond the threshold.

## 7. Point to cell (VERIFIED)

1. Walk the tile tree from the root box (`p[axis] < split` goes low) to a leaf. That leaf's tile is the camera tile, or
   there is none.
2. Quantize: q = (p - tile min) x invScale.
3. Walk the view tree over `m_tileBounds` (max + 1) at midpoints.
4. The leaf's map entry (`mapWidth` bits) is a u32 index into the tile's cell index list.
5. Read local cells until `0xFFFFFFFF`. An empty run means solid voxels (shipment: 374 of 104,268 view leaves). Every
   stock run holds exactly one cell.

The conservative tome's `[0, 0xFFFFFFFF]` list is the same format.

## 8. Clusters (VERIFIED layout; their use is INFERRED)

- **Cluster.** 20 bytes: portal start, portal count (contiguous), then bounds packed like cell bounds in cluster
  coordinates (`treeMin + q x m_clusterCoordScale`).
- **Cluster portal.** 16 bytes in the tile-portal layout: d0 face, bit 27 gate, bits 0-15 target cluster; d1 bits
  0-15 the plane; d2/d3 the rectangle.
- **Which cells.** Every leaf-tile cell names a cluster, and a tile's clusters are consecutive (`m_numCellsAndClusters`
  high half). All non-gate cluster portals have their reverse. Shipment: 3,440 clusters, 41,314 portals.
- **Use.** INFERRED: the cluster graph serves the coarse queries (connectivity, distance). The camera query here does
  not use it.

## 9. Objects

- **m_objBounds** (24 B): min, max. Object 0 of shipment is reflection probe 0 and spans the whole tome.
- **m_objDistances** (32 B): a box, near distance squared, a box max, far distance squared. The far values are squares
  of the cull distances (2.704e9 = 52000², 1.44e6 = 1200², 3.6e7 = 6000²). `+inf` means never culled. VERIFIED values;
  the use (distance from the camera to the box, near <= d² < far) is INFERRED. 2026-11-13: applied that way, a view
  query on Shipment dropped the far container stacks (51 static model collections) that the uncut view shows through
  a gap, so the rule is incomplete (a LOD or field-of-view scale on the distance is likely). The editor's Umbra view
  does not apply the windows (occlusion only).
- **User IDs.** One per object, or CSR ranges through `m_userIDStarts`: hideout 15,890 IDs for 11,575 objects,
  Verdansk 164,299 for 85,477.
  - The class is in bits 28-30 and the index in bits 0-27. The consumer at 0x18CCA80 sets the dpvs bits by class
    (research 96, `replay_render.cpp`).
  - Shipment: 1,024 surfaces, 9,413 static models, 22 primary lights, 18 probes, 6 decal collections.
- **Object lists.** Bit-packed LSB first, entry width = `m_listWidths` bits 0-4 + bits 5-9.
  - When bits 5-9 are 0 (most maps), an entry is one object index.
  - On mp_m_speedball / mp_m_king / mp_m_speed (`m_listWidths` 0xAC / 0xAC / 0xAE, run width 5), an entry is
    `first | count << indexWidth`: objects [first, first + count), with count 1..31.
  - VERIFIED: the list section sizes match exactly (speedball: 15,255 x 17 bits), no run passes `m_numObjects`, and
    with 12-bit entries 1,101 entries pointed past the last object.
  - Lists are shared between cells (overlapping ranges).

## 10. Gates (VERIFIED layout; the state handling is INFERRED)

- **m_gateIndexMap.** `m_numGates` user IDs of class 2, equal to `GfxWorld.umbraGates[i].objID` (hideout 21:
  `0x20000D28` = 536874280, ...).
- **m_gateIndices.** A permutation of 0..n-1.
- **m_gateVertices.** `m_numGateVertices` floats. Gate portals point at 6-float world boxes there. Every gate portal
  of the nine zones points inside the table. On hideout, the first box in the table lies on gate 0's bounds.
- **In the game.** The game passes gate states as an `Umbra::GateStateVector`. Script sets them with
  `SetUmbraPortalState(<gate name>, 0/1)` (`GScr_SetUmbraPortalState` DW 0x141267530, `Com_GetUmbraGateIndex`), and
  the states are sent in snapshots (`MSG_ReadDeltaUmbraGateState`).
- **Zones with gates.** hideout 21, backlot2 1, Verdansk 10.

## 11. Other tables (VERIFIED)

- **m_cellStarts.** `cellStarts[t]` = global index of tile t's first cell, plus a final total.
- **m_slotPaths.** `m_bitsPerSlotPath` bits per tree node (shipment 38, hideout 40 = tree depth + 1). It is the node's
  path stored MSB-aligned: reading from bit B - 1 down, the steps from the deepest one up, with a set bit for a low
  (first) child, then a sentinel 1. All nodes are checked on every zone.
- **m_tileLodLevels / m_tilePortalExpands.** One float per node.

## 12. How the game queries it, and what the editor query does

**The game.**
- Load: `Load_UmbraTome` (DW 0x141923C20).
- Static camera query: the map has `R_Umbra_QueryStaticVisibilityCmd` (DW 0x141925D20),
  `Umbra::Query::queryPortalVisibility` (DW 0x141FF4860) and the `PortalCuller` (`enterTile` DW 0x142000020, the portal
  loop at DW 0x142003670..). INFERRED: they call each other in that order (only `enterTile` and the portal loop were
  read).
- The culler rasterizes portals into coverage / depth buffers (`OcclusionBuffer`, `DepthBuffer`). Its output is object
  user IDs, which the consumer at 0x18CCA80 turns into surface / smodel / light / probe / decal visibility.
- **Dvars** (names VERIFIED in the exe strings, defaults not read):
  - `r_umbra`
  - `r_umbraAccurateOcclusionThreshold`: "query requires base level geo - forcing accurateOcclusionThreshold to
    FLT_MAX"
  - `r_umbraDistance`: the camera uncertainty, which drives the query's distance parameter
  - `r_umbraMinObjectContribution`: the contribution cull that once hid converted props (research 96)
  - `r_umbraQueryParts`
  - `r_umbraExclusive`
  - debug views: `r_umbraShowPortals`, `r_umbraShowViewCells`, `r_umbraShowVisibleVolume`
- The GPU passes cull again afterwards (research 96).

**The editor (`UmbraTome::queryPoint` / `queryView`).**
1. **Start cells.** Point to start cells (section 7). In solid voxels it falls back to the tile cells whose box is
   within `cameraRadius`.
2. **Traversal.** A rectangle portal walk over the leaf-tile graph only. This is the accurate mode the game itself
   forces when it needs base-level geometry.
   - It passes a portal when the camera is on its source side, or within `cameraRadius` of it.
   - The portal's grown thin box is projected with near-plane clipping and intersected with the current screen
     rectangle.
   - Rectangles snap outward to 1/1024, and up to 4 are kept per cell and face before merging, so the walk ends.
   - Gates count as open and are clipped to their boxes.
3. **Objects.** An object is visible when a visible cell lists it, its box overlaps one of that cell's rectangles, and
   (optionally) it is inside its near / far distance.
4. **Faces.** `queryPoint` runs six 90-degree faces.

**Differences from the game.**
- A rectangle can only show more than Umbra's rasterized coverage.
- No LOD tiles: beyond the accurate threshold the game uses coarser cells, which show more, so the editor may hide some
  distant objects the game draws.
- No object-contribution cull (the game may hide small far objects).
- No gate states (all open).
- No camera-uncertainty distance.

A ray-cast conservativeness check (plan 2.4) is still to be written.

## 13. Per-zone numbers (all checks pass; `validate()` and `tests/umbra_tome_tests.cpp`)

| Zone | Bytes | CRC32C | Objects / IDs | Tiles (leaf + LOD) | Cells | Portals int / fine / coarse / gate / out | Clusters | Visible* |
|---|---|---|---|---|---|---|---|---|
| mp_shipment | 5,614,672 | 810A311F | 10,483 / 10,483 | 4,089 (2,045 + 238) | 10,853 | 75,716 / 52,004 / 70,262 / 0 / 18 | 3,440 | 31 % |
| mp_hideout | 2,806,416 | 7C93B531 | 11,575 / 15,890 | 2,397 (1,074 + 113) | 5,024 | 17,476 / 19,940 / 18,910 / 260 / 62 | 2,638 | 26 % |
| mp_aniyah_tac | 11,530,368 | F3D95891 | 51,693 / 51,693 | 6,237 (3,081 + 395) | 16,082 | 78,986 / 79,806 / 114,456 / 0 / 80 | 6,769 | 29 % |
| mp_backlot2 | 8,441,152 | 1EDFBAE8 | 21,418 / 21,418 | 8,021 (3,852 + 473) | 15,858 | 75,068 / 77,182 / 94,154 / 38 / 65 | 6,964 | 19 % |
| mp_rust | 1,206,208 | 9DD48B7C | 7,066 / 7,607 | 957 (479 + 27) | 1,397 | 2,900 / 7,948 / 7,372 / 0 / 168 | 913 | 46 % |
| mp_village2 | 2,131,040 | 6F75D225 | 15,118 / 17,246 | 753 (377 + 24) | 1,389 | 3,036 / 9,292 / 3,480 / 0 / 57 | 899 | 83 % |
| mp_emporium | 4,295,056 | 8AA2A450 | 17,334 / 21,143 | 3,545 (1,713 + 210) | 7,319 | 31,054 / 31,450 / 38,880 / 0 / 42 | 3,290 | 36 % |
| mp_m_speedball | 1,617,776 | 63B9E167 | 3,528 / 3,852 | 1,645 (823 + 90) | 2,139 | 5,732 / 14,276 / 22,036 / 0 / 18 | 1,338 | 75 % |
| mp_donetsk | 60,383,968 | E91F7509 | 85,477 / 164,299 | 20,627 (10,312 + 1,459) | 54,206 | 253,924 / 247,602 / 417,798 / 24 / 18 | 24,465 | 28 % |

\* The mean share of objects that `queryPoint` reports from 5-6 cell centres (distance culling off). The centres are
often in open air, so this is not a gameplay figure.

## 14. What is left for 2.2-2.4

- **2.2 (the view).**
  - What is ready: `cellBounds`, `portalQuad`, `gateBounds` and `cellsAt` for the overlay, and
    `queryView(pos, forward, up, tanX, tanY)` for "cull by tome" with per-class counts. One view is a few ms on MP maps
    and tens of ms on Verdansk, so run it on camera stops or in a worker, not on the paint thread.
  - To map user IDs to editor objects: class 0 is a GfxWorld surface index, class 1 a smodel index, class 4 a primary
    light, class 5 a probe instance, class 6 a decal collection.
- **2.3 (local tile replacement).**
  - Needed: an encoder (the reverse of this reader), and replacing a leaf tile's cells and portals while keeping the
    reverse portals of its neighbours valid. This means both the fine and the coarse links, and the LOD tiles above it,
    which cover the same space.
  - Also needed: the remaining INFERRED fields:
    - the cell one-hot flags;
    - the cluster rebuild;
    - the rule that fills the per-query tile set (to know when the game reads the LOD tiles over an edit).
- **2.4 (a real bake).** The decoded layout is what an encoder must write: trees with rank directories, cells, grown
  portals and their reverses, LOD tiles (or leaf tiles only plus coarse links), clusters and object lists. The
  conservativeness test can use `queryPoint` against ray casts.

## Reproduce

1. **Cut the tomes.** `python out/agent/umbra110/extract.py mp_shipment ...` writes `out/agent/umbra110/tomes/<zone>.tome`
   (read-only on the world cache). The C++ equivalent is `extractUmbraTomeFromGfxMapJson`.
2. **Survey scripts** (Python, all read-only, in `out/agent/umbra110`):
   - `hdr.py <zone>`: header fields;
   - `tiles.py <zone> [tile...]`: tile kinds, LOD levels and one tile's raw fields;
   - `dump.py <zone> <off> <len> [w|f|h|b]`: raw words;
   - `cellperm.py`: the brute force over the cell bound order;
   - `pstats.py` / `graph.py`: portal bit statistics and reverse-portal checks;
   - `vtree.py`: view tree against cell boxes;
   - `umbra.py`: the tree / rank prototype.
3. **Machine code.**
   - `py -3 -I tools/dw_disasm.py "Umbra::PortalCuller::enterTile"`;
   - `python out/agent/umbra110/scan.py 141FEE000 142010000 "0x2000000\b" "shr e.., 0x1d"` finds the portal-word
     tests;
   - `python out/agent/umbra110/scan.py 1420037A0 142003F80 "."` prints the portal loop.
4. **Tests.** `xmake build -y iw8r_tests`, then `bin/release/iw8r_tests.exe` runs these tests:
   - `umbra_crc32c`;
   - `umbra_conservative_tome` (a port of the converter's tome);
   - `umbra_two_tile_tome` (a synthetic two-tile tome with a door portal, a sealed cell, an external portal and
     corruption cases);
   - `umbra_stock_mp_tomes` (eight MP zones);
   - `umbra_stock_verdansk`.

   The stock tests skip when the world cache export is missing.
