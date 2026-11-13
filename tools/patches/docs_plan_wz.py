import io


def edit(p, a, b):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    if b in s:
        return
    assert s.count(a) == 1, (p, a[:60])
    io.open(p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


edit('docs/PLAN_REMAINING.md',
     '''- **Probes:** on layer and decal passes too, with the cb7[186] rough-surface fallback, and more than 128 boxes.''',
     '''- **Probes:** on layer and decal passes too (DONE), with the cb7[186] rough-surface fallback (research 107). More
  than 128 boxes: no MP map has more than about 70 (the cached probe files); only Warzone would, and its probes are in
  the streamed tiles (index.json: "reflectionProbeCount 5620 does not match its records"), so it moves to 1.5b.''')
edit('docs/PLAN_REMAINING.md',
     '''**1.6 Local lights.**''',
     '''**1.5b Warzone baked light per tile.** The main gfx_map lists every prop, but a prop's light grid box is in the
transient zone `worldTransient` (research 105 section 5.1), and the reflection probes are in the tiles too. The tile
lightmap job already decompresses each tile: cut its light grid block there as well (keyed by the transient index),
decode the tile probes, and draw tile props and tile ground with them. Done when Verdansk props and wet roads near the
raised camera use their own tile's light and probes.

**1.6 Local lights.**''')
print('ok')
