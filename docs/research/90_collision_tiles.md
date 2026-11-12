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

## 6. Build plan (converter)

1. `BakeCollision` with tiles: world hulls and mesh triangles are assigned to the tile of their centre; anything
   wider than a tile stays in slot 0. One Havok blob per non-empty tile (same builder as the world shape, rooted at
   the shape), one shared tag table, then the world list with the extra slots.
2. Zones: all 1,024 `<map>_cg_NNNNN_tr.ff` and `<map>_cg_ls_tr.ff` from `CollisionTile` structures through the
   generic writer.
3. A `streaminginfo` asset in the main zone: files (map, tiles, combined), costs by the formulas above, the
   `collisiontile` transient assets, the tile count.
4. Editor: collision layout "one piece" (today) or "tiles" (automatic for a map wider than a set size, or forced);
   the package and the install carry the extra files.

Open before it can be trusted in game: static model collision in tile mode (stock keeps it in the tiles; the
converter keeps its lists in `col_map`), edge lists per tile, and whether a custom map's streaming info is honoured
the same way as a stock one.
