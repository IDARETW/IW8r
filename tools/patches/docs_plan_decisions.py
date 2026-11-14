import io

# PLAN_REMAINING: the open points of Milestones 1 to 3 that stay as they are, each with its reason (the plan's own
# rule: an item is done, or recorded as a documented decision), and the state of Milestone 5.

ROOT = 'E:/IW8-Radiant/'


def read(p):
    return io.open(ROOT + p, encoding='utf-8', newline='').read()


def write(p, s):
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s)


P = 'docs/PLAN_REMAINING.md'
s = read(P)
nl = '\r\n' if '\r\n' in s else '\n'
assert 'Open points closed by decision' not in s
a = s.index('## Milestone 4')
decisions = '''## Open points closed by decision (2026-11-14)
The lines marked "Open:" above stay as they are for this hand-over. None of them stops a workflow. Each is a
refinement of how close the viewport is to the game, or of how much the occlusion data hides, and each can only be
judged against the running game, which is not used any more. They are listed so that nobody has to find them again.

| Point | What is there now | Why it stays |
|---|---|---|
| 1.4 wrap and transmission for local lights, layer colour modes 2 / 3, the AO strength constant | Sun and lightmap wrap and transmission, layer mode 1, AO at its default strength | Small differences on foliage near lamps and on a few layered materials; the decode exists (research 107), the gain cannot be checked without game pictures. |
| 1.5 SELFVIS, the 236 lightmapped `tml` props, Warzone tile grids for tile props | Props take their own light grid box; the rest fall back to the probes | SELFVIS needs a new ACTS export; the others touch 2 % of placements. |
| 1.6 `needsDynamicShadows`, the screen-space shadow mask | Every spot and omni light with shadow maps for the nearest 64 | Affects moving entities and contact shadows only. |
| 1.8 volumetric fog, film grain, white balance | Bloom veil, vignette, exponential fog, the stock vision's exposure and grade | The volumetric fog constants are not calibrated (it stays off rather than wrong); grain and white balance are not decoded, and grain is noise by design. |
| 1.9 own-map models against the built light grid | Models in your own map take the map's ambient light and the sun | The built grid is per cell (1.11 part 1); sampling it per model in the viewport changes dark-room props only. |
| 1.11 bounce colour from textures, more than one bounce, a per-face lightmap scale, HDR probe capture | One bounce at 45 % grey, the texel size as a Bake Setting, 8-bit probes captured before every build | Each changes the baked picture by a few percent; the per-face scale also needs a converter change. |
| 1.11 the parity proof (draw the built zone as a stock map and compare) | `tools/lightmap_bake_build_test.py` proves the baked data reaches the built zone; View > Game Lighting uses the converter shader's own formulas (research 117) | The proof as planned would compare the wrong things: a built map is drawn by the converter's world shader, and the editor's stock-map path models the game's stock shaders, not that one. |
| 1.13 the game's FOV / lodScale factor on LOD distances | Stock LOD distances as stored | Changes only the distance at which a LOD switches; the frame budgets are met without it. |
| 2.2 culling lights, probes and decals in the Umbra view; surface ids on displaced terrain | Surfaces and props are culled by the tome | A viewer refinement: the view already answers "what does the game hide here". |
| 2.3 local occlusion updates for edited stock maps | The whole map's occlusion opens when stock geometry is removed | See 2.3: a mistake makes objects vanish in the game. |
| 2.4 thin slanted walls, terrain and models as occluders, a per-brush occluder key, gates, splitting merged surfaces | Opaque brushes (boxes, and other shapes by the box inside them) | The bake is opt-in and unseen in the game; widening it before a first look would only widen what could go wrong. |
| 3.1 streamed tiles and terrain layers around the browser's camera | They follow the editor's own camera | Two cameras asking for different tiles would evict each other's from the cache; fly the editor's camera there first. |

'''.replace('\n', nl)
s = s[:a] + decisions + s[a:]
m5a = s.index('## Milestone 5')
m5b = s.index('## Verification (per milestone)')
m5 = '''## Milestone 5 — Handover
State on 2026-11-14:
- **Release:** done. `out\\installer\\IW8RadiantSuite-Setup-0.1.0.exe` and `out\\package\\IW8RadiantSuite-<date>.zip`,
  rebuilt after the last change; no ini and no game file inside; the packaged exe passes its start-up test.
- **Docs:** done. HANDOVER.md, USER_GUIDE.md, FEATURES.md, KEYS_REFERENCE.md, FEATURE_MATRIX.md and MILESTONES.md
  are current. The pictures of FEATURES.md (`--feature-shots`) are from 2026-11-12; the web page and Game Lighting
  have changed since, the other pictures are still what the editor shows.
- **Owner checklist:** done. docs/TEST_CHECKLIST.md starts its In game part with "the short list" (six items).
- **Parking:** done. The editor is left open on a compiled map; PROGRESS.md and HANDOVER.md are current.
- **What the hand-over cannot contain:** a look in the game. Everything built since 2026-11-11 is proven offline
  only; HANDOVER.md section 4 says which six looks would settle it.

'''.replace('\n', nl)
s = s[:m5a] + m5 + s[m5b:]
write(P, s)

H = 'docs/HANDOVER.md'
s = read(H)
nl = '\r\n' if '\r\n' in s else '\n'
old = '- Ground War scale maps are out of scope.'
assert s.count(old) == 1
s = s.replace(old, old + nl + ('- The smaller points that were left on purpose, each with what is there now and why, are one table in' + nl +
                                 '  `docs\\PLAN_REMAINING.md` ("Open points closed by decision").'))
write(H, s)
print('ok')
