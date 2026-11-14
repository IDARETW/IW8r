import io

# Docs: the roadmap row and the research index after mount edges were built (2026-11-14).

ROOT = 'E:/IW8-Radiant/'


def patch(p, a, b):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    assert s.count(a) == 1, (p, s.count(a), a[:70])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


patch('docs/ROADMAP.md',
      '''**weapon mounting needs mount edges in the map's edge list and a built map has none, so mounting does not work on a built map: not built yet**; the record layouts and the minimum a converter must write are in the note)''',
      '''weapon mounting needs mount edges in the map's edge list: **built 2026-11-14** (the build finds open wall corners and ledge tops on the map's visible solid brushes, the converter writes them as the MOUNT bucket with a stock-shaped octree; proof `tools/mount_edges_build_test.py`; not seen in game; no adjacency, no edges from placed models))''')
patch('docs/research/README.md',
      '''mounting needs mount edges (a built map has none); query type bits, record layouts, what a converter must write |''',
      '''mounting needs mount edges; query type bits, record layouts, the rotation rule and the octree's grid index from stock data; section 10: what the build writes since 2026-11-14 (which brush edges, the metadata, the trees, the proof) |''')
print('ok')
