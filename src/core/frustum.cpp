#include "frustum.h"

#include <algorithm>
#include <cmath>

namespace iw8r {

namespace {

constexpr double kPi = 3.14159265358979323846;
double rad(double d) { return d * kPi / 180.0; }

// Rounding slack for plane tests so a corner lying on a plane (or a sub-ulp error from the trig) is kept.
double slack(double d) { return 1e-9 * (1.0 + std::fabs(d)); }

// Plane with unit outward normal `n` through `point`.
Plane planeThrough(const Vec3& outwardNormal, const Vec3& point) {
    Plane pl;
    pl.n = normalize(outwardNormal);
    pl.d = dot(pl.n, point);
    return pl;
}

}  // namespace

Frustum makeFrustum(const Vec3& eye, double yawDeg, double pitchDeg, double fovDeg, double aspect, double nearZ,
                    double farZ) {
    const double p = rad(pitchDeg), y = rad(yawDeg);
    const Vec3 f{std::cos(p) * std::cos(y), std::cos(p) * std::sin(y), std::sin(p)};
    // Same as Camera::right() (camera.cpp:17). At exactly +-90 pitch the cross product vanishes, so use its limit.
    const Vec3 c = cross(f, Vec3{0, 0, 1});
    const Vec3 r = length(c) > 1e-12 ? c * (1.0 / length(c)) : Vec3{std::sin(y), -std::cos(y), 0};
    const Vec3 u = cross(r, f);  // Camera::up() (camera.cpp:18)

    const double tx = std::tan(rad(fovDeg) * 0.5);  // horizontal half-slope
    const double ty = tx / aspect;                  // vertical half-slope (camera.cpp:25, cameraRay)

    Frustum fr;
    // Inside: dot(f, x - eye) >= nearZ and <= farZ.
    fr.planes[0] = planeThrough(-f, eye + f * nearZ);
    fr.planes[1] = planeThrough(f, eye + f * farZ);
    // Inside: |dot(r, x - eye)| <= tx * dot(f, x - eye), |dot(u, x - eye)| <= ty * dot(f, x - eye).
    fr.planes[2] = planeThrough(-r - f * tx, eye);  // left
    fr.planes[3] = planeThrough(r - f * tx, eye);   // right
    fr.planes[4] = planeThrough(u - f * ty, eye);   // top
    fr.planes[5] = planeThrough(-u - f * ty, eye);  // bottom
    return fr;
}

bool frustumIntersectsAabb(const Frustum& fr, const AABB& box) {
    if (!box.valid()) return false;
    for (const Plane& pl : fr.planes) {
        // Corner with the smallest dot(n, x): if even that corner is outside, the whole box is outside this plane.
        // (Equivalently, the p-vertex with respect to the inward normal.)
        const Vec3 v{pl.n.x >= 0 ? box.mins.x : box.maxs.x, pl.n.y >= 0 ? box.mins.y : box.maxs.y,
                     pl.n.z >= 0 ? box.mins.z : box.maxs.z};
        if (dot(pl.n, v) > pl.d + slack(pl.d)) return false;
    }
    return true;
}

bool frustumContainsPoint(const Frustum& fr, const Vec3& p) {
    for (const Plane& pl : fr.planes)
        if (dot(pl.n, p) > pl.d + slack(pl.d)) return false;
    return true;
}

std::vector<std::size_t> visibleIndices(const Frustum& fr, const std::vector<AABB>& boxes, CullStats* stats) {
    std::vector<std::size_t> out;
    out.reserve(boxes.size());
    for (std::size_t i = 0; i < boxes.size(); ++i)
        if (frustumIntersectsAabb(fr, boxes[i])) out.push_back(i);
    if (stats) {
        stats->tested += boxes.size();
        stats->culled += boxes.size() - out.size();
    }
    return out;
}

bool beyondDrawDistance(const Vec3& eye, const AABB& box, double maxDistance) {
    if (!box.valid()) return false;
    const double maxD = std::max(0.0, maxDistance);
    double d2 = 0;
    for (int i = 0; i < 3; ++i) {
        const double gap = std::max({box.mins[i] - eye[i], eye[i] - box.maxs[i], 0.0});
        d2 += gap * gap;
    }
    return d2 > maxD * maxD;
}

}  // namespace iw8r
