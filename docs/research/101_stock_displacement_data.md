# 101. Stock world displacement data (Replay 1.20): per-vertex records, scene constants, CPU subdivision

**Why.** The owner noticed "no tessellation" on stock ground: Replay displaces the world surfaces of materials with a
displacement layer (`<n>dn` layer names, `_disp` / `_dsp` single-layer materials, techset hexB token `...8`), and the
editor draws them flat. Research 98 section 5 read the shaders. This note resolves the data the editor needs, the
scene values Replay uses, the difference between the plain and the tessellated path, and adds a CPU implementation
(`src/core/stock_displacement.{h,cpp}`, tests `tests/stock_displacement_tests.cpp`) verified on mp_village2.

**Labels.** VERIFIED = read in the shipped shader disassembly, the Replay exe, or the world / material data (cited).
INFERRED = from names, the IW8 game-test decompile, statistics or arithmetic.

## How it was obtained

- Shaders: the disassemblies kept by research 98 in `out/agent/decals/asm/` (VD `f06c0749` = plain lit displaced VS,
  VT `9a82745f` = WORLDTESS VS, HS `a16af270`, DS `2a7a2c67`, PD `216e2c87`).
- Replay exe (`game_dx12_ship_replay.exe`, read only): the dvar registration code was found by the hashed dvar names
  of the game-test source (`r_dvars.cpp:1052-1059, 1372-1382`), which are plain strings in the Replay exe; disassembled
  with capstone (`out/agent/dsp/py/dvars2.py`, `useSites.py`).
- World data: the editor's world caches (`%LOCALAPPDATA%/IW8 Radiant Suite/IW8 Radiant Suite/world_cache/<map>`),
  surveyed by `out/agent/dsp/py/survey.py` (`survey_village2.txt`).
- Materials: ACTS `-a material -n <75 names>` on `zone/techsets_mp_village2.ff` (the material bodies are in the
  techsets zone, not in `mp_village2.ff`); summary `out/agent/dsp/mats_village2.txt` (`py/mats.py`); the export is kept
  in `out/agent/dsp/m/`. mp_aniyah_tac: all materials of `techsets_mp_aniyah_tac.ff` (`mats_aniyah.txt`; export deleted).
- Height images: ACTS `-a image -n <full name with ~hash>` on `zone/mp_village2.ff` with `--xpak` for every
  `zone/*.xpak` over 64 KB; decoded with `py/bc4_to_pgm.py` (numpy BC4). Exports and decoded images deleted afterwards.
- Real-data runs: the opt-in test `stock_displacement_real_data` (env `IW8R_DSP_ZONE`, `IW8R_DSP_MATS`,
  `IW8R_DSP_IMAGES`); output in `out/agent/dsp/real_runs.txt`, `real_runs2.txt`.

## 1. Per-vertex data (VERIFIED)

`GfxWorldSurfData` (88 bytes, gfx_map `surfaces.surfData`; ACTS "values" form names the fields, the "bytes" form of
big maps has them at these offsets):

| Offset | Field | Use |
|---|---|---|
| 0x08 | `xyzOffset` | posData byte offset of the block's vertex 0; vertex j = `SV_VertexID` (VD:60) |
| 0x0C | `tangentFrameOffset` | aux stream, 4 bytes per vertex: the packed tangent frame (VD:78-80) |
| 0x38 + 4L | `displacementOffset[L]` | aux stream, 4 bytes per vertex for displaced layer L (VD:121-127, VT:84-89) |

- The displacement record of vertex j is at `displacementOffset[L] + 4 j`, j block-relative like the colour and
  lightmap records (the reader adds the surface's first vertex in its block, 0 on every surface seen). Low 16 bits:
  binary16 **lo**; high 16 bits: binary16 **hi**. `displacementOffset[L] == 0`: the shader uses the literal
  `0x3C000000` = lo 0, hi 1 (VD:124), so no displacement.
- **lo** is the painted amplitude, 0..1 (mp_village2: 0 to 1; 8,039 of 21,325 surface vertex records non-zero, 38 %).
- **hi** is the layer's UV per inch (INFERRED meaning, strong statistics): hi / the measured layer UV density
  (sqrt(UV area / world area) per triangle) has median 1.020 on mp_village2, 1.019 on mp_aniyah_tac, 1.001 on
  mp_backlot2, 1.000 on mp_shipment, 1.46 on mp_m_speedball; p10 = 1.000 everywhere, so hi is the larger (max
  stretch) density, not the mean. The shader uses it in the amplitude (heightAuto, section 2) and the height mip.
- **The tangent frame normal is the displacement direction** (VD:84-113, 145-151): the word is a smallest-three
  quaternion: x = bits 0-9 / 1023, y = bits 10-19 / 1023, z = bits 20-28 / 511, each `* 2 - 1` then `* sqrt(1/2)`;
  bit 29 = bitangent sign; bits 30-31 = index of the dropped component (rebuilt as `sqrt(1 - x^2 - y^2 - z^2)`); the
  quaternion is (s,x,y,z) / (x,s,y,z) / (x,y,s,z) / (x,y,z,s) for index 0..3 and the normal is
  `(2(qx qz + qy qw), 2(qy qz - qx qw), 1 - 2(qx^2 + qy^2))` (`stockTangentFrameNormal`). These are smoothed
  normals, not StockWorld's area-weighted face normals: on mp_village2's displaced meshes they differ by 11.5 degrees
  on average, 5,056 of 12,139 vertices by more than 10 degrees (max 89.7, at creases); none points the other way.
  mp_aniyah_tac 7.9 degrees mean, mp_backlot2 2.1, mp_m_speedball 2.6.
- `GfxSurface.tris.displacementParmsOffset` (ACTS name) is not an offset: it reads as a float (129.0, 58.2, 632.0,
  ...), at most the surface's bounding diagonal, on every surface, displaced or not. Not used by the shaders read;
  meaning not determined (a size measure, INFERRED).
- `GfxDrawSurf.fields.tessellation` (gfx_map `surfaceMaterials`) is 0 on all 1,366 mp_village2 surfaces, displaced or
  not: the tessellated technique is chosen at run time (section 3), not by this bit (INFERRED).

## 2. Scene parameter and dvar defaults (VERIFIED in the Replay exe)

Registration in `game_dx12_ship_replay.exe` (image base 0x140000000):

| Dvar (game-test name) | Hashed name | Replay default | Code |
|---|---|---|---|
| r_tessellation (enum `0_Off`, `1_Near`, `2_All`) | MMNMQTSOSP | **1 (Near)** | 0x14192FA0D (`r8d = 1`) |
| r_tessellationLodBias | NNMPPPMOSR | **0.5** | 0x14192FA27 |
| r_tessellationHeightAuto | SQTTTTNQP | **true** | 0x14192FA6A (`mov dl, 1`) |
| r_tessellationHeightScale | OKOSKLQSPS | **1.0** (0 .. 100) | 0x14192FA71 |
| r_tessellationHybrid | LPTSOPRMQL | **1.0** (0 .. 10) | 0x14192FAB6 |
| r_tessellationFactor | NOSQLKNSQO | **60** (0 .. 200) | 0x141930F3D |
| r_tessellationCutoffDistance | LMNOQSTMKN | **960** (0 .. 10000) | 0x141930F6C |

The scene constant setup at 0x1419738B3 (VERIFIED): `param = heightScale; if (heightAuto) param *= -0.00390625`
(the constant -1/256 at 0x142595BD8), the same as game-test `r_scene.cpp:5060`. So with the defaults
**param = cb7[114].z = -1/256**, and the shader's `amp = lo / (param >= 0 ? 1 : hi) * |param|` (VD:138-141) becomes

    amp = lo / (256 * hi)

that is: the relief scales with the texture's world size (1 / hi inches per repeat), "maxDisplacement" is in 1/256
of a repeat (INFERRED reading; the arithmetic is VERIFIED). Without heightAuto it would be `amp = lo`. These are the
registered defaults; whether a user config changes them was not checked (they are not menu options, INFERRED).
Both amplitudes on mp_village2: with heightAuto `amp` 0.0005 .. 0.700, largest possible move `amp * |bias|` 5.5 in;
without it 10.0 in. mp_m_speedball: amp 0.018 .. 0.083, at most 1.67 in (research 98 said 1.7).

## 3. The two shader paths

### Height sample (VERIFIED, VD:117-136, DS:221-253)

- Texture: slot **95 + L** of the material (VS register t0), **red** channel, sampler s3 (wrap).
- UV: the displaced layer's own stored UV, as stored (raw float2 at `texCoordOffset + 8 (layerCount j + L)`), no
  psData UV transform, game convention v = 0 at the top. StockWorld keeps `(u, 1 - v)`, so sample at
  `(u, 1 - layerUv[1])`.
- Mip (SampleLevel): `lod = log2(width) + log2(hi) + log2(depth / (proj11 * factor * ((eyeScale - 1) * saturate(N.V) + 1))) + lodBias`,
  that is `log2(width * hi * E) + 0.5` with `E = depth / (proj11 * r_tessellationFactor)` the world edge length the
  tessellator aims for (about `depth / 127` at a 50-degree vertical FOV): the height is band-limited to the mesh
  resolution. In the DS `log2(hi)` is interpolated barycentrically (VT `o7.y`).
- `d = amp * (h * scale + bias)`, `(scale, bias)` = material constant **45 + L** (`constantTable` literal; also vsData
  row `1 + 3N` of both constant buffers). It equals `(maxDisplacement, -maxDisplacement / 2)` on 75 of 75 mp_village2
  and 205 of 210 mp_aniyah_tac materials; the other 5 (asphalt potholes) are `(14, -1.4)` (mid-level 0.1, the holes go
  down). **Read the constant, do not derive it from maxDisplacement.** `.w` is 0 on all 285.

### Plain lit VS (no tessellation: far surfaces, `r_tessellation 0`, split screen) (VERIFIED)

`P' = P + normalize(N_frame) * d` at the existing vertices only. The pixel shader adds the residual as parallax
(research 98, `r_tessellationHybrid`).

### WORLDTESS (r_tessellation 1 = Near, the default) (VERIFIED structure; selection INFERRED)

- Selection: game-test `R_SurfaceHasTessellationCutoff` (`r_add_bsp.cpp:612`) uses the tessellated technique when
  `rg.tessellation == 1` and the view origin is within `r_tessellationCutoffDistance` (960) of the surface bounds;
  `2_All` always. Replay code not traced (INFERRED).
- HS: triangle domain, integer partitioning, max factor 64 (plus subdomain instancing). Edge factor = edge length /
  `(depth / (proj11 * factor))`, faded to 1 between `cutoff - falloff` and the cutoff (HS:330-367; cutoff 960 VERIFIED, falloff 320 is the
  game-test default, INFERRED: its Replay registration did not disassemble cleanly).
- VT per control point: `BMSP = (amp * bias, amp * scale, saturate(c.w) * lo, (1 - saturate(c.w)) * lo)`,
  `DISPLACEMENT_SCALE.y = amp * scale`, `DENSITY.y = log2(hi)` (VT:84-99).
- DS per new vertex with barycentrics b_i (DS:205-322):
  - `P_flat = sum b_i P_i`.
  - Phong: `P = P_flat - alpha * sum b_i ((P_flat - P_i) . N_i) N_i`, `alpha = 0.75 * saturate(sum b_i (1 - saturate(c.w)) lo_i)`.
    With c.w = 0 everywhere: `alpha = 0.75 saturate(lo)`, so painted areas round off between vertices.
  - Direction: `dir = normalize(sum b_i max(amp_i scale, 1e-4) N_i)`.
  - Length factor `k = 1 + (min(max(|sum|, min_i a_i / (N_i . dir)) / sum b_i a_i, 2) - 1) * sum b_i saturate(c.w) lo_i`
    (a_i = max(amp_i scale, 1e-4)); k = 1 when c.w = 0 (all maps seen).
  - `d = h * sum b_i amp_i scale + sum b_i amp_i bias`, `P' = P + dir * k * d`.
  - At a control point (unit barycentric) this is the plain VS result: the paths agree at the original vertices.

## 4. CPU version for the editor (`src/core/stock_displacement.{h,cpp}`)

The editor does not tessellate on the GPU. `displaceStockMesh` does what the DS does, once, on the CPU:

- **Subdivision**: conforming red-green bisection. Each pass halves every edge longer than
  `max(targetEdge, targetUv / hi)` whose larger end "relief" (`amp * max(|scale + bias|, |bias|)`) is at least
  `minRelief` (default 0.1 in). The decision depends on the edge alone, so both triangles of an edge split it and share
  the midpoint: no T-junctions, no cracks (tested: every interior edge used twice). `targetUv` (default 1/32 of a
  repeat) keeps stretched terrain coarse, as its mip is coarse (mp_aniyah_tac has displaced terrain with hi 1/2048).
- **Evaluation** per vertex from its source triangle: all attributes interpolated (layer UVs, lightmap UV, vertex
  colour bytes, layer normal transforms), Phong, the DS direction / length factor, `d` with the height sampled
  bilinear-wrap-trilinear at the vertex's layer UV and mip `log2(width * hi * edge) + 0.5`, edge = the cut length.
  Normals recomputed (area weighted). `targetEdge <= 0` reproduces the plain VS (existing vertices only).
- **Which level**: the game near the camera cuts to about `depth / 127` (2 in at 250 in, 4 in at 500 in) and fades out
  tessellation by 960 in. A fixed **4-inch** cut with the mip that matches it reproduces the mid-distance look;
  8 inches is the light version; finer adds triangles but little relief (the mip at 2 in is still 64 texels per
  128-inch repeat). Big maps need a budget: `stockDisplacementEdgeForBudget(world, data, budget)` returns the smallest
  edge that fits about `budget` triangles (measured factor 9 A / E^2).

## 5. mp_village2 (VERIFIED, `real_runs.txt`, `real_runs2.txt`)

- **Displaced meshes 75** (75 materials), **81 surfaces**, **12,139 mesh vertices** (21,322 surface vertex records
  before StockWorld's merge), **7,715 triangles = 5.27 %** of 146,349 (the 4.5 % estimate counted `dn` names only;
  `tw/stone_wall_01`, `*_disp`, `*_dsp`, `twc/stone_wall_01_gray_rvl` also displace), 5,172 with lo > 0 at a corner.
  Every mesh matched StockWorld vertex for vertex (no warning); likewise mp_aniyah_tac (210 meshes, 320 surfaces,
  23,931 triangles), mp_backlot2 (10 / 11 / 2,620), mp_m_speedball (2 / 4 / 34, as research 98), mp_shipment (1 / 1 / 138).
- **Height images** (slot 95 + L), all 2048 x 2048 BC4_UNORM (DXGI 80), 12 mips:

| Image | Materials | maxDisplacement / constant |
|---|---|---|
| `stone_wall_01_d` | 48 (the `189dn`, `223dn`, `35dn`, `544dn`, `20dn` blends, `tw/stone_wall_01*`, `twc/stone_wall_01_gray_*`) | 2 (-1), 10 (-5), 20 (-10) |
| `stone_brick_medieval_03_d` | 27 (the `72dn`, `254dn`, `720dn` blends, `tw/stone_brick_medieval_03_dark_disp*`) | 10 (-5), 20 (-10) |
| `masonry_cobblestone_straight_01_h` | 1 (`tw/masonry_cobblestone_straight_ee_01_dsp`) | 7 (-3.5) |

  Decoded red: stone_wall 47..218 (mean 118 = 0.46), brick 25..219 (mean 0.48), cobblestone 0..255 (mean 0.62: the
  cobbles push out).
- **Amplitude**: amp 0.0005 .. 0.700 (heightAuto), bound `amp * |bias|` 5.5 in (10 in without heightAuto).
- **Height difference** (signed move along the displacement direction, displaced vertices only; real heights):

| Run | Triangles | Vertices moved | mean abs | RMS | max up / down | CPU |
|---|---|---|---|---|---|---|
| plain VS (no subdivision, mip for 8 in) | 7,715 | 4,514 | 0.150 in | 0.259 | +1.09 / -1.51 in | <1 ms |
| 8-inch cut | 320,332 | 170,970 | 0.104 | 0.194 | +1.54 / -1.62 | 105 ms |
| **4-inch cut** | **1,191,631** | 618,587 | 0.153 | 0.284 | **+2.60 / -2.56** | 420 ms |
| 2-inch cut | 1,719,722 | 890,097 | 0.135 | 0.258 | +2.60 / -2.58 | 630 ms |

  Reading the displacement records costs 2.5 s on mp_village2 (a second parse of the 212 MB trzone JSON) and 4.4 s on
  mp_aniyah_tac. mp_aniyah_tac at a fixed 8-inch cut would be 10.2 M triangles (three terrain blends over 240,000 to
  530,000 sq ft); with the budget helper (1.5 M) the edge is 29 in and the result 1.62 M triangles.
- The relief is a few inches: what reads as "tessellation" in game near the camera is this plus the pixel parallax of
  the residual (research 98 PD), which a viewport shader can add later.

## 6. Wiring (for the main session)

1. After `readStockWorld`, in the same worker: `readStockDisplacement(zoneDir, world, data)` (returns at once with an
   empty result when the gfx_map has no displaced surface; the trzone files are read only then).
2. Material cache, per displaced material (`data.meshes[i].material`, map-local names as today): keep
   - the slot **95 + L** image of every bit L of `displacedLayers` (it is already in `Entry::textures` as
     `"95+L=<image>"`; export the DDS like the colour images, BC4 -> red),
   - constant **45 + L** from the body's `constantTable` (literal `v[0..3]` = scale, bias, 0, w); `maxDisplacement`
     alone is not enough (asphalt potholes). The vsData rows hold the same values.
3. Decode the DDS (red) and `stockHeightMapFromR8(w, h, rgba, 4)`; build `StockDisplacementLayer{L, scale, bias, w, &map}`.
4. `opts.targetEdge = stockDisplacementEdgeForBudget(world, data, budget /* e.g. 1.5 M */)` (4 in on village2), then
   per mesh `displaceStockMesh(world, data.meshes[i], layers, opts, out)` and `replaceStockWorldMesh(world, mesh,
   std::move(out))` on the copy the viewport uses; `buildReferenceTexBatches` takes it as is. Keep the undisplaced
   mesh for picking / editing if needed. Without height images, skip (h = 0.5 gives the flat mean surface).
5. Not needed for the geometry, only for parity later: the PD parallax and the per-pixel residual.

## 7. API (`src/core/stock_displacement.h`)

```cpp
struct StockDisplacementVertex { float lo = 0, hi = 1; };
struct StockMeshDisplacement { size_t mesh; std::string material; int layerCount; uint32_t displacedLayers;
    std::vector<StockDisplacementVertex> vertices;   // [k * layerCount + L], k = StockWorld::meshes[mesh] vertex
    std::vector<Vec3> normal;                        // tangent-frame normal per vertex
    size_t surfaces, displacedSurfaces, triangles, displacedTriangles; };
struct StockDisplacementData { std::vector<StockMeshDisplacement> meshes; size_t displacedSurfaces, vertices,
    triangles, displacedTriangles; std::vector<std::string> warnings; };
bool readStockDisplacement(const std::string& zoneDir, const StockWorld&, StockDisplacementData&, std::string* err);
bool parseStockDisplacement(const std::string& gfxMapJson, const std::vector<std::string>& trzoneJson,
                            const StockWorld&, StockDisplacementData&, std::string* err);
float stockHalfToFloat(uint16_t);  Vec3 stockTangentFrameNormal(uint32_t);
struct StockHeightMap { std::vector<Level> levels; ... };   // red 0..1, box mips
StockHeightMap stockHeightMapFromR8(int w, int h, const uint8_t*, size_t pixelStride = 1, size_t rowStride = 0);
StockHeightMap stockHeightMapFromR16(int w, int h, const uint16_t*);
float sampleStockHeightMap(const StockHeightMap&, double u, double v /* game v */, double lod);
struct StockDisplacementLayer { int layer; float scale, bias, w; const StockHeightMap* height; };
struct StockDisplacementOptions { double targetEdge = 4, targetUv = 1.0 / 32, minRelief = 0.1; int maxPasses = 8;
    size_t maxTriangles = 2'000'000; float sceneParam = -1 / 256.f; bool phong = true, autoLod = true;
    double lodEdge = 0, lodBias = 0.5, lod = 0; };
struct StockDisplacedMesh { ImportedMesh mesh; lightmapUv; lightmapIndex; layerCount; layerUv; vertexColor;
    vertexColorHigh; layerNormalTransform; std::vector<float> offset; sourceTriangles, subdividedTriangles, passes, truncated; };
bool displaceStockMesh(const StockWorld&, const StockMeshDisplacement&, const std::vector<StockDisplacementLayer>&,
                       const StockDisplacementOptions&, StockDisplacedMesh&, std::string* err);
double stockDisplacementEdgeForBudget(const StockWorld&, const StockDisplacementData&, size_t budget,
                                      double minEdge = 4, double targetUv = 1.0 / 32);
void replaceStockWorldMesh(StockWorld&, size_t mesh, StockDisplacedMesh&&);
```

Tests (`tests/stock_displacement_tests.cpp`, 9 tests): half and tangent-frame decode, height map sampling (wrap,
bilinear, trilinear, R8 / RGBA / R16), flat-quad offsets (`h = 1`: +5 in for lo 1, hi 1/64; heightAuto off: +20;
per-vertex lo; tilted normals), subdivision counts (8 / 16 triangles, watertight and area-preserving at 5 in,
maxPasses / maxTriangles, stretched texture, minRelief), partial displacement (conforming, no T-junction), UV /
lightmap / colour interpolation and the layer-UV height lookup, Phong (`z = 0.75 * 2.5` at an edge midpoint), the
JSON reader against `parseStockWorld` (vertex matching, defaults, mismatch warning, replace), and the opt-in real-data
diagnostic. `xmake build -y iw8r_tests`: 1,221 tests, 141,778 checks, 0 failures (2026-11-13).

## What is not determined

- Whether a Replay user config overrides the tessellation dvars (defaults VERIFIED, not the live values).
- Replay's own technique selection (cutoff test traced in game-test only); the exact HS factor and subdomain code.
- Materials with two displaced layers (mp_village2 `twc4/88n_80n_80n_72dn_72dn`, 1 triangle, lo 0): the CPU path sums
  both offsets (INFERRED); its shader was not read.
- The meaning of `GfxSurface.tris.displacementParmsOffset` (a float size, unused here).
