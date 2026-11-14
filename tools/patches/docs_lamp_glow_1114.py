import io

# Docs 2026-11-14 19:50: the viewport's lamp glow fix.

ROOT = 'E:/IW8-Radiant/'


def patch(p, a, b):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == 1, (p, s.count(a), a[:70])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


patch('CHANGELOG.md', '''## 2026-11-14

''', '''## 2026-11-14

- **Fixed: a lamp's glow in the haze was missing in the viewport.** A lamp with `iw8r_volumetric` lights the
  volumetric air in the built map; the viewport's Game Lighting never showed it, because the glow read the frame's
  light list before it was filled. Found by rendering the same room with and without the key (the two pictures were
  identical); they now differ, and the GUI test checks that a flagged lamp reaches the renderer and an unflagged
  one does not.

''')
patch('docs/PROGRESS.md', '''  - STILL OPEN, in this order:''', '''  - 19:50: the viewport's lamp glow (iw8r_volumetric with a haze) was dead: CameraView::prepareGameLook read
    pixelLightScratch_ right after it was cleared. Now pixelLightOf() makes a light's renderer record for both users.
    Checked with two bench renders (the room with the key 1 and 0: identical before, +5 red on average after) and a
    GUI check (gameLookForTest().volLampCount). Suites: core 182,854, GUI 1,094, 0 failures.
  - STILL OPEN, in this order:''')
print('ok')
