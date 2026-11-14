import io

# Docs for the film grain, and two rows of the decision table that said less than was built (the AO strength has been
# a setting since 1.10; layers with colour mode 2 or 3 leave the colour alone, as in the game).

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

- **Stock maps in Game Lighting: film grain.** The game puts a fine grain on the finished picture, as strong as the
  map's vision asks (Hideout 5 %, Speedball 2.5 %, none where the vision leaves it out). The viewport now draws it
  with the game's own noise formula (research 109), standing still instead of moving. View > Game Lighting Options >
  Film Grain turns it off. A before / after of Hideout: the same average brightness, single pixels up to 5 % apart.

''')
patch('docs/PROGRESS.md', '''  The web list of plan 3.3 / 3.4 is done (the Vision Editor form closed it).''', '''  Film grain: GameLook::grain (= GamePostFx::grain of the stock vision), postFx0.w, ps_post after gamePost (research
  109 section 4.2, T fixed), SceneRenderer::setFilmGrainEnabled, command GameFilmGrain (game/filmGrain). Own maps: 0
  (the vision default). Checked: 1.4's "AO strength" is the existing setting game/aoStrength (r_ssaoStrength, 1 on
  every MP map), and colour modes 2 / 3 are skipped in camera_view.cpp (lc.colorMode >= 2), which is the game's rule
  for the colour; what is not drawn of such layers is their normal, cavity and gloss.
  The web list of plan 3.3 / 3.4 is done (the Vision Editor form closed it).''')
P = 'docs/PLAN_REMAINING.md'
s = read(P)
nl = '\r\n' if '\r\n' in s else '\n'
start = s.index('| 1.4 layer colour modes 2 / 3, the AO strength constant |')
end = s.index(nl, start) + len(nl)
s = s[:start] + ('| 1.4 the normal, cavity and gloss of detail layers on layered prop materials | Sun, lightmap and lamp wrap and transmission; layers with colour mode 2 or 3 leave the colour alone, as in the game; the AO strength is a setting at the game\'s value | 43 layer slots on props in the three maps read (crate foam, concrete slabs); they need the prop materials\' layer tables, which the material export does not give. |' + nl) + s[end:]
a = 'Left by decision' + nl + '  (table "Open points closed by decision"): layer modes 2/3, AO strength.'
assert s.count(a) == 1
s = s.replace(a, 'Left by decision' + nl + '  (table "Open points closed by decision"): the normal and gloss of detail layers on props.')
start = s.index('| 1.8 volumetric fog, film grain, white balance |')
end = s.index(nl, start) + len(nl)
s = s[:start] + ('| 1.8 volumetric fog, white balance | Bloom veil, vignette, film grain (2026-11-14), exponential fog, the stock vision\'s exposure and grade | The volumetric fog constants are not calibrated (it stays off rather than wrong); white balance is not decoded. |' + nl) + s[end:]
a = 'Left by decision (table "Open points closed by decision"): volumetric fog, film' + nl + 'grain, white balance.'
if s.count(a) == 1:
    s = s.replace(a, 'Film grain DONE 2026-11-14. Left by decision (table "Open points closed by decision"): volumetric' + nl + 'fog, white balance.')
write(P, s)
patch('docs/USER_GUIDE.md', '''- **Models.** A placed model is lit by the light at its place''', '''- **Film grain.** On a stock map the picture has the game's fine grain, as strong as that map's vision sets it. View
  > Game Lighting Options > Film Grain turns it off (for screenshots, or if it bothers you while editing).
- **Models.** A placed model is lit by the light at its place''')
print('ok')
