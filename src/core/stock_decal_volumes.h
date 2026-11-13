// Stock Replay 1.20 decal volumes (projected box decals: paint stripes, stains, bullet holes, stencils) read from the
// ACTS gfx_map / gfx_map_trzone exports of a map, and a CPU projector that clips the receiving world triangles to a
// volume's box so the viewport can draw them as a blended decal batch.
// Research: docs/research/99_stock_decal_volumes.md (layout and projection VERIFIED from
// code_post_gfx cs_decal_volume_generate_draw_data and the world pixel shaders' t56/t57 loop), research 80 (containers).
//
// Data (ACTS JSON, field names as exported):
//   gfx_map         decalVolumeMaterials.values[i].material.name   ",i/decal_paint_01_red" (Material carriers)
//                   decalVolumeMasks.values[i].blendMapOverride.name "decal_mask_stain_06_c" (GfxDecalVolumeMask)
//                   decalVolumeCollections.values[i] {transientZoneIndex, decalCount, firstZoneDecal}
//   gfx_map_trzone  transientZoneIndex; decalVolumes.values[0] (GfxDecalVolumeTransientData):
//                   decalVolumes.values[] (GfxStaticDecalVolumePacked, 44 bytes: obbCenter.v, obbQuat, obbHalfSize,
//                   opacityParams, uvMatrixAndOffset[3], packedMaterialInfo, reserved), collectionBounds, decalVolumeUids
//
// Box convention (VERIFIED, cs_decal_volume_generate_draw_data lines 40-69 and 239-283):
//   axis[0], axis[1], axis[2] = the columns of the rotation of obbQuat. axis[0] is the projection direction (it points
//   INTO the receiving surface; -axis[0] is the decal's normal). Half sizes: bits 0-10 along axis[0], 11-21 along
//   axis[1], 22-31 along axis[2].
//   u = 0.5 + 0.5 * dot(axis[1], p - c) / h1,  v = 0.5 + 0.5 * dot(axis[2], p - c) / h2,  depth = dot(-axis[0], p - c) / h0
//   Inside: 0 <= u < 1, 0 <= v < 1, |depth| < 1. (u, v) are D3D texture coordinates: v = 0 is the first (top) image row.
//   Material UV (colour, normal, opacity map; NOT the mask): mu = m00 u + m01 v + offU, mv = m10 u + m11 v + offV, then
//   wrapped (frc in the shader: sample with a WRAP sampler). The mask is sampled at the raw (u, v).
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "geometry.h"
#include "model_import.h"

namespace iw8r {

struct StockWorld;

constexpr uint32_t kStockDecalNoIndex = 0x7FF;  // packedMaterialInfo material / mask index "none"

// One placed decal volume (decoded GfxStaticDecalVolumePacked).
struct StockDecalVolume {
    size_t index = 0;            // position in StockDecalVolumeSet::volumes
    int64_t transientZone = 0;   // the gfx_map_trzone that holds the record
    uint32_t zoneRecord = 0;     // record index inside that zone (GfxDecalVolumeCollection.firstZoneDecal units)
    int collection = -1;         // index in StockDecalVolumeSet::collections, -1 when no collection covers it
    uint32_t uid = 0;            // decalVolumeUids[zoneRecord] (0 when absent)

    int materialIndex = -1;      // index in StockDecalVolumeSet::materials (-1: 0x7FF or out of range)
    std::string material;        // "i/decal_paint_01_red" (no leading comma); "" when none
    int maskIndex = -1;          // index in StockDecalVolumeSet::masks (-1: none)
    std::string mask;            // GfxDecalVolumeMask asset name ("decal_mask_stain_06_c"); its image is the mask
                                 // asset's blendMap ("<mask>~atlasbc4" on every Speedball mask), sampled .r

    Vec3 center;                         // obbCenter, world inches
    std::array<double, 4> quat{0, 0, 0, 1};  // x, y, z, w as decoded
    std::array<Vec3, 3> axis;            // unit box axes (columns of the rotation); axis[0] = projection direction
    Vec3 halfSize;                       // half sizes along axis[0], axis[1], axis[2]

    // opacityParams bytes / 255 as stored: blend-map edges 0 and 1, blend-map scale, depth feather.
    double blendEdge0 = 0, blendEdge1 = 1, blendScale = 1, feather = 0;
    // The effective blend-map remap the compute shader writes (VERIFIED, draw_data lines 77-95):
    // edge0 >= edge1 or scale <= 0 -> (edge0, edge1, scale) = (0, 1, 1). A = scale / (edge1 - edge0),
    // B = -edge0 * scale / (edge1 - edge0). Pixel: op = m * saturate(m * A + B), m = mask * edge fade.
    double opacityScale = 1, opacityBias = 0;
    std::array<double, 4> uvMatrix{1, 0, 0, 1};  // m00, m01, m10, m11 (binary16 as stored)
    std::array<double, 2> uvOffset{0, 0};
    int normalBlendMode = 2;   // packedMaterialInfo bits 22-23: 0 blend, 1 add, 2 the material's flags bit 0
    int mapDrawOrder = 0;      // bits 24-28 (not read by the 1.20 shaders)
    int drawOrder = 0;         // bits 29-31 minus 4 (-4 .. 3): the top of the cull sort key

    // The stored words.
    uint32_t rawQuat = 0, rawHalfSize = 0, rawOpacity = 0, rawInfo = 0, rawReserved = 0;
    std::array<uint32_t, 3> rawUv{0, 0, 0};
};

struct StockDecalVolumeCollection {
    int64_t transientZone = 0;
    uint32_t decalCount = 0;
    uint32_t firstZoneDecal = 0;
    AABB bounds;  // collectionBounds of its transient zone (midPoint +- halfSize); invalid when not exported
};

struct StockDecalVolumeSet {
    std::vector<std::string> materials;  // decalVolumeMaterials, names without the leading comma ("" for null)
    std::vector<std::string> masks;      // decalVolumeMasks blendMapOverride names ("" for null)
    std::vector<StockDecalVolumeCollection> collections;
    std::vector<StockDecalVolume> volumes;  // transient zones ascending, record order inside a zone
    std::vector<std::string> warnings;
};

// Parses the decal volume tables from the gfx_map JSON text and the gfx_map_trzone JSON texts (any number; zones
// without decal volumes are skipped). Only the decal keys are parsed (a key scan, not a DOM of the whole file).
// A map without decal volumes gives an empty set and true. Fails only on malformed decal JSON.
bool parseStockDecalVolumes(const std::string& gfxMapJson, const std::vector<std::string>& trzoneJson,
                            StockDecalVolumeSet& out, std::string* err = nullptr);

// Reads an ACTS export folder laid out as for readStockWorld: <zoneDir>/assets/gfx_map/**/*.json (exactly one),
// <zoneDir>/assets/gfx_map_trzone/**/*.json and streamed tiles under <zoneDir>/streamed. The files are scanned in
// chunks for the decal keys, so they are never held in memory whole.
bool readStockDecalVolumes(const std::string& zoneDir, StockDecalVolumeSet& out, std::string* err = nullptr);

// ---- Record decoding (VERIFIED, cs_decal_volume_generate_draw_data) ----

// obbQuat: bits 30-31 = index of the dropped (largest) part, bits 0-9 / 10-19 = first / second kept part
// (raw * 2/1023 - 1) * sqrt(1/2), bits 20-28 = third kept part (raw * 2/511 - 1) * sqrt(1/2), kept parts in x, y, z, w
// order; the dropped part is sqrt(1 - min(1, sum of squares)). Bit 29 is not read.
std::array<double, 4> decodeStockDecalQuat(uint32_t packed);
// The columns of the rotation matrix of a unit quaternion (x, y, z, w): axis[0], axis[1], axis[2].
std::array<Vec3, 3> stockDecalAxesFromQuat(const std::array<double, 4>& q);
// obbHalfSize: three unsigned small floats (5-bit exponent, bias 15): bits 0-10 (6-bit mantissa), 11-21 (6), 22-31 (5).
Vec3 decodeStockDecalHalfSize(uint32_t packed);
// Fills every decoded field of `out` from the stored words (names and indices are resolved by the caller).
void decodeStockDecalVolumeRecord(const Vec3& center, uint32_t quat, uint32_t halfSize, uint32_t opacity,
                                  const std::array<uint32_t, 3>& uv, uint32_t info, uint32_t reserved, StockDecalVolume& out);

// ---- Projection maths ----

struct StockDecalCoord {
    double u = 0, v = 0;   // box UV, [0, 1) inside
    double depth = 0;      // [-1, 1] inside, +1 at the face the decal normal (-axis[0]) points out of
};
StockDecalCoord stockDecalCoord(const StockDecalVolume& vol, const Vec3& p);
// The material UV before wrapping: (m00 u + m01 v + offU, m10 u + m11 v + offV).
std::array<double, 2> stockDecalMaterialUv(const StockDecalVolume& vol, double u, double v);
// The world AABB that encloses the box.
AABB stockDecalVolumeBounds(const StockDecalVolume& vol);

// Scene constants of the world pixel shaders (cb7[116].x, cb7[118]); arithmetic VERIFIED, values INFERRED (game-test
// dvar defaults r_decalVolumesCutoffAngle 86, r_decalVolumesCutoffAngleRange 25, r_decalVolumesNormalBias 0,
// r_decalVolumesEdgeFade 0.0078125, r_decalVolumesAlphaThreshold 1/255).
struct StockDecalSceneParams {
    double cutoffAngleDeg = 86.0;
    double cutoffAngleRangeDeg = 25.0;
    double normalBias = 0.0;
    double edgeFade = 0.0078125;
    double alphaThreshold = 1.0 / 255.0;
};
// Angle fade from the receiving surface's (vertex) normal: a = saturate(dot(-axis[0], n) + normalBias); 0 when
// a <= cos(cutoff); else saturate((a - cos(cutoff)) / (cos(cutoff - range) - cos(cutoff))).
double stockDecalAngleFade(const StockDecalVolume& vol, const Vec3& normal, const StockDecalSceneParams& p = {});
// Edge fade at box UV (u, v): 1 - saturate(256 * (max(|2u - 1|, |2v - 1|) - (1 - edgeFade))).
double stockDecalEdgeFade(double u, double v, const StockDecalSceneParams& p = {});

// GfxDecalVolumeMaterial (ACTS pool decalvolumematerial; techsets_<map>.ff and <map>.ff both hold them).
struct StockDecalMaterial {
    std::string name;                    // "i/decal_paint_01_red"
    std::array<std::string, 6> channels; // image names without the leading comma ("" for null): 0 = colour&spec,
                                         // 1 = normal&gloss, 2 = opacity map (sampled .r at the material UV)
    uint32_t flags = 0;                  // 4 colour, 8 normal, 16 emissive, 64 opacity map, 128 alpha dissolve,
                                         // 1 = normal add (when normalBlendMode is 2)
    Vec3 colorTint{1, 1, 1};             // multiplies channel 0 rgb (linear)
    double alphaDissolve = 0;
    double emissiveScale = 1;
    int atlasRows = 1, atlasColumns = 1;
};
bool parseStockDecalMaterial(const std::string& json, StockDecalMaterial& out, std::string* err = nullptr);
// GfxDecalVolumeMask JSON (pool decalvolumemask): its name and blendMap image name.
bool parseStockDecalMask(const std::string& json, std::string& name, std::string& image, std::string* err = nullptr);

// The per-volume shading switches of the draw data (VERIFIED, draw_data lines 146-261 and the world PS loop).
struct StockDecalShading {
    // The raw-UV opacity source (t118 at (u, v), clamped, .r): the volume's mask, or, when the volume has no mask,
    // an identity UV transform and A = 1, B = 0, the material's opacity map (channel 2) moved into this slot.
    bool maskFromVolume = false;
    bool maskFromOpacityMap = false;
    bool opacityMap = false;      // channel 2 sampled at the wrapped material UV (flag 64), when not moved
    bool colour = false;          // material flag 4: lerp albedo (and spec code) to channel 0 rgb * tint
    bool normal = false;          // material flag 8: lerp gloss, cavity, normal
    bool normalAdd = false;       // normal xy added instead of lerped (mode 1, or mode 2 with material bit 0)
    bool emissive = false;        // material flag 16
    bool dissolve = false;        // material flag 128: op = saturate(op * alphaDissolve)
    double opacityScale = 1, opacityBias = 0;  // A, B
    double invFeather = 0;        // 1 / feather (0 = no depth feather)
    double alphaDissolve = 0;
    Vec3 tint{1, 1, 1};
};
StockDecalShading stockDecalShading(const StockDecalVolume& vol, const StockDecalMaterial& mat);
// The world PS opacity of one decal at a pixel (CPU reference): maskSample / opacitySample are the .r texel values
// (pass 1 for a source the shading does not use), angleFade from stockDecalAngleFade. Returns 0 below the alpha
// threshold at either test.
double stockDecalOpacity(const StockDecalShading& s, double maskSample, double opacitySample, double u, double v,
                         double depth, double angleFade, const StockDecalSceneParams& p = {});

// ---- CPU projector ----

struct StockDecalVertex {
    Vec3 pos;          // world position on the receiving surface (plus offsetAlongNormal)
    Vec3 normal;       // receiving surface normal (interpolated vertex normal, unit)
    float u = 0, v = 0;     // box UV [0, 1] (mask coordinates)
    float mu = 0, mv = 0;   // material UV before wrapping (colour / normal / opacity map)
    float depth = 0;        // box depth coordinate [-1, 1] (depth feather)
    float fade = 0;         // angle fade [0, 1] (multiply the opacity by it)
    float lmU = -1, lmV = -1;  // the receiver's lightmap UV (StockWorld::lightmapUv convention), -1 when none
    int lightmap = -1;         // the receiver's lightmap index, -1 when none
};

struct StockDecalBatch {
    size_t volume = 0;                // index in StockDecalVolumeSet::volumes (or the caller's volume)
    std::vector<StockDecalVertex> verts;
    std::vector<uint32_t> idx;        // 3 per triangle, the receiver's winding (counter-clockwise from outside)
    size_t candidateTriangles = 0;    // receiver triangles whose bounds touched the box
    size_t clippedTriangles = 0;      // receiver triangles that left a polygon inside the box
};

struct StockDecalProjectOptions {
    StockDecalSceneParams scene;
    double offsetAlongNormal = 0.0;   // push the output along the receiver normal (0 = exactly on the surface)
    bool dropUnlit = true;            // drop polygons whose every vertex has fade 0 (back faces, perpendicular walls)
};

// A bounding volume hierarchy over receiver triangles. It points into the meshes it was given: they must outlive it
// and stay unchanged.
class StockDecalReceivers {
public:
    // Adds every triangle of `mesh`. lmUv / lmIndex (optional, parallel to mesh.pos) are carried to the output.
    void addMesh(const ImportedMesh& mesh, const std::vector<std::array<float, 2>>* lmUv = nullptr,
                 const std::vector<int>* lmIndex = nullptr);
    // Adds the world meshes; meshMask[m] == false leaves mesh m out (sky, caulk, blended sortKey decal surfaces).
    // An empty mask adds every mesh.
    void addWorld(const StockWorld& world, const std::vector<bool>& meshMask = {});
    // Builds the tree (call after the last add, before projecting).
    void build();
    size_t triangleCount() const { return tris_.size(); }

    struct MeshRef {
        const ImportedMesh* mesh = nullptr;
        const std::vector<std::array<float, 2>>* lmUv = nullptr;
        const std::vector<int>* lmIndex = nullptr;
    };
    struct Tri {
        uint32_t mesh = 0, first = 0;  // MeshRef index, index of the triangle's first idx entry
        float mins[3], maxs[3];
    };
    struct Node {
        float mins[3], maxs[3];
        uint32_t left = 0, count = 0;  // leaf: tris_[left .. left + count); inner: children left, left + 1 (count 0)
    };
    const std::vector<MeshRef>& meshes() const { return meshes_; }
    const std::vector<Tri>& tris() const { return tris_; }
    // Calls fn(tri index) for every triangle whose bounds overlap box.
    template <class Fn> void query(const AABB& box, Fn&& fn) const;

private:
    std::vector<MeshRef> meshes_;
    std::vector<Tri> tris_;
    std::vector<Node> nodes_;
};

// Clips the receiver triangles to the volume's box (bounds culled through the tree, then Sutherland-Hodgman against
// the six box planes) and appends the result to `out` (out.volume is left to the caller). Returns the number of
// output triangles appended.
size_t projectDecalVolume(const StockDecalReceivers& receivers, const StockDecalVolume& vol, StockDecalBatch& out,
                          const StockDecalProjectOptions& opt = {});
// Convenience: every world mesh is a receiver (a tree is built for this one call; for many volumes build a
// StockDecalReceivers once).
size_t projectDecalVolume(const StockWorld& world, const StockDecalVolume& vol, StockDecalBatch& out,
                          const StockDecalProjectOptions& opt = {});
// One batch per volume with at least one triangle, in draw order (ascending drawOrder, then index; later batches
// draw over earlier ones).
std::vector<StockDecalBatch> projectDecalVolumes(const StockDecalReceivers& receivers, const StockDecalVolumeSet& set,
                                                 const StockDecalProjectOptions& opt = {});
// The draw order of a set: volume indices sorted by (drawOrder, index).
std::vector<size_t> stockDecalDrawOrder(const StockDecalVolumeSet& set);

// ---- template ----

template <class Fn> void StockDecalReceivers::query(const AABB& box, Fn&& fn) const {
    if (nodes_.empty()) return;
    const float bmin[3] = {float(box.mins.x), float(box.mins.y), float(box.mins.z)};
    const float bmax[3] = {float(box.maxs.x), float(box.maxs.y), float(box.maxs.z)};
    uint32_t stack[64];
    int sp = 0;
    stack[sp++] = 0;
    while (sp) {
        const Node& n = nodes_[stack[--sp]];
        if (n.mins[0] > bmax[0] || n.maxs[0] < bmin[0] || n.mins[1] > bmax[1] || n.maxs[1] < bmin[1] ||
            n.mins[2] > bmax[2] || n.maxs[2] < bmin[2])
            continue;
        if (n.count) {
            for (uint32_t i = n.left; i < n.left + n.count; ++i) {
                const Tri& t = tris_[i];
                if (t.mins[0] > bmax[0] || t.maxs[0] < bmin[0] || t.mins[1] > bmax[1] || t.maxs[1] < bmin[1] ||
                    t.mins[2] > bmax[2] || t.maxs[2] < bmin[2])
                    continue;
                fn(i);
            }
        } else if (sp + 2 <= 64) {
            stack[sp++] = n.left;
            stack[sp++] = n.left + 1;
        }
    }
}

}  // namespace iw8r
