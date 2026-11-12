// UV layout core (src/core/uv_unwrap.h): projection fit and brush unfolding / unwrap.
#include <cmath>
#include <vector>

#include "../src/core/brush_ops.h"
#include "../src/core/uv_unwrap.h"
#include "test_harness.h"

using namespace iw8r;

namespace {

Primitive cube(double size) {
    const double h = size / 2;
    return *makeBrushPrimitive(makeBox(AABB{{-h, -h, -h}, {h, h, h}}, " caulk 64 64 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0"), "\n");
}

bool nearUv(const Uv& a, const Uv& b, double eps = 1e-6) { return std::fabs(a[0] - b[0]) < eps && std::fabs(a[1] - b[1]) < eps; }

}  // namespace

TEST(uv_fit_reproduces_a_projection) {
    const Primitive p = cube(64);
    const auto w = faceWindingsOf(p);
    for (const TexParams tp : {TexParams{64, 64, 0, 0, 0}, TexParams{32, 128, 10, -7, 30}, TexParams{100, -50, 3, 4, -75}}) {
        for (size_t f = 0; f < w.size(); ++f) {
            const Vec3 n = facePlane(p, f)->n;
            std::vector<Uv> t;
            for (const Vec3& v : w[f]) t.push_back(faceUV(n, tp, v));
            const TexParams got = fitTexParams(n, w[f], t, TexParams{});
            for (size_t k = 0; k < w[f].size(); ++k) CHECK(nearUv(faceUV(n, got, w[f][k]), t[k]));
        }
    }
}

TEST(uv_fit_moving_one_vertex_moves_the_face_toward_it) {
    const Primitive p = cube(64);
    const auto w = faceWindingsOf(p);
    const Vec3 n = facePlane(p, 0)->n;
    const TexParams tp{64, 64, 0, 0, 0};
    std::vector<Uv> t;
    for (const Vec3& v : w[0]) t.push_back(faceUV(n, tp, v));
    t[0][0] += 0.5;  // drag one corner half a repeat
    const TexParams got = fitTexParams(n, w[0], t, tp);
    const Uv moved = faceUV(n, got, w[0][0]);
    CHECK(moved[0] > faceUV(n, tp, w[0][0])[0] + 0.1);  // that corner followed (the face is one projection: not exact)
}

TEST(uv_unfold_cube_is_a_cross) {
    const Primitive p = cube(64);
    const auto net = unfoldBrush(p, 0);
    CHECK(net.size() == 6);
    // Every face keeps its size (64 x 64) and no two faces overlap: their centres are at least one face apart.
    std::vector<Uv> centres;
    for (const auto& [f, pts] : net) {
        CHECK(pts.size() == 4);
        Uv c{0, 0};
        for (const Uv& q : pts) c[0] += q[0] / 4, c[1] += q[1] / 4;
        centres.push_back(c);
        CHECK(std::fabs(std::hypot(pts[1][0] - pts[0][0], pts[1][1] - pts[0][1]) - 64) < 1e-6);
    }
    for (size_t a = 0; a < centres.size(); ++a)
        for (size_t b = a + 1; b < centres.size(); ++b)
            CHECK(std::hypot(centres[a][0] - centres[b][0], centres[a][1] - centres[b][1]) > 63.9);
}

TEST(uv_unwrap_cube_texture_continues_across_edges) {
    Primitive p = cube(64);
    const auto tps = unwrapBrush(p, 0);
    CHECK(tps.size() == 6);
    for (const auto& [f, tp] : tps) applyTexParams(p, f, tp);  // false for the root: unchanged
    // Along each edge the root shares with a neighbour, both faces give the same UV at the shared corners.
    const auto w = faceWindingsOf(p);
    const Vec3 n0 = facePlane(p, 0)->n;
    const TexParams t0 = *texParamsOfFace(p, 0);
    int shared = 0;
    for (size_t g = 1; g < w.size(); ++g) {
        const Vec3 ng = facePlane(p, g)->n;
        const TexParams tg = *texParamsOfFace(p, g);
        for (const Vec3& a : w[0])
            for (const Vec3& b : w[g])
                if (length(a - b) < 0.01) {
                    ++shared;
                    CHECK(nearUv(faceUV(n0, t0, a), faceUV(ng, tg, b), 1e-4));
                }
    }
    CHECK(shared == 8);  // four neighbours, two shared corners each
}
