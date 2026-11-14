import io

# Docs after the owner's game test of 2026-11-14 16:15 (Cinder Rail Toon and Shipment with the new toon zones): what
# was seen, what was not, and the occlusion bake's fault.

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

- **Seen in the game (owner, 16:15): the new toon lines, the haze and the bright sky work.** Cinder Rail Toon shows
  clean ink lines from the scene depth, the warm haze toward the horizon and the brighter sky; Shipment shows the toon
  filter with ink lines on the stock world and props. Not yet right: the first-person weapon and hands have no toon
  look (their shaders are in zones shared by every map; research 128 in work), and on Shipment some objects came and
  went (to be checked on a second load: the game was building the new zone's shader cache).
- **The occlusion bake is marked experimental: it failed its first game test.** The same test's first build had
  Occlusion bake on: world surfaces came and went with the camera and placed models (foliage too) were not drawn,
  while their shadows were. Rebuilt without it and redeployed. The Compile panel's tooltip and the build log now say
  so; the cause is open (plan 2.4).
- **The browser editor has the haze and the sky brightness too**: the Atmosphere form has Haze and Haze amount, the
  Sun & Sky form has Sky brightness.

''')
patch('docs/TEST_CHECKLIST.md', '''- [ ] Toon lines (Cinder Rail Toon, rebuilt 2026-11-14): lines join without ends or gaps, no lines between faces in
      one plane, outlines heavier than inner lines, far lines thin. If the lines look like the old ones (square
      ends), the game's scene depth did not reach the shader: say so.
- [ ] Toon lines on Shipment (stock rebuild with the screen filter): the world and props have ink lines.
- [ ] Haze (Cinder Rail Toon: Desert haze 0.7, sky brightness 3, exposure +1): warm haze toward the horizon that
      meets the sky, light shafts where the sun passes gaps, glass and other see-through surfaces fogged like the
      walls behind them, nothing black or washed out.''',
      '''- [x] Toon lines (Cinder Rail Toon, rebuilt 2026-11-14): seen in game 2026-11-14 16:15, the lines come from the scene
      depth and join cleanly (owner's screenshots).
- [x] Toon lines on Shipment (stock rebuild with the screen filter): seen in game 2026-11-14 ("the toon effect now
      works").
- [ ] Shipment with the toon zone: do objects still come and go on a second load? (First load 2026-11-14: yes; the
      game was building the zone's shader cache then.)
- [x] Haze and sky (Cinder Rail Toon: Desert haze 0.7, sky brightness 3, exposure +1): seen in game 2026-11-14, warm
      haze toward the horizon, bright sky.
- [ ] Haze details: light shafts where the sun passes gaps; glass and other see-through surfaces fogged like the
      walls behind them.
- [ ] Cinder Rail Toon without the occlusion bake (redeployed 2026-11-14 16:25): no surface or model goes missing.
      (With Occlusion bake on, surfaces came and went and models were not drawn: the bake is experimental.)
- [ ] Toon look on the first-person weapon and hands: not built yet.''')
s = read('docs/PLAN_REMAINING.md')
nl = '\r\n' if '\r\n' in s else '\n'
a = ('''  - weapon mounting on a built map (6.8, found 2026-11-14 by the field audit, research 124 / 125): OPEN, not built.''').replace('\n', nl)
assert s.count(a) == 1
s = s.replace(a, ('''  - occlusion bake in game (2.4): FAILED its first game test 2026-11-14 16:15 (Cinder Rail Toon built with
    iw8r_umbra_bake 1: world surfaces came and went with the camera, placed models were not drawn, their shadows
    were). OPEN. The offline conservativeness test passes with the editor's own decoder, so the game's Umbra runtime
    reads something differently: suspects are the camera's cell lookup (view tree), the per-object bounds against the
    portal-clipped frustum, and contribution culling of small objects (the one-cell tome avoids all three). Until
    fixed the option is marked experimental in the Compile panel and the build log.
''').replace('\n', nl) + a)
write('docs/PLAN_REMAINING.md', s)

p = 'docs/PROGRESS.md'
s = read(p)
nl = '\r\n' if '\r\n' in s else '\n'
a = '  STATE 2026-11-14 16:15 (read this first;'
assert s.count(a) == 1
note = '''  OWNER GAME TEST 2026-11-14 16:15 (their words): "in toon cinderrail, i see some surfaces disappearing and
  appearing depending on the angle of the camera, and foliage and other objects seem invisible, and the viewmodel
  still doesnt have the toon effect. in shipment, the toon effect now works (not the viewmodel), but objects also
  appear in and out". Their screenshots show the new lines, the haze and the bright sky working in game.
  - Cinder Rail: the build they tested had iw8r_umbra_bake 1 (their own setting, in their open map). Rebuilt from a
    private copy without that key (out/agent/cr_toon_build, same shader and scene keys) and redeployed 16:25 with
    --compile --deploy. NOT CONFIRMED yet that this cures it. Their open map still has the key: an F7 from their
    editor brings the bake back. The bake is now marked experimental (compile log warning, Compile panel tooltip,
    PLAN 2.4 OPEN with the suspects).
  - Shipment popping: only the techset zone differs from stock. The game wrote main/techsets_mp_shipment.psob (41 MB)
    at 16:08, so the guess is objects waiting for their pipeline states on the first load. Asked the owner to check a
    second load. If it stays: look at the spliced shaders (alpha when the stock shader leaves o0.w unwritten on a
    path, the 26 techniques that also read code image 6).
  - Viewmodel toon: an Opus agent is writing docs/research/128_viewmodel_filter.md (which shared zones hold the
    weapon / hands technique sets, what the filter must do differently since their materials live in other zones,
    a scratch build, install risks). Build it from that note. The owner has asked for it three times: do not wait
    for another confirmation, but an install of a shared zone changes every map while installed; say so.
  - GUI suite while the game runs: 1,089 checks, 1 failure ("without a game the Live Link commands say why"); it
    passes with the game closed. Core 181,940 / 1,377.
'''.replace('\n', nl)
s = s.replace(a, note + a)
write(p, s)
print('ok')
