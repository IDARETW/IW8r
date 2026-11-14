// Stock world static props (smodels): a read-only reader of the placed xmodels in an ACTS gfx_map export.
//   Source: <export>/assets/gfx_map/**/*.json, the GfxWorld asset that stock_world.cpp also reads (fields.smodels).
//   Layout, verified on the mp_shipment cache (9,535 instances, 277 models; docs/research/48 section 4):
//     smodels.models.values[i].model.name     XModel name of static model i
//     smodels.collections.bytes               GfxStaticModelCollection, 16 bytes each: u32 firstInstance,
//                                             u32 instanceCount, u16 smodelIndex, u16 transientGfxWorldPlaced,
//                                             u16 clutterIndex, u8 flags (0x20 clutter, 0x80 occluder), u8 pad
//     smodels.instanceFlags.bytes             one byte per instance row; the low four bits are the placement's LOD
//                                             distance scale index (smodelLodScaleOfIndex; research 119)
//     smodels.smodelInstanceData.bytes        GfxSModelInstanceData, 24 bytes each: int32 translation[3] in 1/4096
//                                             inch, u16 orientation[4] (quaternion x, y, z, w; c = u / 32767.5 - 1),
//                                             float scale
//     smodels.modelStaticIndirection          GfxModelStaticIndirection, 8 bytes each (hex bytes or values): u32
//                                             lightingInstanceIndirection, u16 splinedModelInstanceIndex,
//                                             u16 worldTransient, one per instance row
//   Every instance is drawn by the collection that names it (collection c draws instances firstInstance ..
//   firstInstance + instanceCount - 1). The origin is the translation (checked: the rotated model bounds centre
//   matches the collection bounds within 0.01 inch on the cache).
// Read-only. Nothing is written to the export or the cache.
#pragma once

#include <array>
#include <memory>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "geometry.h"

namespace iw8r {

struct StockModelInstance {
    std::string model;                                   // XModel asset name, e.g. tm/foliage_tree_oak_01
    Vec3 origin;                                         // model origin, Z-up inches (the Radiant unit)
    std::array<Vec3, 3> axis{Vec3{1, 0, 0}, Vec3{0, 1, 0}, Vec3{0, 0, 1}};  // model X, Y, Z in world space (unit columns)
    double scale = 1;                                    // uniform scale from smodelInstanceData (raw value)
    uint32_t index = 0;                                  // its row in GfxWorld.smodels.smodelInstanceData (11.5: hide)
    uint32_t collection = 0xFFFFFFFFu;                   // its GfxStaticModelCollection (the unit the Umbra tome culls:
                                                         // user ID class 1); 0xFFFFFFFF = not a placement of the map
    // Baked lighting (docs/research/105 section 1.1), from smodels.modelStaticIndirection[index] and the model's flags:
    // modelFlags & 1 (LIGHTPROBE): lightingIndex is the box in transient zone worldTransient's light grid volumes
    // (stock_lightgrid.h); modelFlags & 2 (LIGHTMAP): lightingIndex indexes GfxWorld.lmapLookup. Defaults when the
    // export has no modelStaticIndirection.
    uint32_t lightingIndex = 0xFFFFFFFF;
    uint16_t worldTransient = 0;
    uint32_t modelFlags = 0;                             // smodels.models[collection.smodelIndex].flags
    // The scale on the distance where the game stops drawing this placement (research 119): the placement's own
    // lodScale when its collection holds this one instance and is not clutter, else 1 (the game's rule). 1 when the
    // export has no instanceFlags.
    float lodCullScale = 1.0f;
    // The map's material for each surface of the model (every LOD, in the model's surface order; XModelLod::surfIndex
    // and surfCount give a LOD's range): smodels.smodelSurfMatIndirection from models[].firstStaticModelSurfaceIndex,
    // smodelExpansionData[].numSurfs entries, into smodels.materials (leading ',' dropped). The map chooses them per
    // static model (a material skin): Speedball's containers use "..._second" / "_blue" materials, not the model's own
    // (whose placeholder tint drew them pink). Null when the export lacks the tables; shared by a model's placements.
    std::shared_ptr<const std::vector<std::string>> surfaceMaterials;
};

// Reads the static model instances of one gfx_map export. exportPathOrDir is either the gfx_map JSON file itself or
// a zone export folder (the folder that holds assets/gfx_map, e.g. the world cache folder of mp_shipment). Instances
// come out in collection order. Fails (false, err set) when the JSON or the smodels arrays are missing or do not
// match their counts, or when a collection names a model or an instance that is not in the export.
bool readStockModelInstances(const std::string& exportPathOrDir, std::vector<StockModelInstance>& out,
                             std::string* err = nullptr);

// Model name -> number of instances placed with it.
std::map<std::string, size_t> stockModelUsage(const std::vector<StockModelInstance>& instances);

// Key/value pairs for a read-only misc_model reference entity:
//   "classname" "misc_model", "model", "origin" ("x y z"), "angles" ("pitch yaw roll", degrees, the editor
//   convention of anglesToMatrix in prefab.h), "modelscale". A scale that is not finite or not positive is written as 1.
std::vector<std::pair<std::string, std::string>> stockModelInstancesAsEntities(const StockModelInstance& inst);

// Inverse of anglesToMatrix (prefab.h): the pitch, yaw, roll in degrees whose rotation has these columns as its
// model axes. The columns are normalised first. Gimbal lock (pitch +-90) folds the roll into the yaw (roll 0).
Vec3 axisToAngles(const std::array<Vec3, 3>& axis);

// The model axes (rotation columns) of a pitch, yaw, roll in degrees: anglesToMatrix(angles) column by column.
std::array<Vec3, 3> anglesToAxis(const Vec3& pitchYawRoll);

}  // namespace iw8r
