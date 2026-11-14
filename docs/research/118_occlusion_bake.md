# 118. An occlusion bake for converted maps: an Umbra tome with real cells and portals

**Why.** Plan 2.4. A map built by the editor carries the converter's one-cell Umbra tome: every object is listed by
the single cell, so Umbra hides nothing and the GPU passes do all culling. That always works, but a map with rooms
and streets draws everything behind its walls. This note records a bake that gives such a map real cells and
portals, how it was checked without the game, and what it does on Cinder Rail.

**Answer in one paragraph.** The bake cuts the map's free space into boxes along the faces of its opaque box
brushes; each free box is a cell, touching cells are joined by a portal over their whole shared rectangle, and a
cell lists the objects whose real box touches it. The result is written in the layout of the converter's proven
one-cell tome, with more cells. The editor's own reader of stock tomes (research 110) parses it and passes it through
every structural check, and a ray-casting test never finds an object that a straight line reaches but the tome
hides. It is **off by default** (worldspawn `iw8r_umbra_bake 1`, Compile > Occlusion bake) and **not seen in game**.

**Labels.** VERIFIED = checked by the reader, the tests or a real build. INFERRED = reasoned, not run in the game.

## 1. Design decisions, and why

- **Objects keep scene-sized bounds.** The owner's playtest of 2026-11 showed Umbra's contribution cull dropping
  props when objects carried their real bounds (`replay_render.cpp`, comment in `BuildConservativeUmbraTome`). So an
  object is hidden only when no cell that lists it is reached; nothing is culled by size or by its own box.
- **One leaf tile, flags 3, no clusters**, as the one-cell tome that the game has been running. Cells have no
  cluster (0xFFFFFFFF) and the flag word 0x80000000, as there.
- **Occluders are boxes.** Only opaque brushes that are axis-aligned boxes count (a portal is an axis-aligned
  rectangle in tile quanta). Their faces move inward to a grid of about 4 units, so a wall is never thicker in the
  tome than in the map.
- **A camera is never without a cell.** The view tree covers the tile's cube; a leaf inside a wall or outside the
  map lists the nearest cell (INFERRED need: what the game does for a point with no cell is not known).
- **Fallbacks.** No occluder, or a bake that fails: the one-cell tome. More cells than the limit (4096): the smaller
  occluders are left out.

## 2. The layout written (VERIFIED by `UmbraTome::parse` / `validate`, research 110 field names)

| Part | Content |
|---|---|
| Header | as the one-cell tome; tree box = the tile's cube, bounds = the scene box, 0x168 = the tile's coordinate scale |
| Objects | bounds and distance boxes = the scene box (near 0, far infinite); one user ID each; list entries of `ceil(log2 n)` bits |
| Tile | a cube from the scene's minimum corner, 65,536 quanta a side; `m_tileBounds` 0..65535 on each axis |
| Cells | 36 bytes: portal range, object list range, cluster none, flags 0x80000000, bounds in quanta |
| Portals | 16 bytes: `face << 29 | 0x02000000` (within the tile), `target cell << 16 | plane`, two rectangles; each has its reverse |
| View tree | 2-bit codes in breadth-first order with the rank directory; splits at midpoints; a map entry per leaf into the cell index lists (ended by 0xFFFFFFFF) |

The view tree splits a node along an axis on which a boundary of a cell it touches lies strictly inside the node,
until the leaf lies in one cell. **A leaf never lists several cells**: every cell run of every stock tome holds
exactly one cell (research 110, the view tree walk), so what the game does with a longer run is unknown. When the
tree would outgrow its node budget (1,000,000) the space is cut again with half the cells, until it fits.

## 3. Checks without the game (VERIFIED)

- `tests/umbra_bake_tests.cpp`:
  - two sealed rooms: each sees its own object and the "everywhere" one, not the other room's; a camera inside the
    wall still has a cell;
  - a wall with a doorway: the room beyond is reached;
  - sixteen rooms with doorways, 48 objects, 60 random cameras: 472 object sightings by ray (to the centre and the
    eight corners of each object's box, against the occluder boxes), **0 missed** by the tome's query, 50 % of the
    objects hidden on average; the view tree has 12,103 nodes, so its rank directory is exercised;
  - limits: no objects, a flat scene, small occluders, the cell budget, an object buried in an occluder.
- The query used is the editor's stand-in for Umbra's portal walk (research 110 section 12): it clips the view to
  each portal's screen rectangle and can only show more than an exact walk, never less. The ray test bounds it from
  the other side.

## 4. A real build (VERIFIED: Cinder Rail copy `out/agent/occ/mp_cr_occ.map`, converter x64-zt 24d1aec7)

- The editor wrote 1,816 occluder boxes (25 opaque brushes are not boxes, 9 brushes are not opaque).
- With 4,096 cells allowed the view tree needed more than its 1,000,000 nodes, so the converter cut again with half
  the cells: 200 occluders used, 1,245 cells, 8,048 portals, 746,547 view tree nodes, 240 objects in 12,806 list
  entries, a tome of 1,007,504 bytes. All five zones pass the game's loaders (ACTS).
- Read back from the built zone (ACTS `gfx_map` export, test `umbra_bake_built_zone_reads_back` with
  `IW8R_TEST_GFXMAP_JSON`): it parses and validates, every portal has its reverse, every object is listed, and none
  of the 373,274 view leaves lists several cells. The objects are 8 world surfaces and 230 static models. From the
  centres of 208 cells, 85 % of the objects are drawn on average.
- **What that means.** The converter merges the world into a few large surfaces (8 here), each touching most cells,
  so the world itself is hardly culled; the gain on this map is the static models behind walls. A map of closed
  rooms gains more (section 3: 50 %).

## 5. Open

- **In game:** everything. In particular whether the game's Umbra accepts several cells in a tile of flags 3 without
  clusters, internal portals with no growth, and a view leaf inside a wall that names the nearest cell (stock tomes
  leave such a leaf empty).
- Occluders that are not boxes (slanted roofs, terrain), and models as occluders.
- The world's merged surfaces limit what can be hidden; splitting them by cell is a converter change.
- Gates (doors that open) and LOD tiles are not written.
