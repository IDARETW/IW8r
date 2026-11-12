# 27. Minimap / HUD compass image

Scope: where x64-zt `build-map` looks up `compass_map_<map>`, what image format it accepts, and how the
world rectangle and orientation of the image are derived. Read-only research on `E:\mw124\x64-zt`
(`src/convert/iw3_iw8`, `src/games/iw8/dump`), checked against this repo's compile path. Implementation:
`src/core/minimap.h`, `src/core/minimap.cpp`, tests in `tests/minimap_tests.cpp`.

Line numbers are from the x64-zt checkout unless a path says `E:\IW8-Radiant`.

## 1. Lookup

- The image name is `compass_map_` + the map id (`convert/iw3_iw8/convert.cpp:731`). Every image in
  `<dump>/images/` is listed by `dumpimg::listImageDumps` (`games/iw8/dump/read_image.cpp:87-108`, extensions
  `.dds`, `.ffImg`, `.iwi`, stems only).
- An image whose name equals that string gets the map's `compassRotation` (`convert.cpp:733-736`) and sets
  `hasCompass` (`:745`). The material is then written and registered (`:747-758`).
- Without it the converter logs `assets: no images/compass_map_<id>.dds, .iwi, or .ffImg; HUD compass will use
  the default material` (`:762-764`). That is the warning in the build log.
- `readImageDump` tries `<images>/<stem>.ffImg`, then `.iwi`, then `.dds`, then a case-insensitive match on
  the stem (`read_image.cpp:110-145`).

## 2. Format the converter reads

This is the important finding. The compass path does not use the material reader.

- **`.dds` (`read_image.cpp:218-300`)**: the magic, a 124-byte header and a 32-byte pixel format are required.
  Accepted are FourCC DXT1, DXT3, DXT5, or a DX10 header with DXGI 71/72 (BC1), 74/75 (BC2), 77/78 (BC3), as a 2D
  (or volume) texture. Sides up to 8192 and up to 14 mips. The refusal text is `needs a complete supported
  BC1/BC2/BC3 DDS` (`:296-298`).
- **`.iwi` (`:174-208`)**: IWi v6, DXT1/3/5 only, one level, sides up to 4096.
- **Uncompressed B8G8R8A8 is refused by this path.** `loaded` is set only on success (`read_image.cpp:206`,
  `:339`, `:406`). An unloaded image counts as a failure (`convert.cpp:737`), and `addMapAssets` results in
  `throw "failed to import N map assets"` (`convert.cpp:1898-1899`). So an uncompressed `compass_map_<id>.dds`
  does not fall back to the default material; it aborts the build.
- The uncompressed reader that doc 21 section 4 describes (`DecodeDds`, `iw3_render_assets.cpp`) belongs to
  `LoadImage` for surface materials (`images/N.dds`). It is a different code path from the compass one.
- Rotation (`compassRotation`) is implemented for RGBA8 and BC1/2/3 with whole 4x4 blocks per mip
  (`games/iw8/dump/conv_image.cpp:260-321`, `:429-442`). The RGBA8 branch is unreachable from DDS because the
  reader above never produces RGBA8.
- No power-of-two check exists in the reader.

## 3. World rectangle and turn

### 3.1 Metadata (`map.json`)

- `validateMetadata` requires `map.json` in the package (`convert.cpp:242-248`, called at `:588`).
- `compassRotation` must be 0, 90, 180 or 270 clockwise; a value that is present sets `compassRotationSet`
  (`convert.cpp:203-211`). Written back when set (`:280-283`).
- `minimapScale` is 0.5..1.5, default 1.0 (`convert.cpp:104-115`, the parse block after `:203`).

### 3.2 Corners (`iw3CompassBounds`, `games/iw8/dump/maps.cpp:366-468`)

- Exactly two `script_origin` entities with `targetname minimap_corner` are needed (`maps.cpp` loop after
  `:385`). Any other count returns false, so no bounds.
- The corners are ordered in the Replay frame: `north = (cos 90, sin 90) = +Y`, `west = (-north.y, north.x) = -X`
  (`:374-380` and `:427`). The result is NW = (min X, max Y) and SE = (max X, min Y). The rectangle is the XY
  box of the two corners.
- Read from `<map>.d3dbsp.ents` (`convert.cpp:2218-2221`, `loadEnts`).

### 3.3 Squaring, scale and turn (`convert.cpp:2222-2281`)

- If `|spanX - spanY| > 1`, the region is squared about its centre to the longer span (`:2226-2245`).
- `minimapScale` scales the region about its centre (`:2256-2259`).
- If `compassRotation` was not set, the image is turned clockwise by `(90 - northYaw) mod 360`
  (`:2264-2281`). `northYaw` is the source worldspawn key, default 0.
- Runtime: `setminimap("compass_map_<m>", ...)` uses the squared corners (`:1913-1932`). The console dvar
  `mw120r_minimap_scale` overrides the scale. The sun-shadow crops use the same rectangle with a margin
  (`:1634-1640`).

### 3.4 What the Radiant compile writes

- `E:\IW8-Radiant\src\core\compile_iw8.cpp:851-856`: the `.ents` worldspawn gets only `classname` and
  `sundirection` (and `suncolor`). There is no `northyaw`, so the converter reads 0 and turns the image 90
  degrees clockwise.
- Nothing in `E:\IW8-Radiant\src` or `tools` writes `map.json`. The build needs one, and `compassRotation` must
  come from it.
- Output side: every converted map writes worldspawn `north 90` (`games/iw8/dump/maps.cpp:715-718`), because
  Replay rejects corners when the key is missing. That is the output, not the input.

## 4. Orientation

Image handed to the converter (source frame, northyaw 0, which is what the Radiant `.ents` implies):

- row 0 (top) = +X; the bottom row = -X;
- column 0 (left) = +Y; the right column = -Y.

The turn (`conv_image.cpp:214-321`) moves source pixel (col x, row y) to (row x, col h-1-y). That is a clockwise
quarter turn. Source top goes to the right, source left goes to the top, so after the turn:

- top = +Y, right = +X, left = -X, bottom = -Y.

This matches the Replay frame, where the NW corner is (min X, max Y).

The renderer in `src/core/minimap.cpp` writes the source frame, so the default converter path (no `map.json`
`compassRotation`, no `northyaw` in the `.ents`) gives the correct in-game orientation:

- `column = (maxY - y) * scale`, `row = (maxX - x) * scale`, with `scale = size / span`.

If the compiler ever writes `northyaw 90` into the `.ents`, or `compassRotation: 0` into `map.json`, the turn
becomes 0 and this frame is 90 degrees off. Either change must be paired with a change in the renderer.

## 5. Rectangle the renderer covers

- If the map has exactly two `minimap_corner` entities, the image covers their XY box squared about its centre.
- Otherwise it covers the XY box of the drawn surfaces, squared about its centre. The converter ignores this
  rectangle when there are no corners; it is only a fallback so an image can still be produced.
- The z range is the range of the drawn surfaces.

## 6. Open questions

1. **Resolved in the writer.** `writeMinimapDds` now writes a legacy FourCC `DXT5` (BC3) DDS with a full mip
   chain, which `read_image.cpp:218-300` accepts. The uncompressed form is not used for the compass.
2. **`map.json` is required and nothing writes it** from the Radiant side. Without it the build fails, and
   without `compassRotation` the default 90-degree turn applies.
3. **Corners.** The converter needs exactly two `script_origin` entities with `targetname minimap_corner`.
   Without them there is no `setminimap` call even when the image exists. Mappers have to place them, and the
   `.ents` writer has to carry them through.
4. **`minimapScale`.** The renderer covers the unscaled rectangle. A map that sets `minimapScale` other than 1
   has a region that no longer matches the image.
5. **Colours and light** are a neutral placeholder (warm grey, one fixed light). They are not calibrated against
   the game art.
