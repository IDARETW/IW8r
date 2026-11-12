// IW8 Radiant Suite - iw3xo-style generators (see generators.h).
#include "generators.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "brush_ops.h"
#include "leak_check.h"

namespace iw8r {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kDeg = kPi / 180.0;
constexpr double kEps = 1e-6;
constexpr int kTubeSides = 8;

// splitmix64: small, fully specified generator so scatter results do not depend on the standard library.
class Rng {
public:
    explicit Rng(uint64_t seed) : s_(seed) {}
    uint64_t next() {
        s_ += 0x9E3779B97F4A7C15ull;
        uint64_t z = s_;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
    // Uniform in [0, 1) with 53 random bits.
    double uniform() { return static_cast<double>(next() >> 11) * (1.0 / 9007199254740992.0); }
    size_t index(size_t n) { return static_cast<size_t>(uniform() * static_cast<double>(n)) % n; }

private:
    uint64_t s_;
};

// XY point-in-polygon for a convex winding (any orientation), with a small tolerance.
bool insideWindingXY(const Winding& w, const Vec3& q) {
    double area2 = 0;
    for (size_t i = 0; i < w.size(); ++i) {
        const Vec3& a = w[i];
        const Vec3& b = w[(i + 1) % w.size()];
        area2 += a.x * b.y - b.x * a.y;
    }
    const double sign = area2 >= 0 ? 1.0 : -1.0;
    for (size_t i = 0; i < w.size(); ++i) {
        const Vec3& a = w[i];
        const Vec3& b = w[(i + 1) % w.size()];
        const double cross = (b.x - a.x) * (q.y - a.y) - (b.y - a.y) * (q.x - a.x);
        if (sign * cross < -kEps * std::max(1.0, length(b - a))) return false;
    }
    return true;
}

// Euler angles (degrees) that map the model's up axis onto the unit normal n (yaw is applied separately).
Vec3 anglesFromNormal(const Vec3& n, double yawDeg) {
    const double roll = std::asin(std::clamp(-n.y, -1.0, 1.0)) / kDeg;
    const double pitch = std::atan2(n.x, n.z) / kDeg;
    return {pitch, yawDeg, roll};
}

struct FloorFace {
    Vec3 n;
    double d;
    Winding w;
};

// Brush planes of a solid, as the plain list dropToFloor / rayConvex use.
std::vector<Plane> planesOf(const ConvexBrush& b) {
    std::vector<Plane> planes;
    planes.reserve(b.size());
    for (const BrushFaceSpec& f : b) planes.push_back(f.plane);
    return planes;
}

// True when p lies strictly inside every half-space of the brush.
bool strictlyInside(const std::vector<Plane>& planes, const Vec3& p) {
    for (const Plane& pl : planes)
        if (dot(pl.n, p) - pl.d > -kEps) return false;
    return true;
}

}  // namespace

std::vector<Vec3> ropeCurve(const Vec3& a, const Vec3& b, double sag, int segments) {
    const int n = std::max(1, segments);
    std::vector<Vec3> pts;
    pts.reserve(static_cast<size_t>(n) + 1);
    for (int i = 0; i <= n; ++i) {
        // (1 - t) * a + t * b keeps both endpoints bit-exact (t = 0 and t = 1).
        const double t = static_cast<double>(i) / n;
        Vec3 p = a * (1.0 - t) + b * t;
        p.z -= sag * 4.0 * t * (1.0 - t);
        pts.push_back(i == 0 ? a : (i == n ? b : p));
    }
    return pts;
}

PatchGrid ropePatch(const Vec3& a, const Vec3& b, double sag, double radius, int segments, const std::string& material) {
    PatchGrid g;
    if (segments < 2 || !(radius > 0) || !std::isfinite(radius)) return g;
    const std::vector<Vec3> curve = ropeCurve(a, b, sag, segments);
    const int rows = static_cast<int>(curve.size());
    const int cols = kTubeSides + 1;
    g.rows = rows;
    g.cols = cols;
    g.material = material;
    g.keyword = "curve";
    g.pos.resize(static_cast<size_t>(rows) * cols);
    for (int r = 0; r < rows; ++r) {
        const Vec3& prev = curve[static_cast<size_t>(std::max(0, r - 1))];
        const Vec3& next = curve[static_cast<size_t>(std::min(rows - 1, r + 1))];
        const Vec3 tangent = normalize(next - prev);
        // Frame perpendicular to the tangent; fall back to +X when the tangent is nearly vertical.
        Vec3 ref = std::fabs(tangent.z) > 0.99 ? Vec3{1, 0, 0} : Vec3{0, 0, 1};
        const Vec3 u = normalize(cross(tangent, ref));
        const Vec3 v = cross(tangent, u);
        for (int c = 0; c < cols; ++c) {
            const double th = 2.0 * kPi * (c % kTubeSides) / kTubeSides;
            g.pos[static_cast<size_t>(r) * cols + c] = curve[static_cast<size_t>(r)] + (u * std::cos(th) + v * std::sin(th)) * radius;
        }
    }
    return naturalTexture(g);
}

std::vector<Placement> scatterOnBrushes(const MapFile& map, const Vec3& center, const ScatterOptions& opt) {
    std::vector<Placement> out;
    if (!(opt.radius > 0) || !std::isfinite(opt.radius) || opt.count <= 0) return out;
    if (!std::isfinite(center.x) || !std::isfinite(center.y) || !std::isfinite(center.z)) return out;
    if (!(opt.maxSlopeDeg >= 0) || !(opt.minScale > 0) || !(opt.maxScale >= opt.minScale)) return out;
    if (!(opt.minSpacing >= 0)) return out;

    const double cosMax = std::cos(std::min(opt.maxSlopeDeg, 90.0) * kDeg);
    std::vector<FloorFace> faces;
    for (const SolidBrush& b : collidingBrushes(map, true)) {
        const std::vector<Winding> windings = brushWindings(b.planes);
        for (size_t i = 0; i < b.planes.size() && i < windings.size(); ++i) {
            const Winding& w = windings[i];
            if (w.size() < 3) continue;
            const Plane& pl = b.planes[i].plane;
            if (pl.n.z <= 0 || pl.n.z < cosMax - kEps) continue;  // upward faces within the slope limit
            AABB wb;
            for (const Vec3& p : w) wb.add(p);
            if (wb.maxs.x < center.x - opt.radius || wb.mins.x > center.x + opt.radius ||
                wb.maxs.y < center.y - opt.radius || wb.mins.y > center.y + opt.radius)
                continue;
            faces.push_back({pl.n, pl.d, w});
        }
    }
    if (faces.empty()) return out;

    Rng rng(opt.seed);
    const long long maxAttempts = static_cast<long long>(opt.count) * 400 + 1000;
    for (long long attempt = 0; attempt < maxAttempts && static_cast<int>(out.size()) < opt.count; ++attempt) {
        const FloorFace& f = faces[rng.index(faces.size())];
        const double r = opt.radius * std::sqrt(rng.uniform());
        const double th = 2.0 * kPi * rng.uniform();
        Vec3 q(center.x + r * std::cos(th), center.y + r * std::sin(th), 0);
        if (!insideWindingXY(f.w, q)) continue;
        q.z = (f.d - f.n.x * q.x - f.n.y * q.y) / f.n.z;

        bool spaced = true;
        for (const Placement& p : out) {
            const double dx = p.origin.x - q.x, dy = p.origin.y - q.y;
            if (std::sqrt(dx * dx + dy * dy) < opt.minSpacing) {
                spaced = false;
                break;
            }
        }
        if (!spaced) continue;

        Placement pl;
        pl.origin = q;
        pl.scale = opt.minScale + (opt.maxScale - opt.minScale) * rng.uniform();
        const double yaw = opt.randomYaw ? 360.0 * rng.uniform() : 0.0;
        pl.angles = opt.alignToSurface ? anglesFromNormal(normalize(f.n), yaw) : Vec3{0, yaw, 0};
        out.push_back(pl);
    }
    return out;
}

std::optional<Vec3> dropToFloor(const MapFile& map, const Vec3& p, double maxDrop) {
    if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) return std::nullopt;
    if (!(maxDrop >= 0)) return std::nullopt;
    const Ray ray{p, {0, 0, -1}};
    std::optional<double> best;
    for (const SolidBrush& b : collidingBrushes(map, true)) {
        if (!b.bounds.valid()) continue;
        if (p.x < b.bounds.mins.x || p.x > b.bounds.maxs.x || p.y < b.bounds.mins.y || p.y > b.bounds.maxs.y) continue;
        const std::vector<Plane> planes = planesOf(b.planes);
        if (strictlyInside(planes, p)) continue;
        const std::optional<double> t = rayConvex(ray, planes);
        if (!t || *t > maxDrop) continue;
        if (!best || *t < *best) best = *t;
    }
    if (!best) return std::nullopt;
    return Vec3{p.x, p.y, p.z - *best};
}

}  // namespace iw8r
