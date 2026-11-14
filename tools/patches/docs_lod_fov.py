import io

# Docs for the field-of-view factor on LOD distances (plan 1.13, research 119, 2026-11-14).

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

- **Stock maps: props change detail level where the game would.** The game picks a prop's level of detail by its
  distance times a factor for the field of view (1 at the game's own 80 degree view; a wider view switches to the
  simpler models sooner, a zoomed view later). The viewport used the plain distance, so a wide editor view showed
  more detail than the game would at that view. It now uses the game's factor (research 119, read in the game-test
  source, not checked against the running game). Hideout: 1.3 % of the picture changes, the frame is a little
  faster.

''')
patch('docs/PROGRESS.md', '''  The web list of plan 3.3 / 3.4 is done (the Vision Editor form closed it).''', '''  Plan 1.13: lodFov in SceneRenderer::cullTexInstances (research 119; IW8R_NO_LOD_FOV turns it off). The
  placement's own lodScale (instanceFlags & 0xF) is not in the stock placement data: taken as 1.
  The web list of plan 3.3 / 3.4 is done (the Vision Editor form closed it).''')
P = 'docs/PLAN_REMAINING.md'
s = read(P)
nl = '\r\n' if '\r\n' in s else '\n'
a = 'Left by decision (table "Open points closed by decision"): the game\'s FOV / lodScale factor on the distances.'
assert s.count(a) == 1
s = s.replace(a, 'The game\'s field-of-view factor on the distances DONE 2026-11-14 (research 119; the placement\'s own lodScale is taken as 1).')
start = s.index('| 1.13 the game\'s FOV / lodScale factor on LOD distances |')
end = s.index(nl, start) + len(nl)
s = s[:start] + ('| 1.13 the placement\'s own lodScale on the cull distance | LOD levels by distance x the game\'s field-of-view factor (2026-11-14), stock cull distances | Needs the placements\' instance flags in the world export; it moves only where a prop disappears. |' + nl) + s[end:]
write(P, s)
patch('docs/research/README.md', '''| 118 | [Occlusion bake](118_occlusion_bake.md) |''', '''| 119 | [LOD distance rule](119_lod_distance_rule.md) | How the game scales a placed model's LOD distance by the field of view and the placement's lodScale (game-test source), and what the viewport does with it |
| 118 | [Occlusion bake](118_occlusion_bake.md) |''')
print('ok')
