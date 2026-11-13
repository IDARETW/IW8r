// Stock GPU ground clutter (Replay 1.20 "clutter_gen_pass"): readers for the data that drives it and an exact CPU
// port of the compute shader's scatter, so the viewport can draw the grass, rocks and saplings the game scatters over
// super-terrain. Research and citations: docs/research/100_stock_ground_clutter.md.
//
// Where the data lives (VERIFIED, research 100 section 1):
//   gfx_map  fields.smodels.clutterCollectionCount / clutterCollections (GfxClutterCollection, 12 bytes:
//            u32 maxInstanceCount, u16 smodelCollectionIndex, u16 terrainSurfaceIndex, u16 clutterLayerIndex, u16 pad)
//            -> smodels.collections[smodelCollectionIndex].smodelIndex -> smodels.models[].model.name (the XModel).
//   stterrain (StTerrain asset of the map zone) surfaces[t].clutterLayers (StTerrainClutterLayer, 84 bytes),
//            surfaces[t].clutterSampleBitmask.bitmask (u32 words), surfaces[t].objToWld, heightMap;
//            clutterSamplePoints.samplePoints (vec2 per sample, shared by every surface).
// Which maps have it: Verdansk (mp_donetsk: 678 collections, 43 models) and Quarry carry super-terrain clutter. Every
// 6v6 map checked (mp_m_speedball included) has clutterCollectionCount 0 and an empty stterrain: their grass is placed
// static models (stock_smodels.h), not GPU clutter.
//
// The scatter (VERIFIED, cs_gpu_generation.hlsl / clutter_gen_pass.219, lines cited in research 100 section 2):
//   for tile (tx, ty) of the layer and sample s in 0..2047 whose bit is set in
//       bitmask[bitmaskOffset + (ty * tileCount.x + tx) * 64 + s / 32] bit (s % 32):
//     layerUv = (samplePoints[sampleOffset + s] + (tx, ty)) * tileSizeUV
//     local   = ((layerUv.x - 0.5) * layerSize.x, (0.5 - layerUv.y) * layerSize.y, height(layerUv) + zOffset)
//     world   = rotate(objToWld.quat, local) + objToWld.origin
//     seed    = FNV-1a(tx, ty, s) ^ asuint(tileSizeUV.x); xorshift32 (13, 17, 5) five times, each u32 * 2^-32:
//               scale, angle x, angle y, angle z, zOffset (lerp of the layer's min / max)
//     quat    = euler quaternion of the half angles; if useSurfaceNormal: shortest arc from its up axis to the
//               height-map normal, applied after it.
// Read-only and pure: no file is written, no global state, the same input gives the same output.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "geometry.h"

namespace iw8r {

struct StockWorld;

// ---------------------------------------------------------------------------------------------------------------
// GfxWorld side: which model each clutter layer draws.

struct StockClutterCollection {
    uint32_t index = 0;                  // row in smodels.clutterCollections (the shader's collection index)
    uint32_t maxInstanceCount = 0;       // instance budget of the layer (all of its tiles)
    uint32_t smodelCollectionIndex = 0;  // row in smodels.collections (flag 32 STATIC_MODEL_COLLECTION_FLAG_CLUTTER)
    uint32_t terrainSurfaceIndex = 0;    // StTerrain.surfaces[] row
    uint32_t clutterLayerIndex = 0;      // that surface's clutterLayers[] row
    uint32_t smodelIndex = 0;            // smodels.models[] row
    std::string model;                   // XModel asset name, leading '#' or ',' removed (e.g. tm/ee_foliage_grass_lm_short_01)
};

// Reads smodels.clutterCollections of a gfx_map export. exportPathOrDir is the gfx_map JSON itself or a zone export
// folder holding assets/gfx_map (a world cache folder). Only the smodels sub-objects are parsed, so the 422 MB
// Verdansk export is read in a few seconds. A map without clutter (clutterCollections null) gives an empty list and
// true. Fails on a missing file, missing smodels, or an index past the models / collections arrays.
bool readStockClutterCollections(const std::string& exportPathOrDir, std::vector<StockClutterCollection>& out,
                                 std::string* err = nullptr);
// The same from the gfx_map JSON text (tests; the file reader calls this).
bool parseStockClutterCollections(const std::string& gfxMapJson, std::vector<StockClutterCollection>& out,
                                  std::string* err = nullptr);

// ---------------------------------------------------------------------------------------------------------------
// StTerrain side: the layer parameters, sample points and bitmasks.

constexpr size_t kClutterLayerBytes = 84;       // sizeof(StTerrainClutterLayer), the shader's structured stride
constexpr uint32_t kClutterSamplesPerTile = 2048;  // 32 thread groups (z) x 64 threads, one bit each
constexpr uint32_t kClutterWordsPerTile = 64;      // 2048 bits

struct StockClutterLayer {  // StTerrainClutterLayer (x64-zt structs.hpp StTerrainClutterLayer), byte offsets in []
    uint32_t sampleOffset = 0;            // [0]  first sample point of this layer
    uint32_t bitmaskOffset = 0;           // [4]  first bitmask word (u32 index) of this layer
    uint32_t tileCount[2]{0, 0};          // [8]  tiles along u, v
    float tileSizeUV[2]{0, 0};            // [16] one tile in layer UV (usually 1 / tileCount)
    float layerSize[2]{0, 0};             // [24] the layer's extent in local inches (x, y)
    float angleMin[3]{0, 0, 0};           // [32] euler angles in radians (INFERRED unit), x y z
    float angleMax[3]{0, 0, 0};           // [44]
    float zOffsetMinMax[2]{0, 0};         // [56] added to the ground height
    float scaleMinMax[2]{1, 1};           // [64] uniform scale
    uint32_t maxInstanceCount = 0;        // [72]
    uint32_t tileInstanceCountOffset = 0; // [76] into StDiskTerrainSurface.clutterTileInstanceCounts (CPU side)
    uint32_t useSurfaceNormal = 0;        // [80] non-zero: tilt each instance onto the height-map normal
};

// Little-endian 84-byte record <-> struct. decode returns false when rec is null.
bool decodeStockClutterLayer(const uint8_t* rec, StockClutterLayer& out);
void encodeStockClutterLayer(const StockClutterLayer& layer, uint8_t* rec);

// GfxPlacement objToWld of a terrain surface: the shader's CB1[2] row 0 (quaternion x y z w) and row 1 .yzw (origin).
struct ClutterFrame {
    std::array<double, 4> quat{0, 0, 0, 1};
    Vec3 origin;
};

struct StockClutterSurface {
    ClutterFrame objToWld;
    std::vector<StockClutterLayer> layers;  // clutterLayers
    std::vector<uint32_t> bitmask;          // clutterSampleBitmask.bitmask (wordCount words)
    double heightOffset = 0, heightScale = 1, halfWldSize = 0, surfaceMapWorldSize = 0;  // as exported (not used here)
};

struct StockClutterTerrain {
    std::string name;
    std::vector<StockClutterSurface> surfaces;
    std::vector<std::array<float, 2>> samplePoints;  // StTerrain.clutterSamplePoints.samplePoints
};

// Parses an ACTS export of the stterrain asset (assets/stterrain/<map>.N.asset.json). Arrays may be exported as
// "values" (objects / numbers) or as hex "bytes"; both are read. An empty terrain (surfaceCount 0, every 6v6 map)
// parses to zero surfaces and true. NOTE: ACTS dev-3.3.0 cannot export the stterrain of mp_donetsk / mp_br_quarry
// ("pointer_extent_required" on the node meshes), so this reader is tested on synthetic JSON only (research 100 4).
bool parseStockClutterTerrain(const std::string& stterrainJson, StockClutterTerrain& out, std::string* err = nullptr);

// ---------------------------------------------------------------------------------------------------------------
// The compute shader's arithmetic, exposed for tests and for the viewport.

uint32_t clutterSeed(uint32_t tileX, uint32_t tileY, uint32_t sample, float tileSizeU);  // FNV-1a + xor
uint32_t clutterXorshift(uint32_t h);                                                    // << 13, >> 17, << 5
float clutterUnit(uint32_t h);                                                           // float(h) * 2^-32
// Quaternion (x, y, z, w) of the shader's euler angles (radians). With a = ax / 2, b = ay / 2, c = az / 2:
//   x = sa sb cc - ca cb sc,  y = ca sb sc + sa cb cc,  z = sa cb sc - ca sb cc,  w = sa sb sc + ca cb cc,
// normalised. Angle x alone turns about +Y, y alone about -Z, z alone about -X (VERIFIED, research 100 2.3).
std::array<double, 4> clutterEulerQuat(double ax, double ay, double az);
// Shortest-arc quaternion that turns unit `from` onto unit `to`: (cross(from, to), 1 + dot) normalised; identity above
// dot 0.999; below -0.999 a half turn about normalize(cross(X, from)) (or cross(Y, from) when that is ~0).
std::array<double, 4> clutterAlignQuat(const Vec3& from, const Vec3& to);
// Hamilton product a * b: the rotation b first, then a (the shader's final = align * random).
std::array<double, 4> quatMultiply(const std::array<double, 4>& a, const std::array<double, 4>& b);
Vec3 quatRotate(const std::array<double, 4>& q, const Vec3& v);
std::array<Vec3, 3> quatAxes(const std::array<double, 4>& q);  // rotation matrix columns (model X, Y, Z in world)

// Ground under a sample, in the surface's local frame: height (local z) and unit normal.
struct ClutterGroundSample {
    double height = 0;
    Vec3 normal{0, 0, 1};
};
// layerU / layerV: the sample's layer UV (0..1); localX / localY: its local position in inches.
// Return false to drop the sample (no ground there).
using ClutterGroundFn = std::function<bool(double layerU, double layerV, double localX, double localY, ClutterGroundSample&)>;

// The shader's height-map read: texel = bilinear(heightMap, (u * uvScale + uvBias, (1 - v) * uvScale + uvBias)),
// height = texel * heightScale + heightBias (CB1[2][2]); normal = normalize(h(-1,0) - h(+1,0), h(0,-1) - h(0,+1),
// 2 * texelWorldSize) with +-1 texel offsets (CB1[2][3].x = texelWorldSize). Clamp addressing (INFERRED sampler).
struct ClutterHeightmap {
    int width = 0, height = 0;
    std::vector<float> texels;  // normalised 0..1, row-major, row 0 = texture v 0
    double heightBias = 0, heightScale = 1;
    double uvScale = 1, uvBias = 0;
    double texelWorldSize = 1;
    bool sample(double layerU, double layerV, ClutterGroundSample& out) const;
};
ClutterGroundFn heightmapGround(const ClutterHeightmap& map);  // the map is copied into the function

// A top-down ground from triangle meshes, for maps whose height image is not available: the highest upward-facing
// triangle under a point. Used to drop clutter onto the editor's own world mesh.
class StockGroundGrid {
public:
    // Triangles of every mesh whose material passes `keep` (all when empty) and whose face normal has z >= minUpZ.
    void build(const StockWorld& world, double cellSize = 256.0,
               const std::function<bool(const std::string& material)>& keep = {}, double minUpZ = 0.3);
    // Adds one triangle (any winding; its normal is turned to face up). Call finish() after the last one.
    void addTriangle(const Vec3& a, const Vec3& b, const Vec3& c);
    void finish();  // sorts the cell index (build() calls it)
    void setCellSize(double cellSize) { cell_ = cellSize > 1e-6 ? cellSize : 256.0; }  // before adding triangles
    // Highest triangle under (x, y) whose height there is <= zMax: z and its unit face normal. False when none.
    bool sample(double x, double y, double zMax, double& z, Vec3& normal) const;
    size_t triangleCount() const { return tris_.size(); }

private:
    struct Tri {
        Vec3 a, b, c, n;
    };
    double cell_ = 256.0;
    std::vector<Tri> tris_;
    std::vector<std::pair<int64_t, uint32_t>> cells_;  // (cell key, triangle), sorted by finish()
    bool sorted_ = true;
};
// A ClutterGroundFn over the grid: local (x, y) -> world through `frame` (translation and yaw only are honoured),
// the highest ground at or below frame.origin.z + zMaxAbove, its height back in local z.
ClutterGroundFn meshGround(const StockGroundGrid& grid, const ClutterFrame& frame, double zMaxAbove = 1e9);

// ---------------------------------------------------------------------------------------------------------------
// The scatter.

struct StockClutterInstance {
    uint32_t model = 0;       // index into StockClutterScatter::models
    uint32_t collection = 0;  // StockClutterCollection::index (or the layer index for scatterStockClutterLayer)
    uint32_t tileX = 0, tileY = 0, sample = 0;
    Vec3 origin;                                                            // world inches, Z up
    std::array<double, 4> quat{0, 0, 0, 1};                                 // as the shader stores it (before packing)
    std::array<Vec3, 3> axis{Vec3{1, 0, 0}, Vec3{0, 1, 0}, Vec3{0, 0, 1}};  // model X, Y, Z in world (quatAxes)
    double scale = 1;
};

struct StockClutterScatter {
    std::vector<std::string> models;  // distinct model names, in first-use order
    std::vector<StockClutterInstance> instances;
    size_t tilesVisited = 0;
    size_t samplesDropped = 0;  // set bits whose ground function returned false
    bool capped = false;        // maxInstances stopped the scatter early
};

struct ClutterScatterOptions {
    // Density cap for the viewport. Tiles are visited nearest first (tile centre to (centreX, centreY), world XY);
    // with radius > 0 only tiles whose centre is within radius + half the tile diagonal are visited, and only
    // instances within radius are kept. maxInstances stops the scatter (whole tiles are not split: the instance that
    // reaches the cap is the last one).
    double centreX = 0, centreY = 0, radius = 0;
    size_t maxInstances = 200000;
    // Optional pacing: keep every Nth instance (1 = all). Applied before the cap; deterministic (sample index % N).
    uint32_t keepEvery = 1;
};

// One layer: the exact placement of clutter_gen_pass for every set bit of the visited tiles.
void scatterStockClutterLayer(const StockClutterLayer& layer, const std::string& model, uint32_t collection,
                              const std::vector<std::array<float, 2>>& samplePoints, const std::vector<uint32_t>& bitmask,
                              const ClutterFrame& frame, const ClutterGroundFn& ground, const ClutterScatterOptions& opt,
                              StockClutterScatter& out);

// Every collection of a map. groundForSurface(t) gives the ground of terrain surface t (null function: the
// surface is skipped). Tiles of all layers are visited nearest first and share opt.maxInstances.
void scatterStockClutter(const std::vector<StockClutterCollection>& collections, const StockClutterTerrain& terrain,
                         const std::function<ClutterGroundFn(uint32_t surface)>& groundForSurface,
                         const ClutterScatterOptions& opt, StockClutterScatter& out);

}  // namespace iw8r
