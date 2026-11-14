import io
import sys

# Docs: mount edges also come from placed models' collision hulls (2026-11-14 evening).
# Argument 1: the sentence with the big-map numbers, for PROGRESS and the research note.

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


patch('data/iw8_entity_defs_overrides.json',
      '''"description": "Mount edges: where a player can rest a weapon on cover. The game mounts only on edges the map lists. The build finds them on the map's visible solid brushes: outside wall corners at least 32 units tall and ledge tops at least 16 units long, where nothing covers them and there is room to stand. Brushes side by side or stacked count as one wall. Clip brushes and placed models hide edges but give none. 0 builds the map without them. Not seen in game.",
          "required": false,
          "examples": [
            "0"
          ]''',
      '''"description": "Mount edges: where a player can rest a weapon on cover. The game mounts only on edges the map lists. The build finds them on the map's visible solid brushes and on the collision hulls of placed models: outside wall corners at least 32 units tall and ledge tops at least 16 units long, where nothing covers them and there is room to stand. Brushes side by side or stacked count as one wall. Clip brushes hide edges but give none. 1 (default): brushes and models. brushes: brushes only. 0: none. Not seen in game.",
          "required": false,
          "examples": [
            "brushes",
            "0"
          ]''')
patch('data/iw8_entity_defs_overrides.json', '''        "iw8r_mount_edges": {
          "type": "bool",''', '''        "iw8r_mount_edges": {
          "type": "string",''')
patch('docs/KEYS_REFERENCE.md',
      '''| `iw8r_mount_edges` | 0 or 1 (default 1) | `0` | Mount edges: where a player can rest a weapon on cover (open outside wall corners at least 32 units tall and ledge tops at least 16 units long of the map's visible solid brushes). 0 builds the map without them. Not seen in game. |''',
      '''| `iw8r_mount_edges` | `1` (default), `brushes` or `0` | `brushes` | Mount edges: where a player can rest a weapon on cover (open outside wall corners at least 32 units tall and ledge tops at least 16 units long). `1`: on the map's visible solid brushes and on placed models' collision hulls; `brushes`: brushes only; `0`: none. Not seen in game. |''')
patch('docs/USER_GUIDE.md',
      '''(32 units and taller) and ledge tops (16 units and longer) of the map's visible solid brushes; the build log counts
  them and View > Show Compiled Collision draws them in yellow. Placed models give no mount edges: put a brush where
  a model should offer one. Worldspawn `iw8r_mount_edges 0` turns them off. Mantling works without them.''',
      '''(32 units and taller) and ledge tops (16 units and longer) of the map's visible solid brushes and of placed
  models' collision hulls (a crate, a barrier; a round shape built from facets gives no corners); the build log
  counts them and View > Show Compiled Collision draws them in yellow. Worldspawn `iw8r_mount_edges brushes` keeps
  the brushes' edges only, `0` turns them off. A mounted player can change sides of cover up to 16 units thick.
  Mantling works without them.''')
patch('docs/HANDOVER.md', '''- Mounting: placed models give no mount edges (only brushes do), and there are no mantle edges: mantling uses the
  game's own trace, as before.''', '''- Mounting: a placed model without a collision hull (it gets a plain box) gives no mount edges, and there are no
  mantle edges: mantling uses the game's own trace, as before.''')
patch('docs/HANDOVER.md', '''be mounted (open wall corners and ledge tops of your brushes; View > Show Compiled Collision draws them in yellow).''',
      '''be mounted (open wall corners and ledge tops of your brushes and placed models; View > Show Compiled Collision draws
them in yellow).''')
patch('CHANGELOG.md', '''  stand and to hold the weapon. Brushes side by side or stacked count as one wall; clip brushes and placed models hide
  edges but give none.''', '''  stand and to hold the weapon. Brushes side by side or stacked count as one wall; clip brushes hide edges but give
  none. The collision hulls of placed models give edges by the same rules (a crate, a barrier), with a stricter
  corner rule so that a barrel built from facets gives none; `iw8r_mount_edges brushes` leaves the models out.''')
patch('docs/PLAN_REMAINING.md', '''Not built: mantle edges (the trace path works), edges from
    placed models.''', '''Edges from placed models' collision hulls are built too (MountSolid::model). Not
    built: mantle edges (the trace path works).''')
patch('docs/ROADMAP.md', '''with the adjacency tables by the stock rule; not seen in game; no edges from placed models))''',
      '''and on placed models' collision hulls, with the adjacency tables by the stock rule; not seen in game))''')
patch('docs/research/125_edge_list_mantle_mount.md',
      '''**Which edges** (`src/core/mount_edges.cpp`, `findMountEdges`). From the world's collision brushes that are solid and
have at least one drawn face (a clip or caulk hull gives none; a placed model's collision gives none; both still hide
edges of others):''',
      '''**Which edges** (`src/core/mount_edges.cpp`, `findMountEdges`). From the world's collision brushes that are solid and
have at least one drawn face (a clip or caulk hull gives none but still hides edges of others), and from the convex
pieces of placed models' collision hulls (not from the plain box a model without a hull gets). For a model piece the
two walls of a corner must be at least 60 degrees apart, so a barrel of eight or more facets gives no corners:''')
patch('docs/PROGRESS.md', '''  - Not built: mantle edges, edges from placed models' collision.''',
      '''  - Placed models: the convex pieces of a model's collision hull are sources too (CollisionBrush::modelHull,
    MountSolid::model: corner walls at least 60 degrees apart; the piece's own box from its windings). Worldspawn
    iw8r_mount_edges: 1 (default) / brushes / 0. BIG_LINE
  - Not built: mantle edges.''' .replace('BIG_LINE', BIG))
print('ok')
