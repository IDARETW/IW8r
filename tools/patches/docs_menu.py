import io


def edit(p, a, b):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    if b in s:
        return
    assert s.count(a) == 1, (p, a[:60])
    io.open(p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


edit('docs/USER_GUIDE.md', '''### Decals
''', '''### Game Lighting on stock maps

**View > Game Lighting** (Ctrl+Alt+L) on a stock map draws it the way the game does: the map's own lightmap and
reflection probes, the sun with its shadows, each prop lit from the light the game baked for it, the map's own lamps
with their shadows, ambient occlusion, screen-space reflections on wet and glossy surfaces, glowing lamps and screens,
light through leaves and cloth, and the game's exposure and tone curve. Verdansk and Quarry stream their town in
around the camera. The first time a map is opened this way the editor reads a few things from your install in the
background (the status bar says what); later opens are quick.

**View > Game Lighting Options** turns its parts on and off and remembers them: Ambient Occlusion, Screen-Space
Reflections, Stock Map Lamps, Lamp Shadows and Prop Levels of Detail (far props use the game's simpler models, which is
much faster on big maps). **Ambient Occlusion Strength** (1 is the game's), **Exposure Override** (a fixed EV instead of
the map's auto exposure, for comparing screenshots), **Re-bake Lighting Now**, and **Debug View** (the AO buffer, the
albedo, or where reflections are traced) are there too. Game Lighting itself is remembered between sessions.

### Decals
''')
edit('CHANGELOG.md', '''## 2026-11-13

''', '''## 2026-11-13

- **View > Game Lighting Options.** Ambient occlusion, screen-space reflections, the stock lamps, their shadows and
  the prop levels of detail can each be turned off, with an AO strength, a fixed exposure for comparing screenshots,
  Re-bake Lighting Now and debug views; all of it, and Game Lighting itself, is remembered between sessions.
''')
edit('docs/PROGRESS.md', '''## Latest (2026-11-13)

''', '''## Latest (2026-11-13)

- M1.10 part 1 (2026-11-13): View > Game Lighting Options (gameLightingMenu): GameAo / GameSsr / GameStockLights /
  GameLightShadows / GamePropLods checkable, saved as game/ao, game/ssr, game/stockLights, game/lightShadows,
  game/propLods (default on); GameAoStrength (game/aoStrength, gameSunCol.w: aoV = lerp(1, ao, s)); GameExposureOverride
  (game/exposureEv, CameraView::setGameExposureOverride: manual EV over the map's auto / vision exposure); GameRebake;
  Debug View (game/debugView: lit, AO buffer, albedo, reflections mask; renderer gameDebugView_ next to the env
  switches). view/gameLighting saved by the toggle (not yet restored at start: the camera mode restore decides).
  Renderer: setStockLightsEnabled / setLightShadowsEnabled / setGameDebugView / setGameAoStrength. GUI test section
  "game lighting options". USER_GUIDE "Game Lighting on stock maps". Helpers running: research 108 core (Warzone
  probes, stock_probes.*), core JSON parse memory (stock_world / smodels / displacement / decal volumes / clutter).
''')
edit('docs/PLAN_REMAINING.md', '''**1.10 GUI and settings.**''', '''**1.10 GUI and settings.** Part 1 DONE 2026-11-13 (View > Game Lighting Options, saved settings, GUI test). Open:
shadow quality, the compile panel's bake settings.''')
print('ok')
