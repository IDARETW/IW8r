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
  offline on mp_br_tut and mp_br_quarry. The same entities work on your own maps (several clusters and named
  zones instead of one zone over every point). Not seen in game.
- **Buy stations.** File > Edit Placed Objects moves and copies the buy stations of a stock map; own Warzone maps
  carry `br_plunder_box` into the map (a custom map never loads the Warzone zone: research 93). The scriptable physics limits rule (two definition flag pairs, exact on 21 stock
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
- **New game types.** Tools > Game Modes > New game type script: a game type of your own under a new name, compiled
  into the map zone (converter c5e182f3). Every build checks its far calls against the stock script index (only files
  the game stores by number link: research 92). Built and ACTS-checked offline; not seen in game.
- **Mode rules.** Tools > Game Modes > Rules: max health, move speed, one weapon instead of the loadout and
  infinite ammo, applied to every player at each spawn by the map's own script, which runs after the game type's start
  (read from the game exe: docs/research/91). Built through the real pipeline offline; not seen in game.
- **Loot items on own maps.** A map with loot points carries the loot item definitions of its loot table from the
  Warzone zone, which a custom map never loads (research 93). Built and ACTS-checked offline; not seen in game.
- **Fix: "no circle" no longer breaks a Warzone map.** The generated map script called
  `br_gametypes::disablefeature`, a far call that cannot link (research 92) and would drop the match or exit the game
  at load. The map now sets only `level.br_circle_disabled`.
- **Build preflight fix**: methods called on `self` (`self takeallweapons()`) are no longer flagged as unknown
  functions.
- **Game mode presets** with the private server's playlist settings, carried in a map or sent at F5.

Editing:

- **Bridge Two Faces** (Brush menu): a new brush that spans two selected faces.
- **Blend By Slope** (Patch > Height Map): the terrain's slope written into its vertex alpha for layered materials.

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
