// IW8 Radiant - spatial draw chunks for frustum culling (roadmap 3.20). Pure core code.
//
// A batch of triangles (one material) is reordered so that nearby triangles are contiguous (Morton order of the
// triangle centroids on a 1024^3 grid over the batch bounds) and cut into chunks of at most `maxTriangles`, each with
// its bounding box. A renderer uploads the triangles in that order and, per frame, draws only the chunks whose boxes
// pass the frustum test, merging neighbouring visible chunks into one draw call.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "frustum.h"
#include "geometry.h"

namespace iw8r {

struct DrawChunk {
    uint32_t firstTriangle = 0;  // in the reordered batch
    uint32_t triangleCount = 0;
    AABB bounds;
};

// order: the new triangle order (order[i] = the input triangle placed at position i). centroids and the triangle
// corners come from `corners` (3 per triangle, in input order).
void buildDrawChunks(const std::vector<Vec3>& corners, size_t maxTriangles, std::vector<uint32_t>& order,
                     std::vector<DrawChunk>& chunks);

// Frustum from a row-vector view-projection matrix (v * M, row-major 16 floats, D3D clip depth 0..w), in the Frustum
// plane convention of frustum.h (inside: dot(n, x) <= d). Plane order: near, far, left, right, top, bottom.
Frustum frustumFromViewProj(const float m[16]);

struct DrawRange {
    uint32_t firstTriangle = 0;
    uint32_t triangleCount = 0;
};

// The visible chunks as merged ranges (neighbours joined). culled (optional) gets the number of chunks left out.
std::vector<DrawRange> visibleDrawRanges(const Frustum& fr, const std::vector<DrawChunk>& chunks, size_t* culled = nullptr);

}  // namespace iw8r
