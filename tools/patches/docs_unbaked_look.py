import io

# Docs for plan 1.9 part 1 (research 117): Game Lighting shows an unbaked own map as the game draws it.

ROOT = 'E:/IW8-Radiant/'


def patch(p, a, b):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == 1, (p, s.count(a), a[:70])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


patch('docs/research/117_own_map_world_shading.md', '''float3 bounce = 1 - min(texel.rgb, .85) * (1 - gtao);            // hlsl:1470''',
      '''float3 bounce = 1 - min(texel.rgb, .85) * (1 - gtao);            // hlsl:1468''')
patch('CHANGELOG.md', '''## 2026-11-13

''', '''## 2026-11-13

- **Game Lighting shows a map without baked lightmaps the way the game draws it.** When "Bake lightmaps" is off, the
  built map has no lightmap: the game gives every surface the same soft sky light (a little brighter on faces that
  look up) and adds the sun with its shadows. The editor used to bake a preview lightmap anyway, so corners and
  shadowed walls looked darker and richer in the editor than in the game. Now View > Game Lighting shows the game's
  flat sky light for such a map, and the baked look only when "Bake lightmaps" is on (then the build makes the same
  lightmap). The status bar says which one is shown. Read from the converter's shader (research 117); not seen in
  game.

''')
patch('docs/PROGRESS.md', '''## Latest (2026-11-13)

''', '''## Latest (2026-11-13)

- Plan 1.9 part 1 (research 117, converter shader read by an Opus agent and checked by me at hlsl:1385, 1468, 1573,
  1620, convert.cpp:99, iw3_vision.cpp:471): editor builds use style iw3 (no MAP_IW8_LIGHTING); an unbaked map's
  surfaces get the constant fill lerp((.22,.23,.24), (.38,.41,.44), N.z/2+.5) + shadowed sun; indirect x multi-bounce
  AO. Viewport: GameLook::skyIsUnbaked (gameSky.w = 2 in ps_tex), aoOwn, CameraView::gameLookUnbaked (own map, bake key
  not 1: no preview bake, lightmap dropped, status once), bench line game_unbaked, GUI check. Cinder Rail A/B:
  out/agent/unb_cr_cmp.png. Open in 1.9: specular-map materials, brushes on a stock map lit as models, fog form,
  exposure parity, cb7[43].z.
''')
patch('docs/USER_GUIDE.md', '''arrow points at the sun). **Ctrl+Alt+L** (View > Game Lighting) shows the map as the built map looks in game: the
editor bakes the world's lightmap in the background (again a moment after each edit; the status bar says when) and
shades with the game's own formula: the lightmap, the sun with its shadows, the light entities in the game's units, then
the game's exposure and tone curve. Worldspawn `iw8r_bake_lightmaps 1` puts the same bake into the build.
''', '''arrow points at the sun). **Ctrl+Alt+L** (View > Game Lighting) shows the map as the built map looks in game. With
**Bake lightmaps** on (Compile panel; worldspawn `iw8r_bake_lightmaps 1`) the editor bakes the world's lightmap in
the background (again a moment after each edit; the status bar says when), as the build will, and shades with the
game's own formula: the lightmap, the sun with its shadows, the light entities in the game's units, then the game's
exposure and tone curve. With it off the build has no lightmap, and the view shows what the game then draws: the same
soft sky light on every surface, without darker corners, plus the sun and the lights.
''')
patch('docs/PLAN_REMAINING.md', '''**1.9 Own brushes match what the build will produce.**
''', '''**1.9 Own brushes match what the build will produce.** Part 1 DONE 2026-11-13 (research 117: the unbaked look, the
multi-bounce occlusion). Open: specular-map materials, brushes added to a stock map, own-map models against the built
light grid.
''')
patch('docs/research/README.md', '''| 116 | [Stock zone install](116_stock_zone_install.md) |''', '''| 117 | [Own map world shading](117_own_map_world_shading.md) | What the converter's world shader computes on an editor build (style iw3; the constant sky fill of an unbaked map; multi-bounce occlusion) against Game Lighting for own brushes |
| 116 | [Stock zone install](116_stock_zone_install.md) |''')
patch('docs/TEST_CHECKLIST.md', '''## What is not expected to work yet
''', '''## In game (optional, 2026-11-13): an own map with and without baked lightmaps

- [ ] A map built with Compile > Bake lightmaps **off**: in the game the shadowed sides of buildings are lit evenly,
      with no darker corners, as View > Game Lighting now shows it.
- [ ] The same map built with Bake lightmaps **on**: corners and covered places are darker, as View > Game Lighting
      shows it with the key on.

## What is not expected to work yet
''')
print('ok')
