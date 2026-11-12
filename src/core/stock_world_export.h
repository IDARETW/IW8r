// OBJ export of the stock Replay world (StockWorld, src/core/stock_world.h) for placing as a misc_model in the editor.
#pragma once

#include <string>
#include <vector>

#include "geometry.h"
#include "stock_world.h"

namespace iw8r {

// Writes the stock world triangles that touch `region` as OBJ text.
//   Filter: a triangle is kept when at least one of its three vertices lies inside `region` (AABB::contains). A
//   triangle that crosses the box with all three vertices outside is dropped (a known limit: large floor triangles
//   can leave a gap at the region edge). An invalid region keeps nothing.
//   skipSky: meshes whose material is a sky dome are dropped. A material counts as sky when its name, without a
//   leading "tw/", starts with "sky" (case-insensitive), e.g. tw/sky_mp_shipment.
//   Coordinates: positions are translated by -centre(region), so the region centre is the origin. The centre is
//   written to *offset when given (add it back to get the stock Z-up inches).
//   Groups: one "g" and "usemtl" per material that has kept triangles, in StockWorld mesh order.
//   Normals come from ImportedMesh::normal (vn); texture coordinates from ImportedMesh::uv (vt, written as stored, no
//   V flip). Meshes without matching uv or normal arrays get "vt 0 0" / "vn 0 0 1".
//   triangles: number of kept triangles. materials: names of the groups written, in order.
std::string stockWorldToObj(const StockWorld& w, const AABB& region, bool skipSky, size_t* triangles,
                            std::vector<std::string>* materials, Vec3* offset = nullptr);

}  // namespace iw8r
