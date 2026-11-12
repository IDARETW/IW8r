#include "spline_tools.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>

#include "brush_ops.h"

namespace iw8r {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kEps = 1e-9;        // points closer than this are duplicates
constexpr size_t kMaxPosts = 100000;  // guard against a tiny spacing filling memory
const std::string kEol = "\r\n";      // MapFile default line terminator

bool allFinite(const std::vector<Vec3>& v) {
    for (const Vec3& p : v)
        if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) return false;
    return true;
}

bool validMaterial(const std::string& m) {
    return !m.empty() && m.find_first_of(" \t\r\n\"") == std::string::npos;
}

// Texture fields of a face: material first, then the usual 64 64 0 0 0 0 (as the editor's trigger faces use).
std::string suffixFor(const std::string& material) { return " " + material + " 64 64 0 0 0 0"; }

// Drops consecutive duplicates. For a closed ring the last point is also dropped when it equals the first.
std::vector<Vec3> dedupe(const std::vector<Vec3>& pts, bool closed) {
    std::vector<Vec3> out;
    for (const Vec3& p : pts)
        if (out.empty() || length(p - out.back()) > kEps) out.push_back(p);
    if (closed)
        while (out.size() > 1 && length(out.back() - out.front()) <= kEps) out.pop_back();
    return out;
}

// Control point k. Closed curves wrap. Open curves mirror the end point (P[-1] = 2 P[0] - P[1]) so the end spans
// have a phantom neighbour and the curve starts and ends at the given points.
Vec3 ctrl(const std::vector<Vec3>& P, long k, bool closed) {
    const long n = static_cast<long>(P.size());
    if (closed) return P[static_cast<size_t>(((k % n) + n) % n)];
    if (k < 0) return P[0] * 2.0 - P[1];
    if (k >= n) return P[n - 1] * 2.0 - P[n - 2];
    return P[static_cast<size_t>(k)];
}

// Point on the centripetal Catmull-Rom span p1..p2 (neighbours p0, p3) at u in [0, 1] (Barry-Goldman form).
Vec3 crSpan(const Vec3& p0, const Vec3& p1, const Vec3& p2, const Vec3& p3, double u) {
    // Knot spacing is the square root of the chord length (alpha = 0.5). The floor keeps coincident points
    // from giving zero denominators.
    auto knot = [](double t, const Vec3& a, const Vec3& b) { return t + std::sqrt(std::max(length(b - a), 1e-9)); };
    const double t0 = 0.0;
    const double t1 = knot(t0, p0, p1);
    const double t2 = knot(t1, p1, p2);
    const double t3 = knot(t2, p2, p3);
    const double t = t1 + u * (t2 - t1);
    auto lerp = [](const Vec3& a, const Vec3& b, double ta, double tb, double x) {
        return a * ((tb - x) / (tb - ta)) + b * ((x - ta) / (tb - ta));
    };
    const Vec3 a1 = lerp(p0, p1, t0, t1, t);
    const Vec3 a2 = lerp(p1, p2, t1, t2, t);
    const Vec3 a3 = lerp(p2, p3, t2, t3, t);
    const Vec3 b1 = lerp(a1, a2, t0, t2, t);
    const Vec3 b2 = lerp(a2, a3, t1, t3, t);
    return lerp(b1, b2, t1, t2, t);
}

// Horizontal unit direction from a to b. nullopt for a vertical-only or zero step.
std::optional<Vec3> planDir(const Vec3& a, const Vec3& b) {
    const Vec3 h{b.x - a.x, b.y - a.y, 0};
    if (length(h) <= kEps) return std::nullopt;
    return normalize(h);
}

// Mitre at a joint between incoming direction u0 and outgoing direction u1. prevEnd is the outward normal of the
// incoming segment's end cap, nextStart that of the outgoing segment's start cap. Both caps lie on one plane
// through the joint. Opposite directions have no bisector, so each segment keeps a flat cap.
void jointNormals(const Vec3& u0, const Vec3& u1, Vec3& prevEnd, Vec3& nextStart) {
    const Vec3 s = u0 + u1;
    if (length(s) > 1e-6) {
        const Vec3 m = normalize(s);
        prevEnd = m;
        nextStart = -m;
    } else {
        prevEnd = u0;
        nextStart = -u1;
    }
}

// Wall segment from a to b (horizontal unit direction u), half-width `half`, vertical height `height`.
// Side faces are vertical and parallel to the segment. The two caps are the mitre (or flat) planes at a and b.
// The bottom plane contains the segment (normal N is perpendicular to the segment and to the side direction,
// pointing down). The top plane is `height` above it in Z.
ConvexBrush wallBrush(const Vec3& a, const Vec3& b, const Vec3& u, const Vec3& startOut, const Vec3& endOut,
                      double half, double height, const std::string& suf) {
    const Vec3 side{-u.y, u.x, 0};
    ConvexBrush br;
    br.push_back({{side, dot(side, a) + half}, suf});
    br.push_back({{-side, dot(-side, a) + half}, suf});
    br.push_back({{startOut, dot(startOut, a)}, suf});
    br.push_back({{endOut, dot(endOut, b)}, suf});
    // The horizontal part of (b - a) is nonzero here, so cross(side, b - a).z < 0 and N points down.
    const Vec3 N = normalize(cross(side, b - a));
    br.push_back({{N, dot(N, a)}, suf});
    br.push_back({{-N, dot(-N, a + Vec3{0, 0, height})}, suf});
    return br;
}

}  // namespace

std::vector<Vec3> sampleSpline(const std::vector<Vec3>& P, double segmentLength, bool closed) {
    const size_t n = P.size();
    if (!(segmentLength > 0) || !std::isfinite(segmentLength) || n < (closed ? 3u : 2u) || !allFinite(P)) return {};

    constexpr int kDense = 64;  // samples per span for the arc-length estimate
    const size_t spans = closed ? n : n - 1;
    std::vector<Vec3> out;
    for (size_t i = 0; i < spans; ++i) {
        const long li = static_cast<long>(i);
        const Vec3 p0 = ctrl(P, li - 1, closed), p1 = ctrl(P, li, closed);
        const Vec3 p2 = ctrl(P, li + 1, closed), p3 = ctrl(P, li + 2, closed);

        std::vector<Vec3> dense(kDense + 1);
        std::vector<double> cum(kDense + 1, 0.0);
        for (int j = 0; j <= kDense; ++j) {
            dense[j] = j == 0 ? p1 : (j == kDense ? p2 : crSpan(p0, p1, p2, p3, double(j) / kDense));
            if (j > 0) cum[j] = cum[j - 1] + length(dense[j] - dense[j - 1]);
        }

        // The span's start point is always kept. Interior pieces are placed at equal arc length, so the spacing
        // is at most segmentLength and close to it.
        const double L = cum[kDense];
        out.push_back(p1);
        if (L <= 1e-12) continue;  // coincident control points
        const int k = std::max(1, static_cast<int>(std::ceil(L / segmentLength - 1e-6)));
        for (int j = 1; j < k; ++j) {
            const double target = L * j / k;
            int m = 1;
            while (m < kDense && cum[m] < target) ++m;
            const double seg = cum[m] - cum[m - 1];
            const double f = seg > 0 ? (target - cum[m - 1]) / seg : 0.0;
            out.push_back(dense[m - 1] + (dense[m] - dense[m - 1]) * f);
        }
    }
    if (!closed) out.push_back(P[n - 1]);
    return out;
}

std::vector<Primitive> wallAlongSpline(const std::vector<Vec3>& pts, double thickness, double height,
                                       const std::string& material, bool closed) {
    if (!(thickness > 0) || !(height > 0) || !std::isfinite(thickness) || !std::isfinite(height) ||
        !validMaterial(material) || !allFinite(pts))
        return {};
    const std::vector<Vec3> P = dedupe(pts, closed);
    const size_t n = P.size();
    if (n < (closed ? 3u : 2u)) return {};
    const size_t segs = closed ? n : n - 1;
    const std::string suf = suffixFor(material);

    std::vector<std::optional<Vec3>> dir(segs);
    std::vector<Vec3> startOut(segs), endOut(segs);
    for (size_t i = 0; i < segs; ++i) {
        dir[i] = planDir(P[i], P[(i + 1) % n]);
        if (dir[i]) {
            startOut[i] = -*dir[i];  // flat caps unless a joint replaces them below
            endOut[i] = *dir[i];
        }
    }
    for (size_t v = 0; v < n; ++v) {
        const bool hasIn = closed || v > 0;
        const bool hasOut = closed || v < segs;
        if (!hasIn || !hasOut) continue;
        const size_t in = (v + segs - 1) % segs;
        const size_t out = v % segs;
        if (dir[in] && dir[out]) jointNormals(*dir[in], *dir[out], endOut[in], startOut[out]);
    }

    std::vector<Primitive> out;
    for (size_t i = 0; i < segs; ++i) {
        if (!dir[i]) continue;
        const ConvexBrush b = wallBrush(P[i], P[(i + 1) % n], *dir[i], startOut[i], endOut[i], 0.5 * thickness,
                                        height, suf);
        if (auto prim = makeBrushPrimitive(b, kEol)) out.push_back(std::move(*prim));
    }
    return out;
}

std::vector<Primitive> postsAlongSpline(const std::vector<Vec3>& pts, double spacing, double size, double height,
                                        const std::string& material) {
    if (!(spacing > 0) || !(size > 0) || !(height > 0) || !std::isfinite(spacing) || !std::isfinite(size) ||
        !std::isfinite(height) || !validMaterial(material) || !allFinite(pts))
        return {};
    const std::vector<Vec3> P = dedupe(pts, false);
    if (P.size() < 2) return {};

    std::vector<double> cum(P.size(), 0.0);
    for (size_t i = 1; i < P.size(); ++i) cum[i] = cum[i - 1] + length(P[i] - P[i - 1]);
    const double total = cum.back();
    const double count = std::floor(total / spacing + 1e-9);
    if (count > static_cast<double>(kMaxPosts)) return {};

    const std::string suf = suffixFor(material);
    const double half = 0.5 * size;
    std::vector<Primitive> out;
    size_t seg = 0;
    for (long k = 0; k <= static_cast<long>(count); ++k) {
        const double s = std::min(k * spacing, total);
        while (seg + 2 < P.size() && cum[seg + 1] < s) ++seg;
        const double len = cum[seg + 1] - cum[seg];
        const double f = len > 0 ? std::clamp((s - cum[seg]) / len, 0.0, 1.0) : 0.0;
        const Vec3 c = P[seg] + (P[seg + 1] - P[seg]) * f;
        AABB box;
        box.mins = {c.x - half, c.y - half, c.z};
        box.maxs = {c.x + half, c.y + half, c.z + height};
        if (auto prim = makeBrushPrimitive(makeBox(box, suf), kEol)) out.push_back(std::move(*prim));
    }
    return out;
}

std::vector<Primitive> pipeAlongSpline(const std::vector<Vec3>& pts, double radius, int sides,
                                       const std::string& material) {
    if (!(radius > 0) || !std::isfinite(radius) || sides < 4 || sides > 16 || !validMaterial(material) ||
        !allFinite(pts))
        return {};
    const std::vector<Vec3> P = dedupe(pts, false);
    if (P.size() < 2) return {};
    const size_t segs = P.size() - 1;
    const std::string suf = suffixFor(material);

    std::vector<Vec3> u(segs), startOut(segs), endOut(segs);
    for (size_t i = 0; i < segs; ++i) {
        u[i] = normalize(P[i + 1] - P[i]);
        startOut[i] = -u[i];
        endOut[i] = u[i];
    }
    for (size_t v = 1; v < segs; ++v) jointNormals(u[v - 1], u[v], endOut[v - 1], startOut[v]);

    // Side planes are tangent to a circle of apothem radius * cos(pi / sides), so the polygon's vertices lie on
    // the circle of `radius`.
    const double apothem = radius * std::cos(kPi / sides);
    std::vector<Primitive> out;
    for (size_t i = 0; i < segs; ++i) {
        const Vec3& d = u[i];
        const Vec3 ref = std::fabs(d.z) < 0.9 ? Vec3{0, 0, 1} : Vec3{1, 0, 0};
        const Vec3 e1 = normalize(cross(d, ref));
        const Vec3 e2 = cross(d, e1);
        ConvexBrush b;
        for (int k = 0; k < sides; ++k) {
            const double th = 2.0 * kPi * k / sides;
            const Vec3 nrm = e1 * std::cos(th) + e2 * std::sin(th);
            b.push_back({{nrm, dot(nrm, P[i]) + apothem}, suf});
        }
        b.push_back({{startOut[i], dot(startOut[i], P[i])}, suf});
        b.push_back({{endOut[i], dot(endOut[i], P[i + 1])}, suf});
        if (auto prim = makeBrushPrimitive(b, kEol)) out.push_back(std::move(*prim));
    }
    return out;
}

}  // namespace iw8r
