import io


def edit(p, a, b):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    if b in s:
        return
    assert s.count(a) == 1, (p, a[:60])
    io.open(p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


edit('docs/TEST_CHECKLIST.md', '''## In game (after deploy)''', '''## Game Lighting against the game (2026-11-13), editor only, compare with your own in-game shots

Open a stock map (File > Open IW8 Stock Map), turn on View > Game Lighting (Ctrl+Alt+L), and compare with the same spot
in game. Only if you want to check it; each line says what should match.

- [ ] Hideout: rooms under lamps are lit by the lamps (pools under ceiling lights, soft beam edges), with shadows from
      shelves and beams; props inside are as dark as the walls next to them.
- [ ] Aniyah Tac: the wall lanterns glow; far props switch to their low-detail models without visible popping.
- [ ] Speedball: grass and leaves glow when the sun is behind them (look toward the sun across the grass).
- [ ] Hideout: the doorway cloths and tarps have the colours you see in game (the editor now reads their tint from the
      material's lit constants; one cloth turned purple-grey, another red).
- [ ] Verdansk: props in the town are lit like the buildings around them (not uniformly bright).

## In game (after deploy)''')
edit('CHANGELOG.md', '''## 2026-11-13

''', '''## 2026-11-13

- **Shadows from the stock maps' own lamps.** Lamps that cast shadows in game (70 on Hideout) now cast them in Game
  Lighting too, with the game's soft exponential shadow edge (research 106): shelves, beams and doorways block their
  light. The nearest 64 shadowed lights get a map; they are drawn once (a few per frame) and again only when something
  changes.
''')
edit('docs/PROGRESS.md', '''## Latest (2026-11-13)

''', '''## Latest (2026-11-13)

- M1.6 DONE (2026-11-13): local light shadows. Core stockLightShadowCamera (research 106 section 4.2: pushed back
  along dir by the bulb's extent, FOV 2 acos(cosOuter), near = offset + bulb along the axis + shadowNearPlaneBias,
  far = offset + radius, k = (1 - softness) 88 + softness, b = shadowBias) and stockLightShadowMatrix (row-vector
  LH perspective, w = depth); test against light 82. Renderer: 64 slots of 512^2 R32 depth (Texture2DArray t24),
  slot records t23 (vp + near, far, k, b); per frame the nearest drawn shadow casters keep / take a slot (LRU), up to 6
  slots drawn a frame with the sun pass's drawCasters (static world + LOD0 props), all again when the casters change;
  the packed record's bits 0-7 point at a drawn slot. gameLocalShadow: 4 taps, linearised depth, exp compare; omnis
  weighted by spotF. IW8R_NO_LIGHT_SHADOWS; bench "stock_lights ... N shadow maps" (Hideout bench camera 40, 8.6 ms).
  Also: StockTechsetTraits::transmission (hexB bit 0), transmission only on rpl / alt sets (glass binds slot 68 too);
  IW8R_NO_EMISSIVE / IW8R_NO_TRANSMISSION / IW8R_NO_WRAP A/B switches; GUI test "unavailable material" uses a stand-in
  status source (the radio's images exist in a Verdansk tile zone now); bench memory_at lines.
''')
edit('docs/PLAN_REMAINING.md', '''**1.6 Local lights.** Part 1 DONE 2026-11-13 (research 106: every spot / omni at runtime, nearest 256, IES,
tile-binned). Open: spot shadow maps (research 106 section 4 / 5.4).''', '''**1.6 Local lights.** DONE 2026-11-13 (research 106: every spot / omni at runtime, nearest 256, IES, tile-binned;
64 shadow map slots). Open: needsDynamicShadows (entities in light maps), the screen-space shadow mask path.''')
print('ok')
