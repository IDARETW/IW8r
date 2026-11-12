# 33. Sky route: stock Replay skies in x64-zt builds

Scope: roadmap 5.4 (sky). Follows doc 29 section 3, which left `world.sky` empty. x64-zt (`E:\mw124\x64-zt`, branch
`iw8radiant/build-extraction`, commit `190aa793`) was read only. Line numbers are from that checkout.

## 1. What the converter needs

### world.json `sky`

- `sky` is an **image name** (not a material). `iw3_render_assets.cpp:6186` reads `world.at("sky")` as a string and
  loads it through the `image` lambda (`:5262-5267`), which calls `LoadImage(exportRoot, sourcePaths, name)`.
- `LoadImage` (`:1272-1340`) tries `<extraction>/images/<name>.dds` first (`:1285-1289`), then `.iwi`, then the
  `--search_path` folders, and throws `missing IW3 image` otherwise (`:1338-1339`).
- The empty name is the only special case: a dark placeholder cube `(24,26,30)` and the info line
  `iw3: map has no sky image; using a dark placeholder sky cubemap` (`:6189-6193`).
- The result must have 6 entries (faces), else `IW3 sky image is not a complete cubemap` (`:6194-6196`).

### DDS layout (`DecodeDds`, `:753-881`)

- A DX10 header (`fourCC "DX10"`) is accepted only for a 2D, non-cube, single-slice **BC7** image (`:765-773`). A DX10
  cube, BC6H or any other DX10 format throws `unsupported DDS DX10 image`.
- A legacy header: fourCC 0 (uncompressed 8/16/24/32 bit with masks, `DecodeUncompressed :712-736`) or DXT1/3/5
  (`:798-800`). Size up to 8192, at most 14 mips.
- A cube is `caps2 & 0x200` and must have all six face bits (`caps2 & 0xFC00 == 0xFC00`, `:801-803`). Faces are read in
  file order, each followed by its mips (`:808-878`).
- Faces are drawn by the sky box `appendSky` (`iw3_fastfile.cpp:6253-6292`): six quads at 32768 units, face `f` maps
  `(u, v)` to the D3D cube axes in IW world space (`+X: (1,-v,-u)`, `-X: (-1,-v,u)`, `+Y: (u,1,v)`, `-Y: (u,-1,-v)`,
  `+Z: (u,-v,1)`, `-Z: (-u,-v,-1)`; `:6259-6264`). The IW7 sky export uses the same table
  (`src/convert/iw7_iw8/export_sky.cpp:35-52`).
- Bytes are sRGB: faces go into the atlas with `Resize(..., true)` (`:6197-6200`, sRGB-aware) or, on the native world
  path, as resident images of the `sky` variant (`:7793-7810`).
- Reference writer: the IW7 route writes exactly this shape: `InitializeCube(DXGI_FORMAT_B8G8R8A8_UNORM, size, size, 1,
  1)`, sRGB bytes, `SaveToDDSFile(..., DDS_FLAGS_FORCE_DX9_LEGACY)`, returns the file stem as the sky name
  (`export_sky.cpp:836-881`). HDR above 1 clamps (`:855-869`).

### Anything else

- No sky material or techset is needed: the sky box is always added (`iw3_fastfile.cpp:6395-6406`, `:6578-6579`), with
  the generated sky variant or one native material per face (`iw3_render_assets.cpp:7793-7810`).
- Sun: unrelated to `sky` (doc 24 / doc 15: `world.sun` and the primary lights). Sky rotation: the IW7 route bakes it into
  the cube (`export_sky.cpp:496-525`); the IW3 route has no rotation input. Sky fog: no input on the IW3 route (the
  IW7 route bakes it too).
- Brush faces with a `sky` material are tool faces in our compiler (not rendered, `compile_iw8.cpp` `isIw8ToolMaterial`).
  The sky box covers every direction, so no sky brushes are needed.

## 2. Stock Replay skies

Catalog (`data/asset_catalog/iw8_images.tsv`, names starting `sky_`): `sky_mp_aniyah_tac`, `sky_mp_backlot2`,
`sky_mp_donetsk` (catalogued in `techsets_mp_donetsk` only), `sky_mp_emporium`, `sky_mp_euphrates` (`mp_village2`),
`sky_mp_hideout`, `sky_mp_m_king`, `sky_mp_m_speedball` (`mp_bm_tut`), `sky_mp_rust`, `sky_mp_shipment`, and
`sky_dev_thermal` (thermal vision; 11 zones). Each `sky_mp_*` has a material `tw/sky_mp_*` (`iw8_materials.tsv`).
Doc 30 shows `tw/sky_mp_shipment` is drawn by a dome mesh in the stock world (about +-82k units).

ACTS export (`acts --noUpdater fastfile -r mw19replay -g <replay exe> --oodle <dll> --xpak <every pak_*.xpak> -a image
-n <name> -l -o <dir> <zone>.ff`, the `MaterialCache::startRun` shape):

| Image | Zone | ACTS result | DDS |
|---|---|---|---|
| `sky_mp_shipment` | `mp_shipment` | ok, 4,194,452 bytes | DX10, BC6H_UF16 (dxgi 95), 2D, 2048x2048, 1 mip, array 1 |
| `sky_dev_thermal` | `mp_shipment` | ok, 131,220 bytes | DX10, BC4_UNORM (dxgi 80), 2D, 512x512, 1 mip |
| `sky_mp_rust` | `mp_rust` | ok | DX10 BC6H_UF16, 2048x2048 |
| `sky_mp_donetsk` | `techsets_mp_donetsk` | "not in zone" | |
| `sky_mp_donetsk` | `mp_donetsk` | ok (37 s, large zone) | DX10 BC6H_UF16, 2048x2048 |

So **the stock sky is not a cube map**. The converter refuses it as is (DX10 non-BC7, and one face, not six).

### Projection of the stock image

Decoded (`sky_mp_shipment`): the centre is the zenith and the whole square border is the horizon (a dark skyline runs
along the edges; the sun sits low near the lower-left edge). `sky_dev_thermal` has square iso-lines of brightness
(`max(|u|,|v|)` constant). Both match a **hemi-octahedral** map rotated 45 degrees: with `u, v` in `[-1, 1]` across the
image, `x = (u + v) / 2`, `y = (u - v) / 2`, `z = 1 - |x| - |y|` (the form x64-zt documents for IW7 normals,
`export_materials.cpp:393-405`). After conversion the horizon is a straight line at the middle of every side face, which
a wrong projection would bend (checked on the shipment faces).

- **UNKNOWN:** the axis orientation and handedness (which image direction is world +X / +Y). The converter maps image
  `+u+v` to world `+X` and `+u-v` to `+Y`; the shipment sun then lands at azimuth about -92 degrees (toward -Y), about 18
  degrees up. Not checked against the stock map's sun or in game. A mirror or a 90-degree turn is possible.
- **UNKNOWN:** the stock HDR scale. Replay draws the dome with its own exposure. The converter has no HDR input (8-bit cube).

## 3. Implementation

- `src/core/sky_image.*`: `skyCubemapFromDds` turns a 2D source (BC6H u/s, BC7, BC1-BC4, RGBA8/BGRA8, RGBA16F, RGBA32F,
  legacy DXT/32-bit) into a legacy B8G8R8A8 cube (128-byte header, `caps2 0xFE00`, one mip, `faceSize` default 1024,
  faces in the D3D order above). It samples the hemi-octahedral source bilinearly. Below the horizon it fades from the
  horizon texel to the mean of the border ring over the first 0.2 of `-z` (about 11 degrees).
  A source that is already a converter cube passes through unchanged. A DX10 cube is refused. `isConverterSkyCubemap`
  mirrors `DecodeDds`'s checks. `skyImageName` turns `tw/Sky_MP_X` into `sky_mp_x`.
- HDR exposure (APPROXIMATION): linear times `0.9 / L99.5` (the 99.5th-percentile luminance), then sRGB. Shipment:
  mean luminance 0.059, p99.5 0.400, exposure 2.25. Only the sun disc clamps.
- `CompileOptions::skyDdsPath` / `skyFaceSize`. When worldspawn has `skyname` (or `sky`) and the path is set,
  `compile_iw8.cpp` writes `extracted/images/<skyImageName>.dds` and `world.json "sky": "<skyImageName>"`.
  `CompileReport::sky` / `skyNote` say what was done. Otherwise the doc 29 warning stays: no image provided, unreadable
  file, or unsupported format (world sky `""`).
- CLI: `iw8r compile ... --sky-dds <dds> [--sky-face n]`.
- Qt: `src/qtapp/stock_sky.*`.
  - `QStringList iw8r::qt::stockSkyNames()` lists the catalog sky images, `sky_mp_*` first.
  - `StockSkyCache` exports one sky with ACTS into the material cache's `images/` folder. It tries the catalog zones,
    then the map zone of a `techsets_` zone and `mp_<x>` of `sky_mp_<x>`.
  - `CompilePanel::setSkySource` takes the cached DDS or starts the export. MainWindow wires it.
  - The compile log says `sky: <name> built (...)` or `sky: <name> not cached yet ...`.
  - Headless check: `IW8Radiant.exe --stock-sky-test <sky> --out <dir>`.

## 4. Verification

- Tests: `tests/sky_tests.cpp` (synthetic RGBA16F hemi-octahedral source: header, masks, cube flags, zenith / horizon /
  below-horizon colours, auto exposure, LDR bytes kept, pass-through, DX10 cube refused, compile writes the cube and
  `world.sky`, missing file and no image keep `""` with a warning).
- End to end: `tests/data/iw8_sample_room.map` plus `"skyname" "sky_mp_shipment"` was compiled with `--sky-dds` of the
  ACTS export. The compiler wrote `images/sky_mp_shipment.dds` (25,165,952 bytes = 128 + 6 x 1024^2 x 4) and
  `"sky":"sky_mp_shipment"`.
- x64-zt `buildextraction` (`--output out\x64zt-extraction-test\output`) built `mp_iw8r_sky`. It logged no placeholder
  sky line, a 2048-pixel atlas cell per sky face (the 1024 faces are upscaled into it), "map output 'mp_iw8r_sky' passes
  structural checks", and published the five zones. The sky is **not checked in game**: orientation, brightness and seams
  are open.

## 5. Open items

1. In-game check of orientation (section 2 UNKNOWN) and brightness. If the sky is mirrored or turned, change
   `hemiOctTexel` in `sky_image.cpp` only.
2. A sky rotation key (the IW7 route bakes a rotation) and sky fog are not supported.
3. `skyFaceSize` 2048 would match the atlas cell, but it makes a 96 MB extraction image. 1024 is the default.
