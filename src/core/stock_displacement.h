// Stock world displacement ("dn" layers, techset token "...0018", material maxDisplacement != 0) for the editor: the
// per-vertex displacement records of a world cache export, and a CPU version of what Replay 1.20's displaced world
// shaders do to the geometry (subdivide like the WORLDTESS hull / domain shaders, displace along the vertex normal).
// Source, formulas and VERIFIED / INFERRED labels: docs/research/101_stock_displacement_data.md (and research 98 section
// 5 for the shader read).
//
// Data (VERIFIED, shaders f06c0749 / 9a82745f / 2a7a2c67 and the world data):
//   GfxWorldSurfData.displacementOffset[L] (byte 0x38 + 4 L): aux stream byte offset of the surface block's vertex 0;
//   vertex j (block-relative, the same j as xyz / lmap / colour) has 4 bytes at displacementOffset[L] + 4 j: two IEEE
//   binary16, lo (low half: amplitude, artist painted 0..1) and hi (high half: the layer's UV per inch). 0 = none: the
//   shader then uses lo 0, hi 1 (no displacement).
//   GfxWorldSurfData.tangentFrameOffset (byte 0x0C): 4 bytes per vertex, the packed tangent frame (smallest-three
//   quaternion 10:10:9 + bitangent sign + 2-bit index); its normal is the displacement direction.
// Shader (per displaced layer L; amp, scale, bias as in stock_decal.h):
//   amp = stockDisplacementAmplitude(lo, hi, param), param = -1/256 in Replay (r_tessellationHeightAuto 1,
//         r_tessellationHeightScale 1: VERIFIED defaults), so amp = lo / (256 hi)
//   d   = amp * (h * scale + bias), (scale, bias) = material constant 45 + L (maxDisplacement, -maxDisplacement / 2)
//   h   = red of the texture at slot 95 + L, sampled at layer L's own UV (raw, no psData UV transform), wrap, mip
//         log2(width * hi * edge) + r_tessellationLodBias (0.5), edge = the tessellated edge length the GPU aims for
//   P'  = P + normalize(N) * d                                       plain lit VS (no tessellation, far surfaces)
//   P'  = Phong(P) + normalize(sum b_i amp_i scale N_i) * k * d       WORLDTESS domain shader (near surfaces)
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "geometry.h"
#include "model_import.h"
#include "stock_world.h"

namespace iw8r {

// One displacement record (the shader's default when the surface has none for the layer: lo 0, hi 1).
struct StockDisplacementVertex {
    float lo = 0.0f;  // amplitude (0 = no displacement); 0..1 on the maps seen
    float hi = 1.0f;  // the layer's UV per inch (1/128, 1/64 ... on mp_village2); also picks the height mip
};

// The displacement data of one StockWorld mesh, vertex for vertex (meshes[mesh].pos[k] <-> normal[k],
// vertices[k * layerCount + L]).
struct StockMeshDisplacement {
    size_t mesh = 0;           // index into StockWorld::meshes (and its parallel arrays)
    std::string material;      // StockWorld::meshes[mesh].material
    int layerCount = 1;        // StockWorld::layerCount[mesh]
    uint32_t displacedLayers = 0;  // bit L: some surface of the mesh has displacementOffset[L] != 0
    std::vector<StockDisplacementVertex> vertices;  // [k * layerCount + L]
    // The vertex normal of the tangent frame stream (unit), the direction the game displaces along. Empty when no
    // surface of the mesh has a tangent frame; a vertex without one gets StockWorld's area-weighted normal.
    std::vector<Vec3> normal;
    size_t surfaces = 0;           // surfaces of the mesh read
    size_t displacedSurfaces = 0;  // of those, with a displacement record
    size_t triangles = 0;
    size_t displacedTriangles = 0;  // triangles with lo > 0 at some corner (the ones the CPU path subdivides)
};

struct StockDisplacementData {
    std::vector<StockMeshDisplacement> meshes;  // only meshes with at least one displaced surface, in mesh order
    size_t displacedSurfaces = 0, vertices = 0, triangles = 0, displacedTriangles = 0;
    std::vector<std::string> warnings;  // meshes that do not match the StockWorld (skipped), records out of range
};

// Reads the displacement records of every displaced mesh of a world cache / ACTS export (the same folder layout as
// readStockWorld: <zoneDir>/assets/gfx_map, gfx_map_trzone and <zoneDir>/streamed). `world` must be the StockWorld
// read from the same folder: the vertices are numbered the way StockWorld numbers them, and each mesh is checked
// against it (vertex count and positions; a mismatch skips the mesh with a warning). Returns true with an empty result
// when the map has no displaced surface (the transient zone files are then not read). Fails on unreadable JSON.
bool readStockDisplacement(const std::string& zoneDir, const StockWorld& world, StockDisplacementData& out,
                           std::string* err = nullptr);
// The same from JSON texts (gfx_map, every gfx_map_trzone).
bool parseStockDisplacement(const std::string& gfxMapJson, const std::vector<std::string>& trzoneJson, const StockWorld& world,
                            StockDisplacementData& out, std::string* err = nullptr);

// IEEE binary16 to float (zero, subnormal, inf, NaN by the IEEE rules).
float stockHalfToFloat(uint16_t h);
// The normal of a packed world tangent frame word (VERIFIED decode of the displaced VS): unit vector.
Vec3 stockTangentFrameNormal(uint32_t packed);

// A height image (the red channel of the slot 95 + L texture), values 0..1, with a box-filtered mip chain.
struct StockHeightMap {
    struct Level {
        int width = 0, height = 0;
        std::vector<float> texels;  // row 0 = the top row of the image (v = 0 in the game's convention)
    };
    std::vector<Level> levels;  // levels[0] = full size
    bool empty() const { return levels.empty() || levels[0].texels.empty(); }
    int width() const { return levels.empty() ? 0 : levels[0].width; }
};
// From 8-bit texels (value / 255): texel (x, y) at data[y * rowStride + x * pixelStride] (rowStride 0 = width *
// pixelStride). pixelStride 4 reads the red byte of RGBA8. Builds every mip down to 1x1.
StockHeightMap stockHeightMapFromR8(int width, int height, const uint8_t* data, size_t pixelStride = 1, size_t rowStride = 0);
// From 16-bit texels (value / 65535), tightly packed.
StockHeightMap stockHeightMapFromR16(int width, int height, const uint16_t* data);
// Bilinear, wrap addressing, trilinear between mips (lod clamped to the chain). (u, v) in the game's texture
// convention: v = 0 is the top row. A StockWorld layerUv entry (u, 1 - v) is sampled at (u, 1 - layerUv[1]).
float sampleStockHeightMap(const StockHeightMap& map, double u, double v, double lod);

// One displaced layer of a material (texture slot 95 + L, material constant 45 + L).
struct StockDisplacementLayer {
    int layer = 0;
    float scale = 0.0f;  // constant 45 + L .x (maxDisplacement: 2, 7, 10, 20 on mp_village2, 40 on mp_m_speedball)
    float bias = 0.0f;   // .y (-scale / 2)
    float w = 0.0f;      // .w: splits lo between Phong (1 - w) and the crease length correction (w); 0 on every map seen
    const StockHeightMap* height = nullptr;  // null: h = 0.5 (no relief, only the bias-free mean surface)
};

struct StockDisplacementOptions {
    // Subdivision: an edge is halved, pass after pass, while it is longer than max(targetEdge inches, targetUv / hi)
    // (hi = the layer's UV per inch, so a texture stretched over a large area is cut coarser, like its height mip) and
    // the largest possible move at one of its ends (amp * max(|scale + bias|, |bias|)) is at least minRelief inches.
    // The decision depends on the edge only: conforming red-green bisection, both triangles of an edge split it the
    // same way, so the displaced mesh has no cracks. At most maxPasses passes. targetEdge <= 0: no subdivision, only
    // the existing vertices move (the plain lit VS: far surfaces / r_tessellation 0).
    double targetEdge = 4.0;
    double targetUv = 1.0 / 32.0;  // 0: inches only
    double minRelief = 0.1;
    int maxPasses = 8;
    size_t maxTriangles = 2'000'000;  // per mesh; subdivision stops (truncated = true) before going over
    // Scene parameter cb7[114].z: stockDisplacementSceneParam(heightScale, heightAuto); Replay default -1/256.
    float sceneParam = -0.00390625f;
    bool phong = true;      // the domain shader's Phong term (alpha = 0.75 saturate((1 - w) lo)); only new vertices move
    // Height mip: autoLod picks log2(width * hi * edge) + lodBias per vertex like the shaders, edge = lodEdge, or when
    // that is 0 the edge length the mesh is cut to (max(targetEdge, targetUv / hi)), or 8 inches without subdivision;
    // otherwise `lod` is used everywhere.
    bool autoLod = true;
    double lodEdge = 0.0;
    double lodBias = 0.5;   // r_tessellationLodBias (VERIFIED Replay default)
    double lod = 0.0;
};

// A displaced (and subdivided) StockWorld mesh: the same per-mesh arrays StockWorld keeps, so it can replace the mesh
// (replaceStockWorldMesh) and every consumer of StockWorld (buildReferenceTexBatches) takes it as is.
struct StockDisplacedMesh {
    ImportedMesh mesh;  // pos (displaced), normal (recomputed, area weighted), uv (layer 0, ImportedMesh v), idx (CCW)
    std::vector<std::array<float, 2>> lightmapUv;  // (-1, -1) where a corner of the source triangle has none
    std::vector<int> lightmapIndex;
    int layerCount = 1;
    std::vector<std::array<float, 2>> layerUv;   // [k * layerCount + L], StockWorld convention (u, 1 - v)
    std::vector<uint32_t> vertexColor, vertexColorHigh;  // bytes interpolated
    std::vector<uint32_t> layerNormalTransform;          // [k * (layerCount - 1) + L - 1], bytes interpolated
    std::vector<float> offset;  // per vertex: how far it moved along the displacement direction, inches (signed)
    std::vector<uint32_t> triOrigin;  // per triangle of mesh.idx: the source triangle it was cut from
    size_t sourceTriangles = 0, subdividedTriangles = 0;  // source triangles, and those with lo > 0 at a corner (displaced)
    int passes = 0;
    bool truncated = false;  // maxTriangles stopped the subdivision early
};

// Displaces world.meshes[disp.mesh]. `layers` are the displaced layers of its material (every bit of
// disp.displacedLayers should have one; a layer without one is not displaced). Fails when the mesh and `disp` do not
// match. Triangles whose corners all have lo == 0 are copied unchanged (their displacement is 0).
bool displaceStockMesh(const StockWorld& world, const StockMeshDisplacement& disp, const std::vector<StockDisplacementLayer>& layers,
                       const StockDisplacementOptions& options, StockDisplacedMesh& out, std::string* err = nullptr);

// The smallest targetEdge (>= minEdge, inches) for which subdividing every mesh of `data` stays within about
// `triangleBudget` triangles: each source triangle with lo > 0 at a corner is counted as 9 A / max(E, targetUv / hi)^2
// (bisection leaves edges between E / 2 and E; the factor 9 was measured on mp_village2 and mp_aniyah_tac), hi the
// largest of its corners (first displaced layer). An estimate that ignores minRelief. Returns minEdge when that fits.
double stockDisplacementEdgeForBudget(const StockWorld& world, const StockDisplacementData& data, size_t triangleBudget,
                                      double minEdge = 4.0, double targetUv = 1.0 / 32.0);

// Puts `m` in place of world.meshes[mesh] and its parallel arrays (lightmapUv, lightmapIndex, layerCount, layerUv,
// vertexColor, vertexColorHigh, layerNormalTransform) and grows world.bounds.
void replaceStockWorldMesh(StockWorld& world, size_t mesh, StockDisplacedMesh&& m);

}  // namespace iw8r
