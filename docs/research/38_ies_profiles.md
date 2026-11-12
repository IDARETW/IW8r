# 38. IES profiles: stock list, light key route, and what custom .ies files need

Research for roadmap 5.13. Read only: x64-zt (`E:\mw124\x64-zt\src`), the editor compiler (`src/core/compile_iw8.cpp`) and this repo's docs. Line numbers are for these trees as read on 2026-11-10.

## 1. Summary

- The fork's stock IES data is two normalized 128-texel curves plus a white segment, not candela tables. Nothing in the fork reads LM-63 text.
- A light selects an IES profile only through its `definition` name, and only when the converter also receives an `ies_profiles` entry with that name. The editor writes `definition` as `light_point_linear` for every spot and omni, and it writes no `ies_profiles` array. No IES key exists on light entities today.
- The only producer of `ies_profiles` is the IW7 exporter, which samples the source game's own 8-bit `*ieslookup` strip.
- Custom .ies support needs a key and profile library in the editor, a parser (now in `src/core/ies_profiles.cpp`), a 128-texel resampler that matches Replay's coordinate mapping, and the editor writing `ies_profiles` into its comworld JSON.

## 2. Stock profiles (fork)

Source: `E:\mw124\x64-zt\src\games\iw8\assets\replay_ies.h` (header comment 12-20, `kLookupName` 21, `Profile` 26-30), `replay_ies.cpp` (strip writer 52-82, LightDefs 87-107, `Register` 110-149), `replay_ies_data.inc` (mip chains, 255 values each).

| Segment | Profile string (`iesProfile`) | LightDef | Array | Note |
|---|---|---|---|---|
| 0 | none | none | fill 0xFF | white; lights without a profile sample it (`replay_ies.cpp:70`) |
| 1 | `IES_16` | `flashlight` | `kIes16` (`replay_ies_data.inc:3`) | registered at `replay_ies.cpp:144`; `profiles[1]` at `:59` |
| 2 | `ies_nvg_flashlight` | `light_ies_nvg_flashlight` | `kIesNvgFlashlight` (`:18`) | registered at `replay_ies.cpp:145`; `profiles[2]` at `:60` |

So there are 2 stock profiles. Each is 128 texels at level 0 with 7 more mip levels (255 values), in R8, so one texel is 1/255 of the peak. Replay's sampling: texel j is profile coordinate j / 255 over the spot's forward hemisphere (`replay_ies.h:23-25`). The strip is 512x1 with 8 levels (`replay_ies.h:14-15`).

The machine-readable list is `data\asset_catalog\iw8_ies_profiles.tsv`, built by `tools\build_ies_catalog.py` (`py -3 -I tools/build_ies_catalog.py`). It holds the segment, LightDef, array, peak, half-peak texel and zero texel for each row. The builder parses `replay_ies.cpp` for the segment and LightDef assignments, so it follows the fork if those change. No candela values exist, so none are recorded.

## 3. The light key route today

### 3.1 Editor side

- `localPrimaryLights` (`src/core/compile_iw8.cpp:587-686`) turns each light entity into a ComPrimaryLight object. Its header comment (583-586) lists the keys it reads: `_color`/`color`, `intensity`, `radius`, `target`, `angles`, `fov_outer`, `fov_inner`. No IES key is read. `grep -i ies` over `src/` finds no IES key.
- `primaryLightJson` (`compile_iw8.cpp:572-579`) writes `definition` as `"light_point_linear"` for types 2 and 3 (577-578), and `""` for types 0 and 1. The editor never writes any other name.
- The compiler writes `<map>.d3dbsp.replay-comworld.json` (`compile_iw8.cpp:1771`) with `primary_lights` (1626-1638). It writes no `ies_profiles` key.

### 3.2 Converter side (same comworld file)

- The fork finds `*.replay-comworld.json` with `FindSingleExport` and reads it (`src/convert/iw3_iw8/iw3_fastfile.cpp:10667`, `:10679`). So the editor's file is the converter's input.
- `commonWorld.ies_profiles` is read at `iw3_fastfile.cpp:10712-10714`; the names go into `iesLightDefs`. It is copied into `lighting.json` at `:8239`.
- `convert.cpp:1074` reads each light's `definition`. A light keeps its name, and nothing else links it to a profile.
- `convert.cpp:1103-1115` reads each `ies_profiles` entry: `definition` (must be non-empty, at most 255 characters), `profile`, and `texels` (exactly 128 uint8 values). It then calls `iw8::ies::Register` at `convert.cpp:1830`.
- `iw3_fastfile.cpp:9514`: a LightDef asset whose name is not in `iesLightDefs` is replaced by `vfxLightDef`. So a custom `definition` with no matching profile does not produce a LightDef of its own.
- `convert.cpp:1281-1296`: when `definition` is not `light_point_linear`, the converter takes its falloff from that named definition's image, and warns if none is found. A custom IES name therefore changes falloff and not the pattern, unless a profile is also registered.

Result: the editor writes `definition: "light_point_linear"` for every light, so no IES profile is ever selected from an editor map.

### 3.3 Where `ies_profiles` comes from

- `E:\mw124\x64-zt\src\convert\iw7_iw8\export_entities.cpp:1177-1214`: the IW7 exporter (`ies_definition`) reads the source's `*ieslookup` (R8, not streamed) and each GfxLightDef with an `iesProfile` and `coordScale > 0`. It resamples 128 texels with `coordinate = texel / 255` and `x = (coordOffset + coordinate * coordScale) * width - 0.5` (`:1205-1206`), then pushes `{definition, profile, texels}` (`:1214`). The output is at `:1419`.
- The IW3 source has no IES data: `replay_ies.h:19-20` says converted local lights keep profile-less definitions.

### 3.4 Engine-side structs

- `GfxLightDef` has `iesProfile` (a `const char*`) at offset 0x08 (`src\games\iw8\structs.hpp:3660`, static assert at `:17983`).
- `GfxWorldDraw.iesLookupTexture` is at 0xF0 (`structs.hpp:12891`, static assert `:24043`).
- Writer: `replay_ies.cpp:87-107` stores the name and the profile string inline (`:93-94`, `:103`) and sets `coordOffset` and `coordScale` from the segment (`:96-97`).
- Neutral path: `src\games\iw8\neutral\lighting.cpp:50-52` copies `iesProfile` out. `:87-89` throws for an IES profile that is not the stock strip, so a profile can only enter through the JSON path (`convert.cpp:1103`). Doc 09 section 6 says the same.

## 4. What custom .ies files need

Missing pieces, in dependency order:

1. **A key on light entities.** None exists. Proposal: `ies_profile` on `light` and `light_spot`, value a file name in a project profile folder. The editor reads it in `localPrimaryLights` (`compile_iw8.cpp:587-686`) and writes `definition` = a profile name, not `light_point_linear`.
2. **An .ies reader.** None in the fork. The core parser `src\core\ies_profiles.cpp` (`parseIesFile`, roadmap 5.13) reads LM-63 type C with TILT=NONE. It does not read TILT files, types A/B or units conversion.
3. **A resampler to Replay's segment.** The strip is 128 R8 texels per profile over the spot's forward hemisphere. The mapping from spot angle to coordinate is described in the converter comment `export_entities.cpp:1170-1173` (`c > 0 ? (1 - c^2)/2 : 1 - (1 - c^2)/2`). I did not verify that formula against Replay's shader. Until it is verified, a resampler cannot be checked. `iesPolarCurve` gives the C0 curve for preview but not the 128-texel strip.
4. **The `ies_profiles` JSON from the editor.** The compiler must add `ies_profiles` with `{definition, profile, texels[128]}` (`convert.cpp:1103-1115`) and give each spot the matching `definition`. The profile name is written as `iesProfile` (`replay_ies.cpp:93-94`).
5. **Profile name rules.** `definition` at most 255 characters (`convert.cpp:1109`). The LightDef name and the profile string are the same in the JSON path.
6. **Strip width.** `Register` grows the strip by powers of two (`replay_ies.cpp:27-33`) and stores width as uint16 (`:121`). That allows up to 512 segments, about 509 profiles. The converter has no limit of its own beyond the 254 local-light slots in doc 24.
7. **No `iesprofile` asset.** The IW8 format table has `iesprofile` as a null row (`src\games\iw8\zone\asset_streams.cpp:10836`; doc 14 section 33). Replay uses the strip, so a custom profile needs no separate asset.
8. **In-game check.** Unverified: that Replay uses `definition` names exactly as the strip LightDefs do. Doc 09 section 6 gives the mapper test.

## 5. Core code added (roadmap 5.13)

- `src/core/ies_profiles.h/.cpp` (namespace `iw8r`): `parseIesFile`, `iesIntensity` (bilinear, symmetry at 90 and 180, wrap for other ends, single-plane tables), `iesPolarCurve` (C0 plane, normalized to peak 1).
- `tests/ies_profiles_tests.cpp`: 10 tests (parse, CRLF and trailing numbers, table and interpolated intensity, clamping, quadrant and half symmetry, full-turn wrap, single plane, polar curve, 17 rejected inputs).
- `tools/build_ies_catalog.py` and `data/asset_catalog/iw8_ies_profiles.tsv`: stock list (section 2).

Not built: a `stockIesProfile` loader. The fork holds no candela tables, so there is nothing to load as candela.

## 6. Open items

- The editor-side key and profile library (section 4, items 1 and 4) are not designed in the editor yet; they belong to the UI and compile follow-up.
- The spot-angle to coordinate mapping (item 3) is unverified against Replay's shader.
- Nothing here was run in game.
