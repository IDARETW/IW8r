import io

# Docs 2026-11-14 18:40: mount adjacency (research 125 section 11); the texts that said "no adjacency" are put right.

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


patch('CHANGELOG.md', '''  (converter `a8114ae6`). The build log counts them;''', '''  (converter `a8114ae6`, `7076f8da`). A mounted player can change sides of thin cover: the two edges of a narrow face
  (a thin wall's end, a thin wall's top, up to 16 units) name each other, by the rule that gives exactly the stock
  maps' own tables (174 of 174 pairs on mp_m_speedball, 190 of 190 on mp_rust, 80 of 80 on mp_shipment). The build
  log counts the edges;''')
patch('docs/PLAN_REMAINING.md', '''NOT SEEN IN GAME. Not built: mantle edges (the trace path works), adjacency
    (sliding round a corner while mounted), edges from placed models.''', '''NOT SEEN IN GAME. Adjacency (changing sides of thin cover while mounted)
    is built too (section 11; buildMapEdgeAdjacency). Not built: mantle edges (the trace path works), edges from
    placed models.''')
patch('docs/ROADMAP.md', '''not seen in game; no adjacency, no edges from placed models))''',
      '''with the adjacency tables by the stock rule; not seen in game; no edges from placed models))''')
patch('docs/research/125_edge_list_mantle_mount.md', '''mantling keeps its trace path (section 2.1). Node sets are aligned to 128 bytes in the zone (the converter's own
reader, `asset_streams.cpp` `stream_mapedgelist`, mirrors the game's loader and refused 4). No adjacency tables: a
mounted player cannot slide round a corner onto the next edge (section 2.3).''', '''mantling keeps its trace path (section 2.1). Node sets are aligned to 128 bytes in the zone (the converter's own
reader, `asset_streams.cpp` `stream_mapedgelist`, mirrors the game's loader and refused 4). The adjacency tables
follow the index list (section 11).''')
s = read('docs/research/125_edge_list_mantle_mount.md')
nl = '\r\n' if '\r\n' in s else '\n'
s = s.rstrip('\r\n') + nl + '''
## 11. Mount adjacency: the rule, from the stock data (added 2026-11-14, main session)

Section 2.3 found that moving from a mounted edge to a neighbour reads `edgeAdjacencyMetadata` and `edgeAdjacency`,
and section 4 gave their layouts. What makes two edges neighbours was read off the stock tables (scratch scripts; the
rule is kept, in Python, in `tools/mount_edges_build_test.py` `predicted_adjacency`, which the proof runs against a
stock map every time).

What the stock tables hold (mp_m_speedball 174 records, mp_rust 190, mp_shipment 90):

- every record joins two mount edges (flags 1 with 1, or 11 with 11), and every record is answered: if A names B,
  B names A;
- the two edges are parallel; they are 4 to 15.75 units apart; one face of each has the same normal;
- 144 of 174 (speedball) join two upright edges, 30 two level ones: the two corners at the end of a thin wall, the two
  top edges of a thin wall;
- `start`/`end` and `adjStart`/`adjEnd` are the parts that lie side by side, each in its own edge's measure (0 at the
  first end point, 1 at the second, as half floats; 0x3C00 = 1), lower value first whichever way the edges run;
- an edge has at most two records (`mountCount` 0, 1 or 2).

The rule that gives exactly these tables. Two static mount edges are neighbours when all of this holds:

1. parallel within about 2.5 degrees (|cos| at least 0.999);
2. 0.5 to 16 units apart (the distance between the two lines);
3. a face of one and a face of the other have the same normal (dot above 0.99), and the step from one line to the
   other lies in that face's plane (within 0.5 units): they are the two long sides of one narrow face;
4. each edge's other face is turned away from the other edge (so the narrow face is solid between them: two crates
   10 units apart are no neighbours across the gap);
5. at least 4 units of their lengths lie side by side.

VERIFIED on data: the rule gives 174 pairs on mp_m_speedball and the table has the same 174, with the same parts
(within 0.02); mp_rust 190 and 190; mp_shipment 80 and the same 80 among static edges (its other 10 records involve
edges of brush models). With "parallel within 8 degrees" mp_rust would get one pair more that the table does not
have (two edges 3.3 degrees apart), hence 0.999.

The build writes the tables by this rule (`buildMapEdgeAdjacency` in `src/core/map_edges.h`): one word per edge
(first record in the low 23 bits, the count in bits 23 and 24), the records after the index list, both aligned to 4
bytes. An edge keeps at most three neighbours, the nearest first (the count has two bits). The proof reads the built
tables back and finds exactly the pairs the Python rule gives (the test map's 16-unit wall: its two top edges and the
corners at each end; the crate and the pillar none).
'''.replace('\n', nl)
write('docs/research/125_edge_list_mantle_mount.md', s)

patch('docs/PROGRESS.md', '''  - Not built: mantle edges, adjacency, edges from placed models' collision.''',
      '''  - Adjacency (changing sides of thin cover while mounted; research 125 section 11): buildMapEdgeAdjacency in
    map_edges.h by the rule that reproduces the stock tables exactly; converter 7076f8da writes the two tables
    (patch tools/patches/zt_map_edges_adjacency.py). The proof checks the rule against stock mp_m_speedball and the
    built tables against the rule.
  - Converter commits in the worktree: a8114ae6 (mount edges), 7076f8da (adjacency). LIVE STATE: see the end of this
    block.
  - Not built: mantle edges, edges from placed models' collision.''')
print('ok')
