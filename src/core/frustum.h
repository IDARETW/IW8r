// IW8 Radiant - view frustum culling math (roadmap 3.20). Pure core code: no viewport, Qt or D3D headers.
#pragma once

#include <cstddef>
#include <vector>

#include "geometry.h"

namespace iw8r {

// Six planes using the geometry.h Plane convention: a point x is inside when dot(n, x) <= d for every plane,
// so each n points OUT of the frustum. Plane order: near, far, left, right, top, bottom.
struct Frustum {
    Plane planes[6];
};

// Frustum of the editor's free-fly camera (src/viewport/camera.h Camera, src/viewport/camera.cpp).
//   forward = (cos p * cos y, cos p * sin y, sin p), yaw in degrees around +Z (0 = +X), pitch in degrees (+ looks up)
//   right   = normalize(cross(forward, +Z)), up = cross(right, forward)           (camera.cpp:13-18)
//   fovDeg  = HORIZONTAL field of view in degrees, full angle (tan half-angle = tan(fov/2)) (camera.cpp:24-25)
//   aspect  = width / height; vertical half-slope = tan(fov/2) / aspect            (camera.cpp:25, cameraRay:38)
//   nearZ / farZ = view-space depth limits along forward. The editor's viewProjection uses 4 and 131072
//   (camera.cpp:23). Pitch must be in (-90, 90) for a meaningful camera; +-90 is handled without NaNs.
Frustum makeFrustum(const Vec3& eye, double yawDeg, double pitchDeg, double fovDeg, double aspect, double nearZ,
                    double farZ);

// Conservative AABB test: uses the box corner most inside each plane (the p-vertex). Returns false only when a
// single plane separates the whole box, so a visible box is never culled. Boxes that straddle a plane or contain
// the eye are reported visible. An invalid (empty) AABB is reported as not visible.
bool frustumIntersectsAabb(const Frustum& fr, const AABB& box);

// True when the point is inside (or on) all six planes.
bool frustumContainsPoint(const Frustum& fr, const Vec3& p);

struct CullStats {
    std::size_t tested = 0;
    std::size_t culled = 0;
};

// Indices (ascending) of the boxes that pass frustumIntersectsAabb. `stats` is optional and accumulates.
std::vector<std::size_t> visibleIndices(const Frustum& fr, const std::vector<AABB>& boxes, CullStats* stats = nullptr);

// True when the nearest point of `box` to `eye` is farther than maxDistance (strictly). Boxes touching the sphere
// are kept. An invalid AABB returns false. Negative maxDistance is treated as 0.
bool beyondDrawDistance(const Vec3& eye, const AABB& box, double maxDistance);

}  // namespace iw8r
