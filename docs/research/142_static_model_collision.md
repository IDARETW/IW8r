# 142. Static model collision: which stock models collide, and with what shape

Date: 2026-11-15. Owner report: "some stock bushes dont have collision and allow the player to walk thru them, make
sure we handle the collision compiling correctly". Evidence: the Replay 1.20 base executable in IDA
(`game_dx12_ship_replay.exe`, symbol names as in the shared database), ACTS exports of the stock zones (xmodel,
physicsasset, col_map, physicslibrary), the game's own Havok type compendium, and the Havok 2018.1 SDK reference on the
owner's NAS (read as a reference only; no SDK code is copied into this repo). Code: `src/core/model_physics.*`,
`tools/build_model_physics_catalog.py`, `data/asset_catalog/iw8_model_physics.tsv`.

## 1. What the game does

| # | Rule | Status | Evidence |
|---|---|---|---|
| 1 | A placed static model gets collision only through the clipmap's static model collision lists (`clipMap_t.staticModelCollisionModelLists`: model, physicsAsset, detailCollision, instances of origin / angles / scale) | VERIFIED | `StaticModels_AddClipmapCollisionShapes` 0x14139D290 walks the lists; nothing else adds static model shapes |
| 2 | An entry is exactly the XModel's `physicsAsset` / `detailCollision` pair | VERIFIED | mp_shipment: 198 of 198 list entries equal the xmodel fields; no model without either is listed |
| 3 | The physics asset's shapes go into the main physics worlds, the detail collision (else the physics asset) into the detail worlds | VERIFIED | same function: `Physics_AddQuantizedAssetShapes` into `s_staticModelsClipmapShapes`, `Physics_AddDetailCollisionShapesToInstanceList` (else the physics asset) into `s_staticModelsClipmapShapesSecondary`; `StaticModels_InstantiateCollisionForClipmapZone` 0x14139E340 takes the secondary one when `Physics_IsDetailWorld` (worlds 1 and 4) |
| 4 | `XModel.collLod` plays no part (stock props carry 7 or 8 with 3 to 6 LODs) | VERIFIED (data) | xmodel exports; the list builder takes the physics asset |
| 5 | Most bushes have no physics asset: walked through and shot through in game (`tm/foliage_alder_bush_large_01`, prosopis, cypress, forsythia: `physicsAsset` null, `XModelPhysicsUseCategory_NoPhysics`); some have one (`foliage_alder_bush_large_02_lm`, `ee_foliage_bush_hedge_128x60`: a 128 x 48 x 36 box) | VERIFIED (data) | xmodel / physicsasset exports of mp_shipment, mp_bm_tut, mp_aniyah_tac, mp_emporium |
| 6 | The live player's movement trace mask is 0x2014011 (0x10011 when `pm_type >= 7`); `PM_traceCapsuleIgnoreBodies` hands the physics query `mask & 0xFDFFBFFF` = 0x0010011 | VERIFIED | `G_ActiveMP_ClientThinkInternal` 0x1411E8E2E; `PM_traceCapsuleIgnoreBodies` 0x140CDE780 |
| 7 | No stock prop body has a bit of that mask (Shipment: 0xB180 155 assets, 0x420B180 23, 0x2180 10, 0x400B180 10, 0x3180 8, 0x4800000 6; only editor dummies have 0x1): in stock maps static models never stop player movement; clip brushes keep players out of props | VERIFIED (data, mask) / INFERRED (that the query filters per body by these contents) | physicsasset `bodyContents` of all 220 Shipment assets |
| 8 | Bullets trace the server detail world (index 1) with mask 0x2806931 | VERIFIED | `G_Weapon_TraceBulletSegment` 0x1411D53F4 / 0x1411D5418: `PhysicsQuery_LegacyBulletTrace(1, ..., 0x2806931, ...)` |
| 9 | Props' contents meet the bullet mask (0xB180 & 0x2806931 = 0x2100, 0x2180 & 0x2806931 = 0x2100): bullets stop at a stock prop's detail collision or physics asset | INFERRED (per-body filter as in 7) | masks above |
| 10 | Grenades and rockets use clip mask 0x2806191 | VERIFIED | `BgMissile::FireRocket`, `BgMissile::InitGrenadeEntity` (`BgEntityData::SetClipMask`) |
| 11 | Player movement queries the world its pmove handler names (`(*(pm+840))->vfunc[31]`); on the client that object is `LocalClientGlobals + 0x6428` (`CG_PredictionState_Init`). The server's trajectory handler names world 0 (`GTrajectory::GetPhysicsWorld` returns 0) | VERIFIED (the call shape) / INFERRED (that movement uses the main world 0 / 3, not the detail world 1 / 4) | `PM_traceIgnoreList` 0x140CDE9E0, `PM_traceCapsuleIgnoreBodies`; bullets name world 1 explicitly (row 8) |

What the editor does with it (owner policy, `src/core/model_physics.h`): own maps have no stock clip brushes, so a
placed stock model whose XModel has a physics asset gets player collision of that asset's shape (row 2: the models that
are solid in game), one without none (rows 5 and 7: the bushes that are walk-through in game), the mapper's own models
their render mesh's hull. `iw8r_collision 0 / 1` on a placement overrides. The stock-map walk preview
(`addStockPropCollision`) uses the same physics shapes (rows 2 and 7: the shape the game has).

## 2. Physics asset layout (Havok 2018 tagfile)

The asset's `havokData` is a tagfile without types: `TAG0 { SDKV "20180200", DATA, TCRF, INDX { ITEM, PTCH } }`. TCRF
holds `40354c32492e6bc1`, the little-endian bytes of the Replay compendium signature 0xC16B2E49324C3540 (VERIFIED: every
stock asset; the IDARETW Replay ZoneTool records the same signature). ITEM entries are `u32 (flags << 24 | type)`,
`u32 offset in DATA`, `u32 count`; flags 0x10 = object, 0x20 = array. A pointer / array / relative-array field holds
the index of the item it names (VERIFIED: every lookup below lands on an item of the member's type).

The types come from the game's global type compendium, `code_post_gfx` physicslibrary `physicsglobaltypecompendium`
(140,172 bytes; `TYPE { TPTR TSTR TNA1 FSTR TBDY THSH TPAD }`, 1,686 types). TNA1 names each index (count + 1, then per
type: name string index, template count, template name / value pairs, all packed integers); TBDY gives per type its
parent, optional parts (bit 0x08 size and alignment, 0x20 members: name, flags, offset, type; the member count is the
low 16 bits of its word). The decoder reads the members by name from this compendium. Type indices used by stock props
(VERIFIED by name):

| Index | Type | Size | Members the decoder reads |
|---|---|---|---|
| 39 | `hknpPhysicsSystemData::bodyCinfoWithAttachment` (parent `hknpBodyCinfo`) | 192 | `shape` +0, `position` +48, `orientation` +64 (quaternion x, y, z, w) |
| 90 | `hkVector4f` | 16 | vertex arrays |
| 210 | `hknpConvexShape` (parent `hknpShape`: `convexRadius` +32) | 64 | `vertices` +58 (`hkRelArray<hkVector4f>`) |
| 1448 | `hknpConvexPolytopeShape` | 88 | `planes` +64, `faces` +68, `indices` +72 (not needed: the hull of the vertices is rebuilt) |
| 1447 | `hknpBoxShape` (parent 1448) | 160 | `obb` +96; vertices as its parent |
| 1481 | `hknpCylinderShape` (parent 1448) | 128 | `a` +96, `b` +112; vertices as its parent |
| 266 | `hknpCompoundShape` | 288 | `instances` +72 (`hkFreeListArray<hknpShapeInstance>`, `elements` +0) |
| 261 / 262 | `hkFreeListArrayElement<hknpShapeInstance>` / `hknpShapeInstance` | 112 | `transform` +0 (`hkTransformf`: rotation columns, translation +48), `scale` +64, `shape` +80, `isEmpty` +92 |
| 602 | `hknpCompressedMeshShape` | 128 | `data` +72, `externShapes` +112 (`hkArray<hknpShapeInstance>`) |
| 1466 | `hknpCompressedMeshShapeData` | 272 | `meshTree` +32 (`hknpCompressedMeshShapeTree`, parent `hkcdDefaultStaticMeshTree`) |
| 534 / 535 / 547 | `hkcdDefaultStaticMeshTree` / `hkcdStaticMeshTree::Base` / `AabbTree<Aabb5BytesCodec>` | 160 | `nodes` +0, `domain` +16, `sections` +64, `primitives` +80, `sharedVerticesIndex` +96, `packedVertices` +112, `sharedVertices` +128 |
| 548 | `hkcdStaticMeshTree::Section` (parent `Aabb4BytesTree`: `nodes` +0, `domain` +16) | 96 | `codecParms` +48 (origin xyz, step xyz), `firstPackedVertexIndex` +72, `firstSharedVertexIndex` +76, `firstPrimitiveIndex` +80, `numPackedVertices` +88, `numPrimitives` +89 |
| 1651 | `hkpLimitedHingeConstraintData` | | not a shape (a destructible's hinge) |

The first decoder (before this note) used these indices without names, mapped a polytope to "the next vertex array"
and treated 602 / 1466 / 1651 as unknown shapes; its 3,494 decoded assets are unchanged by the verified reader.

Units: Havok units are inches / 32 (VERIFIED: x64-zt's `replay_havok.cpp` divides by 32; with 32 the decoded shapes
fit their models' bounds, 110 of 220 Shipment assets at once against 40 of 220 with metres).

## 3. Compressed mesh shapes

`hknpCompressedMeshShape` keeps its geometry in the mesh tree (rules as in the Havok 2018.1 `hkcdStaticMeshTree.h /
.inl`, `hkcdStaticMeshTreeDecoder.inl` and `hkcdStaticTreeCodecs.h / .inl`, Common package; VERIFIED on the stock data,
every decoded vertex within its model's bounds):

- A primitive is 4 bytes (a, b, c, d): `DE AD DE AD` removed; b != d: a triangle (c == d) or a quad (two triangles
  (a, b, c) and (a, c, d)); b == d and c == 3: CUSTOM; b == d otherwise: EXTERNAL (one of `externShapes`).
- A vertex index below `numPackedVertices` is a packed vertex (u32: 11 / 11 / 10 bits, `codecParms` origin + q x step);
  above, `sharedVerticesIndex[firstShared + i - numPacked]` names a shared vertex (u64: 21 / 21 / 22 bits over the
  tree's `domain`, step = extent / (2^bits - 1)).
- CUSTOM: `sharedVerticesIndex[firstShared + a]` = vertices << 8 | tags << 6 | compression << 4 | type, the next entry
  is the first vertex in the shared array; compression GLOBAL (u64 shared vertices) or LOCAL_4 (u32 11 / 11 / 10 bits
  over the primitive's leaf box, two per u64 with each pair swapped on little-endian). b is that leaf's node in the
  section tree: 4-byte nodes (min / max nibbles h, l: min + h^2 x extent / 226, max - l^2 x extent / 226), data bit 0
  internal, right child = this + (data & 0xFE), left = this + 1. Stock props use type 2 with LOCAL_4 for convex pieces
  (the crane's ten beams, sign posts, ladders).

## 4. The catalogue

`tools/build_model_physics_catalog.py` exports the xmodel and physicsasset tables of the 34 model zones plus the
compendium, decodes every asset and writes `data/asset_catalog/iw8_model_physics.tsv` (2.3 MB): `M` records (model,
physics asset, detail collision) and `P` records (physics asset, body contents, convex pieces, triangles, note), model
space inches Z-up. A decoded shape outside the bounds of every model using it (padded by 8 units or a quarter of the
model's size) is not trusted.

| | First decoder | Verified decoder |
|---|---|---|
| Physics assets | 3,869 | 3,869 |
| Convex pieces read | 3,494 | 3,767 |
| Triangle meshes read (737 triangles) | 0 | 17 |
| Both | 0 | 1 |
| Not read (render mesh stands in) | 375 (175 unknown types, 151 outside the bounds, 49 several bodies) | 84 (outside the bounds: weapons with empty bounds, physics shared with a differently placed model) |

The editor (`physicsShapePlanes`): convex pieces become hulls (simplified to 24 planes, slivers half a unit thick, an
open or oversized piece its box); triangles become up to four convex pieces in the build and the own-map preview
(`convexPieces`), and facets (`CollisionWorld::addTriangle`) in the stock-map walk preview.

## 5. Not settled

- Whether player movement uses world 0 / 3 (main) or a detail world: the pmove handler's vfunc 31 was not resolved
  (row 11). The bullet trace naming world 1 explicitly and the trajectory handler returning 0 point to the main world.
- How the physics query filters a static compound's child shapes by contents (rows 7 and 9).
- The 84 assets not read, and capsule / sphere radii (widened here by the convex radius along 14 directions).

## Sources

- IDA: the functions named in section 1 (addresses are the base executable's).
- Havok 2018.1 reference (NAS, read only): `Havok_2018-1-0_20180606_Physics_Base-and-Product_Source/.../Physics/Collide/Shape/` (`hknpShape.h`,
  `Convex/hknpConvexShape.h`, `Convex/Polytope/hknpConvexPolytopeShape.h`, `Composite/Compound/hknpCompoundShape.h`,
  `hknpShapeInstance.h`, `Composite/Mesh/Compressed/hknpCompressedMeshShape.h`), `.../Dynamics/Body/hknpBodyCinfo.h`;
  `Havok_2018-1-0_20190416_Common_Base-and-Product_Source/.../Geometry/Collide/DataStructures/StaticMeshTree/hkcdStaticMeshTree.h, .inl`,
  `.../Geometry/Internal/DataStructures/StaticMeshTree/hkcdStaticMeshTreeDecoder.inl`,
  `.../Geometry/Internal/DataStructures/StaticTree/hkcdStaticTreeCodecs.h, .inl`; `Replay-Havok-Research/Replay-SDK-Search.md`
  (Replay embeds `hk2018_2_0_r1`; compendium signature); `Havok-2018.2-Tagfile-Research/.../havok-meshshape.md` (tagfile
  sections, ITEM / PTCH); `Open-Source-Modding-Havok-Docs/.../hkx_format_wdl.md` (pointer fix-ups hold item indices).
- x64-zt (`E:\mw124\x64-zt\src\games\iw8\havok\`): compendium loading from code_post_gfx, the TBDY layout, and its
  compressed mesh writer as a cross-check of the codecs.
