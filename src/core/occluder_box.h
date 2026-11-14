#pragma once
// An occluder box for a convex brush that is not an axis-aligned box (the occlusion bake, plan 2.4, research 118).
// The bake cuts the map along axis-aligned boxes only, so a slanted or many-sided opaque brush is given the box found
// inside it here. The box lies wholly inside the brush, so it hides at most what the brush itself hides: using it can
// never hide something that should be seen.
#include <algorithm>
#include <cmath>
#include <optional>
#include <vector>

#include "geometry.h"

namespace iw8r {

// A large axis-aligned box inside the convex solid of `planes` (outward normals, a point p is inside when
// dot(n, p) - d <= 0). `corners` are the solid's corner points (any order, at least four not in one plane); the search
// starts at their average, which is inside a convex solid. `margin` keeps every corner of the box that far inside the
// planes. nullopt when the solid has no inside or the box would be thinner than 1 unit.
//
// The search grows a box from the centre: first all sides together (the solid's bounds scaled down until it fits),
// then each of the six sides alone as far as it goes, three rounds. It does not find the largest possible box, only a
// good one, and that is enough: any box inside the solid is a valid occluder.
inline std::optional<AABB> inscribedBox(const std::vector<Plane>& planes, const std::vector<Vec3>& corners, double margin = 0.05) {
    if (planes.size() < 4 || corners.size() < 4) return std::nullopt;
    AABB bounds;
    Vec3 c{0, 0, 0};
    for (const Vec3& p : corners) {
        bounds.add(p);
        c = c + p;
    }
    c = c * (1.0 / double(corners.size()));
    const auto inside = [&](double x, double y, double z) {
        for (const Plane& pl : planes)
            if (pl.n.x * x + pl.n.y * y + pl.n.z * z - pl.d > -margin) return false;
        return true;
    };
    double lo[3] = {c.x, c.y, c.z}, hi[3] = {c.x, c.y, c.z};
    const auto fits = [&] {
        for (int k = 0; k < 8; ++k)
            if (!inside(k & 1 ? hi[0] : lo[0], k & 2 ? hi[1] : lo[1], k & 4 ? hi[2] : lo[2])) return false;
        return true;
    };
    if (!fits()) return std::nullopt;  // the centre itself is not inside by the margin
    const double bmin[3] = {bounds.mins.x, bounds.mins.y, bounds.mins.z}, bmax[3] = {bounds.maxs.x, bounds.maxs.y, bounds.maxs.z};
    const double cc[3] = {c.x, c.y, c.z};
    {
        // All sides together: the bounds scaled about the centre.
        double good = 0, bad = 1;
        for (int it = 0; it < 30; ++it) {
            const double t = 0.5 * (good + bad);
            for (int a = 0; a < 3; ++a) lo[a] = cc[a] + (bmin[a] - cc[a]) * t, hi[a] = cc[a] + (bmax[a] - cc[a]) * t;
            if (fits()) good = t;
            else bad = t;
        }
        for (int a = 0; a < 3; ++a) lo[a] = cc[a] + (bmin[a] - cc[a]) * good, hi[a] = cc[a] + (bmax[a] - cc[a]) * good;
    }
    for (int round = 0; round < 3; ++round)
        for (int side = 0; side < 6; ++side) {
            // One side alone, out to the solid's bounds at most.
            const int a = side / 2;
            const bool high = side & 1;
            double& edge = high ? hi[a] : lo[a];
            const double start = edge, limit = high ? bmax[a] : bmin[a];
            double good = 0, bad = 1;
            for (int it = 0; it < 30; ++it) {
                const double t = 0.5 * (good + bad);
                edge = start + (limit - start) * t;
                if (fits()) good = t;
                else bad = t;
            }
            edge = start + (limit - start) * good;
        }
    if (!fits()) return std::nullopt;
    for (int a = 0; a < 3; ++a)
        if (!(hi[a] - lo[a] >= 1.0)) return std::nullopt;
    AABB out;
    out.add(Vec3{lo[0], lo[1], lo[2]});
    out.add(Vec3{hi[0], hi[1], hi[2]});
    return out;
}

}  // namespace iw8r
