import io


def patch(p, pairs):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    for a, b in pairs:
        a = a.replace('\n', nl); b = b.replace('\n', nl)
        assert s.count(a) == 1, (p, s.count(a), a[:70])
        s = s.replace(a, b, 1)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


patch('docs/USER_GUIDE.md', [
 ('''Wireframe, Fullbright and Normals. **Ctrl+L** turns on the lit preview (sun, light entities and sun shadows; the yellow
arrow points at the sun).''', '''Wireframe, Fullbright and Normals. **Ctrl+L** turns on the lit preview (sun, light entities and sun shadows; the yellow
arrow points at the sun). **Ctrl+Alt+L** (View > Game Lighting) shows the map as the built map looks in game: the
editor bakes the world's lightmap in the background (again a moment after each edit; the status bar says when) and
shades with the game's own formula: the lightmap, the sun with its shadows, the light entities in the game's units, then
the game's exposure and tone curve. Worldspawn `iw8r_bake_lightmaps 1` puts the same bake into the build.'''),
])
patch('docs/TEST_CHECKLIST.md', [
 ('''- [x] View > Camera Mode: Wireframe / Flat / Fullbright / Normals all draw; Ctrl+L lit preview shows sun shadows and the yellow sun arrow.''',
  '''- [x] View > Camera Mode: Wireframe / Flat / Fullbright / Normals all draw; Ctrl+L lit preview shows sun shadows and the yellow sun arrow.
- [ ] Optional, only if you want to check it: a map with worldspawn `iw8r_bake_lightmaps 1` in game next to View > Game
      Lighting (Ctrl+Alt+L) in the editor from the same spot: brightness, shadows, light pools and colours should match.'''),
])
patch('CHANGELOG.md', [
 ('''  world's lightmap table). Not seen in game.
''', '''  world's lightmap table). Not seen in game.
- **View > Game Lighting (Ctrl+Alt+L).** The camera shows the map as the built map looks in game: the editor bakes the
  lightmap in the background with the build's baker (again after edits) and shades with the converter's world shader
  formula (lightmap, sun and its shadows, light entities in the game's units) and the game's exposure and ACES tone
  curve (research 95). Cost about 0.8 ms per frame over the textured view on a 2-million-triangle scene at 1920 x 1080
  (8 ms moving); the small sample room draws in 0.35 ms. Not compared with the game yet.
'''),
])
patch('docs/PROGRESS.md', [
 ('''  maps in game"). Proof tools/lightmap_bake_build_test.py (in run_offline_proofs.py). Next: the viewport renders with
  that shader's math and the baked lightmap.''', '''  maps in game"). Proof tools/lightmap_bake_build_test.py (in run_offline_proofs.py).
- Then View > Game Lighting (Ctrl+Alt+L): core game_look (exposure, ACES LUT, converter light units), viewport
  game_lightmap.* (background bake of the camera's polygons, UVs found by position / normal / polygon centre),
  render.cpp ps_tex game branch (b4, t6-t9), camera_view prepareGameLook / maybeBakeGameLook. GUI test section "game
  lighting"; --viewport-bench has game_still / game_moving (synth_10000: 11.5 ms still, 8.0 ms moving; textured 10.7 /
  7.1). The owner asked for game-like frame rates (about 200 fps on Shipment). Not compared with the game: the
  absolute calibration (EV 10, +1.5 EV base, light window scale 1) is INFERRED (research 95).'''),
])
print('ok')
