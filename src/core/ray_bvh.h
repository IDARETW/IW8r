// IW8 Radiant - a triangle bounding volume hierarchy for ray queries (the lightmap baker, roadmap 5.11 / 5.21).
//
// Built once from a triangle soup; closest-hit and any-hit (occlusion) queries are const and thread-safe. Nodes split
// at the median of the longest centroid axis (good enough for the editor's maps: tens to hundreds of thousands of
// triangles). Triangles are two-sided. Pure core code.
#pragma once

#include <cstdint>
#include <vector>

#include "geometry.h"

namespace iw8r {

class RayBvh {
public:
    struct Triangle {
        Vec3 a, b, c;
        uint32_t id = 0;  // caller data (surface / material index)
    };
    struct Hit {
        double t = 0;       // distance along the (unit) direction
        uint32_t tri = 0;   // index into triangles()
        double u = 0, v = 0;  // barycentrics of b and c
    };

    void build(std::vector<Triangle> triangles);
    const std::vector<Triangle>& triangles() const { return tris_; }
    bool empty() const { return tris_.empty(); }

    // Closest hit of the ray origin + t * dir for tMin < t < tMax (dir need not be unit; t is in its units).
    bool intersect(const Vec3& origin, const Vec3& dir, double tMin, double tMax, Hit& hit) const;
    // True when anything lies on the ray between tMin and tMax.
    bool occluded(const Vec3& origin, const Vec3& dir, double tMin, double tMax) const;

private:
    struct Node {
        AABB box;
        uint32_t left = 0, right = 0;  // children (inner nodes)
        uint32_t first = 0, count = 0; // triangle range (leaves, count > 0)
    };
    uint32_t buildNode(uint32_t first, uint32_t count, std::vector<Vec3>& centroids);
    template <bool AnyHit>
    bool trace(const Vec3& o, const Vec3& d, double tMin, double tMax, Hit* hit) const;
    std::vector<Triangle> tris_;
    std::vector<Node> nodes_;
};

}  // namespace iw8r
