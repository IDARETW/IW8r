// Stock world static props (smodels): a read-only reader of the placed xmodels in an ACTS gfx_map export.
//   Source: <export>/assets/gfx_map/**/*.json, the GfxWorld asset that stock_world.cpp also reads (fields.smodels).
//   Layout, verified on the mp_shipment cache (9,535 instances, 277 models; docs/research/48 section 4):
//     smodels.models.values[i].model.name     XModel name of static model i
//     smodels.collections.bytes               GfxStaticModelCollection, 16 bytes each: u32 firstInstance,
//                                             u32 instanceCount, u32 smodelIndex, u32 flags
//     smodels.smodelInstanceData.bytes        GfxSModelInstanceData, 24 bytes each: int32 translation[3] in 1/4096
//                                             inch, u16 orientation[4] (quaternion x, y, z, w; c = u / 32767.5 - 1),
//                                             float scale
//   Every instance is drawn by the collection that names it (collection c draws instances firstInstance ..
//   firstInstance + instanceCount - 1). The origin is the translation (checked: the rotated model bounds centre
//   matches the collection bounds within 0.01 inch on the cache).
// Read-only. Nothing is written to the export or the cache.
#pragma once

#include <array>
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
