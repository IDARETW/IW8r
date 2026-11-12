# Changes

Newest first. "Not seen in game" means built and checked offline (converter read-back, the game's own loaders through
ACTS, tests) but not yet played; [docs/TEST_CHECKLIST.md](docs/TEST_CHECKLIST.md) lists the in-game checks.

## 2026-11-12

Warzone and stock maps:

- **Loot points.** File > Edit Loot Points shows the loot points of a Warzone stock map as entities (Quarry 2,924,
  Verdansk 23,446); moved, added and deleted points are written back with the stock rules for clusters, zones and
  counts. Own maps take `iw8r_loot_point` entities, in one loot zone named by worldspawn `iw8r_loot_zone` (default
  `map_quarry`). Not seen in game.
- **New loot areas on stock maps.** `iw8r_loot_cluster` and `iw8r_loot_zone` brush entities add loot clusters and
  named loot zones after the map's own (converter editzone `loot_boxes`); loot points inside them join them. Proven
  offline on mp_br_tut and mp_br_quarry. Not seen in game.
- **Buy stations.** File > Edit Placed Objects moves and copies the buy stations of a stock map; own Warzone maps
  place `br_plunder_box` by name. The scriptable physics limits rule (two definition flag pairs, exact on 21 stock
  maps) makes 509 stock scripted objects buildable (was 317). Not seen in game.
- **Vehicles.** Vehicle spawn entities (ATV, tactical rover, SUV, cargo truck, helicopter) on an own map carry the
  stock vehicle and its model from mp_br_quarry. Not seen in game.
- **Streamed world.** File > Load Streamed World Around Camera draws the streamed world tiles of Quarry and Verdansk
  near the camera in the stock reference.
- **Added brushes on stock maps.** Brushes drawn on a stock map are built as models (with collision) and carried in
  on File > Rebuild Stock Zone. Not seen in game.
- **Hidden props lose their collision** on the small stock maps. Not seen in game.
- **Stock patch files.** The game ships patch files with real map data; every stock edit starts from the stock map
  with its own patch, also while an earlier edit is installed.
- **Stock terrain textured on every map.** The terrain blend materials of a stock map (`tw4/`, `twc4/`, named per
  map) are read from the map's own zone; they were checkered on maps outside the material catalogue (mp_br_tut: 346
  materials).
- **Game mode presets** with the private server's playlist settings, carried in a map or sent at F5.

Building:

- **Collision tiles.** Maps can keep their collision in one piece or as the game's streamed tiles (1,024 tile zones
  and a combined package, generated automatically): Edit > Preferences > Build: collision, or worldspawn
  `iw8r_collision`. The private client currently loads only the combined package of a custom map. Not seen in game.
- **Converter fixes**: a fresh rewrite of a stock zone kept every asset (Quarry lost a killstreak definition before);
  zone headers keep their transient file type.

Documentation:

- **Feature list pictures redone** (docs/features): taken on the real window system by `IW8Radiant.exe
  --feature-shots --out <dir>` (45 pictures, readable text, every view settled before the picture).

Tools for this work (read only on the game folder): `zonetool havokinfo`, `zonetool streaminfo`,
`tools/dw_disasm.py` (reads the recompiled game exe with its linker map), and one offline proof script per feature
under `tools/` (`*_test.py`).
