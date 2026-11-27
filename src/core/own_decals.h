// IW8 Radiant - the mapper's own decal volumes in the editor's 3D view (research 132 G6).
//
// An `iw8r_decal` point entity (origin; angles: the forward axis is the projection direction and points into the
// surface; size "depth width height"; material; opacity; uv_rotate, uv_scale, uv_shift) is built as a world decal
// volume (compile_iw8.cpp, "Decal volumes (5.8"): the game projects the material onto every surface inside the box.
// This makes the same projection for the viewport: the box clips the map's drawn brush faces (the stock projector of
// core stock_decal_volumes), and each piece gets the material's UV by the build's UV matrix and offset.
#pragma once

#include <array>
#include <string>
#include <vector>

#include "geometry.h"
#include "map_text.h"

namespace iw8r {

struct OwnDecalPiece {
    std::array<Vec3, 3> pos;             // lifted off the surface a little (no fight with the face under it)
    std::array<std::array<float, 2>, 3> uv;  // material UV
    Vec3 normal;                         // the receiving face's
    float fade[3] = {1, 1, 1};           // the angle fade at each corner (x opacity)
};

struct OwnDecal {
    std::string material;
    double opacity = 1;
    double feather = 0;
    int drawOrder = 0;
    std::vector<OwnDecalPiece> pieces;
};

// The decal volume of one `iw8r_decal` entity as the build reads it; false when it has no material or a bad size.
struct OwnDecalBox {
    Vec3 center;
    std::array<Vec3, 3> axis;  // axis[0]: the projection direction
    Vec3 halfSize;             // along axis[0], axis[1], axis[2]
    std::string material;
    double opacity = 1;
    double feather = 0;
    int drawOrder = 0;
    std::array<double, 4> uvMatrix{1, 0, 0, 1};
    std::array<double, 2> uvOffset{0, 0};
};
bool ownDecalBox(const Entity& e, OwnDecalBox& out);

// Projects every `iw8r_decal` of the map onto its drawn brush faces and Bezier patches (worldspawn, func_group,
// script_brushmodel; tool faces receive none). Returned sorted by draw_order ascending (entity order on ties);
// entityIds receives each one's entity id in the same order.
std::vector<OwnDecal> projectOwnDecals(const MapFile& map, std::vector<Id>* entityIds = nullptr, double lift = 0.1);

}  // namespace iw8r
