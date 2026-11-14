import io
import sys

# Docs 2026-11-14 18:10: mount edges on built maps (research 125 section 10), the overlay, the proof.
# Argument 1: the line about the big-map check (Cinder Rail copy), put into PROGRESS and the research note.

ROOT = 'E:/IW8-Radiant/'
BIG = sys.argv[1] if len(sys.argv) > 1 else ''


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


def append(p, text):
    s = read(p)
    nl = '\r\n' if '\r\n' in s else '\n'
    write(p, s.rstrip('\r\n') + nl + text.replace('\n', nl))


patch('CHANGELOG.md', '''## 2026-11-14

''', '''## 2026-11-14

- **Weapon mounting on built maps: the build writes mount edges.** The game rests a weapon only on edges the map
  lists; a built map listed none. The build now finds them on the map's visible solid brushes: outside wall corners
  (32 units and taller) and ledge tops (16 units and longer), only where nothing covers them and there is room to
  stand and to hold the weapon. Brushes side by side or stacked count as one wall; clip brushes and placed models hide
  edges but give none. The converter writes them into the map's edge list with the search tree stock maps use
  (converter `MOUNT_COMMIT`). The build log counts them; **View > Show Compiled Collision** draws them in yellow;
  worldspawn `iw8r_mount_edges 0` builds without them. Proven offline (`tools/mount_edges_build_test.py`: the built
  list is read back with the game's loaders, passes the same shape checks as a stock map's list, and a search box
  round every edge finds it the way the game searches). Not seen in game. Mantling is unchanged (it never needed
  edges).

''')

patch('docs/USER_GUIDE.md', '''- Weapon mounting (leaning a weapon on cover) does not work on a built map yet: the game needs mount edges that
  the build does not write (found 2026-11-14 by comparing built maps with stock ones; research 125). Mantling works.''',
      '''- Weapon mounting (resting a weapon on cover): the build writes the mount edges the game needs since 2026-11-14
  (research 125); rebuild the map. Proven offline, not seen in game yet. The edges are the open outside wall corners
  (32 units and taller) and ledge tops (16 units and longer) of the map's visible solid brushes; the build log counts
  them and View > Show Compiled Collision draws them in yellow. Placed models give no mount edges: put a brush where
  a model should offer one. Worldspawn `iw8r_mount_edges 0` turns them off. Mantling works without them.''')
patch('docs/USER_GUIDE.md', '''  **View > Show Compiled Collision** after a compile (solid cyan, player clip orange, ladders green).''',
      '''  **View > Show Compiled Collision** after a compile (solid cyan, player clip orange, ladders green, the edges a
  weapon can be mounted on yellow).''')

patch('docs/PLAN_REMAINING.md', '''  - weapon mounting on a built map (6.8, found 2026-11-14 by the field audit, research 124 / 125): OPEN, not built.
    The game takes mount spots only from mount edges in the map's edge list; the converter writes ladder edges only.
    To build: mount edges from the map's own brush edges (the layouts, the octree and the flags are in research
    125), with the rules checked against the stock maps' edges.''',
      '''  - weapon mounting on a built map (6.8, found 2026-11-14 by the field audit, research 124 / 125): BUILT 2026-11-14
    (research 125 section 10). Core src/core/mount_edges.* finds the edges on the world's visible solid brushes
    (wall corners, ledge tops; open parts only; joined along a line); src/core/map_edges.h (shared with the
    converter as map_edges.hpp) holds the record, the metadata rule and the octree; the collision file carries
    "mount_edges"; the converter writes bucket 0 with node sets and sets the used query types. Proof
    tools/mount_edges_build_test.py. NOT SEEN IN GAME. Not built: mantle edges (the trace path works), adjacency
    (sliding round a corner while mounted), edges from placed models.''')

s = read('docs/TEST_CHECKLIST.md')
nl = '\r\n' if '\r\n' in s else '\n'
a = '''- [ ] Occlusion bake after the fix of 2026-11-14 (research 129)'''
assert s.count(a) == 1
s = s.replace(a, ('''- [ ] Weapon mounting on a map built on or after 2026-11-14: at a wall corner and at a waist-high ledge the mount
      prompt shows and the weapon rests on the edge; mantling still works as before.
''').replace('\n', nl) + a)
write('docs/TEST_CHECKLIST.md', s)

append('docs/research/125_edge_list_mantle_mount.md', '''
## 10. What the build writes now (added 2026-11-14, main session)

Built, proven offline, not seen in game.

**Which edges** (`src/core/mount_edges.cpp`, `findMountEdges`). From the world's collision brushes that are solid and
have at least one drawn face (a clip or caulk hull gives none; a placed model's collision gives none; both still hide
edges of others):

- a wall corner: an edge within 15 degrees of upright between two faces within 15 degrees of upright, the faces
  between 45 and 135 degrees apart, 32 units or longer;
- a ledge top: an edge within 15 degrees of level between a top face (normal z above 0.7) and a wall face, 16 units
  or longer.

An edge is tested every 8 units. A part counts when the points 3 units out from the edge (along the middle of the two
normals, and in front of each face just inside the other) are in no other solid; a wall corner also needs the point 16
units out to be free; a ledge needs 20 units free above the edge on both sides of it and the point 16 units in front
of the wall, 8 below the edge, free (a curb is no ledge). Parts on one line with the same two faces, from any number
of brushes, are joined before the length is asked for. These rules are this editor's choice: only the result of
stock's own rules is known (section 7). The game checks at run time whether a player fits at an edge
(`BG_EdgeTuple_CheckMountCharacterSweep`), so an edge too many is only data. Not more than 60,000 (the longest are
kept).

**The metadata** (`src/core/map_edges.h`, `makeMapEdge`): flags 1 (mount) on every edge; the two faces ordered, or
the edge turned round, so that `cross(adjacent, other)` points along the edge (section 8); a ledge keeps its top face
as the adjacent face, which gives the stored angle 0 as on stock ledges; the open angle as a half float (a right
angle is 15944, the stock value).

**The trees** (`buildMapEdgeList`): one per bucket that has edges. The root cube is centred on the bucket's edges,
its side their longest extent. A node with more than 8 edges is split (to depth 10 at most) into a set of eight
children in the stock slot order (offsets 000, 001, 011, 010, 110, 111, 101, 100 for x, y, z); a leaf lists every
edge that crosses its cube; an empty leaf has flags 3; an inner node names its parent as `set * 8 + slot`, the
children of a root as 0xFFFFF8, as stock (checked on mp_m_speedball and mp_rust: 143 and 135 inner nodes). The ladder
tree stays one leaf, as it has been written (and seen working) before.

**The asset** (converter `map_zone.h`, `emitMapEdgeListBody`): `staticQueryTypes` = `dynamicQueryTypes` = the
buckets present (1 mount, 4 ladder); `MapEnts.edgeListUsedQueryTypes` the same, so bit 1 (mantle) stays clear and
mantling keeps its trace path (section 2.1). Node sets are aligned to 128 bytes in the zone (the converter's own
reader, `asset_streams.cpp` `stream_mapedgelist`, mirrors the game's loader and refused 4). No adjacency tables: a
mounted player cannot slide round a corner onto the next edge (section 2.3).

**The proof** (`tools/mount_edges_build_test.py`): a room with a crate, a wall of two brushes, a pillar of two
stacked brushes, a clip brush and a ladder is built; the edge list is read back from the zone with ACTS. The same
shape checks (roots, parent indices, grid index by slot, leaf ranges, set counts, every listed edge crossing its
leaf) pass on stock mp_m_speedball's list and on the built one; on the built one every crossing edge is listed, a box
round each mount edge finds it through the MOUNT tree and never through the ladder tree, the metadata follows the
rule above, and the expected edges are there with their joined lengths (none at a joint, none against the clip
brush).
''' + (BIG + '\n' if BIG else ''))

p = 'docs/PROGRESS.md'
s = read(p)
nl = '\r\n' if '\r\n' in s else '\n'
a = '  ADDED 2026-11-14 17:40 (newest).'
assert s.count(a) == 1
note = '''  ADDED 2026-11-14 18:10 (newest). MOUNT EDGES on built maps (plan 6.8, research 125 section 10).
  - Core: src/core/map_edges.h (header only, shared with the converter: MapEdge, makeMapEdge, mapEdgeNormals,
    buildMapEdgeList with the stock octree layout, queryMapEdgeList, validateMapEdgeList), src/core/mount_edges.*
    (findMountEdges over MountSolid planes; MountEdgeOptions; mountEdgesJson), compile_iw8.cpp (CollisionBrush::
    mountSource = solid + a drawn face + no ladder; collision json "mount_edges" before "dynamic_model_count";
    worldspawn iw8r_mount_edges 0), collision_preview (the overlay's yellow lines), entity defs.
  - Converter (worktree C:\\zsw-iw8r\\scriptables, patch tools/patches/zt_map_edges.py; `--header-only` re-copies the
    header): iw3_fastfile.cpp writes collision.native.edges ("MWEDGE01"), replay_havok reads it into
    BakeResult::mountEdges, map_zone.h emitMapEdgeListBody writes both buckets (node sets aligned to 128), MapEnts
    edgeListUsedQueryTypes = buckets present. Commit and deploy state: see the line below.
  - Tests: tests/mount_edges_tests.cpp (4), compile_iw8_mount_edges, collision preview; proof
    tools/mount_edges_build_test.py (in run_offline_proofs.py).
  - BIG_LINE
  - Not built: mantle edges, adjacency, edges from placed models' collision.
'''.replace('BIG_LINE', BIG or 'big-map check: not run').replace('\n', nl)
s = s.replace(a, note + a.replace(' (newest)', ''))
write(p, s)
print('ok')
