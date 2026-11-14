# 126. Volumetric fog and sky fog on a built map: what stock maps use, what a built map gets, what to change

**Why.** The owner asked for proper volumetric sky and fog, "similar to how rust is, but don't just copy it". This
note lists which assets and fields make a stock map's haze, the values of mp_rust, mp_shipment and an overcast map,
what a built map gets today, and a short change list with a suggested "bright desert haze".

**Labels.** VERIFIED = read in source, a stock export, a shader disassembly or the Replay exe (cited). INFERRED =
follows from verified facts, or comes from the game-test decompile ("GT"). Nothing was run in the game.

**Sources.** Visions: the editor's world cache (`world_cache/<map>/vision/.../<map>.vision.0`) and, for mp_shipment,
an ACTS `rawfile` export (`tools/acts_export.py mp_shipment rawfile <scratch>`, file `vision/mp_shipment.vision.55`).
Token names come from `E:\mw124\Dvars.txt` (research 109's `vision_dump.py`). Lamps: the world cache's `com_map`
JSON. Converter paths are under `src/convert/iw3_iw8/` of the x64-zt worktree (`V:` = `iw3_vision.cpp`, `P:` =
`iw3_fastfile.cpp`, `hlsl` = `resources/map_surface_realtime.hlsl`). Scratch exports were deleted afterwards.

## 1. What drives the haze on a stock map

All of it is in the map's **vision** (the 218-field table) plus one flag per lamp. No GfxWorld or ComWorld field
holds fog settings. VERIFIED for the vision fields below (research 109, 111); the lamp flag is section 1.4.

### 1.1 Distance, height and atmospheric fog (the analytic fog of `cs_post_opaque_processes`)

| Index | Field | Role |
|---|---|---|
| 49 | `fogAtmosphericScattering` | 1 = atmospheric fog (every stock MP map checked uses it) |
| 36 | `fogStartDist` | path starts here |
| 50 | `fogHalfPlaneDistance` | density = ln2 / half |
| 51 | `fogMaxDistance` | path is capped here |
| 53 | `fogExtinctionDesaturation` | makes the extinction grey; it does not desaturate the scene |
| 54 | `fogInscatteringEv` | haze brightness: `2^(EV - 2.84) / 1000` in scene radiance units |
| 55, 56 | `fogRayleighAlbedo`, `fogMieAlbedo` | haze colour (sRGB, linearised by the exe) |
| 57, 58 | `fogMieAnisotropy`, `fogMieBlendStrength` | sun-side glow and how much of it |
| 45 | `fogSunDir` | direction of the glow (toward the sun); not taken from the map's sun |
| 59-62 | `fogHeightFog` base, half plane, maximum offset, additional fog | ground layer; "additional" is a uniform part that keeps the low sky hazed |

`fogColor`, `fogColorIntensity`, `fogMaxOpacity`, `fogSkyIntensity`, `fogDensity` and `fogSun*` are read only by the
plain (non-atmospheric) branch (research 111 section 2, VERIFIED).

### 1.2 How fog meets the sky

A sky pixel uses `fogSkyDistance` (52) as its path and fades the fog by a band between `fogSkyMinAngle` (47) and
`fogSkyMaxAngle` (48) from the zenith; `fogHeightFogAdditionalFog` keeps the band hazed (research 111 section 8).
`r_skyBlendAmount` is 0 on every map read. The volumetric fog adds its own dimming and sun lobe to the sky (1.3).

### 1.3 Volumetric fog (the froxel pass)

| Index | Field | Role |
|---|---|---|
| 138 | `volumetricAirDensity` | x 0.01 = scattering per unit; **0 = the pass does not run** |
| 139 | `volumetricAbsorption` | extra extinction |
| 140 | `volumetricAlbedo` | colour of the scattered light |
| 141 | `volumetricSunBrightness` | sun term (shadowed by the sun shadow: shafts) |
| 142, 143 | `volumetricSpotBrightness`, `volumetricOmniBrightness` | lamp terms, only for flagged lamps (1.4) |
| 144 | `volumetricAmbientBrightness` | light-grid term |
| 145, 146 | `volumetricHeightFogBaseHeight`, `...HalfPlaneDistance` | ground layer (half 0 = uniform) |
| 147 | `volumetricSunAnisotropy` | HG g of the sun lobe |
| 137 | `veilStrength` | the bloom veil that spreads the glow (default 0) |

The grid's depth is the dvar `r_volumetricDepth` (128, 384, 640, 1024: about 2180 units); MP scripts do not change it,
and a vision cannot (research 109 section 2.2). Beyond 2180 units, and on the sky, the whole column applies.

### 1.4 Lamps in the fog (VERIFIED, scatter shader `cs_volumetric_forwardplus_scatter_global.481`, lines 884-910)

A lamp adds in-scatter only when bit 30 of its GPU light word is set (`ubfe r12.w, l(1), l(30)`); inside that branch
the light colour is multiplied by `CB0[18].y` or `.z` (spot or omni brightness), the phase LUT and pi. GT
`rb_draw3d.cpp:8001` sets bit 30 from `ComPrimaryLight.isVolumetric`. Stock lamps flagged:

| Map | Lamps flagged `isVolumetric` |
|---|---|
| mp_rust | 0 of 82 |
| mp_shipment | 22 of 24 (all spots) |
| mp_village2 | 40 of 111 |
| mp_hideout | 52 of 127 |

## 2. Stock values (VERIFIED, visions)

Fields left out of a vision take the table default (`replay_vision_fields.inc`; for example height base 0, half 0).

| Field | mp_rust | mp_shipment | mp_village2 (overcast) |
|---|---|---|---|
| fogStartDist | 512 | 600 | 2300 |
| fogHalfPlaneDistance | 4000 | 5000 | 200000 |
| fogMaxDistance / fogSkyDistance | 2000000 / 1440572 | 50000 / 40000 | 150000 / 500000 |
| fogExtinctionDesaturation | 0.85 | 0 | 0.5 |
| fogInscatteringEv | 21 | 18 | 19.3 |
| fogRayleighAlbedo | .518 .478 .431 | .631 .816 1 | .647 .824 1 |
| fogMieAlbedo | .643 .612 .557 | 1 .949 .839 | .749 .875 1 |
| fogMieAnisotropy / BlendStrength | 0.5 / 0.25 | 0.4 / 0.5 | 0.2 / 0.5 |
| fogSkyMin / MaxAngle | 40.96 / 78.4 | -119.5 / 6.48 | 0 / 75 |
| fogHeightFog base / half / offset / additional | 0 / 170 / 0 / 0.032 | 0 / 2500 / 0 / 0 | 0 / 50000 / 0 / 0 |
| fogSunDir z | 0.34 (up) | 0.45 | -0.57 (below the horizon) |
| veilStrength | 0.09 | 0.09 | 0.09 |
| volumetricAirDensity / Absorption | 0.02 / 0 | 0.002 / 0 | 0.007 / 0 |
| volumetricAlbedo | 1 .957 .910 | 1 .929 .769 | 1 .949 .898 |
| volumetric Sun / Spot / Omni / Ambient | **0.01** / 1 / 1 / 0 | 1 / 0.5 / (1) / 0 | 0.1 / 1 / 1 / 0 |
| volumetric height base / half | (0 / 0) | 400 / 5000 | (0 / 0) |
| volumetricSunAnisotropy | 0.8 | 0.6 | 0.7 |
| sun intensity (ComWorld) | 11.70, warm | 8.63 | 11.05, neutral |

"Overcast" for mp_village2 is INFERRED from its neutral sun colour, its fog sun below the horizon and its thin
fog. Other desert-like maps for reference: mp_br_quarry and mp_donetsk use air 0.01, sun 0.4, ambient 1, g 0.75.

**What makes rust look like rust** (computed with research 111's `fog_ref.py` and research 109's march; see section
5): almost all of the bright haze is the **analytic** fog (EV 21, warm linear albedos, mostly grey extinction, a
170-unit ground layer plus 0.032 uniform fog that fills the low sky: sky T 0 at the horizon, 0.11 at 10 degrees, 0.51
at 20). Rust's volume is nearly unlit (sun 0.01, ambient 0, no flagged lamps): it only dims everything beyond 55 m
and the sky by 35 % (T 0.647). The veil (0.09) spreads the glow. Rust has no light shafts.

## 3. What a built map gets today

- **Style.** The Compile panel passes no `--style` (`compile_panel.cpp:640-657`), so the converter uses `iw3`
  (`convert.cpp:99, 3126`). The stock template (`replay_vision_iw8_template.inc`) is applied only for `iw8`
  (`V:622-705`), so every vision field is the table default unless something below sets it. VERIFIED.
- **Analytic fog: off.** With no `iw8r_fog`: `fogMaxOpacity` 0 (`V:755-760`) and `fogAtmosphericScattering` 0
  (default) give the plain branch with `T = max(..., 1 - maxOpacity) = 1`: no fog, sky included (research 111
  section 5). VERIFIED.
- **Baked fog.** `iw8r_fog "start halfway r g b"` (`atmosphere.h:41`, `readAtmosphere` `atmosphere.cpp:69-82`) is
  written as `setExpFog` in the map script (`fogGsc`, `compile_iw8.cpp:3896`); the converter reads it
  (`ReadMapExpFog`, `P:11148`), writes the plain-fog fields with `fogMaxOpacity` 0 (`V:761-782`) and bakes the fog into
  the surface shader (`FogShaderDefines`, `V:462-515`; `hlsl:1751-1830`). The shader skips sky surfaces
  (`hlsl:1761, 1769`). So the ground fades to one flat colour and the sky never meets it. VERIFIED.
- **Why the native fog stays off.** The surface shader already fogs opaque surfaces, and the native pass fogs every
  opaque pixel after them: turning both on would fog twice (`V:773-776`). VERIFIED in source.
- **Volumetric fog: off.** `volumetricAirDensity` default 0 (`replay_vision_fields.inc:144`), and the pass runs only
  when the density is above 0 or local media exist (research 109 section 2.1). Veil default 0 (`:143`). No editor
  script sets `r_volumetrics`. VERIFIED. Whether `iw8r_volume` boxes count as local media and start the pass: INFERRED
  yes (GT `R_VOL_AnyVisible`), not checked.
- **Lamps.** The editor writes no `volumetric` field for a lamp (`primaryLightJson`, `kLightKeys`,
  `compile_iw8.cpp:735-757`); the converter reads it at `convert.cpp:1387` (default false). No built lamp scatters.
  VERIFIED.
- **What the editor exposes.**
  - `iw8r_fog`, `iw8r_film`, `iw8r_lut`, `iw8r_vision` (`atmosphere.h:41-47`).
  - `iw8r_vision "name=value; name=x y z"` becomes `extracted/vision/<id>.overrides.json` (`compile_iw8.cpp:3897-3903`),
    applied **last** by the converter (`V:808-840`, read at `P:11211-11222`). Names come from
    `replay_vision_names.inc`, or a raw token. The volumetric fields and `veilStrength` have **no names** there, so
    today they work only as tokens (`NSKNLMKQSR=0.01`); a name such as `volumetricAirDensity` stops the build ("names
    no Replay vision field"). VERIFIED.
  - `iw8r_vision` on a trigger brush makes a vision volume (`compile_iw8.cpp:3635-3654`, `P:11235-11259`).
  - `iw8r_volume` boxes become world JSON `volumetrics` (`compile_iw8.cpp:3000-3048`). Units unknown (research 46).
  - World JSON `fog`, `iw7_volumetrics` and `iw7_tonemap` are IW7-extraction keys; the editor does not write them.
    `iw7_volumetrics` skips every `volumetric*` key (`V:744`), so it cannot carry the air. `fog` and `setExpFog`
    together throw (`P:11201-11202`).
- **Sun units.** Under `iw3` the sun intensity is the `suncolor` luminance, about 1 (research 55), where stock suns are
  8.6 to 30.7. The analytic fog's in-scatter is absolute (EV), so stock EVs are about 3.6 EV too bright on a built
  map. The volumetric terms scale with the sun radiance and the light grid, so they need no shift. INFERRED from the
  formulas.

## 4. Change list

1. **Names for the fields** (converter): add `veilStrength` and the ten `volumetric*` names with their tokens to
   `replay_vision_names.inc` (the same tokens as `V:724-734`); then regenerate the editor's `core/vision_fields.inc`
   with `tools/gen_vision_fields.py`.
2. **fogSunDir in both styles** (converter, `BuildReplayVision`): set `RLNQONPKS` from `sunDirection` for `iw3` too
   (move `V:644` out of the `iw8` block), before the overrides. With the plain fog at opacity 0 it changes nothing.
3. **No fog twice** (converter, `P:11224` / `FogShaderDefines` call at `P:12279`): when the final vision has
   `fogAtmosphericScattering` 1, do not pass the baked fog (`expFog`/`nativeFog`) to `FogShaderDefines`, and warn if a
   `setExpFog` is also present. The opaque pass is then fogged only by the game.
4. **Fog on transparent layers** (`hlsl`): the native pass runs before them, so they need their own. Port research
   111's atmospheric branch (`game_fog.cpp` is the CPU copy) reading `replayLighting[131..137]` under a new define, with
   `MAP_LIGHTING_ROWS` at least 138. Use the existing `MAP_VOLUMETRIC_EXTINCTION` path (`hlsl:1773-1836`) for all
   blended and additive materials when air density > 0 (`g_extinctionDefines`, `iw3_render_assets.cpp:5626`).
5. **Lamps** (editor): add `{"iw8r_volumetric", "volumetric", LightKind::Switch, ...}` to `kLightKeys`
   (`compile_iw8.cpp:735`). Default off, as rust; on for lamps that should glow in the haze.
6. **A haze preset** (editor): a worldspawn key (for example `iw8r_atmosphere "desert_haze"`) expanded in the compile
   step next to `compile_iw8.cpp:3897` into overrides, merged under the user's `iw8r_vision` (the user's keys win),
   with `fogSunDir` from `sundirection` and the EV shifted by `log2(11.7 / sunLuminance)`; it clears `iw8r_fog`. Add a
   "Desert haze" entry to `tod_presets.cpp`.
7. **Viewport for own maps** (`camera_view.cpp:2399-2425`): veil, volumetric and `game_fog` are applied only to stock
   maps (`if (stock)`); feed them the own map's merged overrides too, so the preview shows what the build does.

### Suggested "bright desert haze" (same family as rust, not its numbers)

| Field | Value | Rust |
|---|---|---|
| fogAtmosphericScattering | 1 | 1 |
| fogStartDist / HalfPlane | 400 / 5000 | 512 / 4000 |
| fogMaxDistance / SkyDistance | 1000000 / 800000 | 2000000 / 1440572 |
| fogExtinctionDesaturation | 0.8 | 0.85 |
| fogInscatteringEv | 20.6 in stock units: **20.6 - log2(11.7 / L)**, L = suncolor luminance (about 17.0 for "1 0.96 0.88") | 21 |
| fogRayleighAlbedo / MieAlbedo | .56 .50 .42 / .70 .64 .55 | .518 .478 .431 / .643 .612 .557 |
| fogMieAnisotropy / BlendStrength | 0.6 / 0.3 | 0.5 / 0.25 |
| fogSkyMin / MaxAngle | 45 / 80 | 40.96 / 78.4 |
| fogHeightFog base / half / additional | ground z / 250 / 0.04 | 0 / 170 / 0.032 |
| fogSunDir | the map sun | rust's sun |
| veilStrength | 0.09 | 0.09 |
| volumetricAirDensity / Absorption | 0.012 / 0 | 0.02 / 0 |
| volumetricAlbedo | 1 .94 .84 | 1 .957 .910 |
| volumetric Sun / Spot / Omni / Ambient | 0.4 / 1 / 1 / 0.6 | 0.01 / 1 / 1 / 0 |
| volumetric height base / half | ground z / 600 | 0 / 0 |
| volumetricSunAnisotropy | 0.75 | 0.8 |

Where rust's volume only dims, this one is lit by the sun (as quarry and donetsk are), so sun shafts show through
gaps, while the analytic part keeps rust's warm, sky-filling haze.

## 5. Offline numbers (computed, `fog_ref.py` and the research 109 march; eye 64 units up, stock units)

| | rust | haze |
|---|---|---|
| analytic T at 1000 / 4000 / 16000, level | 0.94 / 0.65 / 0.15 | 0.94 / 0.69 / 0.20 |
| analytic L at 4000, level, 40 degrees from the fog sun | 3.56 3.09 2.50 | 3.35 2.70 1.91 |
| sky T at horizon / 10 / 20 / 45 degrees up | 0 / 0.11 / 0.51 / 1 | 0 / 0.18 / 0.60 / 1 |
| volume T, level, beyond 2180 units / 20 degrees up | 0.647 / 0.647 | 0.785 / 0.850 |
| volume sun in-scatter / sun radiance at 0, 15, 30, 90 degrees | .011 .010 .002 .000 | about .25 .23 .07 .005 (sun 0.4) |

Shipment, village2 and quarry give volume T 0.957, 0.859, 0.804 and peak sun in-scatter 0.10, 0.04, 0.23.

## 6. Open points

- **Sky test on a built sky.** The converted sky is drawn at reversed depth 1e-7 (`replay_world_vertex.hlsl:145-146`).
  The native pass treats a pixel as sky when its float Z x 6 is at least `min(cb7[171].z x 1e8, 392999)` (research
  111 section 1). zNear / 1e-7 overflows fp16 (65504 x 6 = 393024), so it should count as sky: INFERRED. If it does not,
  the whole sky takes the full ground fog at `fogMaxDistance`. Offline check: read the float-Z pass and `r_znear` in
  the Replay exe. Only the game shows the final look.
- **Transparents.** That stock transparent shaders fog themselves from cb7[131..137] is INFERRED (research 111
  section 12); the port in change 4 relies on it.
- **Lamp meaning.** That bit 30 gates the lamp's whole in-scatter is VERIFIED in shader 481; that the game sets it from
  `isVolumetric` is GT, INFERRED for Replay.
- **EV shift.** The EV shift by the sun ratio assumes built and stock scenes scale with their suns (no /pi either
  side, research 117). Offline check: render a built map's fog in the editor's Game Lighting with `game_fog` and compare
  haze-to-ground ratios with the rust bench picture.
- **Brightness of the editor sky.** Research 109 section 2.7 found the editor's sky about a third too dark, which the
  missing volume hid. The volume's real effect on the sky (and the overall exposure of a built map) needs the game.
- **`iw8r_volume` units** (density, falloff) are still unknown (research 31, 46).
