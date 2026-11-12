import io
p = 'docs/ROADMAP.md'
s = io.open(p, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
lines = s.split(nl)
hit = [i for i, l in enumerate(lines) if l.startswith('| 5.8 |')]
assert len(hit) == 1
cells = lines[hit[0]].split(' | ')
assert len(cells) == 5, len(cells)
status = ('researched, not built (2026-11-11, research 80, which also corrects research 46). Decal volumes are not entities: the counts, material and mask tables are in GfxWorld, and the 44-byte placement records are in the map\'s transient zone data; stock mp_shipment has 46 volumes in 7 collections with 15 materials. '
          'The container layout is read in the game\'s loader; the bit rules of a record come from another build\'s decompile and reproduce all 46 stock records (collection bounds within 0.74 units), with the opacity curve and the picture\'s u / v direction still open. '
          'Three rules a producer must keep: a collection is drawn only when its visibility bit is set by a class-6 Umbra object; a collection count above zero with no visibility data crashes at client start; a decal material without its decal-volume material gives a bad index at load. '
          'The converter writes nothing for decals today (safe). Plan: a point entity iw8r_decal (origin, angles, size, material, optional opacity / feather / draw order), written by the compiler into world.json and packed by a converter producer of roughly 300 to 450 lines that mirrors the existing volumetric-box path. First step is offline: one stock scorch decal in the test fixture, build, dump and decode')
assert '|' not in status
cells[3] = status
lines[hit[0]] = ' | '.join(cells)
io.open(p, 'w', encoding='utf-8', newline='').write(nl.join(lines))
print('ok')
