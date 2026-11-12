# 56. Foliage sway and wind: what drives it, what a map can set, and the editor preview

Date: 2026-11-10. Scope: roadmap 5.23 "Wind and foliage motion settings with a sway preview" (part of 5.12), with the
foliage notes of doc 46 section 6. Read-only research: no game file was run, no build of the game was made. The editor
change is a preview model in `src/core/foliage_sway.h` and `.cpp` (section 6 and 7), with tests in
`tests/foliage_sway_tests.cpp`. Nothing reaches the build.

Citations are `file:line`. Paths under `x64-zt:` are relative to `E:\mw124\x64-zt`, `ACTS:` to
`E:\mw124\atian-cod-tools`, and `Radiant:` to `E:\IW8-Radiant`. Anything not checked against a file is marked UNVERIFIED.

## 1. Verdict

- Sway is a per-model feature of Replay's reactive-motion simulation. Only static map placements bend, and only when the
  placed model has reactive parts and its material is flagged. DObj (entity) foliage does not bend.
- Replay has no global wind field that the schema or the converter exposes. The wind a model sees is the model's own
  four wind scales (area, amplitude, frequency, 1). Whether Replay has a map-wide wind direction or speed is UNKNOWN.
- A custom map can set the per-model wind today, through the model JSON `reactiveMotion` block, and the material key
  `iw7ReactiveMotion`. It cannot set a map-wide wind. A map-wide `iw8r_wind` key needs a fork: x64-zt would have to read
  it and write a value the runtime uses, and the runtime side is UNKNOWN.
- The editor sway preview is an approximation for the camera. Its amplitudes are editor choices, not Replay values.

## 2. What drives sway today

### 2.1 Placement and flags

- The converter sets `XModelFlag REACTIVE_MOTION` (8) on a model with reactive surfaces and writes
  `STATIC_MODEL_FLAG_REACTIVEMOTION` on its static placements: `x64-zt:src/convert/iw3_iw8/iw3_fastfile.cpp:5196-5201`.
- A placed model is "reactive" only if it has reactive parts: `iw3_fastfile.cpp:4676`. Non-static (DObj) draws skip the sim:
  `x64-zt:src/convert/iw3_iw8/iw3_render_assets.cpp:8209-8212` (the comment says `replay_model_vertex.hlsl` compiles the
  wave and the reactive sim out under `MAP_ENTITY`).
- Doc 46 section 6 (`Radiant:docs/research/46_decals_volumetrics_glass.md:211-260`) reached the same conclusion for static
  placements. This doc confirms it against the converter source.

### 2.2 Vertex data

- Replay's part struct says the sim turns the vertices of a part's piece index (vertex green) about its pivot, blended
  by the vertex weight (vertex blue): `x64-zt:src/games/iw8/dump/xmodel_dump.h:119-121`.
- A part is `centerMS` (vec3), `stiffness` (half float), `parentIdx` (u16), `pivotPosMS` (vec3), `pivotQuatMS`
  (packed quaternion, Dec3n), 32 bytes: `ACTS:config/data/mw19/schema.json` `ReactiveMotionModelPart` (around line 14016),
  and `x64-zt:src/games/iw8/dump/xmodel_dump.h:122-128` (`Iw8ReactiveMotionPart`).
- The IW7 to Replay conversion moves each part about the model root where its lowest-weight vertices sit:
  `x64-zt:src/convert/iw7_iw8/export_models.cpp:299-302`. This is a converter choice, not a Replay rule.

### 2.3 Material and techset

- The material key `iw7ReactiveMotion: true` sets `material.reactiveMotion`: `x64-zt:src/convert/iw3_iw8/iw3_render_assets.cpp:2591`.
  It adds `#define MAP_REACTIVE_MOTION 1` to the flag-wave defines: `iw3_render_assets.cpp:8034`.
- The h1 to IW7 converter maps reactive techsets by name: `m_l_sm_lmpb_reactivemotion_*` to
  `mopw_l_sm_reactivemotion_atest_*`: `x64-zt:src/convert/h1_iw7/assets/material.cpp:141-143`. The h1 to S2 path matches
  `_reactivemotion` in the techset name: `x64-zt:src/convert/h1_s2/assets/techset.cpp:1275, 1457`. The reactive
  centre constants are `CONST_SRC_CODE_REACTIVEMOTION_CENTERS_0..15`: `x64-zt:src/convert/h1_iw7/assets/techset.cpp:351-364`.
  Whether IW8 Replay names its reactive sets the same way is UNVERIFIED.
- The editor's own techset reading: blend is the token before the table token. `rpl` is opaque, `alt` is alpha test
  (foliage, grates), `bad` is blended: `Radiant:src/core/material_blend.h:1-12`, `Radiant:src/core/material_blend.cpp:6-22`.
- Editor material cache (`%LOCALAPPDATA%\IW8 Radiant Suite\IW8 Radiant Suite\material_cache\index.json`, 140 entries):
  - Foliage-named entries (11): canopies, grass, reeds, weeds, flowers and bushes use `lit_3_lit_alt_ta1_*`
    (for example `#tmco/ee_foliage_tree_birch_canopy`, `#tmo/foliage_grass_reeds_01`). Trunk and branch sets of the same
    tree use `lit_3_lit_rpl_ta1_*` (for example `#tmco/ee_foliage_tree_birch_trunk_branches`). Weed cards use
    `card_8_lit_card_rpl_ta1_*`.
  - Blend token counts over the entries that have a token: rpl 115, alt 13, bad 5.
  - No techset token named for reactive motion, wind, sway, or vertex animation appears in the index. The only "reactive"
    hit is the name `create_fx_waypoint_reactive_ent`.
  - The second numeric group differs between canopy (`_1002000201000011`) and trunk (`_1002000001000010`). It may be a
    flag word that includes a wind bit. UNVERIFIED, and I did not decode it.

### 2.4 Model wind parameters

- Replay's reactive info holds `windParams`: `windAreaScale`, `windAmplitudeScale`, `windFrequencyScale`, then an unused
  float: `ACTS:config/data/mw19/schema.json:14144-14165` (`ReactiveMotionWindParams`), with the `ReactiveMotionModelInfo`
  record at `schema.json:13943-13982` and the 96-byte `Replay::ReactiveMotionModelInfo` at `schema.json:84363-84369`.
- The converter's default is 0.1, 0.2, 0.3, 1 (stock grass). It reads the model's `reactiveMotionTweaks` scale when present:
  `x64-zt:src/convert/iw7_iw8/export_models.cpp:476-486`. Per-part tweaks are `ampScale` and `freqScale` (vec4):
  `schema.json:14119-14143`.
- The Replay side of the same record: `x64-zt:src/games/iw8/dump/xmodel_dump.h:132-135`
  (`Iw8ReactiveMotion`, default wind `{0.1, 0.2, 0.3, 1}`), written at `x64-zt:src/games/iw8/assets/write_xmodel.cpp:120, 303`
  and read back at `x64-zt:src/games/iw8/neutral/xmodel.cpp:185, 211`. The data struct is
  `x64-zt:src/core/assets/xmodel_data.hpp:78-103`.

## 3. The Replay sim: what is known and what is not

- Known: the sim samples the wind and the pushes of players and explosions at each part's centre, then turns the part
  about its pivot: `x64-zt:src/convert/iw7_iw8/export_models.cpp:299-300`, `xmodel_dump.h:119-121`.
- Known: the three wind scales and the per-part stiffness and pivot are the sim's inputs in the model record
  (sections 2.2 and 2.4).
- UNKNOWN: the wind's direction and speed, and whether they come from a map-wide value, a per-map constant, or the
  sample of the wind field at each part. No schema field or converter line names them.
- UNKNOWN: how the sim's gust timing relates to the frequency scale. Only the stock values 0.1, 0.2, 0.3 are known.
- Not wind: the word "wind" in `x64-zt:src/games/iw8/assets/replay_sound.cpp:654` is a sound category, and the
  `SwaySettings` entries in `ACTS:config/data/mw19/schema.json` (around lines 25803-26720) are weapon view sway, not foliage.

## 4. Global wind: none found

- ACTS schema: a search of `config/data/mw19/schema.json` for wind, sway, foliage, bend, GfxWind, and windDirection finds
  only the reactive records above, weapon sway, and trigger windings. There is no world or comworld wind field.
- x64-zt source: a word search for `wind` finds only the reactive wind arrays, the converter's own "winding" geometry
  terms, and the sound category. There is no world-level wind read.
- Radiant: `src/core/atmosphere.h` shows how a worldspawn key becomes a build input (fog and film keys, written as extraction
  files that x64-zt reads). A wind key would need the same path, and that path does not exist for wind.

## 5. What a custom map can set today, and what needs a fork

Today, through the extraction pipeline:

| Setting | Where | Effect in build | Source |
|---|---|---|---|
| Per-model wind scales | model JSON `reactiveMotion.wind` `[area, amp, freq, 1]` | Used only when the model has reactive parts | `iw3_fastfile.cpp:2980-2989` |
| Per-model parts | model JSON `reactiveMotion.parts` (center, stiffness, pivot) | Replay sim per part | `iw3_fastfile.cpp:2980-2989`, `export_models.cpp:486` |
| Reactive material | material key `iw7ReactiveMotion: true` | Adds `MAP_REACTIVE_MOTION` to the flag-wave shader | `iw3_render_assets.cpp:2591, 8034` |
| Static placement | `models` in world JSON | Static draws bend; DObj does not | `iw3_fastfile.cpp:5196-5201`, `iw3_render_assets.cpp:8209-8212` |

Needs a fork or new work:

- A map-wide `iw8r_wind` that changes the build: x64-zt would need a reader for the worldspawn key and a writer for the
  runtime's wind value. The runtime's wind input is UNKNOWN (section 3), so the writer cannot be specified yet.
- Per-placement wind: not in the model record. Each placed model shares its model's wind.
- Wind on DObj (entity) foliage: out of reach for this sim (section 2.1).

Radiant can write the per-model `reactiveMotion.wind` today, but that is outside 5.23 and outside the files this task may
change.

## 6. The editor preview model (implemented in src/core/foliage_sway.cpp)

- The amplitude is a horizontal offset in map units:
  `amplitude = 6 * strength * h^2`, where `h = min(heightAboveBase / 128, 1)`.
  At strength 1 the top of a 128 unit plant moves 6 units. Strength is clamped to 0..4, so the bound is 24 units.
- Phase: `0.045 x + 0.031 y + 0.017 z` radians, so points one unit apart differ by about 0.05 radians.
- Wave: `0.7 sin(wt + phase) + 0.3 sin(1.7 wt + 1.3 phase)`, with `w = 2 pi gust`. The weights sum to 1, so `|wave| <= 1`.
- Direction: `(cos yaw, sin yaw, 0)`, yaw in degrees, 0 = +X.
- Zero at and below the base, and for non-finite input. The function is pure: no state, no random numbers.

Every number here is an editor choice, not a measured Replay value. The only numeric anchor is the stock grass wind
`0.1, 0.2, 0.3` (section 2.4). Treat the preview as showing the direction, the height growth and the phase spread, not
the true displacement.

## 7. The iw8r_wind key

- Form: `iw8r_wind "yaw strength gust"`, three finite numbers. Yaw in degrees (any value), strength 0..4, gust 0..5 Hz.
- Malformed text (wrong count, non-numbers, nan or inf) is ignored with a warning, as `iw8r_fog` is in `atmosphere.cpp`.
  Out-of-range strength or gust is clamped, with a warning.
- Round trip: `formatWind` writes the value with `formatNumber`, which is exact, so the text reads back to the same numbers.
- Build status: the key is preview-only. No extraction file is written from it.

## 8. Open questions

1. Does Replay's sim read a map-wide wind direction and speed, and where is it set? (UNKNOWN; check a live Replay
   build or a runtime dump, not done here.)
2. Do IW8 reactive techsets use a name token like the h1 `reactivemotion`? (UNVERIFIED; the editor cache has none.)
3. What do the second numeric group bits of an IW8 techset name mean, and does one of them mark a wind or sway set?
   (UNVERIFIED.)
4. Are the amplitudes of section 6 close to Replay's? (UNVERIFIED; needs a side-by-side capture.)
5. Does the sim's gust frequency map to `windFrequencyScale` directly? (UNVERIFIED.)
