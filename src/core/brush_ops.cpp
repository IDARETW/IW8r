#include "brush_ops.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace iw8r {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kMinArea = 1e-6;     // face area below this does not contribute
constexpr double kMinVolume = 1e-6;   // solid volume below this is degenerate
constexpr double kMaxCoord = 200000;  // vertices beyond this mean the region is unbounded
constexpr double kSnapEps = 1e-7;     // distance below which two winding vertices are the same point

std::vector<Plane> planesOf(const ConvexBrush& b) {
    std::vector<Plane> out;
    out.reserve(b.size());
    for (const auto& f : b) out.push_back(f.plane);
    return out;
}

// Newell vector: magnitude is twice the polygon area, direction is the winding normal.
Vec3 newell(const Winding& w) {
    Vec3 n;
    for (size_t i = 0; i < w.size(); ++i) n += cross(w[i], w[(i + 1) % w.size()]);
    return n;
}

// Removes duplicate vertices, orients the winding with the outward normal and rejects slivers.
Winding orientWinding(const Winding& in, const Vec3& n) {
    Winding w;
    for (const Vec3& p : in)
        if (w.empty() || length(p - w.back()) > kSnapEps) w.push_back(p);
    while (w.size() > 1 && length(w.front() - w.back()) <= kSnapEps) w.pop_back();
    if (w.size() < 3) return {};
    if (dot(newell(w), n) < 0) std::reverse(w.begin(), w.end());
    if (0.5 * length(newell(w)) < kMinArea) return {};
    return w;
}

std::vector<Winding> windingsFor(const std::vector<Plane>& planes) {
    std::vector<Winding> raw = buildBrushWindings(planes);
    for (size_t i = 0; i < raw.size(); ++i) raw[i] = raw[i].empty() ? Winding{} : orientWinding(raw[i], planes[i].n);
    return raw;
}

// Keeps only the faces that contribute to the solid, in their original order.
ConvexBrush reduced(const ConvexBrush& b) {
    const auto ws = windingsFor(planesOf(b));
    ConvexBrush out;
    for (size_t i = 0; i < b.size(); ++i)
        if (!ws[i].empty()) out.push_back(b[i]);
    return out;
}

size_t contributingCount(const ConvexBrush& b) {
    size_t n = 0;
    for (const auto& w : brushWindings(b)) n += !w.empty();
    return n;
}

// Radiant face suffixes start with whitespace; keep the text well formed.
std::string faceSuffix(const std::string& s) {
    if (s.empty() || s[0] == ' ' || s[0] == '\t') return s;
    return " " + s;
}

std::string fmtCoord(double v) {
    const double r = std::round(v);
    if (std::fabs(v - r) < kSnapEps) return formatNumber(r);
    return formatNumber(v);
}

std::string fmtPoint(const Vec3& p) {
    return "( " + fmtCoord(p.x) + " " + fmtCoord(p.y) + " " + fmtCoord(p.z) + " )";
}

// Three vertices of a winding with the largest triangle, labelled so planeFromPoints gives `n`.
std::array<Vec3, 3> pickTriple(const Winding& w, const Vec3& n) {
    size_t a = 0;
    size_t b = 0;
    double bestDist = -1;
    for (size_t i = 0; i < w.size(); ++i) {
        const double dd = length(w[i] - w[a]);
        if (dd > bestDist) { bestDist = dd; b = i; }
    }
    size_t c = 0;
    double bestArea = -1;
    for (size_t i = 0; i < w.size(); ++i) {
        const double ar = length(cross(w[b] - w[a], w[i] - w[a]));
        if (ar > bestArea) { bestArea = ar; c = i; }
    }
    Vec3 p0 = w[a], p1 = w[b], p2 = w[c];
    if (dot(cross(p0 - p1, p2 - p1), n) < 0) std::swap(p0, p2);
    return {p0, p1, p2};
}

// Exact trig for quarter turns so axis-aligned rotations do not pick up round-off.
void trigOf(double degrees, double& c, double& s) {
    const double q = degrees / 90.0;
    if (std::fabs(q - std::round(q)) < 1e-12) {
        const long long k = ((std::llround(q) % 4) + 4) % 4;
        static const double cs[4] = {1, 0, -1, 0};
        static const double sn[4] = {0, 1, 0, -1};
        c = cs[k];
        s = sn[k];
        return;
    }
    const double rad = degrees * kPi / 180.0;
    c = std::cos(rad);
    s = std::sin(rad);
}

Vec3 rotateVec(const Vec3& v, int axis, double c, double s) {
    switch (axis) {
        case 0: return {v.x, c * v.y - s * v.z, s * v.y + c * v.z};
        case 1: return {c * v.x + s * v.z, v.y, -s * v.x + c * v.z};
        default: return {c * v.x - s * v.y, s * v.x + c * v.y, v.z};
    }
}

}  // namespace

ConvexBrush brushFromPrimitive(const Primitive& p) {
    ConvexBrush out;
    out.reserve(p.faces.size());
    for (const auto& f : p.faces) {
        auto pl = planeFromPoints(f.p[0], f.p[1], f.p[2]);
        if (!pl) continue;
        out.push_back({*pl, f.suffix});
    }
    return out;
}

std::vector<Winding> brushWindings(const ConvexBrush& b) { return windingsFor(planesOf(b)); }

std::optional<Plane> facePlane(const Primitive& p, size_t face) {
    if (face >= p.faces.size()) return std::nullopt;
    const auto& f = p.faces[face];
    return planeFromPoints(f.p[0], f.p[1], f.p[2]);
}

std::vector<Winding> faceWindingsOf(const Primitive& p) {
    std::vector<Winding> out(p.faces.size());
    std::vector<Plane> planes;
    std::vector<size_t> owner;
    for (size_t i = 0; i < p.faces.size(); ++i)
        if (auto pl = facePlane(p, i)) {
            planes.push_back(*pl);
            owner.push_back(i);
        }
    std::vector<Winding> w = windingsFor(planes);
    for (size_t k = 0; k < w.size() && k < owner.size(); ++k) out[owner[k]] = std::move(w[k]);
    return out;
}

double brushVolume(const ConvexBrush& b) {
    double v = 0;
    for (const Winding& w : brushWindings(b)) {
        if (w.size() < 3) continue;
        for (size_t i = 1; i + 1 < w.size(); ++i) v += dot(w[0], cross(w[i], w[i + 1]));
    }
    return v / 6.0;
}

bool isValidBrush(const ConvexBrush& b) {
    const auto ws = brushWindings(b);
    size_t n = 0;
    for (const Winding& w : ws) {
        if (w.empty()) continue;
        ++n;
        for (const Vec3& p : w)
            if (std::fabs(p.x) > kMaxCoord || std::fabs(p.y) > kMaxCoord || std::fabs(p.z) > kMaxCoord) return false;
    }
    if (n < 4) return false;
    return brushVolume(b) > kMinVolume;
}

AABB brushBounds(const ConvexBrush& b) {
    AABB box;
    for (const Winding& w : brushWindings(b))
        for (const Vec3& p : w) box.add(p);
    return box;
}

std::optional<Primitive> makeBrushPrimitive(const ConvexBrush& b, const std::string& eol, const std::string& comment) {
    if (!isValidBrush(b)) return std::nullopt;
    const ConvexBrush faces = reduced(b);
    const auto ws = windingsFor(planesOf(faces));
    if (faces.empty()) return std::nullopt;

    Primitive prim;
    prim.kind = PrimKind::Brush;
    prim.dirty = true;
    if (!comment.empty()) prim.lines.push_back({comment, eol});
    prim.lines.push_back({"{", eol});
    for (size_t k = 0; k < faces.size(); ++k) {
        if (ws[k].empty()) return std::nullopt;
        const auto t = pickTriple(ws[k], faces[k].plane.n);
        const std::string text = " " + fmtPoint(t[0]) + " " + fmtPoint(t[1]) + " " + fmtPoint(t[2]) +
                                 faceSuffix(faces[k].suffix);
        prim.lines.push_back({text, eol});
    }
    prim.lines.push_back({"}", eol});
    reparsePrimitive(prim);

    // The text must describe the same planes and suffixes (guards against rounding in the 6-decimal output).
    if (prim.faces.size() != faces.size()) return std::nullopt;
    for (size_t k = 0; k < faces.size(); ++k) {
        const BrushFace& f = prim.faces[k];
        auto pl = planeFromPoints(f.p[0], f.p[1], f.p[2]);
        if (!pl || dot(pl->n, faces[k].plane.n) < 0.9999) return std::nullopt;
        for (const Vec3& p : f.p)
            if (std::fabs(dot(faces[k].plane.n, p) - faces[k].plane.d) > 1e-3) return std::nullopt;
        if (f.suffix != faceSuffix(faces[k].suffix)) return std::nullopt;
    }
    return prim;
}

ConvexBrush makeBox(const AABB& box, const std::string& suffix) {
    const std::string s = faceSuffix(suffix);
    return {
        {{{1, 0, 0}, box.maxs.x}, s},
        {{{-1, 0, 0}, -box.mins.x}, s},
        {{{0, 1, 0}, box.maxs.y}, s},
        {{{0, -1, 0}, -box.mins.y}, s},
        {{{0, 0, 1}, box.maxs.z}, s},
        {{{0, 0, -1}, -box.mins.z}, s},
    };
}

ConvexBrush makePrism(const AABB& box, int sides, int axis, const std::string& suffix) {
    if (sides < 3 || sides > 64 || axis < 0 || axis > 2 || !box.valid()) return {};
    const int u = (axis + 1) % 3, v = (axis + 2) % 3;
    const double ru = 0.5 * (box.maxs[u] - box.mins[u]);
    const double rv = 0.5 * (box.maxs[v] - box.mins[v]);
    if (!(ru > 0) || !(rv > 0)) return {};
    const Vec3 c = box.center();
    const std::string s = faceSuffix(suffix);

    ConvexBrush b;
    for (int i = 0; i < sides; ++i) {
        const double t0 = 2.0 * kPi * i / sides;
        const double t1 = 2.0 * kPi * (i + 1) / sides;
        const double tm = 0.5 * (t0 + t1);
        // Edge from vertex i to i+1 on the ellipse: the normal is the ellipse gradient at the edge midpoint.
        Vec3 m;
        m[u] = std::cos(tm) / ru;
        m[v] = std::sin(tm) / rv;
        const Vec3 n = normalize(m);
        Vec3 p = c;
        p[u] += ru * std::cos(t0);
        p[v] += rv * std::sin(t0);
        b.push_back({{n, dot(n, p)}, s});
    }
    Vec3 nMin, nMax;
    nMin[axis] = -1;
    nMax[axis] = 1;
    b.push_back({{nMin, -box.mins[axis]}, s});
    b.push_back({{nMax, box.maxs[axis]}, s});
    return b;
}

std::optional<ConvexBrush> clipBrush(const ConvexBrush& b, const Plane& keep, const std::string& capSuffix) {
    if (!isValidBrush(b)) return std::nullopt;
    const double len = length(keep.n);
    if (!(len > 0)) return std::nullopt;
    Plane k{keep.n * (1.0 / len), keep.d / len};
    ConvexBrush faces = b;
    faces.push_back({k, faceSuffix(capSuffix)});
    ConvexBrush out = reduced(faces);
    if (!isValidBrush(out)) return std::nullopt;
    return out;
}

std::vector<ConvexBrush> subtractBrush(const ConvexBrush& a, const ConvexBrush& cutterIn) {
    if (!isValidBrush(a)) return {};
    const ConvexBrush cutter = reduced(cutterIn);

    ConvexBrush overlap = a;
    overlap.insert(overlap.end(), cutter.begin(), cutter.end());
    if (!isValidBrush(overlap)) return {a};

    // Piece k is a inside cutter planes 1..k-1 and outside plane k. The pieces are disjoint and their
    // union is a minus the cutter.
    std::vector<ConvexBrush> pieces;
    ConvexBrush inside;
    for (const BrushFaceSpec& cf : cutter) {
        ConvexBrush piece = a;
        piece.insert(piece.end(), inside.begin(), inside.end());
        piece.push_back({{-cf.plane.n, -cf.plane.d}, cf.suffix});
        ConvexBrush r = reduced(piece);
        if (isValidBrush(r)) pieces.push_back(std::move(r));
        inside.push_back(cf);
    }
    return pieces;
}

std::vector<ConvexBrush> hollowBrush(const ConvexBrush& bIn, double thickness) {
    const ConvexBrush b = reduced(bIn);
    if (!(thickness > 0) || !isValidBrush(b)) return {};

    std::vector<ConvexBrush> pieces;
    ConvexBrush earlier;  // inner limits of the earlier faces: dot(n_j, x) <= d_j - t
    for (const BrushFaceSpec& f : b) {
        ConvexBrush piece = b;
        // Wall of face k: dot(n_k, x) >= d_k - t inside the brush.
        piece.push_back({{-f.plane.n, -(f.plane.d - thickness)}, f.suffix});
        piece.insert(piece.end(), earlier.begin(), earlier.end());
        ConvexBrush r = reduced(piece);
        if (isValidBrush(r)) pieces.push_back(std::move(r));
        earlier.push_back({{f.plane.n, f.plane.d - thickness}, f.suffix});
    }
    return pieces;
}

ConvexBrush translateBrush(const ConvexBrush& b, const Vec3& d) {
    ConvexBrush out = b;
    for (auto& f : out) f.plane.d += dot(f.plane.n, d);
    return out;
}

Vec3 rotatePoint(const Vec3& p, int axis, double degrees, const Vec3& pivot) {
    if (axis < 0 || axis > 2) return p;
    double c = 1, s = 0;
    trigOf(degrees, c, s);
    return pivot + rotateVec(p - pivot, axis, c, s);
}

ConvexBrush rotateBrush(const ConvexBrush& b, int axis, double degrees, const Vec3& pivot) {
    if (axis < 0 || axis > 2) return {};
    double c = 1, s = 0;
    trigOf(degrees, c, s);
    ConvexBrush out = b;
    for (auto& f : out) {
        const Vec3 n = rotateVec(f.plane.n, axis, c, s);
        const Vec3 p0 = pivot + rotateVec(f.plane.n * f.plane.d - pivot, axis, c, s);
        f.plane = {n, dot(n, p0)};
    }
    return out;
}

ConvexBrush flipBrush(const ConvexBrush& b, int axis, double about) {
    if (axis < 0 || axis > 2) return {};
    ConvexBrush out = b;
    for (auto& f : out) {
        Vec3 n = f.plane.n;
        n[axis] = -n[axis];
        Vec3 p0 = f.plane.n * f.plane.d;
        p0[axis] = 2.0 * about - p0[axis];
        f.plane = {n, dot(n, p0)};
    }
    return out;
}

ConvexBrush scaleBrush(const ConvexBrush& b, const Vec3& s, const Vec3& pivot) {
    if (!(s.x > 0 && s.y > 0 && s.z > 0)) return {};
    ConvexBrush out = b;
    for (auto& f : out) {
        const Vec3 n = normalize(Vec3{f.plane.n.x / s.x, f.plane.n.y / s.y, f.plane.n.z / s.z});
        const Vec3 p0 = f.plane.n * f.plane.d;
        const Vec3 q = pivot + Vec3{(p0.x - pivot.x) * s.x, (p0.y - pivot.y) * s.y, (p0.z - pivot.z) * s.z};
        f.plane = {n, dot(n, q)};
    }
    return out;
}

std::optional<ConvexBrush> moveFace(const ConvexBrush& b, size_t face, double distance) {
    if (face >= b.size()) return std::nullopt;
    ConvexBrush out = b;
    out[face].plane.d += distance;
    if (brushWindings(out)[face].empty()) return std::nullopt;
    if (!isValidBrush(out)) return std::nullopt;
    return out;
}

}  // namespace iw8r
