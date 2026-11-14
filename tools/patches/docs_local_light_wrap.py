import io

# Docs for lamps on wrap and transmission materials (plan 1.4, 2026-11-14).

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

- **Stock maps in Game Lighting: lamps on foliage, cloth and wrap materials.** The game lights these materials from
  lamps by a different rule than plain ones: the light wraps past the edge of the lit side (grass, pebbles, snow,
  tarps), and thin materials let a lamp shine through from behind. The viewport drew every material with the plain
  rule. It now uses the game's rule for those materials, read again from the game's own shaders (the two research
  notes described two shader families, not one rule). A before / after of a daytime Hideout view is identical, as it
  should be with no lamp near such a surface; a night scene with a lamp beside grass was not compared with the game.

''')
patch('docs/PROGRESS.md', '''  The web list of plan 3.3 / 3.4 is done (the Vision Editor form closed it).''', '''  Plan 1.4, lamps: gameLocalLight(…, wrap, trans, …) in render.cpp. Plain materials keep research 106's bulb-capped
  irradiance; wrap (matW.w) and transmission (matTr.w) materials take diffuse = pow(att, e) x cap x wrapF(N.L, w'),
  specular = att x cap x sat(N.L), cap = sat(bulbRadius^2 / (d + collimationOffset)^2), w' = clamp(sqrt(w^2 + h^2),
  1e-6, 1.25), h = .77 s + .33 s^2 (88d201db.asm:1041-1093, e2969e82.asm:1269-1327; the plain world shader 01631f08
  has no .77 constant, so 106 and 107 are both right for their shaders). Transmission adds the back lobe with the
  lamp's point along -V. A/B on Hideout (out/agent/mapcam_real.ps1, real material cache): identical, no lamp near such
  a material in that view. Open in 1.4: layer colour modes 2 / 3, the AO strength constant.
  The web list of plan 3.3 / 3.4 is done (the Vision Editor form closed it).''')
P = 'docs/PLAN_REMAINING.md'
s = read(P)
nl = '\r\n' if '\r\n' in s else '\n'
a = '(lightmap + sun), probe rough fallback. Left by decision (table "Open points closed by decision"): wrap and transmission for local lights, layer modes 2/3, AO strength.'
assert s.count(a) == 1
s = s.replace(a, '(lightmap + sun), probe rough fallback. Wrap and transmission for local lights DONE 2026-11-14. Left by decision' + nl +
              '  (table "Open points closed by decision"): layer modes 2/3, AO strength.')
start = s.index('| 1.4 wrap and transmission for local lights, layer colour modes 2 / 3, the AO strength constant |')
end = s.index(nl, start) + len(nl)
s = s[:start] + ('| 1.4 layer colour modes 2 / 3, the AO strength constant | Sun, lightmap and lamp wrap and transmission (lamps 2026-11-14), layer mode 1, AO at its default strength | A few layered materials and a global strength; the gain cannot be checked without game pictures. |' + nl) + s[end:]
write(P, s)
R = 'docs/research/107_stock_shading_terms.md'
s = read(R)
nl = '\r\n' if '\r\n' in s else '\n'
s = s.rstrip() + nl + nl + (
    '## Addendum 2026-11-14: lamps, read again' + nl + nl +
    'Research 106 gives a lamp\'s diffuse as the bulb-capped irradiance with a horizon term; section 2.2 above gives' + nl +
    '`wrapF(N.L, h)`. Both are right, for different shaders (VERIFIED in the listings):' + nl + nl +
    '- The plain world and static-model shaders (W `01631f08`, SM) hold no `0.77` constant: research 106\'s form.' + nl +
    '- The wrap shaders (W2 `e2969e82`, Wm) and the transmission shaders (L `88d201db`, Lw `eba993c4`) hold' + nl +
    '  `h = 0.77 s + 0.33 s^2`, `s = bulbRadius / distance to the bulb point`, and:' + nl +
    '  - `cap = saturate(bulbRadius^2 / (d + collimationOffset)^2)` (record offset 0x0C, which research 106 lists as not' + nl +
    '    read by W or SM; `d` is the distance to the nearest point of the tube) (L:1041-1048, W2:1269-1277);' + nl +
    '  - diffuse `= pow(att, e_c) x cap x wrapF(N.L, clamp(sqrt(w_c^2 + h^2), 1e-6, 1.25))`, with `w = 0` and `e = 1` in a' + nl +
    '    shader without the wrap bit (L:1066-1093, W2:1297-1327; `att` is the falloff x shadow product, `e_c` the' + nl +
    '    sun\'s per-channel shadow power, applied by the same integer bit trick);' + nl +
    '  - specular `= att x cap x saturate(N.L)` times the usual lobe.' + nl + nl +
    'The viewport follows this since 2026-11-14 (`gameLocalLight` in `src/viewport/render.cpp`).' + nl)
write(R, s)
print('ok')
