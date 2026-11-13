import io


def patch(p, a, b):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == 1, (p, a[:60])
    s = s.replace(a, b, 1)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


patch('CHANGELOG.md', '## 2026-11-12', '''## 2026-11-13

- **Decals on props.** Stock decal volumes that find no world surface (stencils on crates, paint on the concrete
  barriers) are projected on the props, drawn in view within 4,000 units.
- **Viewport speed on big stock maps.** Hideout went from 56 ms to 17 ms a frame (Speedball 4.4 ms): layer and decal
  constants bound by offset instead of copied per draw, props culled on screen when outside the view or under a
  pixel, a lookup grid for the reflection probes, and layer triangles with no weight left out.
- **Grass.** Back faces of two-sided foliage are lit facing the viewer (the dark speckles are gone) and stock
  cut-outs use the game's 128/255 threshold. Speedball's grass is placed props; GPU ground clutter exists only on
  Verdansk's terrain (research 100).

## 2026-11-12''')
patch('docs/PROGRESS.md', '## Latest (2026-11-12 evening)', '''## Latest (2026-11-13)

- Prop decal volumes (only volumes that miss the world), viewport speed (Hideout 56 -> 17 ms), grass shading,
  research 100 (clutter). Bench: IW8R_PERF_OFF=probes,layers,propdecals,probegrid,instcull; IW8R_DECAL_LOG.
- Not done by choice: displacement (`dn` layers: 0-4.5 % of world triangles, about 2 inches of movement with the
  game's auto height scale; the game tessellates them, the editor shows their normal maps); GPU clutter on Verdansk
  (ACTS cannot export its terrain asset yet: "pointer_extent_required").

## Latest (2026-11-12 evening)''')
print('ok')
