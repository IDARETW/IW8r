#include "geometry.h"

#include <algorithm>
#include <limits>

namespace iw8r {

std::optional<Plane> planeFromPoints(const Vec3& p0, const Vec3& p1, const Vec3& p2) {
    const Vec3 n = cross(p0 - p1, p2 - p1);
    const double len = length(n);
    if (!(len > 1e-12)) return std::nullopt;
    Plane pl;
    pl.n = n * (1.0 / len);
    pl.d = dot(pl.n, p1);
    return pl;
}

void AABB::add(const Vec3& p) {
    mins.x = std::min(mins.x, p.x); mins.y = std::min(mins.y, p.y); mins.z = std::min(mins.z, p.z);
    maxs.x = std::max(maxs.x, p.x); maxs.y = std::max(maxs.y, p.y); maxs.z = std::max(maxs.z, p.z);
}

void AABB::add(const AABB& b) {
    if (!b.valid()) return;
    add(b.mins);
    add(b.maxs);
}

bool AABB::contains(const Vec3& p, double eps) const {
    return p.x >= mins.x - eps && p.x <= maxs.x + eps && p.y >= mins.y - eps && p.y <= maxs.y + eps &&
           p.z >= mins.z - eps && p.z <= maxs.z + eps;
}

namespace {

constexpr double kWorldHalf = 262144.0;  // generous: IW maps use +-131072
constexpr double kClipEps = 0.01;

Winding baseWinding(const Plane& pl) {
    // Pick the major axis to build a basis on the plane.
    Vec3 up{0, 0, 1};
    if (std::fabs(pl.n.z) > 0.9) up = {1, 0, 0};
    Vec3 right = normalize(cross(up, pl.n));
    up = cross(pl.n, right);
    const Vec3 org = pl.n * pl.d;
    right = right * kWorldHalf;
    up = up * kWorldHalf;
    return {org - right + up, org + right + up, org + right - up, org - right - up};
}

// Keeps the part of w behind plane (dot(n,x) - d <= 0).
Winding clipBehind(const Winding& w, const Plane& pl) {
    if (w.empty()) return w;
    std::vector<double> dist(w.size());
    bool anyFront = false, anyBack = false;
    for (size_t i = 0; i < w.size(); ++i) {
        dist[i] = dot(pl.n, w[i]) - pl.d;
        if (dist[i] > kClipEps) anyFront = true;
        else if (dist[i] < -kClipEps) anyBack = true;
    }
    if (!anyFront) return w;
    if (!anyBack) return {};
    Winding out;
    out.reserve(w.size() + 2);
    for (size_t i = 0; i < w.size(); ++i) {
        const Vec3& a = w[i];
        const Vec3& b = w[(i + 1) % w.size()];
        const double da = dist[i], db = dist[(i + 1) % w.size()];
        if (da <= kClipEps) out.push_back(a);
        if ((da > kClipEps && db < -kClipEps) || (da < -kClipEps && db > kClipEps)) {
            const double t = da / (da - db);
            out.push_back(a + (b - a) * t);
        }
    }
    if (out.size() < 3) return {};
    return out;
}

}  // namespace

std::vector<Winding> buildBrushWindings(const std::vector<Plane>& planes) {
    std::vector<Winding> result(planes.size());
    for (size_t i = 0; i < planes.size(); ++i) {
        Winding w = baseWinding(planes[i]);
        for (size_t j = 0; j < planes.size() && !w.empty(); ++j) {
            if (i == j) continue;
            // Skip exact duplicates of this plane to avoid clipping a face away.
            if (dot(planes[i].n, planes[j].n) > 0.999999 && std::fabs(planes[i].d - planes[j].d) < 0.01) {
                if (j < i) { w.clear(); }  // keep the first copy only
                continue;
            }
            w = clipBehind(w, planes[j]);
        }
        result[i] = std::move(w);
    }
    return result;
}

std::optional<double> rayConvex(const Ray& ray, const std::vector<Plane>& planes) {
    double tEnter = -std::numeric_limits<double>::infinity();
    double tExit = std::numeric_limits<double>::infinity();
    for (const Plane& pl : planes) {
        const double denom = dot(pl.n, ray.dir);
        const double distOrigin = dot(pl.n, ray.origin) - pl.d;
        if (std::fabs(denom) < 1e-12) {
            if (distOrigin > 0) return std::nullopt;  // parallel and outside
            continue;
        }
        const double t = -distOrigin / denom;
        if (denom < 0) tEnter = std::max(tEnter, t);
        else tExit = std::min(tExit, t);
        if (tEnter > tExit) return std::nullopt;
    }
    if (tExit < 0) return std::nullopt;
    return std::max(tEnter, 0.0);
}

std::optional<double> rayAABB(const Ray& ray, const AABB& box) {
    double t0 = -std::numeric_limits<double>::infinity();
    double t1 = std::numeric_limits<double>::infinity();
    for (int a = 0; a < 3; ++a) {
        const double o = ray.origin[a], d = ray.dir[a];
        if (std::fabs(d) < 1e-12) {
            if (o < box.mins[a] || o > box.maxs[a]) return std::nullopt;
            continue;
        }
        double ta = (box.mins[a] - o) / d, tb = (box.maxs[a] - o) / d;
        if (ta > tb) std::swap(ta, tb);
        t0 = std::max(t0, ta);
        t1 = std::min(t1, tb);
        if (t0 > t1) return std::nullopt;
    }
    if (t1 < 0) return std::nullopt;
    return std::max(t0, 0.0);
}

double snapToGrid(double v, double grid) {
    if (!(grid > 0)) return v;
    return std::round(v / grid) * grid;
}

Vec3 snapToGrid(const Vec3& v, double grid) {
    return {snapToGrid(v.x, grid), snapToGrid(v.y, grid), snapToGrid(v.z, grid)};
}

}  // namespace iw8r
