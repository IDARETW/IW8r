#include "brush_ops2.h"

#include <cmath>
#include <utility>

namespace iw8r {

namespace {

constexpr double kEps = 1e-6;           // distance below which points and planes coincide
constexpr double kMinArea = 1e-6;       // winding area below this is degenerate (as in brush_ops.cpp)
constexpr size_t kMaxHullPoints = 128;  // the hull search is O(n^4); larger merges are refused

bool nearPoint(const Vec3& a, const Vec3& b) { return length(a - b) <= kEps; }

bool samePlane(const Plane& a, const Plane& b) { return dot(a.n, b.n) > 0.999999 && std::fabs(a.d - b.d) < kEps; }

std::vector<Vec3> uniquePoints(const std::vector<Vec3>& in) {
    std::vector<Vec3> out;
    for (const Vec3& p : in) {
        bool dup = false;
        for (const Vec3& q : out)
            if (nearPoint(p, q)) {
                dup = true;
                break;
            }
        if (!dup) out.push_back(p);
    }
    return out;
}

// Supporting planes of the convex hull of `pts` (outward): every plane through three points that has all
// the other points on one side. Their intersection is the hull.
std::vector<Plane> hullPlanes(const std::vector<Vec3>& pts) {
    std::vector<Plane> out;
    const size_t n = pts.size();
    for (size_t i = 0; i < n; ++i)
        for (size_t j = i + 1; j < n; ++j)
            for (size_t k = j + 1; k < n; ++k) {
                Vec3 nn = cross(pts[j] - pts[i], pts[k] - pts[i]);
                const double len = length(nn);
                if (!(len > 1e-9)) continue;  // collinear triple
                nn = nn * (1.0 / len);
                double d = dot(nn, pts[i]);
                bool pos = false;
                bool neg = false;
                for (const Vec3& p : pts) {
                    const double s = dot(nn, p) - d;
                    if (s > kEps) pos = true;
                    else if (s < -kEps) neg = true;
                }
                if (pos == neg) continue;  // straddles the plane, or every point lies on it
                if (pos) {                 // points lie outside: flip so they all satisfy dot(n, x) <= d
                    nn = -nn;
                    d = -d;
                }
                const Plane pl{nn, d};
                bool seen = false;
                for (const Plane& q : out)
                    if (samePlane(q, pl)) {
                        seen = true;
                        break;
                    }
                if (!seen) out.push_back(pl);
            }
    return out;
}

// Keeps only the faces that contribute to the solid, in their original order.
ConvexBrush contributing(const ConvexBrush& b) {
    const auto ws = brushWindings(b);
    ConvexBrush out;
    for (size_t i = 0; i < b.size(); ++i)
        if (!ws[i].empty()) out.push_back(b[i]);
    return out;
}

}  // namespace

std::optional<ConvexBrush> mergeBrushes(const std::vector<ConvexBrush>& brushes) {
    if (brushes.empty()) return std::nullopt;
    std::vector<Vec3> pts;
    for (const ConvexBrush& b : brushes) {
        if (!isValidBrush(b)) return std::nullopt;
        for (const Winding& w : brushWindings(b)) pts.insert(pts.end(), w.begin(), w.end());
    }
    pts = uniquePoints(pts);
    if (pts.size() > kMaxHullPoints) return std::nullopt;

    const std::string fallback = brushes.front().front().suffix;
    ConvexBrush hull;
    for (const Plane& pl : hullPlanes(pts)) {
        std::string suffix = fallback;
        bool matched = false;
        for (const ConvexBrush& b : brushes) {
            for (const BrushFaceSpec& f : b)
                if (samePlane(f.plane, pl)) {
                    suffix = f.suffix;
                    matched = true;
                    break;
                }
            if (matched) break;
        }
        hull.push_back({pl, suffix});
    }
    const ConvexBrush out = contributing(hull);
    if (!isValidBrush(out)) return std::nullopt;

    // The hull equals the union only when no part of the hull lies outside every input brush.
    std::vector<ConvexBrush> rest{out};
    for (const ConvexBrush& b : brushes) {
        std::vector<ConvexBrush> next;
        for (const ConvexBrush& r : rest) {
            auto pieces = subtractBrush(r, b);
            next.insert(next.end(), pieces.begin(), pieces.end());
        }
        rest = std::move(next);
    }
    double gap = 0;
    for (const ConvexBrush& r : rest) gap += brushVolume(r);
    if (gap > kEps * brushVolume(out)) return std::nullopt;
    return out;
}

std::optional<Plane> clipPlaneFrom3Points(const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& keepSide) {
    auto pl = planeFromPoints(a, b, c);
    if (!pl) return std::nullopt;
    const double side = dot(pl->n, keepSide) - pl->d;
    if (std::fabs(side) <= 1e-9) return std::nullopt;
    if (side > 0) {  // keepSide is on the normal's side: flip so the kept half-space is dot(n, x) <= d
        pl->n = -pl->n;
        pl->d = -pl->d;
    }
    return pl;
}

std::optional<ConvexBrush> extrudeFace(const ConvexBrush& b, size_t face, double distance) {
    if (!(distance > 0) || face >= b.size() || !isValidBrush(b)) return std::nullopt;
    const auto ws = brushWindings(b);
    const Winding& w = ws[face];
    if (w.size() < 3) return std::nullopt;

    const double ln = length(b[face].plane.n);
    const Vec3 n = b[face].plane.n * (1.0 / ln);
    const double base = b[face].plane.d / ln;
    Vec3 c;
    for (const Vec3& p : w) c += p;
    c = c * (1.0 / static_cast<double>(w.size()));
    const std::string& suffix = b[face].suffix;

    ConvexBrush out;
    out.push_back({{-n, -base}, suffix});           // underside: the face itself, the solid lies outside b
    out.push_back({{n, base + distance}, suffix});  // cap: the extruded face
    for (size_t i = 0; i < w.size(); ++i) {
        const Vec3 a = w[i];
        const Vec3 e = w[(i + 1) % w.size()] - a;
        if (!(length(e) > kEps)) continue;
        Vec3 m = normalize(cross(e, n));  // outward in-plane normal of this edge
        if (dot(m, c - a) > 0) m = -m;
        const Plane side{m, dot(m, a)};
        bool seen = false;
        for (const BrushFaceSpec& f : out)
            if (samePlane(f.plane, side)) {
                seen = true;
                break;
            }
        if (!seen) out.push_back({side, suffix});
    }
    const ConvexBrush r = contributing(out);
    if (!isValidBrush(r)) return std::nullopt;
    return r;
}

std::vector<BrushEdge> brushEdges(const ConvexBrush& b) {
    std::vector<BrushEdge> out;
    for (const Winding& w : brushWindings(b)) {
        for (size_t i = 0; i < w.size(); ++i) {
            const Vec3& p = w[i];
            const Vec3& q = w[(i + 1) % w.size()];
            bool seen = false;
            for (const BrushEdge& e : out)
                if ((nearPoint(e.a, p) && nearPoint(e.b, q)) || (nearPoint(e.a, q) && nearPoint(e.b, p))) {
                    seen = true;
                    break;
                }
            if (!seen) out.push_back({p, q});
        }
    }
    return out;
}

std::optional<ConvexBrush> moveBrushEdge(const ConvexBrush& b, const BrushEdge& edge, const Vec3& delta) {
    if (!isValidBrush(b)) return std::nullopt;
    bool isEdge = false;
    for (const BrushEdge& e : brushEdges(b))
        if ((nearPoint(e.a, edge.a) && nearPoint(e.b, edge.b)) || (nearPoint(e.a, edge.b) && nearPoint(e.b, edge.a))) {
            isEdge = true;
            break;
        }
    if (!isEdge) return std::nullopt;

    const auto ws = brushWindings(b);
    std::vector<Winding> moved(ws.size());
    ConvexBrush out = b;
    for (size_t i = 0; i < ws.size(); ++i) {
        if (ws[i].empty()) continue;
        for (Vec3 p : ws[i]) {
            if (nearPoint(p, edge.a) || nearPoint(p, edge.b)) p += delta;
            moved[i].push_back(p);
        }
        const Winding& w = moved[i];
        Vec3 nn;
        for (size_t k = 0; k < w.size(); ++k) nn += cross(w[k], w[(k + 1) % w.size()]);
        const double twiceArea = length(nn);
        if (!(0.5 * twiceArea >= kMinArea)) return std::nullopt;  // collapsed onto a line or point
        nn = nn * (1.0 / twiceArea);
        if (dot(nn, b[i].plane.n) <= 0) return std::nullopt;  // face flipped, e.g. dragged through the opposite edge
        Vec3 c;
        for (const Vec3& p : w) c += p;
        c = c * (1.0 / static_cast<double>(w.size()));
        const double d = dot(nn, c);
        for (const Vec3& p : w)
            if (std::fabs(dot(nn, p) - d) > kEps) return std::nullopt;  // the moved face is no longer planar
        out[i].plane = {nn, d};
    }

    // Convex: every moved vertex lies inside every contributing face.
    for (size_t i = 0; i < ws.size(); ++i) {
        if (ws[i].empty()) continue;
        for (size_t j = 0; j < ws.size(); ++j)
            for (const Vec3& p : moved[j])
                if (dot(out[i].plane.n, p) - out[i].plane.d > kEps) return std::nullopt;
    }
    if (!isValidBrush(out)) return std::nullopt;
    return out;
}

}  // namespace iw8r
