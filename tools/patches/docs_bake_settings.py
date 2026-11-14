import io

# Docs for the Compile panel's Bake Settings (plan 1.10) and the stale "no lightmap baking" line (plan 1.12).

ROOT = 'E:/IW8-Radiant/'


def patch(p, a, b):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == 1, (p, s.count(a), a[:70])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


patch('CHANGELOG.md', '''## 2026-11-13

''', '''## 2026-11-13

- **Compile > Bake Settings.** A dialog for the lightmap bake of your map: the sky's colour and strength, the texel
  size and the number of sky rays, with Restore Defaults. The values are worldspawn keys (`iw8r_bake_sky`,
  `iw8r_lightmap_texel`, `iw8r_bake_rays`), saved with the map and undoable in one step. The build and View > Game
  Lighting now read them through the same code, so the preview bakes with exactly the build's values. A value that
  cannot be read is reported in the build log and the default is used.

''')
patch('docs/PROGRESS.md', '''## Latest (2026-11-13)

''', '''## Latest (2026-11-13)

- Plan 1.10 (compile panel bake settings): core bake_options (MapBakeOptions, mapBakeOptions, bakeOptionKeys) is the
  one reader of iw8r_bake_lightmaps / iw8r_bake_sky / iw8r_lightmap_texel / iw8r_bake_rays for compile_iw8.cpp and
  camera_view.cpp (gameBakeOptionsOf); CompilePanel::editBakeSettings / applyBakeSettings (button
  compileBakeSettings). Tests bake_options_*, GUI check in "game lighting". Still open in 1.10: shadow quality.
''')
patch('docs/USER_GUIDE.md', '''- **Compile > Sun & Sky**: sun pitch/yaw/colour and a stock sky (`sky_mp_*`; exported and converted at build time).
''', '''- **Compile > Sun & Sky**: sun pitch/yaw/colour and a stock sky (`sky_mp_*`; exported and converted at build time).
- **Baked lightmaps**: tick **Bake lightmaps** on the Compile panel and the build bakes the world's lightmap: the
  light of the sky, the light bounced off surfaces, and where the sun reaches. The sun itself and the light entities
  are not baked; the game draws them live on top. **Bake Settings...** beside it sets the sky's colour and strength
  (0.35 is a clear day: the open sky gives about a third of the sun's light), the texel size (world units per
  lightmap texel: smaller is sharper and slower) and the sky rays per texel (more is smoother and slower). View >
  Game Lighting bakes its preview with the same values. Not seen in game.
''')
patch('docs/USER_GUIDE.md', '''by name. Lightmap baking is not available yet.
''', '''by name. Baked lightmaps (section 6) are built and checked offline but not yet seen in game.
''')
patch('docs/PLAN_REMAINING.md', '''**1.10 GUI and settings.** Part 1 DONE 2026-11-13 (View > Game Lighting Options, saved settings, GUI test). Open:
shadow quality, the compile panel's bake settings.
''', '''**1.10 GUI and settings.** Part 1 DONE 2026-11-13 (View > Game Lighting Options, saved settings, GUI test). The
compile panel's bake settings DONE 2026-11-13 (Compile > Bake Settings, core bake_options). Open: shadow quality.
''')
print('ok')
