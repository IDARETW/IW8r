import io

# FEATURES.md had no section for View > Game Lighting (built 2026-11-13 / 14) and no line for Paint Mesh on Faces.

p = 'E:/IW8-Radiant/docs/features/FEATURES.md'
s = io.open(p, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'


def rep(a, b):
    global s
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == 1, a[:60]
    s = s.replace(a, b)


rep('''### Fog, grade, wind and behaviour in the views
''', '''### Game Lighting

- `View > Game Lighting` (Ctrl+Alt+L) draws the camera the way the game shades the map. (owner check pending: built
  from the game's shaders and data, compared with one in-game picture of Speedball, not with the running game since.)
- Your own map: what the built map will look like. With `Compile > Bake lightmaps` on, a preview of the baked
  lightmap (sky light, bounce in the surfaces' colours, sun shadows); with it off, the flat sky light and the live
  sun the game then uses. Placed models take their light from the place they stand in.
- A stock map: its own lightmaps, the light grid on its props, its reflection probes, its lamps (spot and omni, with
  shadows for the nearest ones), the sun with shadows, its exposure and colour grade, fog, bloom, vignette, film
  grain and white balance (the warm cast of Hideout or Verdansk), screen-space reflections and ambient occlusion,
  glowing materials and decals.
- Stock props switch to their simpler models and disappear at the distances the game uses, including the distance
  scale a level designer set on a single placement.
- `View > Game Lighting Options`: Ambient Occlusion, Screen-Space Reflections, Stock Map Lamps, Lamp Shadows, Bloom
  and Vignette, Film Grain, White Balance, Prop LODs, the sun shadow detail and an exposure override. Each one is
  saved. Volumetric Fog is there too, off and marked uncalibrated.
- A screenshot without the editor's markers has the same bloom, vignette and white balance as the view.
- Textures are read and decoded in the background while a map loads, so the view keeps moving.

### Fog, grade, wind and behaviour in the views
''')
rep('''- Vertex paint: `Patch > Vertex Paint Mode` paints control point colour and alpha in the camera.''',
    '''- Paint on brush surfaces: `Brush > Paint Mesh on Faces` lays a mesh with a paint point every N units on the selected faces (Ctrl+Shift+click) or on every drawn face of the selected brushes, with the face's material and texture placement. The face under it becomes caulk, so the brush keeps its shape and collision. One undo step; Vertex Paint Mode starts right after. (not yet seen in game)
- Vertex paint: `Patch > Vertex Paint Mode` paints control point colour and alpha in the camera.''')
rep('''Date: 2026-11-12''', '''Date: 2026-11-12 (pictures taken again and Game Lighting added on 2026-11-14)''')
io.open(p, 'w', encoding='utf-8', newline='').write(s)
print('ok')
