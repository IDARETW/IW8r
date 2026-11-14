import io

# Docs for the game's white balance in Game Lighting (plan 1.8, research 120, 2026-11-14).

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

- **Game Lighting: the map's white balance.** Most stock maps set a white balance in their vision (Hideout,
  Verdansk, Rust, Quarry, Backlot and others: a warm one). The game applies it to the whole picture; the viewport
  did not, so Hideout looked cold and grey where the game is warm. The viewport now applies the game's matrix at the
  game's place in the chain (white on Hideout becomes 1.08, 0.99, 0.89 in red, green, blue; Verdansk 1.07, 0.99,
  0.88). Speedball and Shipment set none and do not change. An own map gets it from the white balance fields in
  worldspawn `iw8r_vision`, which the build also writes into the map's vision. View > Game Lighting Options > White
  Balance turns it off. The formula is from the game-test source and the constant tables from the Replay exe
  (research 120); it was not compared with the running game.
- **Clean screenshots have the view's bloom and vignette.** A screenshot without the editor's markers skipped the
  whole post pass in Game Lighting, so it had no bloom, no vignette and no film grain. It now takes the same path as
  the view.

''')
patch('docs/PROGRESS.md', '''  Plan 1.13: lodFov in SceneRenderer::cullTexInstances (research 119; IW8R_NO_LOD_FOV turns it off). The''',
      '''  Plan 1.8 white balance: core white_balance (gameWhiteBalanceMatrix; research 120), GamePostFx::whiteBalance,
  GameLook::whiteBalance -> ps_post postWb0..2 (cbuffer Veil is 36 floats now); own maps: worldspawn iw8r_vision
  tokens MNMNSSLLOR / NLSRLSTPTN / TRQLLNNLS / OPSMSQNKQ. Toggle GameWhiteBalance (game/whiteBalance);
  IW8R_NO_WHITE_BALANCE is the bench switch. Clean captures (in.clean) use the HDR frame and the post pass now.
  Plan 1.13: lodFov in SceneRenderer::cullTexInstances (research 119; IW8R_NO_LOD_FOV turns it off). The''')
P = 'docs/PLAN_REMAINING.md'
s = read(P)
nl = '\r\n' if '\r\n' in s else '\n'
a = 'Film grain DONE 2026-11-14. Left by decision (table "Open points closed by decision"): volumetric' + nl + 'fog, white balance.'
assert s.count(a) == 1, s.count(a)
s = s.replace(a, 'Film grain DONE 2026-11-14. White balance DONE 2026-11-14 (research 120). Left by decision (table "Open points closed by decision"): volumetric' + nl + 'fog.')
start = s.index('| 1.8 volumetric fog, white balance |')
end = s.index(nl, start) + len(nl)
s = s[:start] + '| 1.8 volumetric fog | Bloom veil, vignette, film grain and white balance (2026-11-14), exponential fog, the stock vision\'s exposure and grade | The volumetric fog constants are not calibrated (it stays off rather than wrong). |' + nl + s[end:]
write(P, s)
patch('docs/research/README.md', '''| 119 | [LOD distance rule](119_lod_distance_rule.md) |''',
      '''| 120 | [White balance](120_white_balance.md) | The game's white balance matrix (vision fields, formula, the exe's tables), which stock maps set one, and what the viewport does with it |
| 119 | [LOD distance rule](119_lod_distance_rule.md) |''')
patch('docs/USER_GUIDE.md', '''- **Film grain.** On a stock map the picture has the game's fine grain, as strong as that map's vision sets it. View
  > Game Lighting Options > Film Grain turns it off (for screenshots, or if it bothers you while editing).''',
      '''- **Film grain.** On a stock map the picture has the game's fine grain, as strong as that map's vision sets it. View
  > Game Lighting Options > Film Grain turns it off (for screenshots, or if it bothers you while editing).
- **White balance.** Many stock maps set a white balance (Hideout and Verdansk a warm one); View > Game Lighting
  shows it as the game does. View > Game Lighting Options > White Balance turns it off. For your own map, set it in
  Compile > Vision Editor (or worldspawn `iw8r_vision`) with the fields `MNMNSSLLOR` (method: 4 is what the stock
  maps use), `NLSRLSTPTN` (1 = daylight at a temperature), `TRQLLNNLS` (the temperature in kelvin: 5000 is neutral,
  higher is warmer) and `OPSMSQNKQ` (green / magenta tint). Example: `MNMNSSLLOR=4;NLSRLSTPTN=1;TRQLLNNLS=5500`.
  The viewport and the built map use the same values.''')
patch('docs/TEST_CHECKLIST.md', '''- [ ] Verdansk: props in the town are lit like the buildings around them (not uniformly bright).''',
      '''- [ ] Verdansk: props in the town are lit like the buildings around them (not uniformly bright).
- [ ] Hideout or Verdansk: the overall colour is as warm as in the game (2026-11-14: the map's white balance). If the
      editor is warmer or colder than the game, turn View > Game Lighting Options > White Balance off and say which
      of the two is closer.''')
print('ok')
