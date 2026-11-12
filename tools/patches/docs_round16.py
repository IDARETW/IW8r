import io
p = 'docs/ROADMAP.md'
s = io.open(p, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
lines = s.split(nl)
hit = [i for i, l in enumerate(lines) if l.startswith('| 5.25 |')]
assert len(hit) == 1
cells = lines[hit[0]].split(' | ')
assert len(cells) == 5, len(cells)
status = ('done: explained, and already produced by the converter (2026-11-11, research 81). The heightfield is not an asset pool: it is an array in the render world (count at GfxWorld +0x3D80) whose entry holds bounds, a lookup matrix and the image *heightmap0, an 8-bit top-down picture of the highest solid surface. '
          'The game uses it only in the renderer, as the "outdoor" input of effect shaders, so rain is not drawn under roofs; no collision, physics, AI, audio, minimap or decal code reads it. Of 3,002 stock technique sets only three use it, for four rain materials of mp_shipment. '
          'Every editor-built map already gets a real one: the converter builds it from the opaque world triangles and the sun-shadow casters (checked in the built mp_iw8r_fix1: one entry, 256 x 256 image with real heights). World brushes and shadow-casting models block rain; sky, glass and brush models do not. '
          'No link to the impact, probe or killstreak symptoms. Optional later: finer pixels (stock is about 2.4 units per pixel, the converter 4.9 to 8); whether an editor map can use the stock outdoor rain materials is unknown. The engine evidence is from a disassembly scan, not decompiled code. NOT SEEN in game')
assert '|' not in status
cells[3] = status
lines[hit[0]] = ' | '.join(cells)
io.open(p, 'w', encoding='utf-8', newline='').write(nl.join(lines))

p = 'docs/PROGRESS.md'
s = io.open(p, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
a = '  - Still open: impacts (F.12), the script-played loop of an own sound (F.6), reverb, probes, game modes, killstreak'
assert s.count(a) == 1
add = [
 '  - RESEARCH 80 (decal volumes, 5.8): plan ready, converter producer of 300 to 450 lines + entity iw8r_decal; not',
 '    built. RESEARCH 81 (heightfield, 5.25): closed as explained; the converter already writes it (rain occlusion',
 '    only). Side fact from 81, not studied: editor-built maps have voxelTreeCount 0 where stock mp_shipment has 1.',
 '    The IDA MCP tools did not answer either agent; both read the exe on disk with a disassembler.',
]
s = s.replace(a, nl.join(add) + nl + a)
io.open(p, 'w', encoding='utf-8', newline='').write(s)
print('ok')
