# 129. Why the baked occlusion tome failed in the game (mp_cinderrail_t, 2026-11-14)

**Why.** The first game test of the occlusion bake (`iw8r_umbra_bake`, research 110 / 121) failed. World surfaces
came and went with the camera angle (once the whole ground under the player was gone while buildings were drawn).
Placed models were never drawn, but their sun shadows were. The editor's own conservativeness test passed on the
same tome. This note finds what the game reads differently. Read only; nothing was built, installed or run in game.

**Labels.** VERIFIED = read in the tome bytes or in machine code (cited, `game_dx12_ship_replay_dw.exe` with its
linker map, capstone through `out/agent/umbra110/scan.py`; the IDA bridge was down). INFERRED = from reasoning.

**Data.** The failing zone `.proxy/backups/mw120r-map-20261114-162449/mp_cinderrail_t/mp_cinderrail_t.ff` and the
stock zones mp_shipment, mp_rust and mp_m_speedball, each exported with ACTS (`-a gfx_map`), tome cut out of
`umbraTomeData`. The bake: 385,632 bytes, 259 objects, one tile, 432 cells, 2,902 portals, 310,671 view tree nodes,
map width 10 bits.

## 1. The cause: the game reads the view tree's leaf values in another format

The tile flags byte (`ImpTile +0x2C`, low 8 bits) has a bit the game tests when it turns a view tree leaf into
cells: **bit 2**.

- `Umbra_BufferAllocator_probeFreeRun` (DW 0x142007B60, really "read a leaf's map value") reads `mapWidth` bits at
  `leaf x mapWidth`, then `test (~(flags >> 2)) & 1` (0x142007BD9-0x142007BE5). When bit 2 is **clear**, a value
  with its top bit set becomes `(v & (mask >> 1)) | 0x80000000`, and all ones becomes -1 (no cell). VERIFIED.
- `Umbra_KDTree_lookupLeafRange` (0x1420079D0) tests the same bit (0x1420079EB-0x1420079FB). VERIFIED:
  - **Bit 2 set** (0x142007B00): the value is a u32 offset into `m_cellIndices`; cells are read until a negative
    word. This is the format research 110 section 7 describes, the one the editor decoder and the bake use.
  - **Bit 2 clear** (0x142007A01): a value with the top bit clear **is the cell index itself** (one cell). A value
    with the top bit set walks a small tree of 8-byte nodes in `m_cellIndices` (two 16-bit cells or child links per
    node, bits 30/31 of the low word say which).
- The camera start-cell search `Umbra_PortalCuller_testObjectTileOverlap` (0x142001AE0) calls this lookup at each
  view leaf (0x142001E76) and then reads the 36-byte cells it returns (0x142001E9C). VERIFIED.

What the tomes hold (VERIFIED):

| | Tile flags | Leaf value means (game) |
|---|---|---|
| Every stock leaf tile (shipment 2,045, rust 479, speedball 823) | 5 (bits 0, 2) | offset into the cell index list |
| Every stock LOD tile | 4 | (no view tree) |
| Converter one-cell tome | 3 (bits 0, 1) | the cell itself: value 0 = cell 0, correct by luck |
| **The bake** | **3** | **the cell itself, but the bake wrote list offsets** |

The bake copied the one-cell tome's flags (3) and wrote stock-format values (offsets into `[cell, 0xFFFFFFFF]` runs,
so offset = 2 x the order of first appearance). Replaying the game's rule over every one of the bake's view leaves
(VERIFIED, script over the tome):

- Leaves inside a free cell other than cell 0 (cell 0 is the big region at offset 0): **100 % get a wrong cell**
  (111,495 leaves a wrong but existing cell, 1,286 an index past the 432 cells, which reads portal bytes as a cell,
  1,727 a top-bit value that walks garbage "tree nodes").
- Only cell 0 (offset 0) comes out right, as in the one-cell tome.

So the game starts its portal walk in an unrelated cell almost everywhere in the play space.

## 2. The two symptoms

- **(a) Surfaces come and go with the angle.** The walk starts in the wrong cell, so the portals it rasterizes are
  those of a far-away cell seen from the real camera. What it reaches changes with the view direction. INFERRED from
  section 1. The ground is listed by most cells (26 surfaces, 9,072 list entries, about 21 per cell; VERIFIED), so it
  vanishes only when the wrong walk reaches little or nothing (an out-of-range or garbage start cell, or portals that
  project behind the camera). Objects listed by a reached cell always pass the object test because every object has
  the scene-sized box.
- **(b) Models never drawn, shadows drawn.** A static model is listed only by the few cells its box touches (230
  models, 349 entries; VERIFIED). A walk that starts in the wrong cell rarely reaches exactly those cells next to the
  real camera. INFERRED: same cause, no second fault found. Sun shadows come from the shadow query, not this camera
  query, which is why they stayed.
- **The user IDs are not the problem.** Class 1 IDs are `0x10000000 + index`, one per object (no `m_userIDStarts`),
  the same IDs as the one-cell tome that draws everything. Stock shipment also has one ID per object. VERIFIED.
- **The object lists are read as the bake writes them.** `Umbra_PortalCuller_rasterizeTileCoverage`
  (0x1420004B1-0x142000539): entry width = `m_listWidths` bits 0-4 + bits 5-9, start bit = cell `+0x08` x width,
  count = cell `+0x0C`. VERIFIED.

## 3. Other differences from stock (not the cause; check after the fix)

Every value below is in the bake and in no stock tome (VERIFIED by survey). None is needed to explain the failure.

- Cell flags `0x80000000` only: stock leaf cells have bit 0 and a one-hot size (0x10..0x2000). The start-cell search
  reads `((flags >> 1) & 0xFFFF) + 1` x a tile-scale constant and keeps the maximum (0x142001EC8-0x142001EE8;
  INFERRED: a camera-cell size used by the query). The bake gives 1 quantum.
- Cell cluster `0xFFFFFFFF` and `m_numClusters` 0 (stock: LOD cells only).
- Internal portal growth 0 and tile `m_portalExpand` 0 (stock: growth one-hot, at least 2; expand 4 or more).

## 4. The fix

In `umbrabake::bake` (`src/core/umbra_bake.h`, copied to the converter as `umbra_bake.hpp`), the last tile write:

- **Write tile flags 5, not 3:** `put(t, tile + 0x2C, uint32_t(uint32_t(tileBytes) << 8 | 5u));`. This is the stock
  leaf tile value and makes the game read the leaf values as list offsets, which is what the bake writes. Bit 1 is
  set on no stock tile; no game read of it was found (all `+0x2C` reads in 0x141FEE000-0x14200B000 checked).
- Alternative (keeps flags 3): write each leaf's cell index itself, with `mapWidth = bitsFor(cellCount) + 1` so the
  top bit is never set, and no index list. Flags 5 is preferred because it matches stock.
- `BuildConservativeUmbraTome` can stay as is (value 0 = cell 0 in both formats).
- Optional, to look like stock: cell flags `0x80000001 | one-hot size`, a non-zero portal growth. Test separately.

## 5. The offline check that would have caught it

- The editor decoder (`UmbraTome` in `src/core/umbra_tome.cpp`, point to cell at about lines 660-676 and 820) ignores
  the flags and always reads list offsets. Make it follow the game: when tile flags bit 2 is clear, a value with the
  top bit clear is the cell, all ones is none, otherwise the 8-byte node walk of 0x142007A17-0x142007AD6. Then the
  existing conservativeness test (`tests/umbra_bake_tests.cpp` with `IW8R_TEST_GFXMAP_JSON` on this zone) fails.
- Add a check in `validate()` and in the bake tests: for every view leaf, the cell the game's rule returns is in
  range and its box contains the leaf (or it is the nearest cell when the leaf is in no cell). On this tome it fails
  for every leaf outside cell 0.
- Add a rule: a tile with a view tree must have flags 5 (the only value with a view tree on stock).
