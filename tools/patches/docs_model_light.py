import io

# Docs for the placed models' light in Game Lighting (plan 1.9, 2026-11-14).

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

- **Game Lighting: models in your own map are lit where they stand.** The build lights every placed model from the
  light grid at its place, so a crate in a dark room is dark and one in the street takes the sky. The viewport gave
  every model the map's average light. Now View > Game Lighting works out the same light for every model (the
  build's own sampler, shared code) in the background and reads it per triangle the way the build's shader does.
  Cinder Rail: 230 models. Core tests check the reader against known samples and the sampler under open sky, under a
  roof and over a coloured floor. In game: not checked.

''')
patch('docs/PROGRESS.md', '''  The web list of plan 3.3 / 3.4 is done (the Vision Editor form closed it).''', '''  Plan 1.9, own-map models: core light_grid lightGridSamplesAt (the build's palette-cell sampler, now one function
  for compile_iw8.cpp and the viewport) and modelLightFromSamples (the converter's InstanceAmbient: 4 x 4 x 4 volume
  by kModelLightingTexelColors, trilinear along the normal, x 2). Viewport: SceneCache::modelLights (entity -> 56
  samples) + modelLightsRev (in the camera geometry key); model triangles carry the light in the vertex tangent with
  lm = (-2, -2) (modelTriangleLight / appendTexturedFace propLight), the shader's own branch reads it like the stock
  props' light grid box; CameraView::maybeLightModels makes the table in a thread (BVH of the drawn brush faces and
  patches, settled 600 ms, redone only when the result differs). Sampled at the middle of the model's bounds; the
  build's grid has a point every 128 x 128 x 64 units, so the two can differ a little near a doorway.
  The web list of plan 3.3 / 3.4 is done (the Vision Editor form closed it).''')
patch('docs/USER_GUIDE.md', '''- **What the bake bounces.**''', '''- **Models.** A placed model is lit by the light at its place (the build's light grid: sky, sun bounce, how closed
  in the spot is), with or without baked lightmaps. View > Game Lighting shows it per model.
- **What the bake bounces.**''')

P = 'docs/PLAN_REMAINING.md'
s = read(P)
nl = '\r\n' if '\r\n' in s else '\n'
a = 'see PROGRESS). Left by decision (table "Open points closed by decision"): own-map models against the built light grid.'
assert s.count(a) == 1
s = s.replace(a, 'see PROGRESS). Own-map models take the build\'s light grid light at their place DONE 2026-11-14.')
start = s.index('| 1.9 own-map models against the built light grid |')
end = s.index(nl, start) + len(nl)
s = s[:start] + s[end:]
write(P, s)
print('ok')
