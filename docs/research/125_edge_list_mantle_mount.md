# 125. The edge list: what mantling and weapon mounting need from a map

**Why.** The field audit of research 124 found that a built map's `edgelist` asset (MapEdgeList) holds 8 edges where
a stock map holds 578 to 862, one octree where stock has three, no adjacency tables, and query types 4 where stock has
7. Research 45 section 3.3 assumed "Replay mantles are computed from collision at runtime" and left open whether a
mantle needs edge data. This note settles that from the Replay exe for mantling, weapon mounting and ladders, gives
the record layouts, and says what a converter would have to write.

**Labels.** VERIFIED = read in the Replay exe (`game_dx12_ship_replay.exe`, IDA, address given) or in a file (path and
line). INFERRED = follows from verified facts. Nothing was run in the game. Function names come partly from an
automatic naming pass; the claims rest on the code, not the names.

Sources: the IDA database; the stock ACTS exports
`C:\iwp\stk\mw19replay\{mp_shipment,mp_m_speedball,mp_rust}\assets\edgelist\mapedgelist_00000.4.asset.json` and
`...\assets\map_ents\maps\mp\<zone>.d3dbsp.5.asset.json`; `C:\zsw-iw8r\scriptables\src\games\iw8\structs.hpp`;
`C:\zsw-iw8r\scriptables\src\games\iw8\zone\map_zone.h`.

## 1. The answer in short

| Move | Needs static edges? | Proof |
|---|---|---|
| Mantle | **No**, as long as `MapEnts.edgeListUsedQueryTypes` bit 1 (value 2) is clear. Then the game finds the ledge with a collision trace. With bit 1 set it uses edges only, with no trace fallback. | VERIFIED: `Mantle_CanMantle` 0x141103c90 tests `*(byte*)(cm_mapEnts + 0x168) & 2`. Clear: `Mantle_TraceLedgeFace` 0x141105f50 (a `BgTrace::LegacyTraceHandler` trace). Set: `Mantle_FindMantleEdge` 0x141105190 (edge query only); 0 candidates returns false. |
| Weapon mount | **Yes.** Mount edges come only from the edge list (bucket 0, MOUNT). No trace path was found. A built map today has no mount edges, so mounting cannot start. | VERIFIED: `PM_ContextMount_FindMountEdge_Initial` 0x140cae650 and `BG_ContextMount_ProbeEdgeMountTarget` 0x140caec60 query bucket 0 and return false with 0 candidates. The built list has `staticQueryTypes` = `dynamicQueryTypes` = 4 (bucket 2 only; `map_zone.h:751-752`). |
| Ladder (centre line) | Yes, bucket 2. The converter writes these already. | VERIFIED: `map_zone.h:677-799`; stock mp_rust has the same shape (one root-leaf octree, bucket 2, 4 edges with flag 4). |

**Can a player mantle on a map whose edge list has no mantle edges?** Yes, if `edgeListUsedQueryTypes` has bit 1
clear, which is what the converter writes (4, `map_zone.h:892`). The built showcase's mantle ledges were seen working
(`docs/DEV_SHOWCASE.md:61`), which fits. If a converter ever set bit 1 without writing mantle edges for every ledge,
mantling would stop wherever an edge is missing. INFERRED from the branch above.

## 2. How the query works

### 2.1 Mantle (`Mantle_CanMantle` 0x141103c90)

| Step | Function, address | What it reads |
|---|---|---|
| 1 | `Mantle_CanMantle` 0x141103c90 | Player state gates (mantle dvar, stance, gestures, skydive, linked). Then the branch on `cm_mapEnts + 0x168` (`edgeListUsedQueryTypes`, `structs.hpp:22854`) bit 1. VERIFIED. |
| 2a (bit clear) | `Mantle_TraceLedgeFace` 0x141105f50 | One trace forward from the player at the climb height. Contents mask 0x810011, because `k_Mantle_traceLedgeFaceFlag` (0x14243892f) is 1 in the image (else 0x1000000). Rejects a hit whose normal has `abs(dot(n, up)) > 0.7` and one more than 75 degrees off the view. With the flag 1 it does not require the hit surface flags 0x2000000/0x4000000. A hit with flag 0x4000000 sets state bit 0 at +116 (the "over" bit, see step 3). Then `Mantle_ApplyEdgeNormalIfValid` and `Mantle_TraceClimbPath` (traces). VERIFIED. |
| 2b (bit set) | `Mantle_FindMantleEdge` 0x141105190 | Builds a sweep box (`Edge_ComputeSweepEnvelope` 0x1413ee4f0, `Edge_InitQueryState` 0x1413ebe80), then `BG_EdgeTuple_CollectMantleCandidates` 0x140caa870 with bucket 1 (MANTLE), up to 16 edges. A second query with bucket 2 (ladder centre lines) runs when the player is not sprinting and not in the air and the ladder-mantle switch is on (`mantle_ladder_enable` in one game mode, else `k_Mantle_findEdgeFlag` 0x142438b05 = 1). For each candidate it reads `Edge_GetFlags` 0x1413ed7e0 (metadata byte +6), keeps edges within 75 degrees of the view and from -10/16 units below to the climb height above the feet, and picks the lowest. No trace fallback: with 0 candidates it returns 0. VERIFIED. |
| 3 | back in `Mantle_CanMantle` | `edgeFlags & 4`: ladder top (`Mantle_CheckLedgeClearance`). `edgeFlags & 0xA == 8`: over only, needs an over position (`Mantle_CalcEndPos`). `== 2`: on only, never over. `== 0xA`: either. Then `Mantle_TraceClimbPath` (traces). VERIFIED. |

`Mantle_CheckActivationConditions` 0x141108a90 reads no edge-list data. VERIFIED.

### 2.2 The candidate query (`BG_EdgeTuple_CollectMantleCandidates` 0x140caa870)

Used for every bucket (callers: `Mantle_FindMantleEdge`, `PM_ContextMount_FindMountEdge_Initial`,
`BG_Mantle_ProbeEdgeCandidates` 0x140cbc0a0). `BG_EdgeTuple_CollectLadderCandidates` is a near copy used by
`BG_ContextMount_ProbeEdgeMountTarget`. VERIFIED (xrefs).

1. Walk `MapEnts.edgeListSpatialTree` (+0x178) with the query box (`SpatialQuery_RadiusBox_Init`) to get edge list
   indices into `MapEnts.edgeLists` (+0x170). VERIFIED.
2. Per list: skip unless `staticQueryTypes` (list +0x28) has bit `bucket`; skip unless the list box `mins`/`maxs`
   (+0x08/+0x14) overlaps the query box. VERIFIED.
3. Find the octree (list +0x58, `numEdgeOctrees` at +0x50, 368 bytes each) whose `edgeBucket` (+0x164) equals the
   bucket; skip unless its `flags[0]` (+0x165) has bit 0. VERIFIED.
4. Walk the octree from `rootNode` (+0x110): an inner node's children are the 8 nodes of node set
   `childNodeSetIndex` in `edgeOctreeNodeSets` (list +0x68, 128 bytes per set, 16 per node); each child box comes from
   `uniformGridIndex` and depth (`Edge_DecodeShortVector`) and is tested against the 6 planes of the query
   (`BG_EdgeTuple_CollectMantleSegmentHits` 0x140cb11d0). A node with flag LEAF (2) yields its edges; a leaf with flag
   ROOT (4) is the root itself. Up to 1024 leaves. VERIFIED.
5. A leaf's edges are `edgeIndices[qEdgeTableIndex .. + qEdgeTableCount]` (list +0x78), merged and de-duplicated into
   at most 512 ids (`BG_EdgeTuple_CopyEdgeBlock` 0x140caa270, cap 0x200). VERIFIED.
6. Dynamic edges: when the first list's `dynamicQueryTypes` (+0x24) has bit `bucket` and the query asks for them,
   `BG_EdgeTuple_QueryBroadphaseEdges` adds edges of moving entities, transformed by `Edge_TransformDynamicSegment`.
   VERIFIED (branch); what the broadphase walks was not read.
7. Each id resolves to its segment (`MapEdgeList_LookupSegment` 0x1413eb350: `edges + 32 * index`, only if `valid`
   +0x34 is non-zero) and is kept if the query's closest-approach test passes; results are sorted by distance.
   VERIFIED.

Edge id: low 32 bits = `(listIndex << 20) | edgeIndex` (`MapEdgeList_LookupSegment`, `MapEdgeList_LookupMetadata`
0x1413eb310); the high 32 bits are a second list/entity index for dynamic edges (`EdgeId_GetListIndex` 0x1413eb3d0).
VERIFIED.

### 2.3 Mount

- `BG_ContextMount_ProbeEdgeMountTarget` 0x140caec60 (from `BG_ContextMount_BuildPlayerMountProperties`): sphere
  query, bucket 0, up to 6 edges; returns false with none. All later steps (`BG_EdgeTuple_ComputeMountTargetPose`,
  `BG_ContextMount_ComputeMountTransform`, `BG_EdgeTuple_EvaluateMountEdgeFrame`) work on an edge already found.
  VERIFIED.
- `PM_ContextMount_FindMountEdge_Initial` 0x140cae650 (from `BG_ContextMount_ApplyMountAction`,
  `BG_ContextMount_BuildMountTargetRecord`): sweep query, bucket 0; with 0 candidates it returns 0. VERIFIED.
- `BG_EdgeTuple_CheckMountCharacterSweep` 0x140cb0aa0 is called only from `BG_EdgeTuple_ComputeMountTargetPose`
  (5 calls) and `BG_ContextMount_ComputeMountTransform` (1). Both run after a mount edge was found. VERIFIED (xrefs);
  the sweep itself was not decompiled (INFERRED: a capsule check of the pose on that edge).
- Moving along an edge to its neighbour (`mount_transition_enable`): `BG_EdgeTuple_SelectNearestEdge` 0x140cae4c0 →
  `BG_EdgeTuple_FindNearestSegment` 0x140cacdf0 → `Edge_FindNearestSegment` 0x1413ed1f0, which reads
  `edgeAdjacencyMetadata` (+0x88) and `edgeAdjacency` (+0x98) and returns 0 when either pointer is null. VERIFIED. So
  adjacency is optional: without it, a mounted player cannot slide round a corner onto the next edge. INFERRED.

`BG_Mantle_ProbeEdgeCandidates` 0x140cbc0a0 (caller `BG_Mantle_ResolveClimbTarget`) also queries bucket 0. What
movement uses it was not traced.

## 3. The query type bits

`edgeQueryBucket_t` (`structs.hpp:10463-10473`): MOUNT 0, MANTLE 1, LADDER_CENTERLINE 2, INTERACTION 3. A bit `1 <<
bucket` in a query-type word means "this bucket has edges". VERIFIED: the query tests `_bittest(word, bucket)` with
bucket 0 for mount and 1 or 2 for mantle (0x140caa870, 0x141105190, 0x140cae650).

| Bits | Meaning |
|---|---|
| 1 | MOUNT edges |
| 2 | MANTLE edges |
| 4 | LADDER_CENTERLINE edges |
| 8 | INTERACTION (seen in no map read) |

| Field | Reader | Stock (shipment, speedball, rust) | Built |
|---|---|---|---|
| `MapEdgeList.staticQueryTypes` (+0x28) | step 2 of 2.2: buckets with a non-empty static octree | 3, 3, 7 (rust has a ladder tree) | 4 |
| `MapEdgeList.dynamicQueryTypes` (+0x24) | step 6 of 2.2, first list only | 7, 7, 7 | 4 |
| `MapEnts.edgeListUsedQueryTypes` (+0x168) | `Mantle_CanMantle` bit 1 | 7, 7, 7 | 4 |

VERIFIED (exports and readers). The stock values fit "static = buckets with static edges, used = static OR dynamic";
INFERRED. Other readers of `+0x168` were not searched beyond the first 200 xrefs of `cm_mapEnts`; among them only
`Mantle_CanMantle` reads it.

**"Dynamic" means edges of brush models (moving entities), not edges computed at run time.** In all three stock lists
the edges with `w = 1` are exactly the edges the brush models list through `cmodel_t.edgeFirstIndex` /
`edgeTotalCount` (shipment 320 of 787, speedball 85 of 578, rust 127 of 862, the sums match), in model space (small
coordinates). The `w = 0` edges are world space and are the only ones in the octrees. VERIFIED (exports). The game
transforms dynamic edges by the entity pose (`Edge_TransformDynamicSegment`, called in 0x140caa870); INFERRED that
`w` marks the two kinds. `numDynamicEdges` (133, 0, 0) does not match these counts; what it counts is not determined.

## 4. The record layouts

`MapEdgeList`, 160 bytes (`structs.hpp:10508-10532`, offsets `:22588-22610`):

| Offset | Field | Use |
|---|---|---|
| 0x00 | name | `mapedgelist_00000` on every map read |
| 0x08, 0x14 | mins, maxs | box of all static edges; tested in step 2 |
| 0x20 | transientIndex | 0 |
| 0x24, 0x28 | dynamicQueryTypes, staticQueryTypes | section 3 |
| 0x2C, 0x30 | numDynamicEdges, numStaticEdges | not read in the paths above |
| 0x34 | valid (bool) | must be non-zero or no edge resolves (0x1413eb350) |
| 0x38, 0x40 | numEdges, edges | `float4[2]` per edge, 32 bytes: end point A (x, y, z, w), end point B (x, y, z, w) |
| 0x48 | edgeMetadata | 8 bytes per edge (below) |
| 0x50, 0x58 | numEdgeOctrees, edgeOctrees | one `PMROctreeMetadata` (368 bytes) per bucket used |
| 0x60, 0x68 | numEdgeOctreeNodeSet, edgeOctreeNodeSets | sets of 8 nodes, 128 bytes |
| 0x70, 0x78 | numEdgeIndices, edgeIndices | u32 edge indices, leaf ranges |
| 0x80, 0x88 | numEdgeAdjacencyMetadata, edgeAdjacencyMetadata | one u32 per edge, optional |
| 0x90, 0x98 | numEdgeAdjacency, edgeAdjacency | 12 bytes each, optional |

`FlaggedEdgeMetadata`, 8 bytes (`structs.hpp:10428-10433`):

| Offset | Field | Meaning |
|---|---|---|
| 0 | packedAdjacentNormal (float, radians) | One face normal of the edge. Reference = world up made perpendicular to the edge; for an edge within about 2.6 degrees of vertical, world X instead. The normal is the reference rotated about the edge direction by this angle. VERIFIED: `Edge_OrientStaticSegment` 0x1413f04e0, constants `(0,0,1)` at 0x1423c7da0 and unit X (`k_Common_vec4UnitX`), the 0.999 test. Stock horizontal mantle edges have 0 (normal = up, the top face); vertical mount edges have 0, 90, -90 or 180 (the four axes). VERIFIED (exports). Which way the rotation turns was not determined. |
| 4 | packedOpenAngle (u16, half float, radians) | The angle between the two faces at the edge. 15944 = 0x3E48 = 1.5703 (90 degrees) on 606 of 787 shipment edges. VERIFIED: `Edge_CalculateOpenAngleRad` 0x1413ec7a0 decodes a half float. |
| 6 | flags (u8) | bit 0 (1) mount, bit 1 (2) mantle on, bit 2 (4) ladder centre line, bit 3 (8) mantle over. VERIFIED: every edge in a stock MOUNT octree has bit 0, every edge in a MANTLE octree has bit 1 or 3, the ladder octree holds flag 4 only (exports); the mantle code reads 4, 0xA, 8, 2 as in 2.1 step 3. The names "on" and "over" for 2 and 8 are INFERRED from that code. |

Stock flag counts (world edges, mp_rust): 1 = 317 (mostly vertical: wall corners), 10 = 230 and 11 = 170 (mostly
horizontal: ledge tops), 2 = 14, 4 = 4. A segment appears once with combined flags, never twice. VERIFIED (exports).

`PMROctreeMetadata`, 368 bytes (`structs.hpp:10475-10485`, `:22570-22578`): `nodeHalfWidth[16]` (float4 per depth;
stock: x = y = z = root half width / 2^depth), `minExtent` (float4: x, y, z of the cube corner, w = -half width),
`rootNode` (16 bytes at 0x110), `nodeSetCount` (0x120), `nodeBoundingRadiusSq[16]` (0x124; the converter writes 3 *
hw^2), `edgeBucket` (0x164), `flags[0]` (0x165: bit 0 must be set for the tree to be searched; bit 1 set on all
stock trees; an empty stock ladder tree has 2). VERIFIED for the offsets the query reads; the meaning of flags bit 1
is not determined.

`PMROctreeNode`, 16 bytes (`structs.hpp:10454-10461`): `uniformGridIndex[3]` (u16), `depth` (u8), `flags` (EMPTY 1,
LEAF 2, ROOT 4), then u32 `parentNodeIndex` / `qEdgeTableIndex` and u32 `childNodeSetIndex` / `qEdgeTableCount`.
Inner node: `childNodeSetIndex` is an absolute index into `edgeOctreeNodeSets` (stock mantle root of speedball points
at set 55, right after the 55 mount sets). Leaf: `qEdgeTableIndex`, `qEdgeTableCount` into `edgeIndices`. Stock
non-leaf children unused in a set are marked EMPTY. VERIFIED (exports and 0x140cb11d0).

`EdgeAdjacencyMetadata`, u32 per edge: `startIndex:23`, `mountCount:2`, `interactionCount:2`
(`structs.hpp:10492-10497`). `Edge_FindNearestSegment` reads the count at bit 23 (mount) or bit 25 and the start index
(low 23 bits) into `edgeAdjacency`. `EdgeAdjacency`, 12 bytes: `start`, `end` (u16 half floats, the part of this
edge, 0..1), `adjEdge` (u32 edge index), `adjStart`, `adjEnd` (half floats on the other edge). 0x3C00 = 1.0.
VERIFIED (0x1413ed1f0, exports).

## 5. What the converter writes today

`emitMapEdgeListBody` (`C:\zsw-iw8r\scriptables\src\games\iw8\zone\map_zone.h:677-799`), only when the map has ladders:

- two edges per ladder: the centre line (top to bottom, flag 4, adjacent normal `-atan2(ny, nx)`, open angle 0) and
  a horizontal width edge at the top (flag 4, open angle 15944) (`:715-741`). VERIFIED.
- `dynamicQueryTypes` = `staticQueryTypes` = 4, `valid` 1, all edges static (`:751-756`). VERIFIED.
- one octree: bucket 2, flags 3, the root is a leaf (flags 6) holding all edges, `edgeIndices` = 0..n-1
  (`:775-797`); no node sets, no adjacency. VERIFIED.
- MapEnts: `edgeListUsedQueryTypes` 4, one edge list, a spatial tree (`:892-895`). VERIFIED.
- `cmodel_t.edgeFirstIndex` / `edgeTotalCount` are not written (no match in `map_zone.h`; the cmodel buffer is
  zeroed, `:637`). INFERRED: 0, no brush-model edges.

No mount or mantle edges are written. VERIFIED.

## 6. What a built map needs

**Mantling: nothing.** It works from collision while `edgeListUsedQueryTypes` bit 1 stays clear. VERIFIED (2.1).
Limits of that path: mantle-over needs the hit face to carry surface flag 0x4000000 (2.1 step 2a); whether built
collision faces carry it is not determined. Writing mantle edges (and setting bit 1) would only be worth it to match
stock mantle-over behaviour, and it then needs an edge on every ledge.

**Mounting: a MOUNT bucket.** Minimum, all VERIFIED as read by the query path:

1. Edges with flag bit 0 in `edges` + `edgeMetadata`, with a correct `packedAdjacentNormal` and `packedOpenAngle`
   (the mount pose is built from them: `Edge_ComputeSegmentBasis`, `Edge_CalculateOpenAngleRad` in 0x140caec60).
   Stock marks both convex horizontal top edges (flag 11 or 1) and vertical corner edges (flag 1).
2. `staticQueryTypes` bit 0 (1) and the list box covering the edges.
3. A second `PMROctreeMetadata` with `edgeBucket` 0 and `flags[0]` bit 0 set, and its leaves in `edgeIndices`.
4. `edgeListUsedQueryTypes` bit 0: not read by the paths above; set it to match stock. INFERRED.

Optional: adjacency (corner transitions only, 2.3), node sets (a root leaf works, as the converter's ladder tree and
stock rust show), dynamic (brush-model) edges.

**Feasibility.** Looks feasible. Mount edges are brush edges where two faces meet at a convex angle: the open angle and
one face normal come straight from the brush planes. The converter already writes the container, the octree and the
indices for ladders, so a MOUNT tree is the same code with bucket 0. Two cautions: a single root leaf returns every
edge of the bucket and the merge keeps at most 512 (0x140caa270), so a map with more than about 500 mount edges needs
a real octree (node sets with `uniformGridIndex`, INFERRED); and the edge selection rules (minimum length, height
above floor, which convex edges count) must be learned from stock edges, since only stock data shows them.
INFERRED.

## 7. Not determined

- The sign of the `packedAdjacentNormal` rotation (which of the two faces it names), and whether mount needs the edge
  direction in a fixed sense (which side the player stands on).
- What stock uses to choose mount and mantle edges (lengths 1.5 to 812 units; heights; whether thin or short edges are
  skipped). Only the exports show the result.
- `BG_EdgeTuple_QueryBroadphaseEdges` and how dynamic edges are linked to entities; `numDynamicEdges`.
- Octree `flags[0]` bit 1, `uniformGridIndex` encoding (`Edge_DecodeShortVector` not decompiled).
- Whether built collision faces carry surface flag 0x4000000 (mantle over on the trace path).
- What `BG_Mantle_ProbeEdgeCandidates` (bucket 0) is used for.
- Readers of `edgeListUsedQueryTypes` beyond the first 200 xrefs of `cm_mapEnts`.

## 8. The two rotation conventions, from the stock data (added 2026-11-14, main session)

Section 7 left open which way the adjacent-normal rotation turns. The stock exports answer it without the game: a
rectangular pillar has four vertical mount edges, and the two faces at each corner are known from the rectangle.

Method (scratch script, not kept): the vertical world edges (w = 0) with an open angle of 90 degrees of mp_rust,
mp_m_speedball and mp_shipment; groups of four at the corners of an axis-aligned rectangle with sides of 8 to 48
units, one edge per corner, all with the same height range. 12 such pillars, 48 edges.

| Hypothesis | Result |
|---|---|
| adjacent normal = reference turned **right-handed** about the edge direction (B - A) by `packedAdjacentNormal` | the normal is one of the corner's two faces on 40 of 48 edges (the other turn: 22 of 48, which is chance) |
| the other face's normal = the adjacent normal turned right-handed about the edge direction by the open angle | holds on 40 of those 40 edges (the other turn: 0 of 40) |

So, VERIFIED on stock data (the 8 misses are two rectangles that are not pillars):

- `adjacentNormal = R(d, +packedAdjacentNormal) * reference`, with `d = normalize(B - A)`, a right-handed turn; the
  reference is world X for a near-vertical edge, else world up made perpendicular to the edge (section 4).
- `otherNormal = R(d, +openAngle) * adjacentNormal`.
- The edge direction is free (242 stock edges of mp_rust run up, 102 down): what must hold is
  `cross(adjacentNormal, otherNormal) . d > 0`. A writer picks the order of the two faces, or turns the edge round,
  to make it so.
- A ledge top (flags 10 and 11) stores the angle 0 on 157 of 172 horizontal edges of mp_rust: its adjacent normal is
  world up, the top face, and the wall face below it is `d x up`.
- World edges have w = 0 on both end points; edges of brush models have w = 1 (model space).

Stock mount edges by kind (static edges): mp_rust 354 mount-only (344 vertical wall corners, length 14 to 812, median
97) and 182 mount + mantle (172 horizontal ledge tops, median length 48); mp_m_speedball 260 mount-only (216
vertical, median 49) and 39 mount + mantle.

## 9. The octree's grid index, from the stock data (added 2026-11-14, main session)

Section 7 also left the encoding of `uniformGridIndex` open. Checked on the three stock exports (scratch script, not
kept): with a node's cube taken as `min = minExtent.xyz + uniformGridIndex * 2 * nodeHalfWidth[depth]` and side
`2 * nodeHalfWidth[depth]`, every edge a leaf lists crosses that leaf's cube: 476 of 476 (mp_m_speedball, mount),
323 of 323 (mantle), 857 of 857 and 669 of 669 (mp_rust), 427 of 427 and 435 of 435 (mp_shipment). VERIFIED on data.

- The root cube: `minExtent` and a side of `2 * nodeHalfWidth[0]`, the largest extent of the list's box. It need not
  be centred on the box.
- An edge is listed in every leaf it crosses (an edge appears in several leaves). Stock leaves hold at most 10
  edges, at depths 1 to 7.
- A leaf with no edge has flags 3 (EMPTY | LEAF); an inner node has flags 0 and `childNodeSetIndex`; the eight
  children of a set have grid indices `2 * parent + (0 or 1)` per axis, in any order.

## 10. What the build writes now (added 2026-11-14, main session)

Built, proven offline, not seen in game.

**Which edges** (`src/core/mount_edges.cpp`, `findMountEdges`). From the world's collision brushes that are solid and
have at least one drawn face (a clip or caulk hull gives none but still hides edges of others), and from the convex
pieces of placed models' collision hulls (not from the plain box a model without a hull gets). For a model piece the
two walls of a corner must be at least 60 degrees apart, so a barrel of eight or more facets gives no corners:

- a wall corner: an edge within 15 degrees of upright between two faces within 15 degrees of upright, the faces
  between 45 and 135 degrees apart, 32 units or longer;
- a ledge top: an edge within 15 degrees of level between a top face (normal z above 0.7) and a wall face, 16 units
  or longer.

An edge is tested every 8 units. A part counts when the points 3 units out from the edge (along the middle of the two
normals, and in front of each face just inside the other) are in no other solid; a wall corner also needs the point 16
units out to be free; a ledge needs 20 units free above the edge on both sides of it and the point 16 units in front
of the wall, 8 below the edge, free (a curb is no ledge). Parts on one line with the same two faces, from any number
of brushes, are joined before the length is asked for. These rules are this editor's choice: only the result of
stock's own rules is known (section 7). The game checks at run time whether a player fits at an edge
(`BG_EdgeTuple_CheckMountCharacterSweep`), so an edge too many is only data. Not more than 60,000 (the longest are
kept).

**The metadata** (`src/core/map_edges.h`, `makeMapEdge`): flags 1 (mount) on every edge; the two faces ordered, or
the edge turned round, so that `cross(adjacent, other)` points along the edge (section 8); a ledge keeps its top face
as the adjacent face, which gives the stored angle 0 as on stock ledges; the open angle as a half float (a right
angle is 15944, the stock value).

**The trees** (`buildMapEdgeList`): one per bucket that has edges. The root cube is centred on the bucket's edges,
its side their longest extent. A node with more than 8 edges is split (to depth 10 at most) into a set of eight
children in the stock slot order (offsets 000, 001, 011, 010, 110, 111, 101, 100 for x, y, z); a leaf lists every
edge that crosses its cube; an empty leaf has flags 3; an inner node names its parent as `set * 8 + slot`, the
children of a root as 0xFFFFF8, as stock (checked on mp_m_speedball and mp_rust: 143 and 135 inner nodes). The ladder
tree stays one leaf, as it has been written (and seen working) before.

**The asset** (converter `map_zone.h`, `emitMapEdgeListBody`): `staticQueryTypes` = `dynamicQueryTypes` = the
buckets present (1 mount, 4 ladder); `MapEnts.edgeListUsedQueryTypes` the same, so bit 1 (mantle) stays clear and
mantling keeps its trace path (section 2.1). Node sets are aligned to 128 bytes in the zone (the converter's own
reader, `asset_streams.cpp` `stream_mapedgelist`, mirrors the game's loader and refused 4). The adjacency tables
follow the index list (section 11).

**The proof** (`tools/mount_edges_build_test.py`): a room with a crate, a wall of two brushes, a pillar of two
stacked brushes, a clip brush and a ladder is built; the edge list is read back from the zone with ACTS. The same
shape checks (roots, parent indices, grid index by slot, leaf ranges, set counts, every listed edge crossing its
leaf) pass on stock mp_m_speedball's list and on the built one; on the built one every crossing edge is listed, a box
round each mount edge finds it through the MOUNT tree and never through the ladder tree, the metadata follows the
rule above, and the expected edges are there with their joined lengths (none at a joint, none against the clip
brush).
Big-map check: a private copy of Cinder Rail (mp_crmnt, deleted again) built with 1,722 wall corners and 1,026 ledge tops (2,748 mount edges; 8,991 covered edges not counted); the zones pass the ACTS loader test; read back (tools/mount_edges_build_test.py --check <zone>): 451 node sets, 3,158 leaves, deepest node 8, at most 8 edges in a leaf, every crossing edge listed, a box round each edge finds it through the MOUNT tree.

## 11. Mount adjacency: the rule, from the stock data (added 2026-11-14, main session)

Section 2.3 found that moving from a mounted edge to a neighbour reads `edgeAdjacencyMetadata` and `edgeAdjacency`,
and section 4 gave their layouts. What makes two edges neighbours was read off the stock tables (scratch scripts; the
rule is kept, in Python, in `tools/mount_edges_build_test.py` `predicted_adjacency`, which the proof runs against a
stock map every time).

What the stock tables hold (mp_m_speedball 174 records, mp_rust 190, mp_shipment 90):

- every record joins two mount edges (flags 1 with 1, or 11 with 11), and every record is answered: if A names B,
  B names A;
- the two edges are parallel; they are 4 to 15.75 units apart; one face of each has the same normal;
- 144 of 174 (speedball) join two upright edges, 30 two level ones: the two corners at the end of a thin wall, the two
  top edges of a thin wall;
- `start`/`end` and `adjStart`/`adjEnd` are the parts that lie side by side, each in its own edge's measure (0 at the
  first end point, 1 at the second, as half floats; 0x3C00 = 1), lower value first whichever way the edges run;
- an edge has at most two records (`mountCount` 0, 1 or 2).

The rule that gives exactly these tables. Two static mount edges are neighbours when all of this holds:

1. parallel within about 2.5 degrees (|cos| at least 0.999);
2. 0.5 to 16 units apart (the distance between the two lines);
3. a face of one and a face of the other have the same normal (dot above 0.99), and the step from one line to the
   other lies in that face's plane (within 0.5 units): they are the two long sides of one narrow face;
4. each edge's other face is turned away from the other edge (so the narrow face is solid between them: two crates
   10 units apart are no neighbours across the gap);
5. at least 4 units of their lengths lie side by side.

VERIFIED on data: the rule gives 174 pairs on mp_m_speedball and the table has the same 174, with the same parts
(within 0.02); mp_rust 190 and 190; mp_shipment 80 and the same 80 among static edges (its other 10 records involve
edges of brush models). With "parallel within 8 degrees" mp_rust would get one pair more that the table does not
have (two edges 3.3 degrees apart), hence 0.999.

The build writes the tables by this rule (`buildMapEdgeAdjacency` in `src/core/map_edges.h`): one word per edge
(first record in the low 23 bits, the count in bits 23 and 24), the records after the index list, both aligned to 4
bytes. An edge keeps at most three neighbours, the nearest first (the count has two bits). The proof reads the built
tables back and finds exactly the pairs the Python rule gives (the test map's 16-unit wall: its two top edges and the
corners at each end; the crate and the pillar none).
