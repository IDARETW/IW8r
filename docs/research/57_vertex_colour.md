# 57. Vertex colour and alpha: from the editor to the built IW8 world

Roadmap 8.11 (Blend painter, with 5.9 layered materials). Status of this note: research plus a core painting module
(`src/core/vertex_paint.*`, tests `tests/vertex_paint_tests.cpp`). The editor tool is not wired (src/qtapp was out of
scope), and nothing here has been seen in game.

Short answer: colour and alpha survive in the `.map` file, but not into the built world. `compile_iw8.cpp` drops them.
The converter (x64-zt) already reads a per-vertex colour from the world JSON, so the missing link is on our side, in
the compiler and tessellator, plus a decision about what the colour means in game.

## 1. What the editor stores (the map format)

- A patch control point may carry a colour block between its position and its texture pair:
  `v x y z c r g b a t s t ls lt`. Example: `samples/iw8/iw8_sample_room.map:167`
  `v 128 128 0 c 255 255 255 255 t 64 0 4 0` (the same line in `tests/data/iw8_sample_room.map:77`). Values are
  integers, R G B A order. The sample uses 255 for white; the converter accepts 0..255 (below). The `.map` text itself
  does not state the scale, so 0..255 is inferred from the sample and the converter (UNVERIFIED against Radiant).
- The editor parses positions only. `PatchVertex` has line, token, row, col and pos, and no colour
  (`src/core/map_text.h:39-46`). `readSt` in `src/core/patch_ops.cpp:71-82` skips the block when it reads texture
  coordinates. `makePatchPrimitive` writes no colour (`src/core/patch_ops.h:12-19`), so a patch made in the editor has
  none unless it came from a map that had one.
- The text layer is lossless. Only `v x y z` tokens are rewritten when a patch moves (`patch_ops.h:101-103`, and
  `Primitive::translate` in `map_text.cpp`), so colour tokens survive every edit and save (`map_text.h:1-9`). So a
  colour already in a map survives the editor today. Nothing in the editor adds or changes one.
- Brushes have no per-vertex colour. A brush is a set of planes (`BrushFace`, `map_text.h:23-30`), so there is no vertex
  to paint. Only patches can carry colour in this format.
- The slope blend in `heightmap.h:55-62` is computed per control point and not stored; its own comment says patches
  carry no colour field.

## 2. What the compiler does (`src/core/compile_iw8.cpp`)

- `gatherPatch` (`compile_iw8.cpp:358-400`) calls `tessellatePrimitive` and keeps only positions and normals. It builds
  each triangle's `RenderVertex` from position, normal, tangent and uv (`compile_iw8.cpp:230-233`).
- `PatchMesh` has `points`, `normals` and `indices` only (`src/core/patch_tess.h:22-28`). Tessellation interpolates
  positions; nothing interpolates colour.
- The world JSON vertex record is `position`, `normal`, `tangent`, `binormal_sign`, `uv`, `lightmap_uv`
  (`compile_iw8.cpp:1766-1772`). There is no `color` key, so the compiler sends no colour to x64-zt.
- Brush faces are fans of positions with the same record, so they carry no colour either.

Result for a built world: a painted or sample colour block is dropped at `gatherPatch`. Every IW8 world surface reaches
x64-zt with the default colour.

## 3. What the converter (x64-zt) accepts

Read-only: `E:\mw124\x64-zt`, branch and commit not touched.

- Default vertex colour is opaque white: `Vertex::color{255,255,255,255}` (`src/convert/iw3_iw8/iw3_fastfile.cpp:119`).
- The world JSON reader takes an optional per-vertex `color` of four ints (`iw3_fastfile.cpp:2264-2273`) and rejects
  values outside 0..255 (`iw3_fastfile.cpp:2445-2447`). `ReadWorldSurface` (`iw3_fastfile.cpp:2400`) stores it per
  vertex. `ReadWorldJson` is called from the reader at `iw3_fastfile.cpp:10679` (code reading only; the path from
  an iw8r `world.json` through `buildextraction` is not run here, UNVERIFIED end to end).
- The Replay world vertex record gets `{"color", vertex.color}` (`iw3_fastfile.cpp:6561`). Model vertices pack the
  colour as ARGB into the XSE vertex (`iw3_fastfile.cpp:4796-4799`), and shadow vertices take alpha from it
  (`iw3_fastfile.cpp:5299`).
- Meaning in the converter's own routes, all from IW7 extractions and none from an iw8r map:
  - A stock layered surface uses its vertex colours as layer weights (`iw3_fastfile.cpp:134`).
  - A layered material's base blends by vertex red, and a decal ramps by vertex alpha (`x64-zt/src/convert/iw7_iw8/export_materials.cpp:1899`).
  - A lit blended decal is tinted by vertex colour, with coverage from vertex alpha through the reveal ramp
    (`iw3_render_assets.cpp:7564`).
  - A lit flow's dissolve reads vertex alpha (`iw3_render_assets.cpp:258`).
- Our research 09 (`docs/research/09_iw8_render_features.md:57`) says layered materials "blend by a vertex colour
  channel with per-layer UVs". It does not say which channel drives which layer.

## 4. Representation chosen for the core module

No map-format change and no edit of `map_text.*` or `patch_ops.*`:

- Colour lives in the same `c r g b a` block the map already has, integers 0..255, R G B A order. A control point
  without the block is white and opaque, matching the converter default.
- Control points are addressed by their index into `Primitive::verts` (file order, row-major for a complete grid, the
  same order `patchGrid` uses).
- `setPatchVertexColour` overwrites an existing block's four values, or inserts ` c r g b a` after the position. It
  rewrites that one line, keeps every other byte, and calls `reparsePrimitive` so token indices stay right.
- `readPatchColours` returns one colour per control point. `hasPatchColour` says whether a block is present.
- `paintPatchVertices(patch, center, brush)` blends the channels in `channelMask` toward `rgba` by
  `strength * brushWeight(distance)`, rounded to a byte, and counts the control points that changed. Control points
  at or beyond the radius are never written. A control point whose rounded colour does not change is not written.
- `brushWeight`: hard (falloff 0) is 1 inside the radius and 0 outside. Otherwise it is 1 out to
  `radius * (1 - falloff)`, then a smoothstep down to 0 at the radius.

Limits: the colour is per control point only. A surface vertex between control points is interpolated only once the
compiler does it (section 5).

## 5. What would need changes (not done here)

Done here: the representation and the paint operation, in core, with tests.

Needed for colour to reach the game:

1. `compile_iw8.cpp` `gatherPatch`: carry a colour per control point to each triangle vertex. `patch_tess` must
   interpolate colour with the same parameterisation as positions (a second grid next to `points`). This is a
   `patch_tess.*` change, outside this task.
2. `compile_iw8.cpp` world JSON writer (around `compile_iw8.cpp:1766-1772`): emit `"color":[r,g,b,a]` per vertex when the
   surface has colour. Omit it otherwise, so untouched maps build exactly as now.
3. Brushes: no vertices to carry colour, so a brush-level convention would be needed (for example a key on the
   entity). Decision for the owner; not designed here.
4. The meaning in game (UNVERIFIED for IW8 Replay): which colour channel drives layer 2, and whether the Replay world
   shader multiplies its lit colour by vertex colour. The converter's routes (section 3) come from IW7 extractions.
   A mapper test is needed: paint a wall, build, check the blend moves in game (`research 09` mapper test).
5. Does an iw8r custom material support a layered or blended material? Not checked here (UNVERIFIED). If not, step 4
   has no material to drive.
6. Editor: a Blend painter tool in `src/qtapp` that calls `paintPatchVertices` with the brush settings, plus a
   visual of the colour on patches. Out of scope for this task.

## 6. Test coverage

`tests/vertex_paint_tests.cpp` covers: the hard and soft brush weights and a partial falloff, the invalid-brush cases,
default white on a patch with no block, the hard brush hitting only control points inside the radius, a line-level
check that only the changed line is rewritten, insertion that leaves xyz and texture tokens intact, strength blending
with rounding, soft falloff, channel masks (red only, alpha only), zero strength / mask / radius, idempotent repeat,
non-patch and bad-index handling, overwriting the sample's existing block, and a round trip through `parseMap` and
`MapFile::text()`.
