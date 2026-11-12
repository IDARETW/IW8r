# 55. Unlit baseline: what a map without lightmaps gets

Roadmap 1.8. Question: a map compiled by the editor has no baked lightmaps and no light grid. What does x64-zt
give its surfaces, static models and sun, and which worldspawn keys change that? Read-only research. Sources:
the editor (`E:\IW8-Radiant`) and the x64-zt fork (`E:\mw124\x64-zt`, not modified). No map was built or rendered,
so everything about the runtime shader is marked UNVERIFIED.

Citations are `file:line` in the two repos. Editor paths are under `src\core\`, converter paths under
`src\convert\iw3_iw8\` unless written in full.

## Summary

- The editor writes **no lightmap** for any surface. Every vertex gets `lightmap_uv [0,0]` (compile_iw8.cpp:1771),
  the surface has no `lightmap` key, so the converter sets `lightmap = -1` (iw3_fastfile.cpp:2242, 2404). A surface
  with `lightmap < 0` takes the unbaked branch: UV zero and bit 4 of the flag word clear (iw3_fastfile.cpp:3962, 3977).
- The editor writes **no light grid**. `PrepareNativeLightGrid` looks for `<map>.lightgrid.json`, finds none and
  warns (iw3_lightgrid.cpp:1210-1215; iw3_fastfile.cpp:12977). Static models then take a flat colour
  (iw3_lightgrid.cpp:1416-1437; iw3_fastfile.cpp:12078, 12084).
- The only real light source for an unbaked editor map is the **sun** (primary light 1) plus the **light entities**
  (primary lights 2+). The converter reads the sun from the `sundirection` and `suncolor` worldspawn keys, which the
  editor passes through; `sunlight`, `csm_*` and any ambient key are not used.
- **Black, sun-below-horizon, or nearly horizontal suns** are the dark-map cases. With no sun and no light entity
  nothing shades the map. The checks in section 6 flag these.
- The editor's `sun.ambient` value of `[0.5,0.5,0.5]` (compile_iw8.cpp:1887) is **dead** for editor maps (section 2.4).
  No ambient fill is authored anywhere in the pipeline.

## 1. What the editor writes

| What | Where | Value for a no-bake map |
|---|---|---|
| Sun pitch/yaw/roll | compile_iw8.cpp:1326-1337 | Default `-50 45 0`. `sundirection` parsed as three numbers; otherwise warned and `-50 45 0` used |
| Sun colour | compile_iw8.cpp:1327, 1340-1349 | Default `1 1 1`. `suncolor` parsed; any component above 1 means all three are 0-255 and divided by 255; negatives clamped to 0 |
| Worldspawn keys in the .ents | compile_iw8.cpp:1588-1592 | `classname`, `sundirection` always; `suncolor` only when it parsed. **`sunlight`, `csm_*` are not written** |
| Sun direction vector | compile_iw8.cpp:111-115 | `(cos p cos y, cos p sin y, -sin p)`, pointing toward the sun. Pitch above zero gives z below zero: below the horizon |
| World JSON `sun` | compile_iw8.cpp:1887-1889 | `ambient [0.5,0.5,0.5]` (constant), `angles` = pitch/yaw/roll, `color` = sunColor, `intensity` = **1** (constant) |
| Sun primary light (index 1) | compile_iw8.cpp:1939-1946 | `type 1`, `color` = sunColor (not normalised), `direction` = sun direction, `can_use_shadow_map false` |
| Light entities | compile_iw8.cpp:595-598, 1776-1780 | Primary lights from index 2 (`kFirstLocalLight = 2`). Light entities are the only local light source |
| Vertices | compile_iw8.cpp:1768-1771 | `lightmap_uv [0,0]` on every vertex. No `lightmap` key on any surface |
| Light grid | (none) | No `lightgrid` or `gpulightgrid` output anywhere in compile_iw8.cpp (grep) |
| No worldspawn | compile_iw8.cpp:1271 | Warning "default sun used"; the sun is still written with the defaults above |

## 2. What the converter does with it

The editor's extraction is read by the IW3 build (`iw3_fastfile.cpp`, `BuildLighting` and the surface readers) and
by `convert.cpp`'s `loadLighting` (convert.cpp:906-960). The hand-off from the editor's file names to that path is
inferred from the names in compile_iw8.h:4-14 and the keys each reader asks for. **The end-to-end run was not
performed** (UNVERIFIED for the exact `--style` and folder naming the mapper uses).

### 2.1 Sun

- `SunDirection`: the `sundirection` entity key if present, else the exported angles (iw3_fastfile.cpp:8184-8191).
  Pitch enters as `z = -sin(pitch)` (iw3_fastfile.cpp:8190), matching the editor's convention, so pitch above zero
  puts the sun below the horizon.
- `BuildLighting` (iw3_fastfile.cpp:8195-8240):
  - colour: `suncolor` entity key if present (8207). If it is absent the fallback is `sun.color`; if that is all zero,
    `sun.ambient` is the fallback (8200-8204).
  - intensity: `sunlight` entity key if present, else `sun.intensity` (8212-8213). The editor never writes `sunlight`,
    and `sun.intensity` is always 1.
  - **override**: if the sun primary light has type 1 and luminance above 0, intensity becomes the luminance of its
    colour and colour becomes `colour / luminance` (8219-8232). The editor always writes that light with the suncolor,
    so for editor maps the effective rule is:
    `intensity = luminance(suncolor)`, `colour = suncolor / luminance`.
    Consequence: **`sunlight` has no effect** for editor maps; **`suncolor` brightness is the sun intensity** (under
    the default style, see 2.2).
- `Iw8LightingMatch` only applies with `--style iw8` (iw3_fastfile.cpp:10698-10708, 12972-12973). Its struct default
  `lightUnitScale = 1` (iw3_vision.h:103-109).
- **Default style is `iw3`** (convert.cpp:87; style parse at 2676-2683). The editor's own code never passes a style
  (grep of `E:\IW8-Radiant\src` finds only port_wizard.cpp:134 `--style iw8`, for CoD4 ports). Which style a mapper
  uses is UNVERIFIED.

**Effective sun, by style (computed, not run):**

| suncolor | luminance | `--style iw3` intensity | `--style iw8` intensity | iw8 light_unit_scale |
|---|---|---|---|---|
| `1 1 1` (default) | 1.000 | 1.00 | 23.9 | 23.9 |
| `1 0.9764 0.945` (stock) | 0.979 | 0.98 | 23.9 | 24.4 |
| `0.2 0.2 0.2` | 0.200 | 0.20 | 8.63 (clamped) | 43.2 |
| `0.05 0.05 0.05` | 0.050 | 0.05 | 8.63 (clamped) | 172.6 |
| `0.5 0.5 1` (blue, night) | 0.536 | 0.54 | 3.10 (night) | 5.8 |

The iw8 column comes from the table at iw3_vision.cpp:382-398 and the matcher at iw3_vision.cpp:407-455: the
luminance is placed on the CoD4 day-sun list (kIw3DaySuns, just before the matcher) and mapped to the shipped
day-sun intensities (8.6 to 30.7), clamped at both ends. The values were computed with a short script that copies
those two tables (scratchpad `sunmatch.py`), not by running the converter. Under `iw8` the suncolor magnitude mostly
moves `light_unit_scale` (which also scales local lights and the light grid units), not the sun brightness.

**Black suncolor:** colour stays 0, intensity stays at its default, and `MatchIw8Lighting` returns intensity 0 so the
override at 12972 is skipped. The sun is black under both styles.

### 2.2 Sun primary light and shadows

- The converter reads the sun primary light from the world JSON (`sun_primary_light_index`, convert.cpp:960), pads the
  table to 256 slots when a type 2 or 3 light is present (convert.cpp:1125-1132), and keeps the sun at index 1.
- Sun shadows: the bake runs with the converter's own budget (convert.cpp:1601-1660). With no caster in the play
  space the depth image is marked unbaked (iw3_fastfile.cpp:12128-12145). The sun still lights surfaces; only the
  shadow term is missing. The editor's `can_use_shadow_map false` (compile_iw8.cpp:1942) is not read for the sun.

### 2.3 Lightmaps and surfaces

- `lightmap` defaults to -1 (iw3_fastfile.cpp:2242 `source.value("lightmap", -1)`; 2404 `value_or(-1)`).
- `EncodedLightmap` (iw3_fastfile.cpp:3956-3978): `baked = surface.lightmap >= 0`. For an unbaked surface the lightmap
  UV is zero and the flag word has bit 4 clear (`baked ? 4u : 0u`, line 3977). Surfaces with a stock material could
  take another path (6420-6422, 6538-6542), but only for stock distortion or shadow-only surfaces.
- **What the Replay shader does with bit 4 clear is UNVERIFIED.** This is the biggest unknown in the whole doc: the
  converter writes the unbaked flag and a zero UV, but the runtime shading for that case was not read.

### 2.4 Ambient

- The editor writes `sun.ambient [0.5,0.5,0.5]` (compile_iw8.cpp:1887). The converter reads `sun.ambient` only as the
  colour fallback when `sun.color` is all zero (iw3_fastfile.cpp:8200-8204). For editor maps `sun.color` is the
  suncolor the editor wrote. If that is `0 0 0`, the entity `suncolor "0 0 0"` is still present and wins
  (`ParseEntityVector` returns the parsed value, iw3_fastfile.cpp:8155-8167; `EntityValue` returns the entity's text),
  so the fallback never applies. If `suncolor` is absent the editor's colour is `1 1 1`. **So `sun.ambient` is never
  used for editor maps.**
- No other ambient key is read. `skylight`, `skydomelighting`, `precomputed_sky_*` (doc 39's stock keys) appear in
  neither `convert\iw3_iw8` nor the editor (grep of x64-zt finds them only in `core\assets\gfxworld_data.hpp` and
  `games\iw8\neutral\gfxworld.cpp`). A no-bake map therefore has **no ambient fill**.

### 2.5 Light grid and static models

- `PrepareNativeLightGrid` (iw3_lightgrid.cpp:1195-1219): no `gpulightgrid.bin` and no `lightgrid.json` gives a
  warning and returns false; no grid file is written. What Replay does without it is UNVERIFIED.
- Static-model lighting image (iw3_fastfile.cpp:12068-12087): with no grid, every texel's RGB is **40** and alpha 255
  (line 12078). Models with their own colours or ground lighting keep those (iw3_lightgrid.cpp:1416-1437). The default
  `0xFF282828` (ground colour 40) comes from the same value.
- **Discrepancy:** the warning at iw3_fastfile.cpp:12084 says "flat 0.31 ambient". 40/255 = 0.157, not 0.31. Either
  the shader doubles the value or the warning is out of date. UNVERIFIED which.

### 2.6 Light entities

- Light entities become primary lights 2+ (compile_iw8.cpp:595-598, 1776-1780). Their intensity is calibrated from the
  radius unless `iw8r_intensity_units` is set (compile_iw8.cpp comment above localPrimaryLights). A map with no light
  entity has only the sun.

## 3. Which editor inputs change the result

| Input | Effect on the map | Source |
|---|---|---|
| `sundirection` pitch | Sun elevation; floor N·L = sin(-pitch). `-50` gives 0.77, stock `-20.261` gives 0.35, `0` gives 0, above 0 the sun is below the horizon | compile_iw8.cpp:1330-1337; iw3_fastfile.cpp:8190 |
| `sundirection` yaw | Which faces are lit; no brightness change | compile_iw8.cpp:113-115 |
| `suncolor` brightness | Sun intensity under `--style iw3` (= luminance); light_unit_scale under `--style iw8` | iw3_fastfile.cpp:8219-8232, 10698-10708 |
| `suncolor` hue | Sun colour | iw3_fastfile.cpp:8229 |
| `suncolor` black or negative | Sun contributes nothing | compile_iw8.cpp:1348; iw3_fastfile.cpp:8207-8213 |
| `sunlight` | No effect (not written by the editor, and luminance overrides it) | compile_iw8.cpp:1588-1592; iw3_fastfile.cpp:8212-8232 |
| `csm_*` | No effect on lighting | sun_keys.cpp rows (doc 39) |
| Light entities | Local lights; the only light besides the sun | compile_iw8.cpp:1776-1780 |
| `--style` (converter, not editor) | Brightness of the sun (table versus luminance) and unit scale | convert.cpp:87, 2676-2683 |

## 4. Risks: black or flat maps

1. **Black sun.** `suncolor` black or negative (clamped). The sun is 0 everywhere. Without lightmaps, a light grid or an
   ambient key, the only light is from light entities. A map with none is black. Flagged: error.
2. **Sun below the horizon.** `sundirection` pitch above zero. Upward faces get N·L < 0. Flagged: error. (Shader
   behaviour for negative N·L is UNVERIFIED.)
3. **Horizontal sun.** Pitch zero gives floors N·L = 0. Pitch from -10 to 0 gives at most 0.17. Flagged: warning.
4. **Dim sun under `--style iw3`.** Luminance below 0.25 is below the dimmest CoD4 day sun (0.3817, iw3_vision.cpp).
   Flagged: warning.
5. **Very small suncolor under `--style iw8`.** Sun brightness does not change (table value), but `light_unit_scale`
   grows (172 at luminance 0.05). Local lights and the grid unit scale go up with it. UNVERIFIED how far this matters.
6. **No ambient.** Unlit faces get only what the runtime gives unbaked surfaces (UNVERIFIED, section 2.3).
7. **Props.** Static models get a flat 40 with no grid (section 2.5). The warning text says 0.31. UNVERIFIED.
8. **No light grid.** Replay's light grid is missing for every editor map (section 2.5). UNVERIFIED effect on
   dynamic objects.

## 5. Recommended worldspawn defaults for a no-bake map

Only keys the converter reads are listed. All other keys are ignored by the editor or the converter.

| Key | Recommended value | Why |
|---|---|---|
| `sundirection` | `-50 45 0` | The editor default (compile_iw8.cpp:1326). Floors get sin 50 = 0.77 of the sun. Stock `-20.261 -15.4 0` gives 0.35, too dim for floors without bakes |
| `suncolor` | `1 1 1` | Luminance 1.0, so `--style iw3` gives intensity 1.0, and the hue is neutral. Stock `1 0.9764 0.945` (luminance 0.98) is also fine for a warmer sun |
| `sunlight` | do not set | Not written by the editor, and the luminance override ignores it (iw3_fastfile.cpp:8212-8232) |
| `csm_enable`, `csm_max_resolution`, `csm_sample_size` | do not set | Not read by the converter (doc 39) |
| ambient | no key exists | Nothing in the pipeline reads one. Light from light entities is the only other fill |

Guideline for a dim or night map: keep the suncolor luminance at 0.25 or more under `--style iw3`, and use a blue
suncolor only where night is intended (the matcher treats `b > 1.4 max(r,g)` as night, iw3_vision.cpp:407-420). Pitch
should be between -10 and -90 degrees.

## 6. Rules implemented (checkUnlitBaseline)

Implemented in `src\core\unlit_baseline.cpp` with tests in `tests\unlit_baseline_tests.cpp`.

| Rule | Level | Condition | Message |
|---|---|---|---|
| R0 | warning | No worldspawn | default sun used |
| R1 | error | `sundirection` pitch above 0 | sun below the horizon |
| R2 | warning | `sundirection` pitch from -10 to 0 (inclusive) | nearly horizontal |
| R3 | warning | `sundirection` not three numbers | editor uses -50 45 0 |
| R4 | warning | No `sundirection` key | editor uses -50 45 0 |
| R5 | error | `suncolor` luminance 0 (after 0-255 scaling and clamping) | sun black |
| R6 | warning | `suncolor` luminance above 0 and below 0.25 | sun dim |
| R7 | warning | `suncolor` not three numbers | editor uses 1 1 1 |
| R8 | warning | No `suncolor` key | editor uses 1 1 1 |
| R9 | error | R1 or R5 holds and there is no light entity | nothing lights an unbaked map |

Light entities are counted with `collectLights` (light_budget.h). Ambient is not checked: no ambient key is read
(section 2.4), so a missing ambient key is not a fault.

## 7. UNVERIFIED

- The shader's treatment of unbaked surfaces (bit 4 clear, zero UV) and of negative N·L.
- What Replay does without a light grid, and whether the 40 static-model value is the intended ambient (the warning says 0.31).
- The `--style` the mapper's build uses, and the exact file hand-off from the editor folder to `BuildLighting`.
- The effect of `light_unit_scale` at very small suncolor luminance.
- Brightness thresholds in R2 and R6 (0.25, -10 degrees): chosen as guidelines from the converter's constants, not
  measured in game.
- Iw8 column values in section 2.1: computed by script from the transcribed tables, not run through the converter.

## 8. Other findings

- `sun.ambient` (compile_iw8.cpp:1887) is written but never reaches a visible result for editor maps (section 2.4).
- `sun.intensity` is hard-coded to 1 (compile_iw8.cpp:1888); the converter overrides it from the sun colour, so it
  carries no mapper input.
- The editor passes `sunlight` through nowhere (compile_iw8.cpp:1588-1592), which matches sun_keys.cpp.
- A negative suncolor is silently clamped to 0 by the editor (compile_iw8.cpp:1348) with no warning, so the map
  goes black without a message. R5 in section 6 is the only check that reports it.
