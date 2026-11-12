#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

#include "draw_chunks.h"
#include "frustum.h"
#include "test_harness.h"

using namespace iw8r;

namespace {

constexpr double kPi = 3.14159265358979323846;

// The editor camera's view-projection (src/viewport/camera.cpp viewProjection), row-vector convention.
void cameraViewProj(const Vec3& e, double yawDeg, double pitchDeg, double fovDeg, double aspect, float out[16]) {
    const double p = pitchDeg * kPi / 180, y = yawDeg * kPi / 180;
    const Vec3 f{std::cos(p) * std::cos(y), std::cos(p) * std::sin(y), std::sin(p)};
    const Vec3 r = normalize(cross(f, Vec3{0, 0, 1}));
    const Vec3 u = cross(r, f);
    const double view[16] = {r.x, u.x, f.x, 0, r.y, u.y, f.y, 0, r.z, u.z, f.z, 0, -dot(r, e), -dot(u, e), -dot(f, e), 1};
    const double zn = 4.0, zf = 131072.0;
    const double tx = std::tan(fovDeg * kPi / 360);
    const double xs = 1.0 / tx, ys = aspect / tx;
    const double proj[16] = {xs, 0, 0, 0, 0, ys, 0, 0, 0, 0, zf / (zf - zn), 1, 0, 0, -zn * zf / (zf - zn), 0};
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) {
            double s = 0;
            for (int k = 0; k < 4; ++k) s += view[i * 4 + k] * proj[k * 4 + j];
            out[i * 4 + j] = float(s);
        }
}

double planeDist(const Plane& pl, const Vec3& x) { return dot(pl.n, x) - pl.d; }

}  // namespace

TEST(draw_chunks_frustum_from_view_proj_matches_camera) {
    const Vec3 eye{100, -200, 64};
    float m[16];
    cameraViewProj(eye, 30, -10, 90, 16.0 / 9.0, m);
    const Frustum a = frustumFromViewProj(m);
    const Frustum b = makeFrustum(eye, 30, -10, 90, 16.0 / 9.0, 4, 131072);
    std::mt19937 rng(7);
    std::uniform_real_distribution<double> d(-3000, 3000);
    int agree = 0, total = 0;
    for (int i = 0; i < 4000; ++i) {
        const Vec3 p{eye.x + d(rng), eye.y + d(rng), eye.z + d(rng) * 0.3};
        // Skip points within 2 units of any plane (float matrix rounding).
        bool nearPlane = false;
        for (const Plane& pl : b.planes) nearPlane |= std::fabs(planeDist(pl, p)) < 2;
        if (nearPlane) continue;
        ++total;
        agree += frustumContainsPoint(a, p) == frustumContainsPoint(b, p);
    }
    CHECK(total > 3000 && agree == total);
}

TEST(draw_chunks_order_and_bounds) {
    std::mt19937 rng(3);
    std::uniform_real_distribution<double> d(-5000, 5000);
    std::vector<Vec3> corners;
    for (int t = 0; t < 10000; ++t) {
        const Vec3 c{d(rng), d(rng), d(rng) * 0.1};
        for (int k = 0; k < 3; ++k) corners.push_back(c + Vec3{double(k) * 8, double(k == 2) * 8, 0});
    }
    std::vector<uint32_t> order;
    std::vector<DrawChunk> chunks;
    buildDrawChunks(corners, 512, order, chunks);
    CHECK(order.size() == 10000 && chunks.size() == 20);
    std::vector<uint32_t> sorted = order;
    std::sort(sorted.begin(), sorted.end());
    bool perm = true;
    for (uint32_t i = 0; i < sorted.size(); ++i) perm &= sorted[i] == i;
    CHECK(perm);
    double chunkVolume = 0;
    bool inside = true;
    for (const DrawChunk& ch : chunks) {
        for (uint32_t i = ch.firstTriangle; i < ch.firstTriangle + ch.triangleCount; ++i)
            for (int k = 0; k < 3; ++k) inside &= ch.bounds.contains(corners[3 * size_t(order[i]) + size_t(k)], 1e-9);
        const Vec3 s = ch.bounds.maxs - ch.bounds.mins;
        chunkVolume += s.x * s.y;
    }
    CHECK(inside);
    // Spatial order: the chunks' areas add up to far less than 20 times the whole spread (10000 x 10000).
    CHECK(chunkVolume < 0.5 * 20 * 1e8);
    std::vector<uint32_t> o2;
    std::vector<DrawChunk> c2;
    buildDrawChunks({}, 512, o2, c2);
    CHECK(o2.empty() && c2.empty());
}

TEST(draw_chunks_visible_ranges_merge_and_cull) {
    // Four chunks along +X; the camera at the origin looks down +X with a narrow view: the chunk behind is culled.
    std::vector<DrawChunk> chunks(4);
    const double xs[4] = {-500, 200, 400, 600};
    for (int i = 0; i < 4; ++i) {
        chunks[size_t(i)].firstTriangle = uint32_t(i * 10);
        chunks[size_t(i)].triangleCount = 10;
        chunks[size_t(i)].bounds.add(Vec3{xs[i] - 50, -50, -50});
        chunks[size_t(i)].bounds.add(Vec3{xs[i] + 50, 50, 50});
    }
    const Frustum fr = makeFrustum(Vec3{0, 0, 0}, 0, 0, 60, 1, 4, 131072);
    size_t culled = 0;
    const auto r = visibleDrawRanges(fr, chunks, &culled);
    CHECK(culled == 1 && r.size() == 1 && r[0].firstTriangle == 10 && r[0].triangleCount == 30);
}
