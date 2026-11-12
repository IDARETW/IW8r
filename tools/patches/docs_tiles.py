import io

def load(p):
    s = io.open(p, encoding='utf-8', newline='').read()
    return s, ('\r\n' if '\r\n' in s else '\n')

def save(p, s):
    io.open(p, 'w', encoding='utf-8', newline='').write(s)

def rep(s, nl, a, b, p):
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == 1, (p, s.count(a), a[:70])
    return s.replace(a, b)

p = 'docs/research/90_collision_tiles.md'
s, nl = load(p)
s = rep(s, nl, '''## 6. Build plan (converter)

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
''', '''## 6. What was built (2026-11-12)

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
''', p)
save(p, s)

p = 'docs/ROADMAP.md'
s, nl = load(p)
lines = s.split(nl)
idx = next(i for i, l in enumerate(lines) if l.startswith('| 13.6 |'))
new_rows = [
    '',
    '## L14. Big maps',
    '',
    '| # | Item | Status | Size |',
    '|---|---|---|---|',
    '| 14.1 | Collision in one piece or as the game\'s streamed tiles (owner request 2026-11-12: both, tiles generated automatically) | done offline (2026-11-12, research 90): the converter cuts the world collision into the 32 x 32 grid of 8192-unit tiles, writes the 1,024 tile zones, the combined package and the map\'s streaming info (fork 44d21398); the editor picks the layout (worldspawn `iw8r_collision`, Preferences, automatic above 20000 units), packages and validates the extra files. Proofs: `tools/collision_tiles_test.py` (37 checks), core tests, an F7 build of a tiled test map. The private client accepts only the combined package of a custom map (research 90 section 7). Not seen in game. | L |',
    '| 14.2 | Stock patch files with real map data (owner note 2026-11-12) | done offline (2026-11-12): every stock map edit reads the zone with the game\'s own patch; while an edit is installed, rebuilds and File > Open IW8 Stock Map read the stock patch saved at install (`editzone` `stock_patches`, fork 9a86b345; core `installedStockPatchOverrides`). Proof: `tools/stock_patch_test.py` (a second rebuild with an edit installed equals the first, byte for byte). | M |',
]
lines[idx + 1:idx + 1] = new_rows
save(p, nl.join(lines))

p = 'docs/USER_GUIDE.md'
s, nl = load(p)
s = rep(s, nl, '''### Game modes of your map
''', '''### Big maps: collision in tiles

A Warzone-size map keeps its collision the way the stock big maps do: cut into the game's grid of 32 x 32 tiles of
8192 units. A client loads the tiles near the player; a host loads the combined package. The editor makes the tiles
by itself from the same brushes and models.

- Edit > Preferences > Build: collision. Automatic (the default) uses tiles for a map wider than 20000 units on X or
  Y and one piece for anything smaller. You can also pick always one piece or always tiles.
- A map can decide for itself: worldspawn key `iw8r_collision` with `auto`, `one` or `tiles`.
- The build log says which layout was used. A tiled map's package has 1,024 more small files and one combined
  package; they are copied with the map.
- The private client currently loads only the combined package of a custom map; that is enough to play. Not seen in
  game.

### Game modes of your map
''', p)
save(p, s)

p = 'docs/TEST_CHECKLIST.md'
s, nl = load(p)
s = rep(s, nl, '''## In game: an edited stock map (2026-11-12), not seen in game
''', '''## In game: a map with collision in tiles (2026-11-12), not seen in game

- [ ] Open any small map, set worldspawn `iw8r_collision` to `tiles`, build (F7). The log has "collision is built as
      streamed tiles"; the package folder has `<map>_cg_ls_tr.ff` and 1,024 `<map>_cg_NNNNN_tr.ff`.
- [ ] Play it (F5): walk on the floor, jump on boxes, shoot walls. Say whether you fall through anything. If the map
      does not load, set the key to `one` and build again, and tell me.

## In game: an edited stock map (2026-11-12), not seen in game
''', p)
save(p, s)

p = 'docs/PROGRESS.md'
s, nl = load(p)
s = rep(s, nl, '''    OPEN for Warzone now: vehicles, whether common_br_mp loads for an own map, zones / clusters on an own map, new
''', '''    STOCK PATCHES + COLLISION TILES (2026-11-12 late, roadmap L14, research 90): converter live at 44d21398 (old exes
    zonetool_927eb63a / 9a86b345.exe). editzone "stock_patches" reads a zone with the saved stock patch while an edit
    is installed (stock maps ship .fp files with real map data); the editor passes the saved patches
    (installedStockPatchOverrides) and opens stock maps from a clean copy while an edit is installed
    (stockZoneForReading). Collision tiles: collision JSON "collision_tiles" (converter cuts the 32 x 32 grid, writes
    1,024 tile zones + <map>_cg_ls_tr + streaminginfo); editor core collision_layout.*, worldspawn iw8r_collision,
    Preferences "Build: collision", package/validation of the tile zones. New converter commands: havokinfo,
    streaminfo; tools/dw_disasm.py reads the recompiled game exe with its linker map (read only). FINDING: the
    private client accepts only <map>_cg_ls_tr of a custom map's tile zones (research 90 section 7).
    REPOSITORY: the project is in the private GitHub repo IDARETW/IW8r (branch main); commit and push finished work
    there, no co-author trailer. docs/features/FEATURES.md is the feature list; its pictures come from
    IW8Radiant.exe --feature-shots (src/qtapp/feature_shots.cpp, real fonts, off screen).
    OPEN for Warzone now: vehicles, whether common_br_mp loads for an own map, zones / clusters on an own map, new
''', p)
save(p, s)
print('ok')
