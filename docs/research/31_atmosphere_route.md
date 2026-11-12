# 31. Atmosphere route: fog, vision, grading, exposure, sun flare, volumetrics, decal volumes

Scope: roadmap L5 items 5.3 (sun flare and cascades), 5.6 (vision, fog, grading, exposure), 5.7 (volumetrics) and 5.8 (decal volumes), as the x64-zt `buildextraction` route consumes them. Date: 2026-11-08. Read-only: nothing was built, run, edited or committed, and nothing under `E:\IW8\Builds\1.20-replay` was touched.

Converter root: `E:\mw124\x64-zt\src\convert\iw3_iw8\` (short name `iw3_iw8\` below). Replay-side readers: `E:\mw124\x64-zt\src\games\iw8\common\replay_render.cpp`. Editor compile: `E:\IW8-Radiant\src\core\compile_iw8.cpp`. Extraction contract: `docs\research\15_iw8_extraction_contract.md`, `docs\research\17_x64zt_buildextraction.md`, `docs\research\09_iw8_render_features.md`.

Notation: `P:` = `iw3_fastfile.cpp`, `R:` = `iw3_render_assets.cpp`, `V:` = `iw3_vision.cpp`, `L:` = `iw3_lightgrid.cpp`, `C:` = `convert.cpp`. All line numbers are from the checkout read today.

## 0. Corrections to earlier docs

- Doc 15 section 2.1 says `sky` is "Not read by this route" and "absent from every reader". Wrong. `R:6186` reads it with `world.at("sky")`, so it is REQUIRED (an empty string is allowed, `R:6187-6193`).
- Doc 15 section 2.1 says `volumetrics` is "Not read by this route". Wrong. `R:5682` reads it with `world.value("volumetrics", [])`, and `P:6146` puts it in the mesh output.
- Doc 15 section 2.1 says the `fog` object is OPTIONAL. The key is optional, but once present every key except `color`, `sun_color` and `sun_dir` is read with `at()` and throws if missing (`P:10888-10909`).
- Doc 09 section 10 and section 11 cite `iw3_fastfile.cpp:10747` and `:10752-10760`. Today's lines are `P:10919-10923` and `P:10924-10926`.

## 1. Summary table

| # | Feature | Optional inputs the converter reads (exact) | Reader | Absent behaviour | Minimal editor model | Example |
|---|---|---|---|---|---|---|
| 5.6a | Exponential fog (IW3 style) | `maps/mp/<m>.gsc` and `maps/createart/<m>_art.gsc`, call `setExpFog(start, halfway, r, g, b [, transition])` | `V:214-263`, called `P:10857` | No IW3 fog. Replay default vision fog has `fogMaxOpacity` forced to 0 (`V:757-760`) | Map script line, or worldspawn-level fog keys written into a generated `.gsc` | 3.1 |
| 5.6b | Native (IW7-shape) fog | `world.json` `fog` object with 12 required keys | `P:10888-10916` | No native fog | Dedicated fog panel writing `world.json` `fog` | 3.2 |
| 5.6c | Film grade (IW3 `r_film*`) | `vision/<m>.vision` text with `r_filmenable "1"` and `r_film*` keys; `visionsetnaked` may name another vision | `V:265-334`, called `P:10860` | No film; neutral grading CLUT | Grade panel writing a `.vision` text file | 3.3 |
| 5.6d | Grading LUT (IW7 shape) | `world.json` `grading_clut {size, file}`, file = RGBA8 `size^3 x 4` bytes, sRGB, red fastest | `P:10866-10876`, `V:577-613` | Identity CLUT only if film absent and `exposureAdjust != 0` (`P:10877-10885`); otherwise no grade | LUT import panel, or generate from film | 3.4 |
| 5.6e | Exposure | `exposureAdjust` in the build metadata JSON, -8..8 EV (`C:225-230`, written to `map.json` `C:291-292`) | `C:2514`, `P:10877`, `V:520-525` | 0 EV | Exposure slider in map settings | 3.5 |
| 5.6f | Tonemap and vision fields | `world.json` `iw7_tonemap` object (17 names, `V:680-698`) | `P:10673`, `V:678-702` | Table defaults; style iw8 also sets `tonemapExposure` from the light-unit scale | Advanced tonemap panel | 3.6 |
| 5.7 | Volumetrics | `world.json` `volumetrics` array of boxes with masks | `R:5682-5732`, Replay `replay_render.cpp:1406-1465` | No volumes (the stock air of the template vision still applies) | Volume tool, sidecar list | 3.7 |
| 5.7b | IW7 veil and light scatter | `world.json` `iw7_volumetrics` object (keys in `V:716-735`) | `P:10919`, `V:706-754` | Stock scatter and veil | Advanced panel | 3.8 |
| 5.8 | Decal volumes | No world input. Only IW3 FX materials (`materials/<name>.json`, technique `[mw]c_l_sm_b0c0(n0)?(s0)?`, cameraRegion `decal`) create a `decalVolumeDefinition` (`R:2062-2064`, `R:2141-2186`, `R:8533-8577`) | as stated | No decal volume | Material-level decal, placement UNKNOWN | 3.9 |
| 5.3a | Sun sprite and flare | `sun/<m>.sun` (`r_*` keys, `sun/` only) | `P:10462-10521` (render `sunFlare`, `P:12918`); `V:336-375` (sprite) | No sprite, no flare; stock blind and glare | Sun panel writing `sun/<m>.sun` | 3.10 |
| 5.3b | Sun direction, colour, intensity | Worldspawn `sundirection`, `suncolor`, `sunlight` in `.ents` (required `world.sun` and `primary_lights` too) | `P:8133-8225` | Exported `world.sun` angles, colour, intensity | Sun inspector, already written by `compile_iw8.cpp:1094-1107` | 3.10 |
| 5.3c | Sun shadows and cascades | Per-material `gameFlags` `CASTS_SHADOW` (`R:2566-2576`); sun shadow mask from `world.sun_primary_light_index` (`P:6136-6139`); vision 148-149 | as stated | Every material casts (`R:193`); mask as stated below; cascades UNKNOWN | Per-material shadow flag, sun index | 3.11 |
| 5.3d | Sky | `world.sky` (required, string) | `R:6186-6196` | Empty string: dark placeholder cubemap | Sky field | 3.12 |

## 2. Shared facts

- Export root. `exportRoot` is the `extracted/` folder: `worldPath` up three parents (`P:10646`). Every optional file below is relative to `extracted/`. The map name is `sourceMap` = the world file name without `.d3dbsp` and `.replay-world.json` (`P:10647-10652`).
- Output vision. The converter always writes `vision/<target map>.vision` into the zone, generated from defaults plus overrides (`P:10920-10926`, `V:615-824`). The 218-field file is never authored directly. An authored file's Replay tokens are ignored: `ReadPreparedRawFiles` skips every `.vision` (`P:1604-1606`), and only `r_film*` are read from an authored file (`V:298-332`).
- Light units. `--style iw3` (default, `C:87`) keeps IW3 units. `--style iw8` (`C:2516-2517`) matches the compiled sun against shipped maps (`P:10670-10687`, `V:407-460`) and sets `tonemapExposure = 10 + log2(lightUnitScale)` (`V:674`).
- Trap. A non-empty `world.iw7_tonemap` disables that match (`P:10673-10674`). The map then keeps IW7 light units (`lightUnitScale` 1, no exposure match). Writing `iw7_tonemap` changes the scene's light units, not just the look.
- Trap. `setExpFog` (script) and `world.fog` (JSON) cannot both be present (`P:10910-10911` throws).
- Precedence. An authored film (`ReadMapFilm`) wins over `world.grading_clut`, with no warning (`P:10860-10866`).
- `FogShaderDefines` bakes fog into the surface shader for both styles (`V:462-515`). Replay's native fog stays off (`fogMaxOpacity` 0, `V:776`, `V:793`).

## 3. Details

### 3.1 setExpFog (IW3 fog, feature 5.6a)

- Inputs: `maps/mp/<m>.gsc` and `maps/createart/<m>_art.gsc`. Comments are stripped (`V:52-84`, `V:198-211`). Both files are concatenated, so a fog call in either counts.
- Format: regex `\bsetexpfog\s*\(([^()]*)\)`, case-insensitive (`V:220`). Arguments are literal floats only. Five or six arguments: `start`, `halfway`, `r`, `g`, `b`, optional `transition` (ignored). A call is rejected (warning, skipped) if `start < 0`, `halfway <= 0`, or any argument is not a literal (`V:242-248`).
- Clamps: colours to 0..1 (`V:251`). In the vision: `fogStartDist` to 0..20000 and `fogDensity` (halfway) to 1..70000 (`V:765-768`).
- Multiple calls: only the first is converted, and a warning is logged when a later call differs (`V:254-257`).
- Absent: no fog and `fogMaxOpacity` = 0 (`V:757-760`).
- Shader: `ln2 / halfway` density and a colour (`V:510-514`, `V:512`).
- Editor data: fog start, halfway distance, RGB colour. Write as a generated `maps/mp/<m>.gsc` containing one call. This is the simplest supported route and works for both styles.

Example, `extracted/maps/mp/room.gsc`:

```
main()
{
    setExpFog(0, 1800, 0.55, 0.60, 0.70);
}
```

### 3.2 Native fog object (IW7 shape, feature 5.6b)

- Input: `world.json` `fog` object. Read in `P:10888-10916`.
- Keys read with `at()` (missing key throws): `density` (halfway, float), `start`, `color_intensity`, `max_opacity`, `sun_density`, `sun_color_intensity`, `sun_begin_fade_angle`, `sun_end_fade_angle`, `sky_intensity`, `sky_min_angle`, `sky_max_angle`.
- Optional vectors (fall back to the struct default when absent or not length 3): `color` (default 1 1 1), `sun_color` (default 1 1 1), `sun_dir` (default 1 0 0). These are read at `P:10890-10906`.
- Struct defaults (`V:27-43`) are unused once the object is present, because `at()` throws first.
- Converted to vision (`V:783-804`): `fogDensity` = density, `fogStartDist` = start, `fogColor` = color, `fogColorIntensity` = color_intensity x lightUnitScale, `fogSunDensity`, `fogSunColor`, `fogSunColorIntensity` (x lightUnitScale), fade angles, `fogSunDir`, `fogSkyIntensity` forced to 0, sky min and max angles, `fogAtmosphericScattering` 0.
- Shader: `FogShaderDefines` emits linear fog with sun-side colour and fade (`V:485-507`).
- Units: colour intensities are in the converted light units (the `lightUnitScale` multiply). UNKNOWN whether the editor should write IW7 units or IW3 units.
- Editor data: a fog object in the sidecar, written as `world.json` `fog` with all 12 required keys.

Example (world.json fragment; the file also needs the rest of the world schema from doc 15 section 2.1):

```
"fog": {
  "density": 70000, "start": 0,
  "color": [0.80, 0.85, 0.90], "color_intensity": 1.0,
  "max_opacity": 0.0,
  "sun_density": 70000, "sun_color": [1.0, 0.95, 0.85], "sun_color_intensity": 1.0,
  "sun_begin_fade_angle": 0.0, "sun_end_fade_angle": 360.0, "sun_dir": [1, 0, 0],
  "sky_intensity": 0.0, "sky_min_angle": 0.0, "sky_max_angle": 90.0
}
```

### 3.3 Film grade (IW3 r_film*, feature 5.6c)

- Input: `vision/<m>.vision`, the IW3 text vision. `m` is the source map unless `VisionSetNaked("<name>")` names another vision that exists under `vision/` (`V:268-279`). The map or its createart script holds that call.
- Format: one `key "value"` per line, regex `^\s*(\S+)\s+"([^"]*)"` (`V:285`). Keys are lower-cased (`V:295`).
- Enable: `r_filmenable` must be present and non-zero. If absent it reads as 0 and the film is off (`V:313-316`).
- Keys: `r_filmcontrast`, `r_filmbrightness`, `r_filmdesaturation`, `r_filminvert` (0 or non-zero), `r_filmlighttint` and `r_filmdarktint` (three floats). Defaults: contrast 1, brightness 0, desaturation 0, tints 1 1 1 (`V:47-55`).
- An identity film is dropped (`V:326-327`).
- Applied by `ApplyFilm` (`V:156-190`) into the grading CLUT of `BuildClut` (`V:520-559`), which is written as `mw120r_<map>_clut` (`P:10862`).
- Non-film keys in the file are ignored, and so are all other Replay tokens (`V:298-332`).
- Editor data: the five film scalars and two tints. Write as a generated `vision/<m>.vision` with only `r_*` keys. Do not write Replay tokens into it: they are ignored.

Example, `extracted/vision/room.vision`:

```
r_filmenable "1"
r_filmcontrast "1.1"
r_filmbrightness "0"
r_filmdesaturation "0.85"
r_filminvert "0"
r_filmlighttint "1.05 1 0.90"
r_filmdarktint "0.90 0.95 1"
```

### 3.4 Grading LUT (IW7 shape, feature 5.6d)

- Input: `world.json` `grading_clut` = `{"size": N, "file": "<path under extracted>"}` (`P:10866-10876`).
- File: `size^3 x 4` bytes, RGBA8, sRGB, red fastest (`V:577-583`). Size must be at least 2. Trilinear lookup (`V:584-612`).
- Only read if no film is authored (film wins, see section 2).
- Editor data: LUT file path and size. Store the LUT as a file in the extraction (`grading/<name>.rgba`), not in the JSON.

Example:

```
"grading_clut": { "size": 32, "file": "grading/room_clut.rgba" }
```

### 3.5 Exposure (feature 5.6e)

- Input: `exposureAdjust` in the build metadata JSON, a finite number from -8 to 8 EV (`C:225-230`). Written to the package `map.json` when non-zero (`C:291-292`).
- Effect: scales the linear input of the grading CLUT by 2^EV (`V:520-525`). It is a grade-level exposure, not the tonemap exposure field.
- If the map has neither a film nor a LUT, a non-zero `exposureAdjust` creates an identity CLUT with that exposure (`P:10877-10885`).
- Editor data: one map-level number. Write it as the metadata key.

Example metadata fragment: `{"exposureAdjust": 0.5}`.

### 3.6 Tonemap fields (feature 5.6f)

- Field table: vision indices 63-82 (`replay_vision_fields.inc` lines 69-88). Names and defaults in section 5.
- Style iw3: defaults from the table (`tonemapMode` 3, `tonemapExposure` 10, `tonemapMaxExposure` -10, and so on), unless `iw7_tonemap` overrides them.
- Style iw8: the stock Shipment template (`replay_vision_iw8_template.inc` lines 31-48) is applied first, then the converter erases its tonemap entries (`V:669-673`: tonemapMode, tonemapAuto, the auto curve, the exposure limits, the band EVs and adjusts, and so on). Those fields therefore revert to the table defaults. Only `tonemapExposure` is set again, to 10 + log2(lightUnitScale) (`V:674`).
- `iw7_tonemap` (style-independent override, `V:678-702`): the converter maps these JSON names to tokens: `tonemapAuto` NLMSSPLMPK, `tonemapAutoExposureAdjustCurve` MRKQQMPTKN, `tonemapExposure` LPKQPKSRKQ, `tonemapAutoExposureAdjust` NOOTTPORTQ, `tonemapMaxExposure` LTNRTLQOKQ, `tonemapExposureAdjust` NNRKNLLSQN, `tonemapAdaptSpeed` MMQMQKSTS, `tonemapDarkEv` LSLPLPRQNO, `tonemapMidEv` NLPRMSTSMM, `tonemapLightEv` MOPORNQOTQ, `tonemapDarkExposureAdjust` MTPSRMRRLP, `tonemapMidExposureAdjust` LRPSSPPPPT, `tonemapLightExposureAdjust` NMKNKNMKNN, `tonemapMinExposureAdjust` MOSRNQNQTN, `tonemapMaxExposureAdjust` NSROSTQOSN, `tonemapGrainStrength` LNQNRRPNPP, `toneWhitePoint` LRROOOSOMM.
- Trap: `iw7_tonemap` also disables the light-unit match (section 2). Use it only for IW7-unit maps.

### 3.7 World volumetrics (feature 5.7)

- Input: `world.json` `volumetrics` array (`R:5682`). The converter copies each entry, replaces each mask `image` with a resident image name (`R:5684-5731`), and writes the array into the mesh JSON (`P:6146`).
- Replay reader (`replay_render.cpp:1406-1465`, in `Load` at `:1253`):
  - `live_path` (string, optional, at most 255 chars).
  - `flags` (uint, optional, default 0). Only bits 2 and 4 are allowed (`flags & ~6` must be 0, `:1436`).
  - `center` (3 floats), `half_size` (3 floats, each > 0), `color` (3 floats), `axes` (exactly 3 vectors of 3).
  - `density` (required, >= 0), `falloff` (required, > 0), `height_fade` (optional, default 0), `anisotropy` (optional, default 0).
  - `masks` (optional array, at most 4). Each mask: `type` (must be 2, opacity), `axis` (0 to 2, plus 4 for negative), `image` (2D image, `R:5686-5692`), `scale` (2 floats, non-zero), `offset` (2), `scroll` (2).
- Only opacity masks draw (`:1451`).
- Units of `density`, `falloff` and `height_fade`: UNKNOWN. Doc 09 section 10 states no unit.
- IW3 converter route: no IW3 source produces volumetrics. The only producer on record is the IW7 exporter (`iw7_iw8\export_world.cpp:2016-2025`, which writes `live_path`, `flags`, `center`, `axes`, `half_size`, `color`, `density`, `falloff`, `masks`; it does not write `height_fade` or `anisotropy`).
- Editor data: an oriented box list. Mask images are DDS files under `images/`.

Example (world.json fragment):

```
"volumetrics": [
  {
    "live_path": "", "flags": 0,
    "center": [0, 0, 128],
    "axes": [[1, 0, 0], [0, 1, 0], [0, 0, 1]],
    "half_size": [256, 256, 128],
    "color": [1, 1, 1],
    "density": 0.2, "falloff": 64.0,
    "height_fade": 0.0, "anisotropy": 0.3,
    "masks": []
  }
]
```

### 3.8 IW7 veil and light scatter (feature 5.7b)

- Input: `world.json` `iw7_volumetrics` object (`P:10919`).
- Used keys (`V:713-751`): `r_volumeLightScatter`, `r_volumeLightScatterLinearAtten`, `r_volumeLightScatterQuadraticAtten`, `r_volumeLightScatterAngularAtten`, `r_volumeLightScatterDepthAttenNear`, `r_volumeLightScatterDepthAttenFar`, `r_volumeLightScatterBackgroundDistance`, `r_volumeLightScatterColor`, `veilStrength`. Names that start with `volumetric` are skipped (`V:744-745`), so the air density, absorption, albedo and brightness keys are not used.
- Absent: the stock values of the template stay.
- Editor data: scatter and veil scalars only.

### 3.9 Decal volumes (feature 5.8)

- No world-level input. Decal volumes come only from IW3 FX materials (`R:8533-8577`). The converter does not write `GfxStaticDecalVolumePacked` placements (no writer found in `iw3_iw8\` or `iw7_iw8\export_*`). Placement UNKNOWN.
- Material gate (`R:2062-2064`): `techniqueSet` matches `[mw]c_l_sm_b0c0(n0)?(s0)?`. Read through `FindMaterial` (`R:2001`, `materials/<name>.json`).
- Required keys (`R:2068-2070`, `R:2071-2073`, `R:2141-2186`):
  - `_game` "iw3", `_type` "material", `_version` 1.
  - `textureAtlas` with `rows` and `columns` (both must be 1 for decals, `R:2144-2145`).
  - `cameraRegion` "decal" and `stateFlags` 52 or 53 (`R:2143-2144`).
  - `textures` (1 to 3 entries): one `colorMap` (required), optional `normalMap`, optional `specularMap`. Any other semantic throws (`R:2166-2167`).
  - `constants`: `colorTint` required (`R:2184-2185`). `envMapParms` optional. Any other constant throws (`R:2181`).
- Emitted (`R:8533-8577`): `decalVolumeMaterial` = `i/mw120r_fx_decal_<id>` and a six-channel `decalVolumeDefinition` with flags 1100 and a one-cell atlas (`R:8567-8576`).
- Default when a decal material is not built: `i/vfx_decal_surface_glass_2` (`R:4583`).
- Vision field 207 `decalVolumeDrawDistance` default 5000 (`replay_vision_fields.inc:213`). Not overridden by the converter.
- Editor data: decal material records (colour, normal, specular, tint). Placement box: UNKNOWN (no source in the converter, and no `GfxStaticDecalVolumePacked` writer). Keep as a material-level feature until a placement writer exists.

Example, `materials/<name>.json` (decal FX material; the FX graph must reference it for it to load):

```
{
  "_game": "iw3", "_type": "material", "_version": 1,
  "techniqueSet": "mc_l_sm_b0c0",
  "cameraRegion": "decal", "stateFlags": 52,
  "textureAtlas": { "rows": 1, "columns": 1 },
  "textures": [ { "semantic": "colorMap", "image": "decal_grime" } ],
  "constants": [ { "name": "colorTint", "literal": [1, 1, 1, 1] } ]
}
```

### 3.10 Sun sprite, flare and sun inspector (feature 5.3a, 5.3b)

- Sun direction and colour. Worldspawn keys in the `.ents` (`P:8162-8170`, `P:8185-8191`):
  - `sundirection` "pitch yaw roll" (degrees). Negative pitch looks up (`V:362`). Default: exported `world.sun.angles`.
  - `suncolor` "r g b". Default: exported `world.sun.color`, else `ambient` normalised.
  - `sunlight` (float >= 0). Default: exported `world.sun.intensity`.
  - If `commonWorld.primary_lights[sun_primary_light_index]` is a type-1 light, its colour and luminance replace these (`P:8198-8209`).
- Sun light index: `world.sun_primary_light_index` (`P:10694-10699`, must be below `primary_light_count`).
- Sun sprite file: `sun/<m>.sun`. Two readers:
  - `ReadSunFlare` (`P:10462-10521`, render key `sunFlare`, `P:12918`). Keys (case-insensitive, unknown keys ignored, value in quotes):
    - `r_sunsprite_size` (default 0), `r_sunflare_min_size`, `r_sunflare_max_size`, `r_sunflare_min_angle` (45), `r_sunflare_max_angle` (2), `r_sunflare_max_alpha` (0), `r_sunflare_fadein` (0.7 s), `r_sunflare_fadeout` (1 s).
    - `r_sunblind_min_angle` (30), `r_sunblind_max_angle` (5), `r_sunblind_max_darken` (0.5), `r_sunblind_fadein` (2.5), `r_sunblind_fadeout` (6).
    - `r_sunglare_min_angle` (30), `r_sunglare_max_angle` (5), `r_sunglare_max_lighten` (0.3), `r_sunglare_fadein` (2.5), `r_sunglare_fadeout` (6).
    - Angles become cosines, fade times milliseconds (`P:10497-10501`).
    - Without the file: the stock Replay values apply (no sprite, no flare; `P:10459-10461`).
  - `ReadMapSun` (`V:336-375`, shader and vision side). Needs `r_sunsprite_shader` to be a non-empty string (`V:347-349`, only emptiness is checked). Uses `r_sun_fx_position` "pitch yaw" (`V:357-361`), `r_sunsprite_size` (`V:369-370`, 480-line pixels at a 65 degree view, clamped 0.2 to 10 degrees) and `r_sunflare_max_alpha` (clamped 0..1, `V:371`).
  - Sprite texture: the converter does not convert IW3 sun sprite materials. Replay's stock `,sun` and `,sun_flare` are used (`P:2020-2022`, `P:2043-2044`).
- Editor data: sun direction, colour and intensity (worldspawn), and the `r_*` values above. The editor already writes `sundirection` and `suncolor` (`compile_iw8.cpp:992-1016`, `:1094-1107`) and a lighting block (`compile_iw8.cpp:1290`, `:1335`). It does not write `sun/<m>.sun`.

Example, `extracted/sun/room.sun`:

```
r_sunsprite_shader "sun"
r_sunsprite_size "16"
r_sun_fx_position "-35 120"
r_sunflare_min_size "0"
r_sunflare_max_size "120"
r_sunflare_max_alpha "0.4"
r_sunflare_min_angle "45"
r_sunflare_max_angle "2"
r_sunflare_fadein "0.7"
r_sunflare_fadeout "1"
```

Worldspawn example in the `.ents`: `"sundirection" "-35 120 0"`, `"suncolor" "1 0.92 0.84"`, `"sunlight" "1.3"`.

Note: `ReadSunFlare` does not require the shader, but the shader sprite (`ReadMapSun`) does. A `.sun` with only flare keys gets flare and blind and glare, but no shader sprite. Confirm this is intended.

### 3.11 Sun shadows and cascades (feature 5.3c)

- Membership: each material's `gameFlags` containing `CASTS_SHADOW` sets `castsShadow` (`R:2566-2576`). With no `gameFlags` key the default is true (`R:193`).
- Mask: `sunCount = world.sun_primary_light_index`, rejected above 5, and `sunShadowMask = ((1 << sunCount) - 1) << 1` (`P:6136-6139`). This value uses the index as a count. The sun index is 1 in the Discovery sample (doc 15 section 2.1). For index 0 the mask is 0 and no surface casts a sun shadow. SUSPECT: verify intent before the editor relies on this.
- Sun-shadow vision fields: `sunshadowSoftness` 148 (default 0) and `sunshadowSampleSizeNear` 149 (default 0.25). Not set from any input.
- Cascades: no input, no count, no selection rule. UNKNOWN.
- Editor data: a per-material `CASTS_SHADOW` flag and the sun index. Cascade bands UNKNOWN.

### 3.12 Sky (feature 5.3d)

- `world.sky` (required string, `R:6186`). Empty means the dark placeholder cubemap (`R:6187-6193`). Otherwise an images cubemap of six faces (`R:6194-6196`).
- Sky surfaces: sky surfaces are drawn at infinity, with their vertices as directions (`P:2197`, `P:5315-5316`). Surfaces whose material name contains `sky` are left out of the playable-bounds calculation (`P:11340`).

### 3.13 Light grid (read as asked; not an atmosphere input)

- Paths: `maps/mp/<m>.d3dbsp.gpulightgrid.bin` as-is (`L:1195-1209`), or `maps/mp/<m>.d3dbsp.lightgrid.json` (`L:1210-1233`). The JSON has `schema` 1, `name` "maps/mp/<m>", optional `available` (false = none), `row_axis`, `col_axis`, `mins`, `maxs`, `entry_count`, `color_count`, `row_count`, `row_data.bytes`, and `sun_primary_light_index` and `has_light_regions` (`L:1439-1449`).
- Absent: "source map has no exported light grid" and no grid (`L:1214`).
- Editor data: none for this milestone. The grid is baked data, not a setting.

## 4. Editor implementation plan

### 4.1 Panels

1. Sun and sky (5.3). Sun direction (pitch, yaw), colour, intensity. Sun sprite and flare (`r_sunsprite_size`, flare min and max size and angle, max alpha, blind and glare). Sky name. Writes worldspawn `sundirection`, `suncolor`, `sunlight` and `sun/<m>.sun`.
2. Fog (5.6a/b). Mode selector: none, IW3 `setExpFog`, or native `fog` object (mutually exclusive, see section 2). IW3 mode: start, halfway, colour. Native mode: the 12 keys, with a warning about IW7 units.
3. Grade and exposure (5.6c/d/e). Film scalars and two tints (writes `vision/<m>.vision`). LUT import (writes `grading/<name>.rgba` and `grading_clut`). Exposure EV (-8..8, metadata). Warn that a LUT and a film together: the film wins.
4. Tonemap (5.6f, advanced). Only `iw7_tonemap` fields. Warn that this switches the map to IW7 light units.
5. Volumes (5.7). List of oriented boxes (centre, three axes, half size, colour, density, falloff, height fade, anisotropy, masks of type opacity). Preview the boxes in the viewport. Units of density and falloff UNKNOWN; start with a single value and check in game.
6. Decal materials (5.8). Material records only (colour, normal, specular, tint, atlas 1x1, technique). No placement. Placement is UNKNOWN.

### 4.2 Data model

- Worldspawn (in `.ents`): `sundirection`, `suncolor`, `sunlight`. Already written by the editor.
- Sidecar (one file per map in the project, for example `atmosphere.json`, owned by the editor): fog mode and values, film values, LUT path, `exposureAdjust`, `tonemap` overrides, `volumetrics` list, `iw7_volumetrics` overrides, sun flare values, and the material-level decal records. The compiler reads the sidecar and writes the extraction files listed in section 4.3.
- Do not put fog and grade into worldspawn keys: the converter does not read them there.

### 4.3 Compiler changes

No converter change is required for the core panels. The editor writes the existing extraction files:

| Panel | Writes | Converter reads |
|---|---|---|
| Sun | `.ents` worldspawn, `sun/<m>.sun` | `P:8162-8191`, `P:10462-10521`, `V:336-375` |
| Fog (IW3) | `maps/mp/<m>.gsc` with one `setExpFog` | `V:214-263` |
| Fog (native) | `world.json` `fog` | `P:10888-10916` |
| Film | `vision/<m>.vision` with `r_*` only | `V:265-334` |
| LUT | `grading/<name>.rgba`, `world.json` `grading_clut` | `P:10866-10876` |
| Exposure | metadata `exposureAdjust` | `C:225-230` |
| Volumes | `world.json` `volumetrics` | `R:5682-5732`, `replay_render.cpp:1406-1465` |
| Decal materials | `materials/<name>.json` | `R:2001-2186`, `R:8533-8577` |

Changes that would be needed:

1. Writer for `GfxStaticDecalVolumePacked` placements (no writer exists; UNKNOWN layout of `obbQuat`, `opacityParams`, `packedMaterialInfo`, doc 09 section 2). Needed for real decal boxes.
2. Confirm or fix the sun-shadow mask (`P:6136-6139`, index used as a count). If it is a bug, the fix is a converter change, not an editor change.
3. Optional: a vision override file (for example `vision/<m>.overrides.json` mapping a field name to a value) applied in `BuildReplayVision` (`V:615-824`). It would give the editor access to any of the 218 fields, including those in section 5. Not present today. Needs a converter change.
4. Optional: make the `setExpFog` and `fog` conflict a warning in the editor instead of a converter throw.

### 4.4 Tests the editor can run (the GUI test harness exists)

- Build an extraction with each panel on and off, run `zonetool -game iw8 buildextraction` (doc 17 section 6), and read the converter's info lines: `iw3: exponential fog ...`, `iw3: film contrast ...`, `iw3: sun sprite pitch ...`, `iw3: converted vision (...)`.
- In game: fog halfway distance, film tint, exposure EV, and volume placement. Record in MILESTONES. Not yet verified in game for any of these.

## 5. Vision field groups for the editor panel

Source: `replay_vision_fields.inc` (218 entries, index is the line order in the table, `static_assert` at `V:43`). The names come from that table where it has them, and otherwise from the converter's own names (`V:716-735`, `V:680-698`). Defaults are the reader's defaults for a missing token.

| Group | Index | Token (name) | Default | Editor meaning |
|---|---|---|---|---|
| Fog | 35 | MSTOLQOTKN (fogDensity) | 70000 | Halfway distance |
| | 36 | RPNOKKL (fogStartDist) | 0.1 | Start distance |
| | 37 | NTPTLNNQQL (fogColorIntensity) | 1 | Colour intensity |
| | 38 | NMLMMNQLSQ (fogColor) | 1 1 1 | Colour |
| | 39 | TRPROKTLS (fogMaxOpacity) | 1 | Max opacity (0 when baked by the shader) |
| | 49 | MKKLQTSKSO (fogAtmosphericScattering) | 0 | 1 selects atmospheric fog |
| | 50 | SNNNRLTML (fogHalfPlaneDistance) | 5000 | Height-fog half plane |
| | 51 | NRTMTRQTRQ (fogMaxDistance) | 50000 | Max distance |
| | 52 | NLQKSSPMKP (fogSkyDistance) | 50000 | Sky distance |
| | 53 | MKQSQRPPQR (fogExtinctionDesaturation) | 0 | Extinction desaturation |
| | 54 | LNOQTMMLSP (fogInscatteringEv) | 15 | In-scatter EV |
| | 55-56 | LNMRMNLNTT, LRKPNSNORL (fogRayleighAlbedo, fogMieAlbedo) | 1 1 1 | Scatter albedo |
| | 57-58 | MPRLMQNQO, NPMPRRLNPO (fogMieAnisotropy, fogMieBlendStrength) | 0.5 | Mie |
| | 59-62 | NOSTLRKPQ, POKSTQPPQ, LOLKSRNSMR, NKOQRKQRMT (fogHeightFog base, half plane, max offset, additional) | 0, 5000, 0, 0 | Height fog |
| | 217 | MLOSSOSRMM (fogSpline) | 0 | Spline (kind fogSpline) |
| Sun and sky | 33-34 | LLQOLROKNQ, NSMOTTMQOT (r_skyBlendAmount, r_skyBlendFeather) | 0, 0.001 | Sky blend |
| | 40-42 | MPNPSMMTTT, MLORTTKOLN, MRTOMMTTQM (fogSunDensity, fogSunColorIntensity, fogSunColor) | 70000, 1, 1 1 1 | Sun-side fog |
| | 43-45 | NKLTMMOOLP, NONQQTPNLT, RLNQONPKS (fogSunBeginFadeAngle, fogSunEndFadeAngle, fogSunDir) | 0, 360, 1 0 0 | Sun-side fade and direction (set from the map sun in style iw8) |
| | 46-48 | NORQKLTPTR, LOROLOTNMP, SPQNRNKQR (fogSkyIntensity, fogSkyMinAngle, fogSkyMaxAngle) | 0, 0, 90 | Sky fog |
| Exposure and tonemap | 63 | LTTOTTRLTS (tonemapMode) | 3 | Curve mode |
| | 64-65 | NLMSSPLMPK, MRKQQMPTKN (tonemapAuto, tonemapAutoExposureAdjustCurve) | 0 | Auto exposure |
| | 66 | LPKQPKSRKQ (tonemapExposure) | 10 | Fixed exposure (EV-like; set in style iw8) |
| | 67-69 | NOOTTPORTQ, LTNRTLQOKQ, NNRKNLLSQN (tonemapAutoExposureAdjust, tonemapMaxExposure, tonemapExposureAdjust) | 0, -10, 0 | Exposure limits |
| | 70 | MMQMQKSTS (tonemapAdaptSpeed) | 0.02 | Adapt speed |
| | 71-73 | LSLPLPRQNO, NLPRMSTSMM, MOPORNQOTQ (tonemapDarkEv, tonemapMidEv, tonemapLightEv) | 2.84, 7.82, 12.81 | Auto bands |
| | 74-78 | MTPSRMRRLP, LRPSSPPPPT, NMKNKNMKNN, MOSRNQNQTN, NSROSTQOSN (dark, mid, light, min, max exposure adjust) | -3.17, -0.65, 1.47, -3.17, 2.3 | Band adjusts |
| | 79 | LNQNRRPNPP (tonemapGrainStrength) | 0 | Film grain |
| | 80-81 | LOPONQOQNN, MOLOSSRQPS (tonemapLocalStrength, tonemapLocalEvBounds) | 0, 4 | Local tonemap |
| | 82 | LRROOOSOMM (toneWhitePoint) | 1 1 1 | White point |
| Grading | 213 | NONMKTKNNM (grading CLUT, gradingClut) | none | Written by the converter as `mw120r_<map>_clut` |
| | 214 | TLROSMMNT (gradingClut, unnamed) | none | Not written by the converter |
| | 15 | NQNPKMQRLT (r_ssaoStrength) | 1 | SSAO strength |
| Volumetric scatter | 7-14 | NKSQTPNOOK, LTQQSMPMKN, LSNRTORRKM, NLLMLNPMPM, MNQPPKSLMR, MRQKRNTMRK, LQLPRTOQKP, SLLNNNQSP (r_volumeLightScatter, Linear, Quadratic, Angular, DepthNear, DepthFar, BackgroundDistance, Color) | 0, 1, 0, 0.25, 0, 0, 100000, 0.2 | Light scatter (IW7 keys overwrite these) |
| | 137 | MMQLTMKTPK (veilStrength) | 0 | Veil |
| | 138-147 | NSKNLMKQSR, MQMPKLKK, NKKRQROOQK, LKNNNKTPTM, MQSQOLLMRM, RRKMLNPKP, MQRSTPRORQ, NPKPLTKRNS, MLPOMNQPPK, MTNSRNMTKN (volumetricAirDensity, Absorption, Albedo, SunBrightness, SpotBrightness, OmniBrightness, AmbientBrightness, HeightFogBaseHeight, HeightFogHalfPlaneDistance, SunAnisotropy) | 0, 0, 1 1 1, 1, 1, 1, 1, 0, 0, 0.15 | Stock air. Style iw8 erases the template values for these (`V:653-656`), so the defaults apply. Only IW7 veil and scatter keys (section 3.8) override them |
| Sun shadows | 148-149 | MLNTKKPNMM, LPSPPMRPMO (sunshadowSoftness, sunshadowSampleSizeNear) | 0, 0.25 | Shadow softness |
| Screen-space shadows | 172-174 | TSLKOOMLT, MTQQKPLMKS, QTOKMKRQM (samples total, per light, trace distance) | 16, 4, 8 | SSS quality |
| Decal | 207 | LLNSLMOQKT (decalVolumeDrawDistance) | 5000 | Decal draw distance |
| Thermal | 159-171 | OKKRMNNQLL ... TORMKPPLR | see table | Not an atmosphere input (thermal vision) |
| Scoped NVG | 209 | LLLRLSKLKS (scopedNVG) | 0 | Not an atmosphere input |
| Bloom | UNKNOWN | no named token in the table | UNKNOWN | Not identified. Possible unnamed indices exist (for example 94-98, 100-102), but no evidence yet |

Other tokens (indices 0-6, 16-32, 83-112, 113-136, 150-158, 175-206, 208, 210-212, 215-216) are unnamed in the table and are UNKNOWN. Do not expose them.

## 6. Traps and unverified items

- `iw7_tonemap` changes light units (section 2).
- `setExpFog` and `world.fog` together throw (`P:10910-10911`).
- A film takes priority over a LUT, silently (`P:10860-10866`).
- `world.fog` requires all 12 keys (section 3.2).
- A `sun/<m>.sun` shader key is required for the sprite, but not for flare (section 3.10).
- Sun-shadow mask uses `sun_primary_light_index` as a count (SUSPECT, section 3.11).
- Cascades: UNKNOWN.
- Decal placement: UNKNOWN.
- Bloom: UNKNOWN.
- Volumetric units (density, falloff, height fade): UNKNOWN.
- Fog colour intensity units on the native path (IW7 or IW3): UNKNOWN.
- Nothing in this report is verified in game.

## 7. UNKNOWN register

1. Volumetric `density`, `falloff`, `height_fade` units and visual scale.
2. Decal volume placement format and writer.
3. Bloom field, if one exists in the 218 fields.
4. Sun cascade count, bands and selection rule.
5. Whether `sunShadowMask` as written is intended (section 3.11).
6. Native fog colour units for an editor-authored map (IW7 vs IW3 light units).
7. Whether `r_sunsprite_shader` value matters beyond emptiness (`V:347-349`).
8. Meaning of vision fields 214, 215, 216 for a grading CLUT.
