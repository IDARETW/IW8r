import io

# Docs, 2026-11-14 17:20: offline proof of the occlusion bake fix on the map that failed in game; what was ruled out
# for Shipment's popping objects; the GUI suite no longer reads a running game.

ROOT = 'E:/IW8-Radiant/'


def read(p):
    return io.open(ROOT + p, encoding='utf-8', newline='').read()


def write(p, s):
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s)


def patch(p, a, b):
    s = read(p)
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == 1, (p, s.count(a), a[:70])
    write(p, s.replace(a, b))


patch('CHANGELOG.md', '''## 2026-11-14

''', '''## 2026-11-14

- **The occlusion bake fix is proven on the map that failed.** The failed Cinder Rail build's occlusion data, read
  back by the editor's reader (which now follows the game's rule), fails the read-back test, as it failed in game.
  The same map built again with the fixed bake passes it: 432 cells, 259 objects (27 surfaces, 230 models), no view
  leaf without its cell, 94 % of the objects drawn on average from 216 cell centres. Not seen in game since the fix.
- **The GUI test suite never reads a running game.** One check called the Live Link reader; with the game open it
  read the game. It is now left out, with a line saying so, whenever a game from the game folder runs.

''')
p = 'docs/PROGRESS.md'
s = read(p)
nl = '\r\n' if '\r\n' in s else '\n'
a = '  STATE 2026-11-14 17:00 (newest; read first).'
assert s.count(a) == 1
note = '''  ADDED 2026-11-14 17:20. Suites with the game closed: core 181,970 checks / 1,377 tests, GUI 1,089, 0 failures.
  - Bake fix proven on the failed map: the failed zone (<game>\\.proxy\\backups\\mw120r-map-20261114-162449) read back
    with IW8R_TEST_GFXMAP_JSON fails umbra_bake_built_zone_reads_back; the same map rebuilt with the fixed converter
    (a private copy as mp_crbake, deleted again) passes: 432 cells, 259 objects, 94 % drawn from 216 cell centres.
  - Shipment's popping objects (owner 16:15), RULED OUT offline: missing pipeline states (the zone has 2,455 state
    records with 535 distinct hashes and main/techsets_mp_shipment.psob holds exactly 535 entries, written once at
    16:08); argument order (stock code image arguments are sorted by register, not by index, and the re-point keeps
    the register); a technique flag for depth readers (stock techniques with code image 0 have flags 0 like the
    rest); shaders refused by the driver (all states are in the cache). All four output components are written by
    every stock lit shader, so the splice leaves no alpha undefined. NOT RULED OUT: something the ink makes visible
    that was there before (LOD and cull-distance changes of props show much more with outlines); the thermal mask
    slot now holding the scene depth in a mode other than thermal (cb7[13].x == 2 in the stock shaders). If the owner
    reports it again: build Shipment once with the plain filter only (edits2.json) to separate the ink from the
    colour filter.
  - GUI test: the Live Link check without a game is left out while a game from the game folder runs
    (MainWindow::gameFolder, CompilePanel::gameRunningFrom); the suite must never read a running game.
  - Disk: E: was down to 4.7 GB; out/agent/occ/iw8build, cr_toon_build/iw8build, cr_bake_check, hz_a, hz_b deleted
    (all rebuildable, mine). 7.5 GB free.
'''.replace('\n', nl)
s = s.replace(a, note + a)
write(p, s)
print('ok')
