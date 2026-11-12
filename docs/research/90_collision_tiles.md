# 90. Collision of big maps: streamed tiles and the combined package

Date: 2026-11-12. Owner request: "both transient tile collision and full collision support; the transient tiles
should be automatically generated". This file records how the stock big maps do it (read from the game files and from
the game's own code) and the build plan. Nothing here has been seen in game.

Tools used (all read-only on the game folder):

- `zonetool havokinfo <file> [--items] [--type <name>] [--dump <item>]`: objects of a Havok data blob.
- `zonetool streaminfo <zone> [<out.json>]`: the streaming info of a map zone as JSON.
- `tools/dw_disasm.py <symbol>`: disassembly of one function of the recompiled game exe, named with its linker map.
- Exports and blobs are under `out/collision_study`.

## 1. Two layouts

**Small maps** (every core map, also `mp_br_tut`): all collision is in the main zone. `col_map.havokWorldShapeData` is
a `HavokPhysicsShapeList` with one shape (`World Entity Main_Full`, an `hknpCompressedMeshShape` that holds the mesh
triangles and the brush hulls as shape instances). The streaming info names one file (the map) and
`transientCollisionTileCount` is 0. This is what the converter builds today.

**Big maps** (`mp_br_quarry`, `mp_donetsk`): the map is cut into a fixed grid of 32 x 32 tiles of 8192 units, centred
on the origin (it covers -131072 .. +131072 on X and Y). Tile index = `gridY * 32 + gridX`; a tile's `origin` is its
**centre** (`-126976 + grid * 8192`), z 0.

| File | Holds |
|---|---|
| `<map>.ff` | `col_map` with the world shape list (section 2), `map_ents`, the streaming info |
| `<map>_cg_NNNNN_tr.ff` (1,024 files, every index) | one `collisiontile`; a tile with content also has an `edgelist`, `physicsasset`s and `xmodeldetailcollision`s of its static models. An empty tile is 0x87 bytes of zone data |
| `<map>_cg_ls_tr.ff` | all 1,024 `collisiontile`s and their sub-assets in one package |

Who loads which (game code, section 4): the **host** asks for the combined `_cg_ls_tr` package (mode 2: all collision
resident), a **client** streams the numbered tiles around the player (mode 1). So a build must write both.

## 2. The data

`CollisionTile` (72 bytes): `name` ("collisiontile<N>"), `index`, `gridId[2]` (x, y), `origin`,
`staticModelCollision` (a compressed model list; null on an empty tile), `havokTileShapeData` (+ size),
`havokTileShapePointer` (run time), `worldCollisionShapeIdx`.

- `havokTileShapeData` is a self-describing Havok tagfile whose root is the `hknpCompressedMeshShape` itself (the
  same kind of shape as a small map's world shape, without the list around it). Coordinates are absolute, in Havok
  units of 32 game units, as everywhere else. The game loads it in place (`hkSerialize::InplaceLoad`) and makes one
  static body at the origin with identity rotation (`WorldCollision_InstantiateCollisionForCollisionTile`).
- The main zone's `HavokPhysicsShapeList` has one **slot** per shape: slot 0 is the map's own always-loaded shape
  (pointer in `shapes`, name in `shapeNames`; both arrays have one element), and one more slot per **non-empty tile**,
  ascending by tile index. The per-slot arrays (`shapeIndices`, `vertCounts`, `triCounts`, `minMaxes` (two vectors
  per slot), `shapeContents`, `convexCounts`) have all slots: Quarry 52 (1 + 51 tiles). `shapeIndices` is -1 for slot
  0 and the tile index for a tile slot. `shapeTagData` (the table that shape tags of triangles and hull instances
  index) is shared by the world shape and every tile.
- A tile's `worldCollisionShapeIdx` is its slot (tile 692 of Quarry: 36; an empty tile: 0). The game reads the
  slot's contents value through it when it makes the tile's body.
- Tiles overhang: geometry is assigned as whole objects, so a tile's bounds reach a few hundred units past its
  square. Quarry's slot 0 spans the whole grid (76 triangles, 735 hulls): the things that are not cut into tiles.
- Edge lists (ladders, mantles): `MapEdgeList_GetList` indexes `MapEnts.edgeLists` by a number that comes from the
  edge spatial tree, so the lists a map has are whatever its tree names. One list in the main zone stays valid.

## 3. The streaming info declares the tiles

Every stock map zone has a `streaminginfo` asset; the converter's maps have none (and load). Its `TransientInfo`:

| Field | Small map | Quarry |
|---|---|---|
| `transientInfoFlags` | 0 | 7 (1 collision dedupe, 2 low always-loaded flagging, 4 BR map) |
| `transientFiles` | 1: the map, type 0 | 1,894: the map (type 0), world tiles (types 2 and 0), `_cg_00000_tr` .. `_cg_01023_tr` and `_cg_ls_tr` (type 3), an infil (type 5) |
| `transientCollisionTileCount` | 0 | 1024 |
| `transientAssets` | 0 | 3,876; for collision: `collisiontile0` .. `collisiontile1023` (type 107), `mapedgelist_NNNNN` (type 116), tile physics assets (type 8); `mpParentIndex` 0xFFFFFFFF |
| `transientCosts` | none | one row per file: `residentPageSizes` = ceil(block 5 size / 65536), `tempPageSizes` = ceil((block 1 + block 2) / 65536), both at least 1 (checked on tiles 0, 465, 659, 692 and `cg_ls`); content count 0 for collision files |

`CL_TransientsCollisionMP_SetupFromAsset(zone name, TransientInfo)` reads the tile count from
`transientCollisionTileCount` (and two flag bits for the memory budget). `..._RegisterFileIndex` takes each transient
file name, skips the zone name and 4 characters (`_cg_`), and reads the rest as `ls_tr` (the combined package) or
`%05u_tr` (a tile number). So the names and the count are data of the map zone: a custom map can declare tiles.

## 4. Mode

`CL_TransientsCollisionMP_SetTransientMode`: after `DB_LoadLevelXAssets` on the hosting path the game sets mode 2
(combined package) and processes the files; a client that still has mode 0 at game start sets mode 1 (numbered
tiles). The private client keeps this and only falls back to tiles on Verdansk when the combined file is absent.

## 5. Writer check

The converter's zone writer reproduces stock tile zones: `IW8_REWRITE=1 verifyzone` gives byte-equal data for
`mp_br_quarry_cg_00000_tr`, `_cg_00692_tr` and `_cg_ls_tr`; `IW8_REWRITE=fresh` loads with the same assets.

## 6. What was built (2026-11-12)

Converter (fork 44d21398, live):

1. `BakeCollision` with `tiles`: every world hull and mesh triangle goes to the tile of its centre; anything wider
   than a tile or outside the grid stays in slot 0 (if nothing would stay there, the first tile's collision does:
   the list needs one shape). One Havok blob per tile with collision (rooted at its compressed mesh shape), one shared
   tag table, then the world list with one slot per tile (`shape_list_entry` without a shape; `add_shape_list`
   writes only real shapes into `shapes` / `shapeNames`).
2. All 1,024 `<map>_cg_NNNNN_tr.ff` and `<map>_cg_ls_tr.ff` from `CollisionTile` structures (index, grid, centre,
   data, slot), written through the generic writer with xfile version 0xFF3 and transient file type 3 as stock
   (`fastfile::transient_type`, kept by every rewrite now).
3. A `streaminginfo` in the main zone: the map, the 1,024 tiles and the package as files with costs from the written
   block sizes, `collisiontile0..1023` as transient assets, the tile count.
4. Switch: collision JSON `"collision_tiles": true`. The package check accepts all tile zones or none.

Editor: worldspawn `iw8r_collision` (`auto`, `one`, `tiles`) or Edit > Preferences > Build: collision (default
automatic: tiles for a map wider than 20000 units on X or Y; core `collision_layout.*`). The build log says which
layout was chosen and why. The package (deploy) carries the tile zones; validation tests the combined package (it
holds every tile) and treats the main zone's single "streaminginfo" failure as the known loader-test limit (the same
on stock `mp_br_quarry` and `mp_donetsk`), any other failure still fails the build.

Proofs: `tools/collision_tiles_test.py` (37 checks: byte-identical without the switch; with it the slots hold
exactly the one-piece build's vertices, triangles and hulls, every tile and the package read back with ACTS, header
type 3, streaming info files / costs / assets, 0 failed besides the known one), core tests
`collision_layout_*` and `compile_iw8_collision_tiles_switch`, and an editor build of a test map with
`iw8r_collision tiles` through the F7 path (1,031 files, build ok).

## 7. The private client accepts only the combined package of a custom map (finding)

The private client checks every unsigned zone it loads (`integration/replay/DB_ResetFileReader.cpp`,
`DB_IsNativeUnsignedReplayPackage`, read only): a zone must have a stored `IWffc100` header, version 11, xfile
0xFF7 and transient type 0 or 1, except a file ending in `_cg_ls_tr.ff` with xfile 0xFF3 and type 3. The built
tiles have exactly that header, so a custom map's **combined package passes and its numbered tile zones would be
refused**. The game's level load asks for the combined package (mode 2) before anything else, and only a client
that reaches its first frame with no mode set asks for tiles, so with this client the combined package is what
loads. Letting numbered tiles of custom maps load needs a one-line change in the client (also accept
`_cg_NNNNN_tr.ff` with 0xFF3 / type 3); that is the owner's project, not changed here.

## 8. Open

Static model collision stays in `col_map` in tile mode (stock keeps it in the tiles), edge lists stay in the main
zone, and nothing of this has been seen in game.
