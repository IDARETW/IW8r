#include "draw_chunks.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>

namespace iw8r {
namespace {

uint64_t spread3(uint64_t x) {  // 10 bits -> every third bit of 30
    x &= 0x3ff;
    x = (x | (x << 16)) & 0x30000ff;
    x = (x | (x << 8)) & 0x300f00f;
    x = (x | (x << 4)) & 0x30c30c3;
    x = (x | (x << 2)) & 0x9249249;
    return x;
}

}  // namespace

void buildDrawChunks(const std::vector<Vec3>& corners, size_t maxTriangles, std::vector<uint32_t>& order,
                     std::vector<DrawChunk>& chunks) {
    order.clear();
    chunks.clear();
    const size_t n = corners.size() / 3;
    if (n == 0) return;
    maxTriangles = std::max<size_t>(1, maxTriangles);
    std::vector<Vec3> c(n);
    AABB all;
    for (size_t t = 0; t < n; ++t) {
        c[t] = (corners[3 * t] + corners[3 * t + 1] + corners[3 * t + 2]) * (1.0 / 3.0);
        all.add(c[t]);
    }
    const Vec3 size = all.maxs - all.mins;
    const double ext = std::max({size.x, size.y, size.z, 1e-9});
    std::vector<uint64_t> key(n);
    for (size_t t = 0; t < n; ++t) {
        const auto q = [&](double v, double lo) { return uint64_t(std::clamp((v - lo) / ext * 1023.0, 0.0, 1023.0)); };
        key[t] = spread3(q(c[t].x, all.mins.x)) | (spread3(q(c[t].y, all.mins.y)) << 1) | (spread3(q(c[t].z, all.mins.z)) << 2);
    }
    order.resize(n);
    std::iota(order.begin(), order.end(), 0u);
    std::stable_sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b) { return key[a] < key[b]; });
    for (size_t first = 0; first < n; first += maxTriangles) {
        DrawChunk ch;
        ch.firstTriangle = uint32_t(first);
        ch.triangleCount = uint32_t(std::min(maxTriangles, n - first));
        for (size_t i = first; i < first + ch.triangleCount; ++i)
            for (int k = 0; k < 3; ++k) ch.bounds.add(corners[3 * size_t(order[i]) + size_t(k)]);
        chunks.push_back(ch);
    }
}

Frustum frustumFromViewProj(const float m[16]) {
    // clip = v * M: clip_j = sum_i v_i * m[i * 4 + j]. Column j = (m[j], m[4 + j], m[8 + j], m[12 + j]).
    const auto col = [&](int j) { return std::array<double, 4>{m[j], m[4 + j], m[8 + j], m[12 + j]}; };
    const auto c0 = col(0), c1 = col(1), c2 = col(2), c3 = col(3);
    // Inside when a.x + b.y + c.z + d >= 0 for: z >= 0 (near), w - z, w + x, w - x, w - y, w + y.
    const std::array<std::array<double, 4>, 6> in = {{
        {c2[0], c2[1], c2[2], c2[3]},
        {c3[0] - c2[0], c3[1] - c2[1], c3[2] - c2[2], c3[3] - c2[3]},
        {c3[0] + c0[0], c3[1] + c0[1], c3[2] + c0[2], c3[3] + c0[3]},
        {c3[0] - c0[0], c3[1] - c0[1], c3[2] - c0[2], c3[3] - c0[3]},
        {c3[0] - c1[0], c3[1] - c1[1], c3[2] - c1[2], c3[3] - c1[3]},
        {c3[0] + c1[0], c3[1] + c1[1], c3[2] + c1[2], c3[3] + c1[3]},
    }};
    Frustum fr;
    for (int p = 0; p < 6; ++p) {
        // a.x + d >= 0  <=>  dot(-a, x) <= d. Normalised so distances are in world units.
        const double len = std::sqrt(in[p][0] * in[p][0] + in[p][1] * in[p][1] + in[p][2] * in[p][2]);
        const double s = len > 0 ? 1.0 / len : 1.0;
        fr.planes[p].n = Vec3{-in[p][0] * s, -in[p][1] * s, -in[p][2] * s};
        fr.planes[p].d = in[p][3] * s;
    }
    return fr;
}

std::vector<DrawRange> visibleDrawRanges(const Frustum& fr, const std::vector<DrawChunk>& chunks, size_t* culled) {
    std::vector<DrawRange> out;
    size_t skipped = 0;
    for (const DrawChunk& ch : chunks) {
        if (!frustumIntersectsAabb(fr, ch.bounds)) {
            ++skipped;
            continue;
        }
        if (!out.empty() && out.back().firstTriangle + out.back().triangleCount == ch.firstTriangle)
            out.back().triangleCount += ch.triangleCount;
        else
            out.push_back({ch.firstTriangle, ch.triangleCount});
    }
    if (culled) *culled = skipped;
    return out;
}

}  // namespace iw8r
