#include "primitives.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace iw8r {

namespace {

constexpr double kPi = 3.14159265358979323846;

// Box with positive extent on every axis.
bool usableBox(const AABB& box) {
    if (!box.valid()) return false;
    for (int i = 0; i < 3; ++i)
        if (!(box.maxs[i] > box.mins[i])) return false;
    return true;
}

std::string faceSuffix(const std::string& s) {
    if (s.empty() || s[0] == ' ' || s[0] == '\t') return s;
    return " " + s;
}

// Drops faces that do not contribute to the solid, keeping the original order.
ConvexBrush keepContributing(const ConvexBrush& b) {
    const auto ws = brushWindings(b);
    ConvexBrush out;
    for (size_t i = 0; i < b.size(); ++i)
        if (!ws[i].empty()) out.push_back(b[i]);
    return out;
}

// Splits a direction code into an axis (0 = x, 1 = y) and a sign (+1 for the positive direction).
bool decodeDir(int dir, int& axis, double& sign) {
    if (dir < 0 || dir > 3) return false;
    axis = dir < 2 ? 0 : 1;
    sign = (dir % 2 == 0) ? 1.0 : -1.0;
    return true;
}

// Convex prism: the polygon (given in the (uAxis, vAxis) coordinates, any winding, convex) extruded along
// `axis` from lo to hi. Each polygon edge gives a lateral face; the two caps are at lo and hi.
ConvexBrush prismFromPolygon(std::vector<std::pair<double, double>> poly, int uAxis, int vAxis, int axis,
                             double lo, double hi, const std::string& suffix) {
    const size_t n = poly.size();
    double area2 = 0;
    for (size_t i = 0; i < n; ++i) {
        const auto& a = poly[i];
        const auto& c = poly[(i + 1) % n];
        area2 += a.first * c.second - c.first * a.second;
    }
    if (area2 < 0) std::reverse(poly.begin(), poly.end());

    ConvexBrush b;
    for (size_t i = 0; i < n; ++i) {
        const auto& a = poly[i];
        const auto& c = poly[(i + 1) % n];
        // Counter-clockwise polygon: the outward edge normal is (dy, -dx).
        Vec3 nrm;
        nrm[uAxis] = c.second - a.second;
        nrm[vAxis] = -(c.first - a.first);
        nrm = normalize(nrm);
        Vec3 p;
        p[uAxis] = a.first;
        p[vAxis] = a.second;
        b.push_back({{nrm, dot(nrm, p)}, suffix});
    }
    Vec3 nMin, nMax;
    nMin[axis] = -1;
    nMax[axis] = 1;
    b.push_back({{nMin, -lo}, suffix});
    b.push_back({{nMax, hi}, suffix});
    return keepContributing(b);
}

}  // namespace

ConvexBrush makeCone(const AABB& box, int sides, int axis, const std::string& suffix) {
    if (sides < 3 || sides > 64 || axis < 0 || axis > 2 || !usableBox(box)) return {};
    const int u = (axis + 1) % 3, v = (axis + 2) % 3;
    const std::string s = faceSuffix(suffix);

    // Unit-ellipse vertices, then affinely fitted so the polygon spans the box's rectangle in u and v.
    std::vector<std::pair<double, double>> unit;
    for (int i = 0; i < sides; ++i) {
        const double t = 2.0 * kPi * i / sides;
        unit.push_back({std::cos(t), std::sin(t)});
    }
    double uMin = 1e30, uMax = -1e30, vMin = 1e30, vMax = -1e30;
    for (const auto& p : unit) {
        uMin = std::min(uMin, p.first);
        uMax = std::max(uMax, p.first);
        vMin = std::min(vMin, p.second);
        vMax = std::max(vMax, p.second);
    }
    const Vec3 c = box.center();
    const double uMid = 0.5 * (uMin + uMax), uHalf = 0.5 * (uMax - uMin);
    const double vMid = 0.5 * (vMin + vMax), vHalf = 0.5 * (vMax - vMin);
    const double ru = 0.5 * (box.maxs[u] - box.mins[u]);
    const double rv = 0.5 * (box.maxs[v] - box.mins[v]);

    std::vector<Vec3> base;
    for (const auto& p : unit) {
        Vec3 q;
        q[u] = c[u] + ru * (p.first - uMid) / uHalf;
        q[v] = c[v] + rv * (p.second - vMid) / vHalf;
        q[axis] = box.mins[axis];
        base.push_back(q);
    }
    Vec3 apex = c;
    apex[axis] = box.maxs[axis];

    ConvexBrush b;
    for (int i = 0; i < sides; ++i) {
        const Vec3& p0 = base[i];
        const Vec3& p1 = base[(i + 1) % sides];
        // Lateral face through p0, p1 and the apex, oriented away from the interior point (the box center).
        Vec3 nrm = normalize(cross(p1 - p0, apex - p0));
        if (dot(nrm, c - p0) > 0) nrm = -nrm;
        b.push_back({{nrm, dot(nrm, p0)}, s});
    }
    Vec3 nBase;
    nBase[axis] = -1;
    b.push_back({{nBase, -box.mins[axis]}, s});
    return keepContributing(b);
}

std::vector<ConvexBrush> makeSphere(const AABB& box, int sides, const std::string& suffix) {
    if (sides < 4 || sides > 32 || !usableBox(box)) return {};
    const std::string s = faceSuffix(suffix);

    // Support-plane normals: the six axis directions plus a latitude/longitude grid (poles included as
    // the axis directions). Duplicates are dropped.
    std::vector<Vec3> dirs;
    auto addDir = [&dirs](Vec3 n) {
        n = normalize(n);
        for (const Vec3& e : dirs)
            if (dot(e, n) > 1.0 - 1e-9) return;
        dirs.push_back(n);
    };
    addDir({1, 0, 0});
    addDir({-1, 0, 0});
    addDir({0, 1, 0});
    addDir({0, -1, 0});
    addDir({0, 0, 1});
    addDir({0, 0, -1});
    const int rings = std::max(2, sides / 2);
    for (int j = 1; j < rings; ++j) {
        const double phi = -0.5 * kPi + kPi * j / rings;
        for (int i = 0; i < sides; ++i) {
            const double th = 2.0 * kPi * i / sides;
            addDir({std::cos(phi) * std::cos(th), std::cos(phi) * std::sin(th), std::sin(phi)});
        }
    }

    // Ellipsoid inscribed in the box. A support plane with unit normal n sits at
    // dot(n, x - center) = h(n) = sqrt(sum (semi_i * n_i)^2), so the ellipsoid lies inside every plane.
    const Vec3 c = box.center();
    const Vec3 half = (box.maxs - box.mins) * 0.5;
    ConvexBrush b;
    for (const Vec3& n : dirs) {
        const double h = std::sqrt(half.x * half.x * n.x * n.x + half.y * half.y * n.y * n.y +
                                   half.z * half.z * n.z * n.z);
        b.push_back({{n, dot(n, c) + h}, s});
    }
    return {keepContributing(b)};
}

std::vector<ConvexBrush> makeStairs(const AABB& box, int steps, int upAxisDir, const std::string& suffix) {
    int axis = 0;
    double sign = 1;
    if (steps < 1 || steps > 256 || !decodeDir(upAxisDir, axis, sign) || !usableBox(box)) return {};

    const double lo = box.mins[axis], hi = box.maxs[axis];
    const double run = hi - lo;
    const double height = box.maxs.z - box.mins.z;
    const std::string s = faceSuffix(suffix);

    // Slab boundaries along the run axis, measured from the low end (sign +) or the high end (sign -),
    // so the highest step is always at the side the direction points to.
    std::vector<double> edge(steps + 1);
    for (int k = 0; k <= steps; ++k) edge[k] = lo + run * k / steps;
    edge[steps] = hi;

    std::vector<ConvexBrush> out;
    for (int i = 0; i < steps; ++i) {
        const double top = (i == steps - 1) ? box.maxs.z : box.mins.z + height * (i + 1) / steps;
        const double a0 = sign > 0 ? edge[i] : edge[steps - 1 - i];
        const double a1 = sign > 0 ? edge[i + 1] : edge[steps - i];
        AABB slab;
        slab.mins = box.mins;
        slab.maxs = box.maxs;
        slab.mins[axis] = std::min(a0, a1);
        slab.maxs[axis] = std::max(a0, a1);
        slab.maxs.z = top;
        out.push_back(makeBox(slab, s));
    }
    return out;
}

std::vector<ConvexBrush> makeArchBlocks(const AABB& box, int segments, double thickness, int axis,
                                        const std::string& suffix) {
    if (segments < 2 || segments > 64 || axis < 0 || axis > 2 || !usableBox(box)) return {};
    // Profile: height on Z unless the opening runs along Z, then on Y. Width is the remaining axis.
    const int h = (axis == 2) ? 1 : 2;
    const int w = 3 - axis - h;
    const double ru = 0.5 * (box.maxs[w] - box.mins[w]);
    const double rv = box.maxs[h] - box.mins[h];
    if (!(thickness > 0) || !(thickness < std::min(ru, rv))) return {};

    const double cu = 0.5 * (box.mins[w] + box.maxs[w]);
    const double bot = box.mins[h];
    const double ri = ru - thickness, rh = rv - thickness;
    const std::string s = faceSuffix(suffix);

    // Radial cut k at angle pi - pi k / segments; outer and inner ellipse points on each cut.
    auto outer = [&](int k) {
        const double th = kPi - kPi * k / segments;
        return std::pair<double, double>{cu + ru * std::cos(th), bot + rv * std::sin(th)};
    };
    auto inner = [&](int k) {
        const double th = kPi - kPi * k / segments;
        return std::pair<double, double>{cu + ri * std::cos(th), bot + rh * std::sin(th)};
    };

    std::vector<ConvexBrush> out;
    for (int k = 0; k < segments; ++k) {
        std::vector<std::pair<double, double>> quad = {outer(k), outer(k + 1), inner(k + 1), inner(k)};
        out.push_back(prismFromPolygon(quad, w, h, axis, box.mins[axis], box.maxs[axis], s));
    }
    return out;
}

ConvexBrush makeRamp(const AABB& box, int upDir, const std::string& suffix) {
    int a = 0;
    double sign = 1;
    if (!decodeDir(upDir, a, sign) || !usableBox(box)) return {};
    const int b = 1 - a;  // the other horizontal axis (lateral faces)
    const std::string s = faceSuffix(suffix);

    // Low end (floor edge) and high end (full-height edge) along the run axis.
    const double e0 = sign > 0 ? box.mins[a] : box.maxs[a];
    const double e1 = sign > 0 ? box.maxs[a] : box.mins[a];
    const double run = box.maxs[a] - box.mins[a];
    const double rise = box.maxs.z - box.mins.z;

    ConvexBrush out;
    // Floor.
    out.push_back({{{0, 0, -1}, -box.mins.z}, s});
    // Vertical back at the high end.
    Vec3 nBack;
    nBack[a] = sign;
    out.push_back({{nBack, sign > 0 ? box.maxs[a] : -box.mins[a]}, s});
    // Sloped top through (e0, floor) and (e1, top). Outward normal points up and toward the low end.
    Vec3 nSlope;
    nSlope[a] = -sign * rise;
    nSlope.z = run;
    nSlope = normalize(nSlope);
    Vec3 pSlope;
    pSlope[a] = e0;
    pSlope.z = box.mins.z;
    out.push_back({{nSlope, dot(nSlope, pSlope)}, s});
    // Two triangular sides.
    Vec3 nLo, nHi;
    nLo[b] = -1;
    nHi[b] = 1;
    out.push_back({{nLo, -box.mins[b]}, s});
    out.push_back({{nHi, box.maxs[b]}, s});
    return keepContributing(out);
}

}  // namespace iw8r
