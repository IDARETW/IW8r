import io

# Research 109: the volumetric constants read in the Replay exe, and the measured comparison with the owner's
# Speedball picture that keeps the volumetric fog off.

ROOT = 'E:/IW8-Radiant/'


def patch(p, a, b):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == 1, (p, s.count(a), a[:70])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


patch('docs/research/109_bloom_fog_sun_post.md', '''## 3. Sun disc and flare
''', '''### 2.7 Addendum 2026-11-14: the constants in Replay, and a measured comparison

**The constant block (VERIFIED, Replay `R_VOL_SetConstants` at `0x1418B94B0`, read in Hex-Rays; 320 bytes).** It
confirms the slot meanings that section 2.3 inferred:

- CB0[8] = (grid width, grid height, 1 / width, 1 / height);
- CB0[13].w = 128 (the slice count);
- CB0[15].yzw = three floats of the view's volumetric settings (the albedo);
- CB0[16] = (airDensity x 0.01, the next setting (absorption), two more (the anisotropy among them));
- CB0[17].w = a setting squared;
- CB0[18] = four floats copied as one block, only for the scatter stage (the sun, spot, omni and ambient brightness);
- CB0[19].zw = (height base, `-ln2 / max(halfPlane, 0.1)` or 0 when halfPlane is 0).

No further scale appears in the fill. So the editor's march (section 2.6) uses the game's constants as they are.

**Against the owner's picture** (`out/compare/speedball_ingame.webp`, bench camera `-1080 230 330 -4 -27 120`, mean
luminance of 8 x 4 blocks of the frame, 0 .. 255):

| | mean block error | sky row | lower rows | view / game |
|---|---|---|---|---|
| volumetric off (the default) | 11.3 | 9.2 | 12.0 | 1.055 |
| volumetric on | 16.8 | 20.3 | 15.6 | 0.951 |

With the volume on, the sky comes out too dark (the volume takes a third of it, as the shader says it does), while
the editor's sky without the volume already matches the game's picture. So the sky's own brightness in the editor is
about a third too low, and the missing volume hides it: the two errors cancel. Turning the volume on needs the sky's
radiance settled first (its intensity and the conversion exposure of the sky image, research 95 / 103), which this
one picture cannot separate from the volume's sun halo. Until then the option stays off: the picture is closer to the
game without it.

## 3. Sun disc and flare
''')
P = 'docs/PLAN_REMAINING.md'
s = io.open(ROOT + P, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
start = s.index('| 1.8 volumetric fog |')
end = s.index(nl, start) + len(nl)
s = s[:start] + ('| 1.8 volumetric fog | Bloom veil, vignette, film grain and white balance (2026-11-14), exponential fog, the stock vision\'s exposure and grade; the volumetric march is built and off | Measured 2026-11-14 against the owner\'s Speedball picture: the frame is closer to the game with it off (block error 11.3 against 16.8). Its constants are verified in the Replay exe (research 109 section 2.7); the sky\'s own brightness has to be settled before it can go on. |' + nl) + s[end:]
io.open(ROOT + P, 'w', encoding='utf-8', newline='').write(s)
patch('docs/PROGRESS.md', '''  Background texture decode: core keyed_job_pool.h''', '''  Volumetric fog measured (research 109 section 2.7): IW8R_BENCH_VOLUMETRIC=1 turns it on in a bench; Speedball
  block error against out/compare/speedball_ingame.webp 11.3 off, 16.8 on: stays off. The comparison script is the
  one in that section (8 x 4 block means of luminance).
  Background texture decode: core keyed_job_pool.h''')
print('ok')
