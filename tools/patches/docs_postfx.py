import io


def edit(p, a, b):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    if b in s:
        return
    assert s.count(a) == 1, (p, a[:60])
    io.open(p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


edit('CHANGELOG.md', '''## 2026-11-13

''', '''## 2026-11-13

- **The game's bloom and vignette in Game Lighting.** Stock maps now get their vision's soft glow (the "veil": a wide
  bloom of the whole frame, 8 % on Speedball) and its darkened screen corners, built the way the game builds them
  (research 109). Against your in-game Speedball shot the frame's edges and corners now match far better (left edge 181
  -> 140, the game 137). View > Game Lighting Options > Bloom and Vignette turns them off.
''')
edit('docs/PROGRESS.md', '''## Latest (2026-11-13)

''', '''## Latest (2026-11-13)

- M1.8 part 1 (2026-11-13, research 109): core GamePostFx (gamePostFxFromVision: veilStrength, vignette*, grain x 0.25;
  gameVignetteConstants = cb7[214..216]; gameVignetteFactor CPU reference; test game_post_fx_from_vision: Speedball
  corner 0.600, edge 0.693, Hideout corner 0.950). Renderer: the veil chain (veilS_ mips 1..n+1, veilV_ 2..n,
  R16G16B16A16F; ps_veil_karis on mip 1, ps_veil_down 8 taps, ps_veil_up with s_veilWeights 0 .025 .075 .15 .2 .25
  .3, fixed rotation n = 0.5), ps_post: veil lerp then vignette before gamePost; b12 Veil cbuffer, t25-27. GameLook
  veil / vignette from the stock vision; View > Game Lighting Options > Bloom and Vignette (game/postFx). Bench line
  post_fx. Speedball vs out/compare/speedball_ingame.webp (editor before -> after, game): centre 107.7 -> 99.8 (112.6),
  top-left 126.7 -> 80.2 (66.8), bottom-right 74.5 -> 46.5 (29.2), left edge 181.3 -> 140.2 (136.6). Not done: grain,
  the volumetric fog (research 109 section 2: re-tune the analytic fog first), Hideout's white balance matrix.
  Bench note: out/agent/mapcam.ps1 copies the exe into out/audit_bin, which fails silently while another bench runs from
  it; out/agent/mapcam2.ps1 uses out/audit_bin2.
''')
edit('docs/PLAN_REMAINING.md', '''**1.8 Atmosphere and post.**''', '''**1.8 Atmosphere and post.** Part 1 DONE 2026-11-13 (research 109: bloom veil and vignette). Open: volumetric fog, film
grain, white balance.''')
print('ok')
