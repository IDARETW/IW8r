// IW8 Radiant Suite - build preview (3.10): the collision hulls of a compiled extraction
// (`<id>.d3dbsp.replay-collision.json`, doc 15 section 2.3) as edge segments for an overlay, so the mapper sees exactly
// what the build gives the player to collide with.
#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "geometry.h"

namespace iw8r {

struct CollisionHullEdges {
    uint32_t contents = 0;
    bool ladder = false;
    bool mount = false;  // not a hull: the build's mount edges ("mount_edges"), all in one entry, the last
    std::vector<std::pair<Vec3, Vec3>> edges;  // unique hull edges
};

// Parses the collision JSON text. Each brush's planes plus its mins/maxs box planes (the converter adds those too)
// give the hull. Brushes whose planes enclose no volume are skipped.
bool readCollisionHulls(const std::string& jsonText, std::vector<CollisionHullEdges>& out, std::string* err = nullptr);

// The render surfaces of a compiled extraction (`<id>.d3dbsp.replay-world.json` "surfaces": vertices + triangle indices)
// as unique triangle edges, for View > Show Compiled World. *triangles receives the triangle count.
bool readCompiledSurfaces(const std::string& jsonText, std::vector<std::pair<Vec3, Vec3>>& edges, size_t* triangles = nullptr,
                          std::string* err = nullptr);

}  // namespace iw8r
