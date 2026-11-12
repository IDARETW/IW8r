# 96. Removing stock world geometry (render, collision, sun shadow)

Date: 2026-11-12. Roadmap 11.5: props of a stock map could already be hidden (static model scale 0) and lose their
collision (research 88, next step 6). This file covers the other half: world geometry baked into the stock world
(walls, floors, crates that are world surfaces, not static models).

Nothing in this file has been seen in game. Everything below is proven offline with ACTS read-backs.

## Result

`editzone` key `remove_world_surfaces` (fork branch `iw8radiant/scriptables`, commits 373d6ad2, 626bfcc9 and 78f04f41
(occlusion), new file `src/convert/iw3_iw8/stock_world_remove.{h,cpp}`):

```json
"remove_world_surfaces": {
  "boxes": [{"mins": [x, y, z], "maxs": [x, y, z], "render": true, "collision": true}],
  "surfaces": [n, ...],
  "collision": true,
  "rebake_sun_shadow": false,
  "occlusion": "auto",
  "acts": "<acts.exe>"
}
```

| Part | What is removed | How |
|---|---|---|
| Render | every world triangle whose three vertices lie inside one box (of any surface); every triangle of the listed surfaces | the triangle's indices become (i0, i0, i0) in a copy of the transient zone's index buffer |
| Collision | the triangles and convex shapes (brush hulls) of the world Havok mesh that lie inside a box (all their vertices) | in place, as Havok's own primitive removal does it (below) |
| Sun shadow | with `rebake_sun_shadow`, the removed triangles stop casting | the existing `sun.direction` bake (fork f9c14d9f, `stock_sun.*`) runs at the stock direction; it skips degenerate triangles |
| Occlusion | the Umbra occlusion of every occluder (map-wide; below) when a removed triangle belongs to an occluder surface | the stock Umbra tome is replaced by the converter's conservative tome with the stock objects |

A box with `"render": false` takes only the collision (a walk-through wall), one with `"collision": false` only the
triangles (an invisible wall). The report (`<zone>.edit-report.json`) lists the triangles removed, the surfaces that
lost every triangle, the collision counts before and after, and the sun bake.

Selection is per triangle, not per surface: a stock world surface is one material merged over a wide area (on
mp_shipment `tw/dam_metal_black_dirty_01` has 399 triangles spread over 1,200 x 1,300 units), so "a surface whose every
vertex lies inside the box" would rarely match a single object. A triangle that reaches out of the box stays. The
editor's boxes should be a little larger than the object but must not take in the ground below it: ground triangles
that lie entirely inside the box go too.

## Render: degenerate triangles

The world triangles are 16-bit indices in `GfxWorldTransientZone.drawVerts.indices` (research 30). On mp_shipment no
two surfaces share an index range (checked: 0 overlaps over 1,039 surfaces), so changing the indices of one triangle
changes no other triangle. A triangle (i0, i0, i0) has no area: the rasterizer draws nothing for it in any pass that
reads the world index buffer (opaque, depth prepass, sun and spot shadow maps, decal surfaces), and every count,
offset, surface index, flag and bound stays as stock. Nothing the engine sizes or sorts at load changes.

Not chosen:

- `triCount` 0: changes counts that batching or load-time tables may rely on; unknown.
- Clearing the surface flags (`GFX_SURF_CASTS_SUN_SHADOW` ...): the zone also keeps precomputed lists
  (`dpvs.sortedSurfIndex`, 1,056 entries; `shadowGeomOptimized`); a flag that disagrees with them is a risk, and the
  degenerate triangles cast nothing anyway. Flags stay stock.
- Moving vertices: surfaces share vertex ranges (research 30), so a moved vertex would change other surfaces.

## Collision: in-place primitive removal

Small maps keep all world collision in `clipMap_t.havokWorldShapeData`: a self-describing Havok tagfile whose root is
a `HavokPhysicsShapeList` with one `hknpCompressedMeshShape` (`World Entity Main_Full`; research 90). mp_shipment:
9,360,600 bytes, 173,943 mesh triangles, 9,895 convex shapes (box and polytope brush hulls), 16 primitives already
removed by the Replay tools.

The first try rebuilt the mesh with the converter's own writer (`add_compressed_mesh_shape`, the one that bakes the
converted maps). It works but re-quantizes the whole map: on mp_shipment every vertex could move by up to 0.5 units
(the shared-vertex tolerance had to grow to fit the 65,536 shared-vertex table), 38 sliver triangles collapsed and the
data grew from 9.4 to 13.1 MB. Dropped for the in-place edit below.

The game has Havok's own primitive removal (Replay `game_dx12_ship_replay.exe` 0x141E24BE7, read only): a triangle
primitive becomes `DE AD DE AD`, a quad loses one half by repeating an index (`(a,b,c,d)` without `(a,c,d)` becomes
`(a,b,c,c)`; without `(a,b,c)` it becomes `(a,c,d,d)`), and a list of keys is updated. The stock meshes carry such
removed primitives, and the converter's reader (`read_compressed_mesh_shape`) shows that their keys are not in the
mesh's SIMD tree. `RemoveCollision` does the same:

1. Decode the mesh (`read_compressed_mesh_shape`): triangles with their primitive keys (section << 8 | primitive << 1 |
   half), convex shapes with their keys.
2. Triangles with all three corners inside a box (game units = Havok units x 32), convex shapes whose placed vertices
   are all inside a box (the convex radius is not counted).
3. In the primitive array: a whole primitive becomes `DE AD DE AD`; a quad that keeps one half becomes a triangle
   (the half that moves to slot 0 takes the interior flag of its old key in `triangleIsInterior`).
4. In the SIMD tree (`hkcdSimdTree::Node`, 128 bytes: six float rows of four slots, four data words, the leaf flag)
   the leaf slot of each removed key gets the data and bounds of an empty slot (data 0xFFFFFFFF).
5. The edited arrays hold no pointers, so their bytes are found (exactly once) in the stock blob and overwritten
   there. Every other byte stays stock: the vertices, sections, section trees (now conservative), the list's
   vertex / triangle / convex counts, the shape tags.
6. Read back: the blob decodes, nothing is left inside a box, and the counts add up.

On mp_shipment the proof box removes 276 triangles and 12 convex shapes; 5,069 bytes of 9,360,600 change.

Refused: a map whose shape list has collision-tile slots (Warzone, research 90); `editzone` stops with a message.
The tiles are separate zones (`<map>_cg_NNNNN_tr.ff`, `_cg_ls_tr.ff`) with the same mesh kind, so the same edit applies
to them, but writing tile zones from editzone is not built.

Not touched: `MapEnts.havokEntsShapeData` (brush models such as doors), static model collision (research 88: props),
the navmesh.

## Sun shadow

`rebake_sun_shadow: true` runs the existing stock sun bake (research 88 / fork f9c14d9f) at the stock sun vector,
exactly (no pitch / yaw round trip); the vision's fog sun direction and the worldspawn `sundirection` are not
rewritten. The casters are read from the zone in memory after the removal, and `stock_sun.cpp` now skips degenerate
triangles, so the removed triangles no longer cast. It needs ACTS (`acts` or `IW8_ACTS`) for the static models'
streamed geometry. Without it the stock compressed sun shadow stays, and the removed object's baked shadow stays on
the ground.

The bake is the converter's, not the stock one: on mp_shipment 88.3 MB (stock 50.6 MB), another tile layout, a 256-unit
tolerance (research 88 explains the choice). The control bake in the proof (same direction, nothing removed) shows
that the removal changes it: 417,230 caster triangles against 417,644.

## Occlusion (Umbra)

### What it is

Replay 1.20 has no other occlusion data for a stock world: `GfxWorld.dpvsPlanes` holds one DPVS cell on mp_shipment
(`cells`, `cellCasterBits` one entry each), and the visibility of world surfaces, static models, primary lights,
reflection probes and decal collections comes from the Umbra 3 query over the precomputed tome
`GfxWorld.umbraTomeData` (the query consumer at 0x18CCA80 sets the dpvs visibility bits by the object's user-ID class:
0 surface, 1 static model, 3 volumetric, 4 primary light, 5 probe, 6 decal collection; see `replay_render.cpp`). The
GPU passes cull again afterwards (frustum, Hi-Z, LOD).

The tome is an Umbra 3 `ImpTome` (version 0xD6000014; the field names of the IW7 headers, `src/games/iw7/structs.hpp`
`Umbra::ImpTome` / `ImpTile`, match its offsets). mp_shipment: 5,614,672 bytes; 10,483 objects (1,024 surfaces,
9,413 static model placements, 22 primary lights, 18 probe instances, 6 decal collections), one user ID per object;
4,089 tiles in a k-d tree (2,045 leaf tiles of 512 units with their own view tree, cells and portals; 238 coarser LOD
tiles of 1,024 and 2,048 units used at distance); 3,440 clusters; no gates; computation string `SO2 0.4 SH 4.000 BF 100
F 0`; checksum field 0xD15AB1ED ("disabled": the stock tomes carry no CRC).

The occluders are not a list in the tome: when the map was built, the surfaces with `GFX_SURF_IS_UMBRA_OCCLUDER`
(flag 64) and the static model collections with `STATIC_MODEL_COLLECTION_FLAG_UMBRA_OCCLUDER` (128) were voxelized and
the empty space between them became cells joined by portals. A query walks from the camera's cell through the portals;
what lies beyond solid space (no portal) is hidden. The records that make the proof's pallet an occluder:

- the occluder flag on 5 of its 6 surfaces: 22 `tw/dam_metal_black_dirty_01`, 229
  `tw/metal_aluminum_insulation_panels_01`, 409 `tw/fabric_insulation_fiberglass_01_yellow`, 411 `twc4/296n_297`, 544
  `twc4/47n_48n` (flags 67 = sun shadow + sun0 + occluder); surface 1 `tw/shadowcaster` has flags 3 (no occluder);
- the cells and portals of the tiles around the box, baked with those surfaces as solid: leaf tiles 2181
  (`-1024,1536,0..-512,2048,512`, 16 cells) and 2227 (`-1024,2048,0..-512,2560,512`, 10 cells), LOD tiles 1142 (13
  cells), 1156 (20 cells) and 654 (3 cells);
- the surfaces are also Umbra objects (targets: 1121, 3607, 3498, 3045, 3042), which only decides whether they are
  drawn; their triangles are degenerate now, so that no longer matters.

### What changes

The cells and portals cannot be edited for one object: their encoding (cell nodes, portal records, view trees, the
cluster graph and the LOD tiles that repeat the same space) is not decoded, and a wrong portal would hide or show
wrong things everywhere without anything offline to catch it. So `remove_world_surfaces` takes the safest switch the
converter already runs in game with: `"occlusion"`

| Value | What happens |
|---|---|
| `"auto"` (default) | when a removed triangle belongs to an occluder surface (flag 64), the stock tome is replaced (below); otherwise it stays |
| `"open"` | always replaced |
| `"stock"` | never replaced (the removed geometry keeps occluding) |

The new tome is `replayrender::BuildConservativeUmbraTome`, split out of `BuildUmbraTome` (the tome of every converted
map; the proof's Python port of it rebuilds the tome of a converted map written before the split, mp_scrtest, byte for
byte, and the editzone tome equals that port): the stock objects with the
stock user IDs in the stock order, one tile and one cell over the stock tome bounds (`m_treeMin`/`m_treeMax`), every
object spanning the whole tome, a valid CRC32C. Umbra then finds every object visible from anywhere; the GPU passes
do all culling. A tome with gates (`GfxWorld.numUmbraGates`, the tome's `m_numGates`), tome collections or several
user IDs per object is refused (the conservative tome has none; Warzone maps are not edited anyway).

Every other GfxWorld field stays stock, the surface flags too: a degenerate triangle draws nothing in any pass, so the
flag on an emptied surface has no effect, and the flag is a bake input (research 54: stock sets the static model bit
on 6,745 placements). mp_shipment: the tome goes from 5,614,672 to 648,000 bytes.

### Cost

- Map-wide, not local: nothing anywhere on the map is hidden by Umbra any more, and its object-contribution cull
  (small far objects) is gone too. The GPU passes (frustum, Hi-Z occlusion from the depth of what is drawn, LOD)
  still cull every surface and placement exactly, so the picture is right; the CPU submits more draws, lights, probes
  and decal collections each frame. All converted maps run this way; on mp_shipment (10,483 objects) it is the
  configuration the converted maps already have. Not measured in game.
- The primary lights, reflection probe instances and decal collections are always passed on (their vis bits come
  only from the query), so the light binning and the decal pass get all of them every frame.
- `"occlusion": "stock"` keeps the stock culling and the stale occluder (the old limitation).

Not chosen: clearing the occluder flag (the tome is baked; the flag changes nothing at run time for the removed
triangles); widening `ImpTile.m_portalExpand` of the tiles around the box (portals grow in their own plane only, so a
view through the removed object is not reliably opened, and its run-time use is not traced); replacing only the tiles
around the box with one-cell tiles (needs the portal and cluster encoding: the next step if the map-wide cost shows in
game).

## Editor

- Core (`src/core/stock_zone_edit.*`): class `iw8r_remove_world` (`kRemoveWorldClass`, `isRemoveWorldClass`); each
  brush or patch of such an entity is one box (`documentWorldRemovals`); entity keys `render` 0 and `collision` 0;
  worldspawn `iw8r_remove_world_sun` `rebake` (default) or `keep` (`stockWorldRemovalRebakesSun`);
  `stockWorldRemovalJson` makes the `remove_world_surfaces` object (the re-bake only when a box removes triangles),
  and `stockEditJson` takes it as its last argument. Core test `stock_zone_edit_world_removal`.
- The entity never reaches the entity text: `splitStockDocument` already leaves out every non-worldspawn entity that
  holds brushes.
- Entity definition in `data/iw8_entity_defs_overrides.json` (buildAccepted false: not built into own maps), keys in
  `docs/KEYS_REFERENCE.md` (section "Stock world removal").
- The Qt side (`prepareStockEdit`) is not wired yet; see the owner notes in the session report.

## Proof (`tools/stock_world_remove_test.py <work> --exe <zonetool.exe>`, mp_shipment, 35 checks, RESULT: PASS)

The box `(-960, 1815, 17.5)..(-820, 2140, 125)` holds a stack of insulation rolls on a pallet frame baked into the world:
414 triangles of 6 surfaces (`tw/fabric_insulation_fiberglass_01_yellow` 106 of 106, `twc4/47n_48n` 256 of 256,
`tw/metal_aluminum_insulation_panels_01` 22 of 22, `tw/shadowcaster` 12 of 12, `twc4/296n_297` 2 of 2,
`tw/dam_metal_black_dirty_01` 16 of 399); the ground (z 15 to 17) stays outside. Read back with ACTS from the written
zone:

- the surface table and the vertex positions are stock; exactly the 414 triangles are (i0, i0, i0), every other index
  of 1,256,001 is stock; the compressed sun shadow is stock without the re-bake;
- collision (decoded by `zonetool havokinfo <blob> --mesh <json>`, new): no triangle and no live convex shape inside
  the box; the 173,667 other triangles and 9,883 other convex shapes are there with the stock coordinates; same size,
  5,069 bytes differ;
- `"render": false` gives the same collision and no render change; `"collision": false` the same render change and
  the stock collision bytes;
- the re-bake keeps the stock sun vector, leaves visions and worldspawn alone, and differs from a control bake;
- occlusion: the stock tome is read (0xD6000014, checksum disabled, no gates) and the box's occluder surfaces and
  tiles are listed (section "Occlusion"); the report opens the occlusion for exactly the 5 occluder surfaces; the
  written tome equals the conservative tome rebuilt in Python from the stock user IDs and bounds (648,000 bytes): one
  tile, one cell, all 10,483 stock user IDs in the stock order, valid CRC32C, no gates; every other GfxWorld field
  is stock (the ACTS export without runtime addresses); `"render": false` and `"occlusion": "stock"` keep the stock
  tome byte for byte, `"collision": false` writes the same tome as the full removal, `"occlusion": "open"` writes it
  for a box that removes nothing, and the sun control bake (nothing removed, `"auto"`) keeps the stock tome;
- ACTS `--test` gives the stock result for every written zone (51 tested, 0 failed, 1 unavailable).

About 1 minute; it writes only into the work folder.

## What stays (limitations)

1. **Umbra occlusion is opened map-wide, not locally.** With an occluder removed (`"occlusion": "auto"`), the stock
   tome is replaced by the conservative one: nothing behind the removed object is culled wrongly any more, but Umbra
   culls nothing anywhere (section "Occlusion", Cost); the GPU passes still cull. The frame cost on stock maps is not
   measured in game; a local edit of the stock cells and portals is not built.
2. **Baked lighting.** Lightmaps, the light grid and reflection probes keep the removed object's contact shadow and
   bounce light (a dark patch on the ground where it stood).
3. **Holes.** The stock compiler did not keep faces hidden by the object (the ground under a crate, a wall behind
   it): taking the object out can open a hole into the void.
4. **Sun shadow** stays stock unless re-baked; the re-bake is the converter's bake (bigger, other tolerance).
5. **Static models** in the box are not touched (hide them with Hide Stock Props In Selection, research 88).
6. **Warzone maps**: collision tiles are refused, streamed render surfaces are skipped (counted as
   `surfaces_not_resident`), the sun re-bake is refused.
7. The list's `vertCounts` / `triCounts` / `convexCounts` keep the stock numbers (upper bounds).
8. The editor's stock world reference view still draws the removed geometry.
