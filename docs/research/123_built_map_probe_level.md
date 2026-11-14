# 123. Why a built map's reflection probes showed nothing, and what they are stored as now

**Why.** Owner playtest 2026-11-11 (ROADMAP F.10): "reflection probes make no visible change". Research 73 found the
first cause (every probe was the same flat grey picture; fixed by capturing before every build). The plan kept a
second item open, "HDR probe capture", with the note that the converter would have to take another image format.
Measuring a built zone on 2026-11-14 showed the real fault, and it is not the capture's range.

**Labels.** VERIFIED = measured on zones or read in source. INFERRED = follows from verified facts. Nothing was run
in the game.

## 1. What the game does with a probe (VERIFIED, research 95)

The stock lit shader does not use a probe's picture as light. It multiplies it by the luminance of the baked light at
the surface: `P = probe(R, mip) * luma709(Lr)`. So stock probes are stored **luminance-normalised**: a picture whose
mean luminance is about 1, with its colour and with texels far above 1 where the sky or a lamp is. The converter's own
shader for a built map's surfaces uses the same rule (`map_surface_realtime.hlsl`:
`ProbeReflection(...) * luminance`), and every stock-shaded object in a built map (weapons, players, carried stock
props) reads the map's probes that way too.

## 2. Measured: stock probes against a built map's (VERIFIED)

Report test `stock_probes_zone_report` (tests/stock_probes_tests.cpp; `IW8R_TEST_PROBE_CACHE`, or
`IW8R_TEST_PROBE_JSON` + `IW8R_TEST_PROBE_DEC`): per probe the mean luminance of level 0, the brightest texel, and how
coloured it is (the mean of (max - min) / max over the texels; 0 = grey).

| Zone | Probes | Mean luminance | Brightest texel | Colour |
|---|---|---|---|---|
| mp_shipment | 17 | 0.59 .. 1.01, average 0.82 | 9,576 | 0.281 |
| mp_hideout | 48 | up to 1.03, average 0.87 (one black probe) | 1,092 | 0.358 |
| mp_m_speedball | 4 | 0.90 .. 0.97, average 0.93 | 39 | 0.374 |
| built map before the fix (mp_cinderrail_t) | 1 | **0.139** | 1.03 | **0.001** |
| built sample after the fix | 1 | 0.907 | 2.33 | 0.188 |

A built map's probe was six to seven times darker than a stock probe and had no colour. Multiplied by the baked
light's luminance, its reflections were six to seven times too dark on everything: that is "no visible change".

## 3. Where that came from (VERIFIED, x64-zt `iw3_render_assets.cpp` `DecodeDdsCubemap`)

The converter's probe reader was written for Call of Duty 4 captures:

- CoD4's captures are clipped and over-saturated (its fences and grass tinted every weapon), so the reader keeps only
  each texel's luminance. An editor capture is the editor's own render of the map: its colour is right, and it was
  thrown away with the rest.
- The reader scales a picture so it "keeps its former average" and never exceeds 1. For CoD4 that level was tuned over
  many rounds. For an editor capture it leaves the picture at the level of a tone-mapped screenshot (mean 0.14 on the
  maps measured), not at the level the shader's multiplication expects.
- The reader already takes half-float cubes (`D3DFMT_A16B16G16R16F`, from IW7 extractions) and keeps their colour. So
  the plan's note ("the converter would have to take another image format") was out of date; but that path also
  stores at most 1 and at the picture's own average, so writing a float capture alone would not have fixed the level.

## 4. What is built now

- **Editor** (`compile_iw8.cpp`): every probe entry of the world JSON carries `"capture": "editor"`. The capture is
  128 texels a face (was 64: the game's 256-texel octahedral image is filled by faces of about 104 texels, so 64 was
  blurred), every level written, each texel the mean of four samples of the frame (`CameraView::captureCube`).
- **Converter** (x64-zt): a probe marked that way keeps its colour, and the probe array stores it times a gain that
  brings its mean luminance (linear, weighted by solid angle) to 0.9, the stock level. The gain is held to 64; a black
  capture keeps 1. Texels above 1 are kept (the array is BC6H). The zone's per-probe SH describes the stored picture.
  Two things keep their old values on purpose: the absolute-level table the CoD4-style paths use
  (`MAP_PROBE_LEVELS`: stored value x level = the capture's own radiance) and the glass environment (`MAP_GLASS_SH`).
  A probe without the mark (a CoD4 or IW7 conversion) is read exactly as before.
- The encoded array's cache key holds the gain, so an old cached array is not reused.

## 5. Proof (offline)

`tools/probe_capture_build_test.py` builds the sample through the editor with the converter, reads the zone's probe
images back and checks: the game's loaders accept the zone (ACTS, 0 failed); every probe's mean luminance is in the
stock range (0.6 .. 1.3; measured 0.907); the probes are coloured (at least 0.05; measured 0.188, was 0.001). The
converter's log says what it did: "reflection probe 1 is an editor capture: colour kept, stored at 6.40x".

## 6. Not determined

- Nothing here was seen in the game. That the level was the whole reason for F.10 is INFERRED from the shader's rule
  and the measurement.
- The capture is still a tone-mapped 8-bit picture, so a sky is at most about 2.3 times the picture's mean after the
  gain, where a stock sky texel can be 40 times and a lamp thousands. Highlights of very bright things in mirror-like
  reflections are therefore softer than stock. A capture of the renderer's linear frame would give that range; the
  level and the colour, which decide whether reflections are there at all, do not depend on it.
- A probe is still chosen per surface by the nearest probe origin (research 73), not per pixel by box.
