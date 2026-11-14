# 24. Light entities as IW8 primary lights

Research for roadmap 5.1. Sources: x64-zt read only (`E:\mw124\x64-zt\src`), IW8-Radiant compiler (`src/core/compile_iw8.cpp`), and the IW3 Radiant reference (`reference/iw3xo-radiant/src/ggui/entity.cpp`). Line numbers are for these trees as read on 2026-11-08.

## 1. Where primary lights are read

`convert_extraction` reads the comworld export as `commonWorld` and copies its table into the sun's `lighting.json` (`BuildLighting`, `iw3_fastfile.cpp:8172-8224`; `"primary_lights"` copied at `:8216`). The reader is in `convert.cpp`:

| Step | Where |
|---|---|
| Table present, size 2..65535, `sun_primary_light_index` `< size` | `convert.cpp:953-962` (throws otherwise) |
| Per entry: `type` (0..3), `exponent` (0..255), `can_use_shadow_map` | `:983-989` |
| `color` (3 floats, none negative), `direction` (3 floats, normalized; non-zero for type != 0), `origin` | `:991-1010` |
| Spot/omni `up` built from direction (or the authored `up` of IW7 sources) | `:1012-1044` |
| `radius`, `cos_half_fov_outer`, `cos_half_fov_inner`, `rotation_limit`, `translation_limit` (all required) | `:1049-1053` |
| Optional shape/shadow fields (`entity_id`, `bulb_radius` 1, `bulb_length`, `volumetric`, `needs_dynamic_shadows`, `shadow_softness`, `shadow_bias`, `shadow_area`, `distance_falloff`, `fade_offset`, `uv_intensity`, `ir_intensity`), defaults from `map_zone.h:1097-1130` | `:1054-1072` |
| `definition` (required, <= 255 chars) | `:1074` |
| Optional `intensity`; when present the light is "authored" (`authoredIntensity`) | `:1076-1085` |
| Spot cone check: `0 < cos_outer < cos_inner <= 1` | `:1090-1095` |

Field meanings in `games/iw8/zone/map_zone.h:1097-1130` (`struct MapSun::PrimaryLight`): `type`, `exponent`, `canUseShadowMap`, `color`, `direction`, `up`, `origin`, `radius`, the two cosines, `rotationLimit`, `translationLimit`, `intensity` (default 1), `authoredIntensity`, `definition`, plus the shadow and bulb fields above.

### Sun index and count

- `sun_primary_light_index` (world.json) is read at `convert.cpp:960`, and `primary_light_count` must equal the comworld table size and be `2..65535` (`iw3_fastfile.cpp:10693-10699`; the same check at `:10694-10699`).
- `BuildLighting` takes the sun's colour from the light at `sun_primary_light_index` when its `type == 1` (`iw3_fastfile.cpp:8189-8198`), and its direction from `SunDirection` (`:8162-8170`).
- Light 0 is conventionally empty (type 0).
- `convert.cpp:1119-1132`: an empty table becomes 2 entries, and if any light has type 2 or 3 the table is padded to 256 entries (`sortedLightLimit = 256`). The comment there says Replay allocates `min(primaryLightCount, 256)` proxy slots but indexes them by sorted rank, so a table over 256 entries is not safe.

So the practical count is **256 slots**: index 0 empty, index 1 sun, indices 2..255 local lights (254 lights). The compiler enforces this (see section 4).

### Shadowed lights

- `can_use_shadow_map` and `needs_dynamic_shadows` are copied into the GfxWorld local-light input at `convert.cpp:1854-1868`. `NeedsDynamicShadows` (`games/iw8/assets/replay_locallights.h:29-34`) is true only for type 2 with both flags. The BVH over those lights uses leaves of up to 16 lights (`replay_locallights.cpp:53`, `:414-431`). That is a leaf size, not a count limit.
- I found no hard cap on shadow-mapped lights in the converter. The IW7 path caps spot openings for shadowed spots at cos 0.259 (about 75 degrees, `src/convert/iw7_iw8/export_entities.cpp:1264-1277`). That clamp is not applied on the IW3 path.
- The compiler writes `can_use_shadow_map: false` and `needs_dynamic_shadows` at its default, so no shadow limit is reached.

### Calibration and the GfxWorld volumes

- `calibrateLocalLights` (`convert.cpp:1237-1300`) sets the intensity of each local light. Lights with `authoredIntensity` get `intensity * lightUnitScale` (`:1243-1245`). Others get a value from a reference distance (a ray hit for spots, `radius/2` for omnis, at least 16 units), so the authored brightness of IW3 is reproduced. Lights with radius 0 are skipped (`:1240`).
- Local lights reach GfxWorld through `iw8::locallights::Build` (`convert.cpp:1851-1871`). Each `light.direction` is negated (`:1861`), so the stored direction is "back toward the light" and the converter uses the emission axis.

## 2. IW3 light keys

### Existing IW3 conversion code

There is none for `light` entities. The IW3 export carries no light entities; the IW7 exporter says so (`export_entities.cpp:126`, `maps.cpp:603-605`), and IW3 maps are read from the sun and `primary_lights` alone.

### Replay's entity key table

`iw3KeyToIw8KeyId` (`games/iw8/dump/maps.cpp:25-70`) keeps 28 IW3 names: `_color` (0x09F9), `angles` (0x0050), `origin`, `radius` (0x030E), `target` (0x042E), `targetname`, `suncolor`, `sundirection`, `sunlight`, `script_*`, and more. Any other key is dropped with a count-and-warn (`maps.cpp:683-685`).

So `light`, `intensity`, `fov_outer` and `fov_inner` are **not kept** in `.ents`. This is one reason the compiler does not write light entities into `.ents`: the converter would not accept the class, and the keys would not reach Replay.

### IW3 Radiant reference

`reference/iw3xo-radiant/src/ggui/entity.cpp`, `eclass_light_templates` (line 23):

| Key | Default | Notes |
|---|---|---|
| `_color` | `1.0 1.0 1.0` | colour |
| `exponent` | `1.0` | not used (the comworld exponent is an integer 0..255) |
| `def` | `light_point_linear` | falloff definition |
| `fov_inner` | `60.0` | range 1..136 in the dialog (`:1488-1493`) |
| `fov_outer` | `90.0` | range 1..136 (`:1498-1503`) |
| `intensity` | `1.0` | float (`:1508`) |
| `radius` | `400` | float (`:1464`) |

The fov values are full cone angles in degrees. That is an inference from the range and the defaults; the dialog does not say. `light` (the brightness key of stock maps) is not in this template at all.

### Mapping used by the compiler

| Radiant key | Used as | Default |
|---|---|---|
| classname `light` / `light_spot` / `light_omni` | point entity; `light_omni` is always an omni, `light_spot` always a spot | |
| `_color` (or `color`) | colour; if any component > 1, all divided by 255 | 1 1 1 |
| `intensity` | multiplies the colour; not written as an authored intensity, so the converter calibrates brightness from the radius | 1 |
| `radius` | `radius` (must be > 0) | 400 |
| `target` | spot aimed at the origin of the entity whose `targetname` matches | |
| `angles` | spot aim when no target: `(cos p cos y, cos p sin y, -sin p)`, pitch positive down | 0 0 0 |
| `fov_outer`, `fov_inner` | cones: `cos(fov/2)`; outer clamped to 1..170 degrees; inner below outer | 90, 60 |

A light is a **spot** when it is `light_spot`, or a `light` with a resolved `target`, or with an fov key. Otherwise it is an **omni**.

## 3. Primary light layout written by the compiler

| Index | Entry |
|---|---|
| 0 | type 0, all zero |
| 1 | type 1 (sun): colour from `suncolor`, direction toward the sun |
| 2.. | one per light entity, in map order |

Each light object carries the fields the reader requires (`type`, `exponent` 0, `can_use_shadow_map` false, `color`, `direction`, `origin`, `radius`, `cos_half_fov_outer`, `cos_half_fov_inner`, `rotation_limit` 0, `translation_limit` 0, `definition`). `definition` is `light_point_linear` for spots and omnis, as the samples in doc 15 do. The converter requires a non-zero direction for types 1..3, so an omni is written with direction `(0, 0, 1)`; its axis is unused.

`direction` is stored as "back toward the light": a spot's emission axis negated. The converter then negates it again to get the emission axis (`convert.cpp:1861`).

## 4. Limits enforced by the compiler

- Total table: 256 entries (`kMaxPrimaryLights`). Local lights beyond 254 are dropped with a warning.
- Light entities stay out of `.ents`. The class is not in `isReplayEntity`, and `pl#` script lights are the only accepted form (`maps.cpp:603-608`).
- Shadows are off, so the shadow-limit path is not reached.

## 5. Open questions

1. **Intensity scale.** `intensity` only scales colour here, and brightness is calibrated from the radius. The IW3 `light` key was not found in any tree I read, so there is no evidence of its meaning. If a mapper expects `light 300` to mean something, it is ignored.
2. **fov units.** Full cone angle assumed (see section 2). Not checked against an IW3 spot in game.
3. **Falloff definition.** Every light uses `light_point_linear`. A `def` key is not read. The converter falls back to linear with a warning for unknown definitions (`convert.cpp:1295`).
4. **Spot shadow cap.** The IW7 path clamps shadowed spots to 75 degrees. Not needed while shadows are off.
5. **Proxy slots.** The 256 limit is from the converter's comment about proxy slots, not from a measured Replay limit.

## 6. Native falloff definition (found by the end-to-end run)

The converter refuses a map-local `light_point_linear` unless `extracted/lights/light_point_linear` exists with exactly 16 bytes: `b`, `falloff_linear`, NUL (`iw3_fastfile.cpp:11074-11090`, the `localLinearLightDef` check; the path is `exportRoot / "lights"`, and `exportRoot` is `extracted/`, `:10646`). It is the same file doc 15 lists under `lights/` (section 2, "read only when a name in the map refers to them"). Without it, `buildextraction` fails with "map-local IW3 light_point_linear needs its exact attenuation export".

The compiler writes that file only when at least one local light is written (`compile_iw8.cpp`, the block before the `ok` write chain). A map with no lights gets no `lights/` folder.

## 7. End-to-end run (2026-11-08)

- Fixture: `out\lights-test\lights_room.map` = `tests\data\iw8_sample_room.map` plus one `light` (omni, origin `-200 0 200`, `_color 1 0.8 0.6`, radius 600) and one `light_spot` (origin `200 0 200`, angles `70 180 0`, `_color 255 255 255`, `fov_outer 70`, `fov_inner 35`, `intensity 2`).
- Compile: `iw8r compile ... --map mp_iw8r_lights` writes `out\lights-test\extraction`. Comworld: index 0 empty, 1 sun (`direction [0.4545, 0.4545, 0.7660]`), 2 omni (type 3, colour `[1, 0.8, 0.6]`, radius 600), 3 spot (type 2, colour `[2, 2, 2]`, direction `[0.342, 0, 0.940]`, cos outer 0.8192 = cos 35 deg, cos inner 0.9537 = cos 17.5 deg, radius 400 default). World: `primary_light_count` 4, `sun_primary_light_index` 1. `.ents`: no light entity.
- `zonetool buildextraction ... --map mp_iw8r_lights ...` exit 0. Converter log: `matched map-local light_point_linear to Replay's native type-34 definition`; `local lights: reserve 252 null primary slots for Replay's 256 sorted lights`; `local lights: 2 volume(s), 0 dynamic-shadow`; `validate: map output 'mp_iw8r_lights' passes structural checks`. Output: `%USERPROFILE%\AppData\Local\IW8 Radiant Suite\IW8 Radiant Suite\x64zt_output\mp_iw8r_lights\`. Log: `out\lights-test\run.log`.
- ACTS `--test` (exit 0 each, 0 failed, 0 unavailable): `mp_iw8r_lights.ff` 794 loaded, 19 tested; `ww_mp_iw8r_lights.ff` 2 loaded, 1 tested; `eng_mp_iw8r_lights.ff` 9 loaded, 2 tested; `techsets_mp_iw8r_lights.ff` 91 loaded, 4 tested; `srv_mp_iw8r_lights.ff` 0. Temp dir: `%USERPROFILE%\AppData\Local\Temp\claude\40643af5ee92b5b9\f955f107-10f4-450a-a1df-e62aedc1d809\scratchpad\acts_lights`.
- Not verified: how the lights look in game (no Replay run).
