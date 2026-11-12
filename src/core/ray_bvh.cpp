#include "ray_bvh.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace iw8r {

namespace {

constexpr uint32_t kLeafSize = 4;

bool rayBox(const AABB& b, const Vec3& o, const Vec3& inv, double tMin, double tMax) {
    for (int a = 0; a < 3; ++a) {
        double t0 = (b.mins[a] - o[a]) * inv[a];
        double t1 = (b.maxs[a] - o[a]) * inv[a];
        if (t0 > t1) std::swap(t0, t1);
        tMin = std::max(tMin, t0);
        tMax = std::min(tMax, t1);
        if (tMax < tMin) return false;
    }
    return true;
}

// Moller-Trumbore, two-sided.
bool rayTriangle(const RayBvh::Triangle& tr, const Vec3& o, const Vec3& d, double tMin, double tMax, double& t, double& u, double& v) {
    const Vec3 e1 = tr.b - tr.a, e2 = tr.c - tr.a;
    const Vec3 p = cross(d, e2);
    const double det = dot(e1, p);
    if (std::fabs(det) < 1e-12) return false;
    const double inv = 1.0 / det;
    const Vec3 s = o - tr.a;
    u = dot(s, p) * inv;
    if (u < 0.0 || u > 1.0) return false;
    const Vec3 q = cross(s, e1);
    v = dot(d, q) * inv;
    if (v < 0.0 || u + v > 1.0) return false;
    t = dot(e2, q) * inv;
    return t > tMin && t < tMax;
}

}  // namespace

void RayBvh::build(std::vector<Triangle> triangles) {
    tris_ = std::move(triangles);
    nodes_.clear();
    if (tris_.empty()) return;
    std::vector<Vec3> centroids(tris_.size());
    for (size_t i = 0; i < tris_.size(); ++i) centroids[i] = (tris_[i].a + tris_[i].b + tris_[i].c) * (1.0 / 3.0);
    nodes_.reserve(tris_.size() / 2 + 1);
    buildNode(0, uint32_t(tris_.size()), centroids);
}

uint32_t RayBvh::buildNode(uint32_t first, uint32_t count, std::vector<Vec3>& centroids) {
    const uint32_t index = uint32_t(nodes_.size());
    nodes_.emplace_back();
    AABB box, cbox;
    for (uint32_t i = first; i < first + count; ++i) {
        box.add(tris_[i].a);
        box.add(tris_[i].b);
        box.add(tris_[i].c);
        cbox.add(centroids[i]);
    }
    nodes_[index].box = box;
    const Vec3 ext = cbox.maxs - cbox.mins;
    const int axis = ext.x >= ext.y && ext.x >= ext.z ? 0 : (ext.y >= ext.z ? 1 : 2);
    if (count <= kLeafSize || ext[axis] <= 1e-9) {
        nodes_[index].first = first;
        nodes_[index].count = count;
        return index;
    }
    // Median split on the longest centroid axis (triangles and their centroids move together).
    std::vector<uint32_t> order(count);
    std::iota(order.begin(), order.end(), first);
    const uint32_t mid = count / 2;
    std::nth_element(order.begin(), order.begin() + mid, order.end(),
                     [&](uint32_t x, uint32_t y) { return centroids[x][axis] < centroids[y][axis]; });
    std::vector<Triangle> t(count);
    std::vector<Vec3> c(count);
    for (uint32_t i = 0; i < count; ++i) {
        t[i] = tris_[order[i]];
        c[i] = centroids[order[i]];
    }
    std::copy(t.begin(), t.end(), tris_.begin() + first);
    std::copy(c.begin(), c.end(), centroids.begin() + first);
    const uint32_t left = buildNode(first, mid, centroids);
    const uint32_t right = buildNode(first + mid, count - mid, centroids);
    nodes_[index].left = left;
    nodes_[index].right = right;
    return index;
}

template <bool AnyHit>
bool RayBvh::trace(const Vec3& o, const Vec3& d, double tMin, double tMax, Hit* hit) const {
    if (nodes_.empty()) return false;
    const auto inv1 = [](double x) { return std::fabs(x) < 1e-30 ? (x < 0 ? -1e30 : 1e30) : 1.0 / x; };
    const Vec3 inv{inv1(d.x), inv1(d.y), inv1(d.z)};
    uint32_t stack[128];
    int sp = 0;
    stack[sp++] = 0;
    bool found = false;
    double best = tMax;
    while (sp > 0) {
        const Node& n = nodes_[stack[--sp]];
        if (!rayBox(n.box, o, inv, tMin, best)) continue;
        if (n.count) {
            for (uint32_t i = n.first; i < n.first + n.count; ++i) {
                double t, u, v;
                if (!rayTriangle(tris_[i], o, d, tMin, best, t, u, v)) continue;
                if (AnyHit) return true;
                found = true;
                best = t;
                hit->t = t;
                hit->tri = i;
                hit->u = u;
                hit->v = v;
            }
        } else if (sp + 2 <= 128) {
            stack[sp++] = n.right;
            stack[sp++] = n.left;
        }
    }
    return found;
}

bool RayBvh::intersect(const Vec3& origin, const Vec3& dir, double tMin, double tMax, Hit& hit) const {
    return trace<false>(origin, dir, tMin, tMax, &hit);
}

bool RayBvh::occluded(const Vec3& origin, const Vec3& dir, double tMin, double tMax) const {
    return trace<true>(origin, dir, tMin, tMax, nullptr);
}

}  // namespace iw8r
