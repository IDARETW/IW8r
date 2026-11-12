import io


def load(p):
    s = io.open(p, encoding='utf-8', newline='').read()
    return s, ('\r\n' if '\r\n' in s else '\n')


def rep(p, pairs):
    s, nl = load(p)
    for a, b in pairs:
        a = a.replace('\n', nl); b = b.replace('\n', nl)
        assert s.count(a) == 1, (p, s.count(a), a[:80])
        s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


rep('docs/features/FEATURES.md', [
 ('''- Chamfer All Edges and Inset Selected Face: `Brush` menu. A bridge between two faces is not built.''',
  '''- Chamfer All Edges, Inset Selected Face and Bridge Two Faces: `Brush` menu. Bridge takes two selected brush faces (Ctrl+Shift+click) and adds the convex brush that spans them, with the first face's material (a beam or a ramp between two walls).'''),
 ('''and runs thermal and hydraulic erosion. Slope blend values are computed but not written as vertex colour.''',
  '''and runs thermal and hydraulic erosion. Blend By Slope writes the slope into the terrain's vertex alpha (flat up to one angle, fully blended from another), so the second layer of a layered material shows on the steep parts; colours already painted keep their red, green and blue.'''),
 ('''- Bridge two faces (row 2.23). The exposed-parameter editor and bake-to-model for prefabs (row 8.8). Slope blend colour on patches (row 8.14).
''', ''''''),
])

p = 'docs/ROADMAP.md'
s, nl = load(p)
lines = s.split(nl)
for i, line in enumerate(lines):
    if line.startswith('| 2.23 |'):
        assert '; bridge two faces open |' in line, line[-200:]
        lines[i] = line.replace('; bridge two faces open |', '; Brush > Bridge Two Faces (2026-11-12: the convex hull of two selected faces, first face\'s material; core bridgeFaces / bridgeSelectedFaces, test bevel_bridge_two_faces) |')
    if line.startswith('| 8.14 |'):
        a = 'slope blend values are computed but patches carry no vertex colour yet'
        assert a in line, line[-200:]
        lines[i] = line.replace(a, 'Patch > Height Map > Blend By Slope writes the slope blend into the vertex alpha of the selected terrain (2026-11-12, core applySlopeBlendAlpha; painted colours keep their RGB; one undo step)')
io.open(p, 'w', encoding='utf-8', newline='').write(nl.join(lines))

rep('CHANGELOG.md', [
 ('''Building:
''', '''Editing:

- **Bridge Two Faces** (Brush menu): a new brush that spans two selected faces.
- **Blend By Slope** (Patch > Height Map): the terrain's slope written into its vertex alpha for layered materials.

Building:
'''),
])
print('ok')
