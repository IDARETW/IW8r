import io

# Docs for the coloured bounce and the later bounces of the bake (2026-11-14), and the plan's "Open:" lines, which
# are settled by the decision table and now say so where they stand.

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


patch('CHANGELOG.md', '''## 2026-11-14

''', '''## 2026-11-14

- **Baked light bounces in colour, and more than once.** The lightmap bake (Compile > Bake lightmaps) and the light
  grid that lights models, players and the viewmodel used to bounce every surface as 45 % grey, once. Now light
  leaves a surface in that surface's own colour (the average of its material's colour image: a red wall tints the
  floor beside it), and the bounces after the first are added by an estimate: the first bounce times
  1 / (1 - colour x how enclosed the spot is), so a closed room is lighter than before and open ground is unchanged.
  View > Game Lighting previews the same bake. The build log says how many materials gave a colour. Core tests check
  the colour ratio and the series exactly; the lightmap build proof passes. In game: not checked.

''')
patch('docs/PROGRESS.md', '''  The web list of plan 3.3 / 3.4 is done (the Vision Editor form closed it).''', '''  Bake (plan 1.11): BakeSurface::albedo / triAlbedo, BakeSettings::laterBounces (on), bakeAlbedoFromRgba (core
  lightmap_bake); compile_iw8.cpp bounceOf(material) from the library's colour image, used by the lightmap bake and by
  the light grid's rays (triangle id -> gridAlbedo); viewport: GameBakeInput::Poly::albedo, gatherGameBake(albedoFor),
  CameraView::bakeAlbedoFor (a 32 px level of the DDS, or the image file), the bake key takes the count of loaded
  textures so a bake is redone when images arrive. The light grid still bounces once.
  The web list of plan 3.3 / 3.4 is done (the Vision Editor form closed it).''')
patch('docs/USER_GUIDE.md', '''- **Occlusion bake** (Compile panel, off by default):''', '''- **What the bake bounces.** Light that hits a surface goes on in that surface's colour (the average colour of its
  material), and the bounces after the first are estimated from how enclosed a place is: a room with one window is
  lit softly all through, and a coloured wall tints what is near it.
- **Occlusion bake** (Compile panel, off by default):''')

P = 'docs/PLAN_REMAINING.md'
s = read(P)
nl = '\r\n' if '\r\n' in s else '\n'
cut = s.index('## Open points closed by decision')
head, tail = s[:cut], s[cut:]
a = 'per cell, 56 directional samples). Open: the bake items below, probes, the offline parity proof.'
assert head.count(a) == 1
head = head.replace(a, 'per cell, 56 directional samples). 2026-11-14: the bounce takes the surface\'s colour and the later bounces are' + nl +
                    'added (lightmap; the light grid takes the colour). Left by decision (table "Open points closed by decision"): a' + nl +
                    'per-face lightmap scale, HDR probes, the parity proof.')
n = head.count('Open: ')
head = head.replace('Open: ', 'Left by decision (table "Open points closed by decision"): ')
a = '| 1.11 bounce colour from textures, more than one bounce, a per-face lightmap scale, HDR probe capture | One bounce at 45 % grey, the texel size as a Bake Setting, 8-bit probes captured before every build | Each changes the baked picture by a few percent; the per-face scale also needs a converter change. |'
assert tail.count(a) == 1
tail = tail.replace(a, '| 1.11 a per-face lightmap scale, HDR probe capture | Bounces in the surfaces\' colours with the later bounces estimated (2026-11-14), the texel size as a Bake Setting, 8-bit probes captured before every build | The per-face scale needs a converter change; HDR probes change glossy reflections by a little. |')
a = 'The lines marked "Open:" above stay as they are for this hand-over.'
assert tail.count(a) == 1
tail = tail.replace(a, 'The lines marked "Left by decision" above stay as they are for this hand-over.')
write(P, head + tail)
print('ok, lines reworded:', n)
