// IW8 Radiant - voxel flood-fill leak (sealing) check.
//
// Sealing brushes: brushes of worldspawn and func_group entities whose contents collide (brushContents in
// iw8_contents.h) and that are not `contents detail;`. Trigger, hint, skip, origin, portal, water and
// nonColliding brushes never seal. Patches are not sealing in this version (UNKNOWN for the compiler).
//
// The grid covers the sealing brushes' bounds padded by two cells. A cell is solid when its centre lies
// inside a sealing brush. The outside is flood-filled (6-connected) from the grid boundary. A point entity
// (any entity with an origin other than worldspawn and func_group) whose cell is free and reachable from the
// outside is a leak; its shortest cell path to the boundary is returned as a Radiant pointfile polyline.
// Point entities outside the padded grid are not tested. Cells thinner than a wall can leak through it:
// keep the cell size at or below the thinnest sealing wall.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "brush_ops.h"
#include "geometry.h"
#include "map_text.h"

namespace iw8r {

struct LeakResult {
    bool sealed = true;
    size_t leakingEntity = 0;      // index into MapFile::entities of the first leaking point entity
    std::string classname;         // its classname
    // Polyline from the entity origin through the cell centres to a point outside the grid (Radiant
    // pointfile order). Empty when sealed.
    std::vector<Vec3> path;
    size_t solidCells = 0, cells = 0;
    std::string error;             // set when the check could not run (sealed is then false)
};

LeakResult checkLeak(const MapFile& map, double cell = 16, size_t maxCells = 64'000'000);

// A convex brush of a brush entity, as used by the sealing test and the gameplay checks.
struct SolidBrush {
    size_t entity = 0;   // index into MapFile::entities
    ConvexBrush planes;  // outward half-spaces (contributing and redundant faces)
    AABB bounds;
    bool detail = false; // `contents detail;`
};

// Colliding brushes of worldspawn and func_group (brushContents().collides). Detail brushes are included
// only when `includeDetail` is set; they do not seal (checkLeak) but do block players (gameplay checks).
std::vector<SolidBrush> collidingBrushes(const MapFile& map, bool includeDetail);

// True for the classes that are brush containers rather than point entities.
bool isBrushEntityClass(const std::string& classname);

}  // namespace iw8r
