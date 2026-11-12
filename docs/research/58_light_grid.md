# 58. Light grid: what the converter needs from the editor

Research, read-only. Sources: the editor `E:\IW8-Radiant` (`src\core\`) and the converter fork `E:\mw124\x64-zt`
(not edited, not built, no branch changes). Citations are `file:line`. Converter paths are under
`src\convert\iw3_iw8\` unless written in full; the runtime loader is `src\games\iw8\common\`. Doc 55 covers the
unlit baseline and is not repeated here. Anything not run or read is marked **UNVERIFIED**.

## Summary

- The converter reads a grid only from `extracted\maps\mp\<id>.d3dbsp.lightgrid.json` plus the binary files that JSON
  names. The editor writes none of them (doc 55). Without the JSON the converter warns and writes no
  `<id>.d3dbsp.gpulightgrid.native` (iw3_lightgrid.cpp:1212-1216; iw3_fastfile.cpp:12977-12978).
- The lattice is fixed: sample points sit every 32 units in x and y and every 64 in z, with origin -131072
  (iw3_lightgrid.cpp:249-251, 609-611). The editor cannot pick another spacing. It chooses which lattice points exist
  and what each one holds.
- Cheapest valid grid: a dense box of lattice points covering the map plus padding. Every point uses one palette colour
  (the ambient fill). Each point stores `primaryLight` 1 (sun) and `traceMask` 0. Stage 1 needs no ray tracing and
  already removes the black viewmodel. Stage 2 sets `primaryLight` 0 where the sun ray is blocked, using
  `CollisionWorld::traceRay`.
- Size for a 5000 x 3000 x 1024 map (padding one cell, height assumed): about 280k entries, 1.1 MB of entry data. The
  converter's native payload is about 50 MB, since a full box gives 6 tetrahedra per cell (UNVERIFIED runtime size).
- Main risk: the converter's reader throws on any malformed field, and the call at iw3_fastfile.cpp:12977 has no
  try/catch. A bad file fails the build; a missing file only warns. The writer must validate or omit the file.

## 1. Converter input

| Item | Where | Value |
|---|---|---|
| Source root | iw3_fastfile.cpp:10670 | `exportRoot = worldPath` up three levels = the `extracted` folder |
| Asset name | iw3_fastfile.cpp:10671-10672 | `sourceAsset` = world file name minus `.replay-world.json`, i.e. `<id>.d3dbsp` |
| Grid JSON | iw3_lightgrid.cpp:1210-1211 (also 1201 for IW7 mesh, 1241, 1434, 1702) | `<exportRoot>\maps\mp\<id>.d3dbsp.lightgrid.json` |
| Editor output dir | compile_iw8.cpp:1988 | `root\extracted\maps\mp`, so the editor's JSON goes in the same folder as `replay-world.json` (compile_iw8.h:4-14) |
| IW7 alternative | iw3_lightgrid.cpp:1201-1209, 934-940 | `<id>.d3dbsp.gpulightgrid.bin` ("IW7GLG01", SH per probe, explicit tetrahedra). Takes precedence if present; not used here (harder to write) |
| Output | iw3_fastfile.cpp:12977-12978 | `mapDirectory\<id>.d3dbsp.gpulightgrid.native` (also consumed by the runtime loader, section 8) |

The converter always calls the grid path (iw3_fastfile.cpp:12977). A missing or `available:false` JSON returns
without a file (iw3_lightgrid.cpp:1218-1222). Any exception propagates out of the converter.

## 2. Reader rules (what must be true or the build throws)

`ReadJson` and `ReadArray` (iw3_lightgrid.cpp:105-158):
- Every binary array is named by `{"file": "<relative path>", "bytes": <n>}`. The path must be relative, use forward
  slashes only, contain no `:`, no `..` component, and resolve inside the root (lines 130-152). The root is
  `extracted`, so the file is `maps/mp/<id>.d3dbsp.lightgrid.<x>.bin`, not a path under `maps/mp` alone.
- `bytes` must equal the expected size and the on-disk size must match (lines 132-133, 155-156). Limits: 64 MB per
  array (line 118).

Required JSON keys (missing one throws, because `metadata.at` is used):
`schema` (must be 1), `name` (must be `"maps/mp/" + sourceAsset`, i.e. `"maps/mp/<id>.d3dbsp"`), `row_axis`,
`col_axis` (lines 163-166 require `{0,1}` or `{1,0}`), `mins` and `maxs` (3 integers each, 0..65535), `row_count`,
`entry_count`, `color_count`, `row_starts`, `row_data`, `entries`, `colors` (lines 170-188). Lines 1223-1225 check
`schema` and `name`. `sun_primary_light_index` is required by the static-model sampler (line 1448). `available`
defaults to true; `has_light_regions` defaults to false (lines 1218, 1449).

Grid coordinates (iw3_lightgrid.cpp:249-251, 1461-1463, 1493-1499):
- world x = i·32 − 131072, world y = j·32 − 131072, world z = k·64 − 131072, where (i, j, k) are the integer grid
  indices held in `mins`/`maxs` (x,y,z order).
- So i = (x + 131072) / 32 must be an integer; the same for j, and k = (z + 131072) / 64.
- Limits: `mins >= 0`, `maxs >= mins`, `maxs <= 65535` (lines 172-176).

Row count and entry count (iw3_lightgrid.cpp:177-182): `row_count = maxs[row] − mins[row] + 1`; `entry_count <= 8,388,608`;
`color_count <= 65,536`.

`row_starts` (lines 184, 206): one `uint16` per row, little-endian. The value is a start index in units of 4 bytes into
`row_data`. `0xFFFF` marks an empty row (skipped, line 207).

`row_data` (lines 209-263): for each non-empty row, at byte `start * 4`, a 12-byte header:
- `u16 firstColumn` (absolute grid index on the column axis), `u16 width`, `u16 bottom` (absolute grid index of z),
  `u16 height`, `u32 firstEntry`. Lines 212-216.
- `firstEntry` must equal the running entry count (line 218). `width` and `height` must be non-zero and in range
  (lines 218-221).
- Then column runs until `width` columns are covered (lines 223-257). Each run is `u8 run`, `u8 count`, and when
  `count > 0` one more byte `u8 startZ`. `run` must be non-zero and fit the row; `startZ + count <= height` (lines
  228-239). `run` is a byte, so a run covers at most 255 columns.
- Cell order inside a row (lines 241-255): for each column of the run, then for each z of the run. Entries are
  assigned in that order (`expectedEntry++`).
- After each row, pad with zeros to a 4-byte boundary (lines 258-262). After the last row the padded size must equal
  the whole `row_data` (line 264).

`entries` (lines 187, 190-198): 4 bytes per entry:
`u16 color` (index into the palette, must be `< color_count`), `u8 primaryLight` (byte 2), `u8 traceMask` (byte 3).

`colors` (lines 188, 381, 399): 168 bytes per palette entry. The 168 bytes are 56 directional samples x 3 channels,
byte index `sample*3 + channel`. The converter projects them to L1 SH (lines 290-377 and 379-406). Shading value in
8-bit units, scaled by `1/255` and `lightUnitScale` (lines 389, 401).

## 3. What the entry fields mean

Derived from the converter's own sampler, `SampleStaticModelLighting` (iw3_lightgrid.cpp:1390-1690). The grid is
read there as the runtime does (comment at 1491, "R_LightGridLookup").

- **primaryLight** (lines 1574, 1579-1587, 1591-1595, 1605-1624, 1642-1645). The sun is primary light index 1
  (`sun_primary_light_index`, editor compile_iw8.cpp:1919). A corner with `primaryLight == 1` is sunlit. A corner with
  `primaryLight == 0` counts as occluded when the primary is a directional light (type 1) (lines 1611, 1622), so that
  corner shadows the sun. `255` means "use the sun" (line 1592). If the chosen primary is 0, the sun weight is 0
  (line 1645). Stage 2 shadows depend on this.
- **traceMask** (lines 1538-1561). Bit `corner` set means the runtime (or this sampler) runs a sight trace from the
  model to that corner and suppresses the corner if blocked. `0` means no sight tests at all. Recommended: `0`, since
  the editor bakes shadows into `primaryLight` and the converter's blocked callback is then never called.
- **color** (lines 1638-1647). Colour is the non-sun part of the light; the sun comes in as the alpha weight, not
  through the colour. Line 1680-1683 comments: "The model's primary light gets the weight; only a sun primary lights
  the model with the sun (alpha = sun weight)". So the grid colour is ambient only (inference from that code; the
  runtime side is UNVERIFIED).

## 4. Uniform grid behaviour

- Build: complete cells (all 8 corners present, mask 255) are split into 6 tetrahedra each (lines 658-673, 690-710).
  A full dense box therefore builds without the sparse path. Empty-grid errors are at 798-807 and do not apply.
- Reconstruction: the 56-sample projection is least-squares, and it verifies that it reproduces the basis (lines
  363-373). A constant palette is exactly the DC term (`Y0 = 0.2820948`, line 272), so every direction reconstructs to
  `byte/255 * lightUnitScale` (derived from those lines, not run). The L1 terms are 0, so packed directional bytes are
  neutral (lines 428-444). The probe is uniform ambient; directional shading on models comes from the sun primary light
  weight (section 3), not from the grid.
- Converter ambient-only entries (`primaryLight 0`) give a model zero sun weight, which is the desired shadow.
- Entries that are not present fall back: the static-model sampler uses the nearest cell in growing shells up to radius
  8 (lines 1308-1333) for its own baking. The runtime fallback for missing samples is UNVERIFIED. Pad the box so every
  model sits inside it.

## 5. Exact file set (minimum for a valid grid)

Files under `extracted\maps\mp\` (id = map id; `<X>` = `<id>.d3dbsp.lightgrid`):
`<X>.json`, `<X>.row_starts.bin`, `<X>.row_data.bin`, `<X>.entries.bin`, `<X>.colors.bin`. The JSON's `file` values are
relative to `extracted\`, i.e. `maps/mp/<X>.<part>.bin`.

**JSON** (all keys required unless marked):

```
{
  "schema": 1,
  "name": "maps/mp/<id>.d3dbsp",
  "available": true,
  "row_axis": 0,
  "col_axis": 1,
  "mins": [i0, j0, k0],
  "maxs": [i1, j1, k1],
  "row_count": i1 - i0 + 1,
  "entry_count": N,
  "color_count": 1,
  "sun_primary_light_index": 1,
  "has_light_regions": false,
  "row_starts": {"file": "maps/mp/<X>.row_starts.bin", "bytes": 2*rows},
  "row_data":   {"file": "maps/mp/<X>.row_data.bin",   "bytes": R},
  "entries":    {"file": "maps/mp/<X>.entries.bin",    "bytes": 4*N},
  "colors":     {"file": "maps/mp/<X>.colors.bin",     "bytes": 168}
}
```

**row_starts.bin**: one `u16` per row. Row r starts at byte `16*r`, so the value is `4*r` (16-byte rows, section below).

**row_data.bin**: 16 bytes per row (dense layout, Y columns in one run, Z entries per column; requires Y <= 255 and
Z <= 255 for one run):

| Offset | Size | Value |
|---|---|---|
| 0 | u16 | firstColumn = j0 |
| 2 | u16 | width = Y (j1 − j0 + 1) |
| 4 | u16 | bottom = k0 |
| 6 | u16 | height = Z (k1 − k0 + 1) |
| 8 | u32 | firstEntry = r * Y * Z |
| 12 | u8 | run = Y |
| 13 | u8 | count = Z |
| 14 | u8 | startZ = 0 |
| 15 | u8 | pad 0 |

If Y > 255, split the row into runs of at most 255 columns. Each run adds 3 bytes; keep the row padded to 4 bytes.

**entries.bin**: N records of 4 bytes: `u16 color = 0`, `u8 primaryLight` (1 sunlit, 0 shadowed), `u8 traceMask = 0`.
Order: row index r (x), then column c (y), then z fastest: index = `(r*Y + c)*Z + z`. Line references: 241-255.

**colors.bin**: 168 bytes. Byte `3*s + ch` for samples s in 0..55 and channels ch in 0..2 = ambient byte for channel ch.
Every sample is the same, so the 168 bytes are the RGB triplet repeated 56 times.

Coordinate mapping for the editor: `i = (x_min_pad + 131072) / 32` rounded down, `i1` rounded up; `k` uses 64.

## 6. Generation recipe

Inputs from the editor: the `MapFile`, the world bounds (`bounds` in compile_iw8.cpp:1349-1350, including `kBoundsPad`,
compile_iw8.cpp:44), worldspawn `sundirection` and `suncolor` (parsed at compile_iw8.cpp:1353-1378).

1. **Box.** Take the world bounds, grow each side by 32-64 units (one or two cells), and convert to grid indices by the
   formulas in section 2. Compute `Y = j1 − j0 + 1`, `X = i1 − i0 + 1`, `Z = k1 − k0 + 1`, `N = X*Y*Z`.
2. **Ambient colour.** Let `s = suncolor` after the same parse (0-255 scaled above 1, otherwise 0..1). Set
   `colour_c = clamp(round(255 * a * s_c), 0, 255)` with a default `a = 0.7`. The default follows the lit-preview ratio
   0.55 / 0.75 = 0.73 at src\qtapp\main_window.cpp:1400 (a display choice, not a converter rule; UNVERIFIED in game).
3. **Sun visibility, stage 1.** `primaryLight = 1` for every entry. The viewmodel always gets the sun weight. This is
   the first step that fixes the black gun.
4. **Sun visibility, stage 2.** Direction to the sun from `sunDirection(pitch, yaw)` (compile_iw8.cpp:114-117; z is
   `-sin(pitch)`, so pitch above 0 puts the sun below the horizon). If the sun is below the horizon, every entry is 0.
   Otherwise, for each lattice point p: build `CollisionWorld` once from the map (collision_world.h:28-44, cpp:49-64),
   then `traceRay(p + 1*dir, p + L*dir)` with `L` about twice the bounds diagonal. Set `primaryLight = 1` if the trace
   has `fraction == 1` and `!startSolid`, else 0. Caveats: the world only includes brushes that block players (cpp:59);
   player-clip brushes would cast shadows here but may not in game (UNVERIFIED); patches are thin facets (header line
   33). Points inside solid rock come out 0.
5. **Entries and palette.** `color = 0` for all, `traceMask = 0`, colours.bin as in section 5.
6. **Write and validate.** Write the five files. Re-read them and check the byte counts, `N = X*Y*Z`, `rows = X`,
   every `firstEntry`, and the padding. Only then write the JSON. If any check fails, write no grid file (warn-only path,
   iw3_lightgrid.cpp:1212-1216), so the build keeps going.
7. **Placement.** Add the writes next to the existing `replay-world.json` and `replay-comworld.json` writes
   (compile_iw8.cpp:2117-2119), under `mapDir = root\extracted\maps\mp` (compile_iw8.cpp:1988).

Stage 1 is a few hundred lines in one file plus a call. Stage 2 adds one trace per lattice point.

## 7. Sizes (estimates, not measured)

Assumptions: a 5000 x 3000 unit map, height 1024 (UNVERIFIED; real heights differ), padding one 32-unit cell per side.

- X = ceil(5064/32)+1 = 160 (in grid steps; order of magnitude); Y = 97; Z = ceil(1088/64)+1 = 18 (rounded as in
  section 2). N = 160 * 97 * 18 = **279,360 entries**.
- entries.bin: 4N = **1.12 MB**. row_data.bin: 16 bytes x 160 rows = **2.5 KB**. row_starts.bin: 320 bytes.
  colors.bin: 168 bytes. JSON: about 1 KB.
- Converter native payload: a full box is about 259k complete cells (159 x 96 x 17), 6 tetrahedra each, so about
  1.56M tetrahedra x 32 bytes = **about 50 MB** `.gpulightgrid.native` (UNVERIFIED; the loader's file size formula is at
  games\iw8\common\replay_lightgrid.cpp:53, and it accepts up to 512 MB at line 39). Limits: cells <= 2M
  (iw3_lightgrid.cpp:600), tetrahedra <= 8M (line 719), voxels <= 8M (line 650), so about 1.3M cells fit a full box.
- Voxels: rootShift starts at 5 (32 units). Roots about 160 x 97 (about 15.5k) and voxels about 0.5M x 4 bytes = about
  2 MB at that shift (rough estimate; the shift loop is at iw3_lightgrid.cpp:634-652).
- Entity grid (BuildEntityLightGrid, same JSON): about 280k texels. The 4096-row image holds about 68 rows, so the
  fallback warning at iw3_lightgrid.cpp:1883-1896 does not trigger.
- Runtime time: about 280k traces against all player-blocking brushes. The broad phase checks bounds per solid
  (collision_world.cpp:159-168). Expect seconds to tens of seconds (UNVERIFIED; must be measured). Run it once per
  compile.

Denser-than-needed option: the converter accepts gaps (`count 0` runs, lines 223-241) and sparse hulls (lines 690-797),
so the grid can be limited to columns that contain play space. This is UNVERIFIED for the sun shadows and changes the
native size.

## 8. Which surfaces the grid affects

Evidence, from strongest to weakest:

1. **Viewmodels, players and weapons: yes.** The converter says so: "the native light grid that stock-shaded players and
   weapons sample" (iw3_fastfile.cpp:12967-12969). Viewmodel and hand models are named as player characters
   (iw3_fastfile.cpp:1949-1951).
2. **Static models: yes, at convert time.** The converter computes a 4x4x4 volume per instance from the grid
   (SampleStaticModelLighting, iw3_lightgrid.cpp:1390-1393; called at iw3_fastfile.cpp:12060). Without a grid those
   instances take the flat `40` (iw3_fastfile.cpp:12078), with the warning "flat 0.31 ambient" (line 12084). Doc 55
   flags that 40/255 is 0.157, not 0.31 (UNVERIFIED which is right).
3. **Entities and dynamic props: yes.** `BuildEntityLightGrid` reads the same JSON (iw3_fastfile.cpp:11654;
   iw3_lightgrid.cpp:1699-1732).
4. **Unlit world surfaces: UNVERIFIED.** The converter writes no lightmap and clears the lightmap flag bit for these
   surfaces (doc 55 section 2.3). The only reader of the native grid in x64-zt is the loader
   (`games\iw8\common\replay_lightgrid.cpp:25-37`; emit at 99-120). Nothing in the converter shows world surfaces
   sampling it.

So the grid fixes the black gun and the flat models. World walls with no lightmap are a separate question (doc 55).

## 9. Runtime loader and failure modes

- `replay_lightgrid.cpp:25-37`: the native file is found by replacing `.render.json` with `.gpulightgrid.native`. If it is
  missing, the loader returns an empty grid silently, and `Emit` writes nothing (line 101-102). This matches the owner's
  black gun: no native file, no grid.
- Format checks (lines 46-96): magic `IW8GLG01` or `IW8GLG02`; counts at offset 8 (16 bytes); size must equal
  `264 + probes*44 + tets*32 + roots*12 + voxels*4` (line 53); probe positions must be finite; tetrahedron probe indices
  and neighbours must be in range; root leaf ranges are checked. The converter's own writer is the source of these,
  so a valid converter run passes.
- The converter writes `IW8GLG01` payloads through `BuildPayload` (iw3_lightgrid.cpp:1228-1229). The writer format
  was not read in full (UNVERIFIED that the loader accepts it end-to-end).

## 10. Risks

1. **Build failure from a bad field.** Every throw listed in section 2 (names, sizes, row ranges, unconsumed data,
   off-lattice positions, empty grid) aborts the converter at iw3_fastfile.cpp:12977, with no catch. Mitigation:
   validate before writing, and write nothing on failure.
2. **8-bit clamp.** Palette bytes top out at 255 (`/255`), so under `--style iw3` (lightUnitScale 1) the ambient cannot
   exceed the sun's luminance (iw3_lightgrid.cpp:389, 401; iw3_vision.cpp:447 for the iw8 scale). Keep `a <= 1`.
3. **Units match only by derivation.** The recipe assumes grid bytes are in the sun's units, so `a` is a ratio to
   `suncolor`. Derived from iw3_fastfile.cpp:8219-8232 (intensity = luminance, colour = suncolor / luminance) and the
   comment at iw3_lightgrid.cpp:383-388. Not checked in game.
4. **Style.** Under `--style iw8` the scale is `sunIntensity / luminance` (iw3_vision.cpp:447) and the grid is multiplied
   by it (iw3_fastfile.cpp:12970-12977), so the ratio holds. The editor passes no style (doc 55), so `iw3` applies
   (UNVERIFIED for the mapper's own flags).
5. **Shadow approximation.** Brush-only, no patches exactly, player-clip brushes counted as blockers, plus a start nudge.
   Shadow edges may be off by a cell (32 units in x and y, 64 in z).
6. **Size and time.** About 50 MB native for a full box, and a trace per point. Use the sparse option only after
   measuring.
7. **Grid colour may already contain sun.** If the runtime ignores `primaryLight` for viewmodels, the sun is missing
   there (UNVERIFIED).
8. **Old files.** A later compile must rewrite or delete the grid files. Otherwise a stale grid from another map
   version is read (the name check at iw3_lightgrid.cpp:1223-1225 only checks the map id).
9. **Doc 55 discrepancy.** 40 (iw3_fastfile.cpp:12078) versus "0.31" (line 12084). Still unresolved.

## 11. UNVERIFIED list (not run, not read)

- The runtime shader (Replay or the game) for viewmodel, world surface and entity sampling; whether the colour excludes
  the sun and how alpha is used (section 3 is inference from iw3_lightgrid.cpp:1680-1683).
- Converter output size, time, and whether `IW8GLG01` loads in game for a dense or sparse grid.
- Exact probe count in `BuildPayload` (per cell or per palette colour; line 905 is the only use of `PackProbe`).
- Whether the runtime falls back correctly for missing lattice points.
- Map height and bounds for the 5000 x 3000 example.
- Ambient ratio 0.7 (a starting value, not from measurement).
- Traces: time, and whether player-clip brushes should cast shadows in game.

## 12. Next steps (not done here)

1. Stage 1 writer in `compile_iw8.cpp`, all `primaryLight` 1, with the validator in section 6 step 6. Test: a
   compiled map loads, the converter log shows the native grid written (iw3_fastfile.cpp:12978 timing line), and the
   viewmodel is no longer black in game.
2. Stage 2 traces using `CollisionWorld`. Test: a shadowed corner reads `primaryLight 0`, and the build time is recorded.
3. Owner check in game: viewmodel, a static model, a dynamic prop; compare `a` values.
