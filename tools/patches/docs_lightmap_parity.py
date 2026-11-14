import io

# Docs for the lightmap parity proof (plan 1.11, 2026-11-14).

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

- **Proof that a built map holds the light the editor showed.** The lightmap proof now also reads the light planes
  out of the built zone itself (with the reader the editor uses for stock maps) and compares them, texel by texel,
  with what the editor baked and draws in View > Game Lighting. On the proof map: 10,328 lit texels, 0.23 % apart
  on average, 99 % of them within 0.5 %, the total light 0.1 % apart. This is the plan's parity proof in the form
  that can be made without the game: it shows the data the game will sample is the editor's; how the game's shader
  then draws it is still only seen in the game. The proof map has no directional light share, so the stored light
  direction was not exercised.

''')
patch('docs/PROGRESS.md', '''  ROADMAP 4.27 map text: core map_strings.{h,cpp}''', '''  Plan 1.11 parity proof: tests/built_lightmap_parity_tests.cpp (env IW8R_TEST_BUILT_DEC + IW8R_TEST_PAIR_*; skipped
  without them) compares extractStockLightmapsFromDecompressedZone of the built <map>.ff.dec with
  decodeLightmapTexel of the extraction's pair; tools/lightmap_bake_build_test.py decompresses the built zone
  (ACTS fastfile -d --noAssetDump) and runs it. Result on the proof map: 0.23 % mean, 99 % within 0.5 %.
  ROADMAP 4.27 map text: core map_strings.{h,cpp}''')
P = 'docs/PLAN_REMAINING.md'
s = read(P)
nl = '\r\n' if '\r\n' in s else '\n'
start = s.index('| 1.11 the parity proof (draw the built zone as a stock map and compare) |')
end = s.index(nl, start) + len(nl)
s = s[:start] + s[end:]
a = 'HDR probes, the parity proof. The per-face lightmap scale is DONE 2026-11-14 (in the bake itself; no converter change).'
assert s.count(a) == 1, s.count(a)
s = s.replace(a, 'HDR probes. The parity proof is DONE 2026-11-14 as a comparison of the data: the light planes read from the built zone against the editor\'s bake, texel by texel (0.23 % apart on average; `tools/lightmap_bake_build_test.py`). Drawing the built zone as a stock map was not done: it would compare two different shaders. The per-face lightmap scale is DONE 2026-11-14 (in the bake itself; no converter change).')
write(P, s)
patch('docs/HANDOVER.md', '''- `IW8Radiant.exe --viewport-bench <map> --out <dir>`: frame times and pictures of every view mode.''',
      '''- Among those proofs: the light planes of a built map, read back from the zone, equal the editor's bake texel by
  texel (0.23 % apart on average), and a map's own HUD picture and text are read back from its zones.
- `IW8Radiant.exe --viewport-bench <map> --out <dir>`: frame times and pictures of every view mode.''')
print('ok')
