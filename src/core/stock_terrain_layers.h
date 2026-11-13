// Stock super-terrain material layers (Replay 1.20 `stterrain`, Verdansk mp_donetsk / Quarry mp_br_quarry): the per-layer
// weights the game blends near the camera instead of the baked combined albedo, rebuilt on the CPU as layer batches.
// Research: docs/research/103_stock_terrain_layers.md (shader PS afc8b731 / 5a4c3a9f, game-test r_st.cpp), 102 (mesh), 97
// (the reveal ramp, same arithmetic as the tw4 world layers).
//
// What the game does (VERIFIED in the lit PS of twh5x/lit_superterrain unless marked):
//   - Per pixel, nearer than kStockTerrainCombinedDistance * r_st_combined_bias_high (default 1.0, so 17,368 inches),
//     the layers are blended; farther, the surface's combinedAlbedoMap / combinedNogMap are used.
//   - The layers evaluated for a triangle are the set bits of its layerMask.data u32 (bit L = surface layer L); nodes
//     without per-triangle masks use the node's layerMask. The loop runs from the lowest set bit up and lerps each
//     layer over the result so far (the colour starts at black, layer 0 is normally the first).
//   - Layer L's weight comes from its layer mask image (flattenedImages[layerMaskMapIdxs[L]], BC4, one texel per
//     height-map texel, sampled at LOD 0 at the height-map position). Layer 0's mask is the builtin $white (1).
//   - For L > 0 with flags & 0x10000 the reveal image (materialTextureIdxs[2]: R = alpha, G = reveal) is sampled at the
//     layer UV; with flags & 4 the weight goes through the reveal ramp (k = alphaRevealSoftEdge as a byte / 255),
//     else it is mask * alpha. flags & 0x20000 thresholds the result at 0.502.
//   - Layer UV = stFromXYZ rows applied to the world position (row . (x, y, z, 1)). flags & 0x400 divides the UV by
//     1 + 16 n with n = floor(sqrt(distance * |row0|) / 4) (anti-tiling far away; n = 0 within 2,048 inches at 1/128).
//   - Textures: materialTextureIdxs[0] colour (+ spec code in alpha), [1] normal + gloss (+ cavity), [2] alpha + reveal,
//     [3] displacement. colorTint is not read by the PS.
//
// The editor's CPU path: take a built StockTerrainMesh (stock_super_terrain.h), keep the triangles near a point, draw
// layer 0 as the base and, in ascending layer order, each layer that has weight there as a layer batch with per-vertex
// weight = its mask value at the vertex (the shader's mask input; the batch applies the ramp with revealK()).
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "model_import.h"
#include "stock_super_terrain.h"

namespace iw8r {

// The PS compares the squared camera distance with (17368.228516 * bias)^2 (literal in PS afc8b731; the front end uses
// 301655360 = 17368.23^2 for its per-node and per-surface tests). bias = r_st_combined_bias_high, registered with
// default 1.0 (range 0..8); r_st_combined_bias_low (default 0.5) is used under low-memory always-loaded flagging.
inline constexpr double kStockTerrainCombinedDistance = 17368.228516;
inline constexpr double kStockTerrainCombinedBiasHigh = 1.0;
inline constexpr double kStockTerrainCombinedBiasLow = 0.5;

// layerData.flags bits the lit PS reads (other bits: 1, 2, 0x40, 0x80, 0x200 are set on most layers and not read there).
enum StockTerrainLayerFlag : uint32_t {
    kStLayerRevealRamp = 0x4,         // weight = reveal ramp(mask, reveal G, k)
    kStLayerDistanceTiling = 0x400,   // far UV scale 1 / (1 + 16 n)
    kStLayerAlphaReveal = 0x10000,    // sample the reveal image (R alpha multiplies the weight)
    kStLayerHardEdge = 0x20000,       // weight = weight > 0.501961 ? 1 : 0
};

struct StockTerrainLayerInfo {
    int layer = 0;                    // StTerrainMaterialLayer.layer (equals the row on every surface seen)
    std::string material;             // e.g. "mc/dirt_soil_01" (no leading ',')
    double stFromXYZ[2][4] = {{0, 0, 0, 0}, {0, 0, 0, 0}};
    double textureScale[2] = {0, 0};
    double colorTint[3] = {1, 1, 1};  // informational: the lit PS does not read it
    uint32_t flags = 0;               // layerData.flags
    double alphaRevealSoftEdge = 1;   // layerData.alphaRevealSoftEdge (0..1)
    double himipRadiusInvSq = 0;      // streaming (informational)
    // The layer mask: flattenedImages[layerMaskMapIdxs[layer]].
    enum class Mask { Image, White, Black, None };
    Mask maskKind = Mask::None;
    std::string maskImage;            // GfxImage name when maskKind == Image (e.g. mp_donetsk_layer_mask_17_3)
    // flattenedImages[materialTextureIdxs[0..3]] as ACTS prints them (e.g. "dirt_soil_01_c&defaultspec~6923169343254932927");
    // "" for 0xFFFF. The editor's material cache stores these as <name with & and ~ replaced by _>.dds.
    std::string colorImage, normalImage, revealImage, displacementImage;

    bool alphaReveal() const { return (flags & kStLayerAlphaReveal) != 0; }
    bool revealRamp() const { return alphaReveal() && (flags & kStLayerRevealRamp) != 0; }
    bool hardEdge() const { return (flags & kStLayerHardEdge) != 0; }
    // The packed soft-edge byte (min(255, int(255 * soft))) and the ramp's k = byte / 255.
    int softEdgeByte() const;
    double revealK() const { return softEdgeByte() / 255.0; }
    // At mask weight 1 the layer hides what is under it (INFERRED for culling; the game's per-triangle masks drop layer
    // 0 where later layers are at 1): no alpha image, or an alpha channel that is the builtin white ("white&...").
    bool coversAtFullWeight() const;
};

struct StockTerrainSurfaceLayers {
    int surface = -1;
    std::vector<StockTerrainLayerInfo> layers;   // indexed by layer (row)
    double combinedMapStreamRadiusInvSq = 0;     // streaming of the combined maps (informational)
};

struct StockTerrainLayerTable {
    std::vector<std::string> flattenedImages;    // StTerrain.flattenedImages names ("" for null), ',' kept for builtins
    std::vector<StockTerrainSurfaceLayers> surfaces;
    std::vector<std::string> warnings;
};

// Parses the layer tables of the stterrain export (flattenedImages, every surface's layerMaterials, layerMaskMapIdxs).
// The quadtree is skipped without parsing. Accepts `values` arrays or hex `bytes` (u16) for the index arrays.
bool parseStockTerrainLayers(const std::string& stterrainJson, StockTerrainLayerTable& out, std::string* err = nullptr);
bool readStockTerrainLayers(const std::string& jsonPath, StockTerrainLayerTable& out, std::string* err = nullptr);

// The layer mask GfxImages of a surface (all surfaces when surface < 0), distinct, in layer order: export with
// `acts ... -a image -n <names> --xpak <every zone/*.xpak over 64 KB>` (they are streamed from pak_magma1.xpak).
std::vector<std::string> stockTerrainLayerMaskImageNames(const StockTerrainLayerTable& table, int surface = -1);

// A decoded layer mask: 8-bit weights, row-major (row 0 = the surface's -y edge, like the height map), padded size.
struct StockTerrainLayerMask {
    int width = 0, height = 0;
    std::vector<uint8_t> data;
    bool empty() const { return data.empty(); }
    uint8_t at(int i, int j) const;                     // clamped to the image
    double sample(double tx, double ty) const;          // bilinear in texel units (texel centres at integers), / 255
};

// DXGI 80 (BC4_UNORM, what the game ships) or 61 (R8_UNORM); first mip only.
bool decodeStockTerrainLayerMaskDds(const std::string& ddsBytes, StockTerrainLayerMask& out, std::string* err = nullptr);
bool readStockTerrainLayerMaskDds(const std::string& path, StockTerrainLayerMask& out, std::string* err = nullptr);
// One BC4 block (8 bytes) to 16 values, row-major.
void decodeStockTerrainBc4Block(const uint8_t block[8], uint8_t out[16]);

// The PS's reveal ramp (research 97): lo = sat(1 - w - k sqrt(w)), hi = sat(1 - w + k sqrt(1 - w)),
// sat((reveal - lo) / (hi - lo)); hi == lo gives a step at lo.
double stockTerrainRevealRamp(double w, double reveal, double k);
// The PS's blend factor b of a layer from its mask value and the reveal image's R (alpha) and G (reveal) at the pixel.
// Layer 0 uses the mask alone (the PS skips the alpha / reveal block for bit 0).
double stockTerrainLayerBlendWeight(const StockTerrainLayerInfo& layer, double mask, double alpha = 1, double reveal = 1);

// The texel coordinate of world (x, y) in surface s's height / mask grid (texel i at origin - half + i * texelSize).
inline double stockTerrainTexelX(const StockTerrainSurface& s, double x) { return (x - s.origin.x + s.halfWldSize) / s.texelSize; }
inline double stockTerrainTexelY(const StockTerrainSurface& s, double y) { return (y - s.origin.y + s.halfWldSize) / s.texelSize; }

// Layer L's UV at world p in the ImportedMesh convention: (s, 1 - t).
std::array<double, 2> stockTerrainLayerUv(const StockTerrainLayerInfo& layer, const Vec3& p);

// The mask weight of every layer at world (x, y): masks[L] (Image), 1 (White), 0 (Black / None / missing image).
void stockTerrainLayerWeightsAt(const StockTerrainSurface& surface, const StockTerrainSurfaceLayers& layers,
                                const std::vector<StockTerrainLayerMask>& masks, double x, double y,
                                std::vector<double>& weights);
// How much of the final colour each layer gives with every layer's blend factor taken as its mask weight (reveal and
// alpha ignored, INFERRED approximation): c_L = w_L * prod_{K > L} (1 - w_K). Returns the layer with the largest c.
int stockTerrainLayerContributions(const std::vector<double>& weights, std::vector<double>& contributions);

// Surfaces whose square comes within radius of (x, y), smallest first (the detailed ones before the periphery).
std::vector<int> stockTerrainSurfacesNear(const StockTerrain& terrain, double x, double y, double radius);

struct StockTerrainLayerPatchOptions {
    double centerX = 0, centerY = 0;
    double radius = 6000;             // keep the triangles whose centroid is within this xy distance (<= 0: all)
    double minWeight = 0.5 / 255;     // a layer gets a triangle when one of its vertices has at least this mask weight
    // Culling (on by default): skip a layer's triangle when a later covering layer is at weight 1 on all 3 vertices, or
    // when its largest vertex contribution w_L * prod_{later covering K} (1 - w_K) is below minContribution. Mask weights
    // stand in for the per-pixel blend factors here (the reveal ramp shows about a w share of the area, INFERRED).
    bool cullHidden = true;
    double minContribution = 4.0 / 255;
    bool skipLayerZero = true;        // layer 0 is the base (its weight is 1 everywhere)
};

struct StockTerrainLayerSet {
    int layer = 0;                    // row in the surface's layer table; draw the sets in this (ascending) order
    std::string material;
    uint32_t flags = 0;
    double revealK = 0;               // for the batch's reveal ramp (when revealRamp)
    bool revealRamp = false, alphaReveal = false, hardEdge = false;
    std::string colorImage, normalImage, revealImage;
    // pos / normal as the base; uv = this layer's UV; idx counter-clockwise from +Z (same triangles as the base).
    ImportedMesh mesh;
    std::vector<float> weight;        // per vertex, the mask value in [0, 1]
    double meanWeight = 0;            // over the set's vertices
};

struct StockTerrainLayerPatch {
    int surface = -1;
    int baseLayer = 0;
    std::string baseMaterial;
    ImportedMesh base;                // the kept triangles; uv = layer 0's UV
    std::vector<std::array<double, 2>> albedoUv;   // combined albedo UV of the base vertices
    std::vector<uint32_t> sourceVertex;            // base vertex -> StockTerrainMesh vertex
    std::vector<StockTerrainLayerSet> layers;      // ascending layer order, layers with no triangle left out
    size_t layerTriangles = 0;        // over all sets
    std::vector<size_t> dominantCount;             // per layer: base vertices where it has the largest contribution
    std::vector<std::string> warnings;
};

// Builds the layer sets of one surface around options.center. mesh must come from buildStockTerrainMesh for the same
// surface (any stride; its vertices sit on height texels). masks[L] is layer L's decoded mask (may be empty for White /
// Black layers; a missing Image mask skips that layer with a warning).
bool buildStockTerrainLayerPatch(const StockTerrain& terrain, const StockTerrainLayerTable& table, const StockTerrainMesh& mesh,
                                 const std::vector<StockTerrainLayerMask>& masks, const StockTerrainLayerPatchOptions& opt,
                                 StockTerrainLayerPatch& out, std::string* err = nullptr);

}  // namespace iw8r
