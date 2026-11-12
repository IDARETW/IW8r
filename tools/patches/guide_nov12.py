import io


def rep(p, pairs):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    for a, b in pairs:
        a = a.replace('\n', nl); b = b.replace('\n', nl)
        assert s.count(a) == 1, (p, s.count(a), a[:80])
        s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


rep('docs/USER_GUIDE.md', [
 ('''- **Brush flags**: Ctrl+M makes a brush detail''', '''- **Bevel and bridge** (Brush menu): Chamfer All Edges, Inset Selected Face (border and recess), and Bridge Two
  Faces: select two brush faces (Ctrl+Shift+click in the camera) and a new brush spans them, with the first face's
  material (a beam between two walls, a ramp between two ledges). One undo step each.
- **Terrain blending**: Patch > Height Map > Blend By Slope writes the terrain's slope into its vertex alpha (flat up
  to one angle, fully blended from another), so a layered material shows its second layer on the steep parts. Colours
  you painted (Patch > Vertex Paint Mode) keep their red, green and blue.
- **Brush flags**: Ctrl+M makes a brush detail'''),
 ('''- **Save Selection As Plain .map Prefab** writes the old single-file form. **Insert Prefab** places a `.map` file or a
  bundle (pick the `prefab.json` in its folder).''', '''- **Save Selection As Plain .map Prefab** writes the old single-file form. **Insert Prefab** places a `.map` file or a
  bundle (pick the `prefab.json` in its folder).
- **Sharing**: Export Prefab Bundle As Zip writes one bundle folder into a zip; Import Prefab Bundles From Zip puts the
  bundles of a zip into your prefab folder (zips made by Windows or other tools work too). An import never replaces a
  bundle you already have, and each bundle is checked before it lands.
- **Convert Plain .map Prefab To Bundle** wraps an old single-file prefab into a bundle beside it, with your own
  material and model files and the same origin, so existing placements line up.
- **Placement hints**: a bundle whose `prefab.json` says `"placement": {"surface": "wall", "yaw_step": 90}` turns to
  face out of the wall it is placed on (aim the camera at the wall and Insert Prefab). **Rotate Selected Prefab** turns
  a placement by its bundle's yaw step (90 for a plain prefab).'''),
])

s = io.open('docs/USER_GUIDE.md', encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
i = s.index('### Editing a stock map')
j = s.index(nl + '### ', i + 10)
section = s[i:j]
assert 'Relight' in section or True
add = (nl + '**Sun of a stock map**: File > Relight Stock Map Sun picks a new colour and intensity for the stock sun (worldspawn' + nl +
       '`iw8r_sun_color`, linear, and `iw8r_sun_intensity`); File > Rebuild Stock Zone writes them. The sun keeps its direction,' + nl +
       'because the stock map\'s baked sun shadows hold it. The rebuild report in the Console shows the old values.' + nl)
s = s[:j] + add + s[j:]
io.open('docs/USER_GUIDE.md', 'w', encoding='utf-8', newline='').write(s)
print('ok')
