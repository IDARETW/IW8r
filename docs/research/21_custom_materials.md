# 21. Custom materials and textures for the IW8 extraction route

Scope: how x64-zt `buildextraction` (MW2019 Replay 1.20 target) reads a world-surface material and its
images, and what the editor must emit so a mapper's own PNG/TGA/JPG/DDS texture becomes a playable
surface. Read-only research on `E:\mw124\x64-zt` (`src/convert/iw3_iw8/iw3_render_assets.cpp`, line
numbers from that checkout). Acceptance results are in section 8.

## 1. Lookup path

- A surface names a material (`surfaces[].material`). `ReadMaterial` (`iw3_render_assets.cpp:2539-2562`)
  calls `FindMaterial` (`:1954-1979`), which tries `<extraction>/materials/<name>.json`, then the same
  relative path under each source root. Only `mc/lambert1` and `mc/gfx_missing_fx` get a built-in stub
  when no file exists (`:2550-2557`). Anything else throws (`:2559-2562`). So every surface material needs
  its own JSON file; there is no stock lookup by name for world surfaces (doc 18 section 2.3).
- An image name `N` that does not start with `$` resolves in `LoadImage` (`:1272-1311`) to
  `<extraction>/images/N.dds`, then `images/N.iwi`, then the same under each source path. Built-in names
  with no file: `$white`, `$black`, `$water_specular`, `$identitynormalmap` (`:1277-1285`). `$gray` is a
  last fallback (`:1306`). A missing image throws (`:1313-1318`).
- `ReadMaterial` never opens a technique-set file. `techniqueSet` is only a string that is regex-matched for
  tags (section 3). It is required (`:2577`).

## 2. Material fields that matter

| JSON key | Read at | Effect |
|---|---|---|
| `techniqueSet` (string, required) | `:2577`, `:2629-2657` | Tags only (section 3). |
| `surfaceTypeBits` (uint, default 0) | `:2566` | Footstep/impact surface type bit. |
| `textures[]` `{name, semantic, image}` | `:2660-2682` | Slots by `semantic`: `colorMap` (colour), `normalMap`, `specularMap` (response), `detailMap` (by semantic or by name), `replayColorMap`, `replayNormalGlossMap`, `replayRevealMap`, `maskMap`. |
| no colour map | `:2779-2780` | Throws "IW3 material has no color map". Always write `colorMap`. |
| no normal map | `:5425` | Skipped; the engine default applies. |
| `stateBits[]` `{cullFace, colorWriteRgb, alphaTest, polymodeLine}` | `:2637-2650`, `:2760-2778` | First state with `colorWriteRgb` and no `polymodeLine` sets `cullFace` (`none`/`back`/`front`, default `back`). `alphaTest` `gt0`/`lt128`/`ge128` enables alpha test (default mode `ge128`). |
| `gameFlags` (array) | `:2567-2576` | Only `"CASTS_SHADOW"` is read. |
| `constants` | `:2728-2759` | Optional (`colorTint`, `detailScale`, ...). |

## 3. techniqueSet tags (name regex, no file lookup)

- Alpha test: `(^|_)[at][0-9]` or the substring `alphatest` (`:2654-2656`), e.g. `wall_a1`.
- Blend: `(^|_)b[0-9]` (`:2657`).
- Shadow-only caster: `shadowcaster`, `mc_shadowcaster`, or a name containing `caulk_shadow` (`:2629-2636`).
  Not for visible surfaces.
- Sky: `(^|_)sky($|_)` (`:2649-2651`) skips the surface. Not for visible surfaces.
- Consequence: a generated name containing `_t1`, `_b2`, `_a0` and similar changes the alpha or blend mode
  silently. The editor therefore builds the techniqueSet from a hash with no underscore before the tag-like
  token (section 6).

## 4. Image formats LoadImage accepts (`DecodeDds`, `:753-881`)

- **Uncompressed 2D DDS, 8/16/24/32 bits per pixel**, colour masks read from the header
  (`DecodeUncompressed`, `:712-737`). Luminance (no G and B masks) is also accepted. B8G8R8A8 and R8G8B8A8
  both decode (the doc 15 grey probe is B8G8R8A8).
- **Legacy FourCC DXT1, DXT3, DXT5** (`:764`, `:800-801`). DXT5 alpha is decoded (`DecodeDxt5Alpha`), and the
  DXT5 normal-map swizzle (section 5) applies.
- **DX10 BC7_UNORM only** (`:764-776`): 2D, one array slice, not a cube. Any other DX10 format (BC1, BC3,
  BC4, BC5, RGBA8 in a DX10 header) is refused with "unsupported DDS DX10 image". Legacy `ATI2` (BC5) and
  `BC4U` FourCCs are not in the list at `:801` and are refused too.
- **Size:** each side at most 8192 (`:770`, `:800`); mip count in the header at most 14 (`:800`).
  **No power-of-two check** on 2D images.
- **Mips:** the header count is read and the mip bytes must be present (`:863-877`). The count must not run
  past 1x1 ("too many mip levels", `:865-867`), so write exactly floor(log2(max side)) + 1 levels.
- **Cube maps** (`:939-1060`): square, at most 4096, six faces, no power-of-two check. Used only for
  reflection probes (`images/<probe>.dds`, doc 15 section 7).
- **PNG, TGA, JPG and BMP are not read by x64-zt.** The editor converts them to DDS.

## 5. How a world-surface image becomes a Replay resident image

- The world path (`computeResident`, `:6413-6535`) builds the mip chain from level 0: levels are counted by
  halving (`:6434-6441`) and `MipChain` runs. Texture 0 (colour) is sRGB (`:6417`, format 7 for RGBA8, `:6526`).
  Other channels, including normal and specular, are linear (format 6).
- The default output is **uncompressed RGBA8** (`.rgba`, format 6 or 7). BC7 output (format 44/45, `.bc7`)
  happens only when `world.bc7_images` is true (`:6398`, default false). BC7 needs dimensions that are
  multiples of 4 (`:6470-6472`).
- So with the default flags any size is accepted by the converter. The editor still writes power-of-two
  sizes (section 6) so nothing depends on that.
- **Normal maps:** no encoding is enforced. X is moved from alpha to red only when RGB has R close to G on at
  least 97% of texels and alpha varies (`:5418-5447`). A plain RGB normal map (X in R, Y in G, Z in B, opaque
  alpha) passes through unchanged. Whether Replay expects +Y or -Y green (OpenGL or DirectX convention) is
  NOT-VERIFIED here; the editor writes the OpenGL convention (+Y green, 0..255 maps -1..1).
- **Specular:** `specularMap` RGB is sRGB reflectance (dielectric near 56/255, `:4477-4480`). Its alpha is read as
  gloss when gloss is packed (`:4512-4522`). Optional.
- **Alpha:** alpha drives alpha-test coverage (`PreserveAlphaCoverage`, `:6431-6433`) and the blend tags.
  Opaque surfaces should have alpha 255.

## 6. What the editor emits (implemented with this change)

For a library material `custom/brick_red` defined in `<library>/custom/brick_red.iw8mat.json`:

- Images: `extracted/images/iw8rm_<sanitised name>_c.dds` (colour), `_n.dds` (normal), `_s.dds` (specular).
  Names use only `[A-Za-z0-9_]`.
- Material: `extracted/materials/custom/brick_red.json` with
  `techniqueSet` = `iw8rcm` + 8 hex digits (FNV-1a of the name) + `_a1` only when `alphaTest`,
  `surfaceTypeBits` = `iw3SurfaceTypeBits(type)`,
  `stateBits` = `[{"cullFace":"none","colorWriteRgb":true}]` only when `twoSided`,
  and `textures` = colorMap, then normalMap and specularMap when given.
  The hashed name keeps tag-like tokens out (section 3).
- Image format: **uncompressed B8G8R8A8 2D DDS with a full mip chain**, side resized to a power of two no larger
  than 2048 (aspect not preserved, sides resized independently; no cropping).
  Colour keeps alpha only when `alphaTest`; otherwise alpha is 255. Normal maps are written with alpha 255.
  Specular keeps its alpha (gloss).
- Collision: a brush whose most-used face material is a library material takes that material's surface type.

Materials not in the library keep the grey material, and the warning lists them.

## 7. Open points

- NOT-VERIFIED: the normal map green-channel convention Replay expects (section 5).
- NOT-VERIFIED: whether a 2D B8G8R8A8 DDS builds into a zone (doc 17 proved only the cube probe). Section 8 records it.
- NOT-VERIFIED: non-power-of-two 2D images. The editor never writes them.

## 8. End-to-end result

Test: `out\custom-mat-test` (library: one 256x256 brick PNG for colour, normal and specular; the map is a copy of
`tests\data\iw8_sample_room.map` with `tm2co/me_curb_stone_01` replaced by `custom/brick_red`).

- `iw8r compile ... --materials library`: material `custom/brick_red` with colorMap, normalMap and specularMap, three
  uncompressed B8G8R8A8 DDS images; the other surfaces stay on `iw8r/grey`.
- x64-zt `buildextraction`: built the map with exit 0. Log: `iw3: 2 world material(s)`, no missing-image or
  material error. The normal map was passed through without the DXT5 X-from-alpha swizzle (`0 of 1`), as the code predicts.
- ACTS `--test` (0 failed) on all five zones: mp_iw8r_cmat 19 tested, techsets 4, ww 1, eng 2, srv 0.
- Accepted image format: uncompressed B8G8R8A8 2D DDS with a full mip chain, power-of-two sides.
- Rejected: nothing. The first build passed, so no other DDS variant was run; the rejections in sections 4 and 5
  (DX10 BC1/BC3/BC5, legacy BC5) come from code reading only and are not verified by a run.
- Not verified: whether the normal map's green channel is the sign Replay expects, and whether the bump looks
  correct in game. The zone builds and passes ACTS; the image is packed into the world atlas.
