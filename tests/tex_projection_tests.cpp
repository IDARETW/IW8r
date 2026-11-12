// Tests for the shared texture projection (src/core/tex_projection.h).
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <optional>
#include <random>
#include <string>
#include <vector>

#include "../src/core/brush_ops.h"
#include "../src/core/tex_projection.h"
#include "test_harness.h"

using namespace iw8r;

namespace {

constexpr double kPiT = 3.14159265358979323846;

bool near(double a, double b, double eps = 1e-9) { return std::fabs(a - b) <= eps; }
bool nearVec(const Vec3& a, const Vec3& b, double eps = 1e-9) {
    return near(a.x, b.x, eps) && near(a.y, b.y, eps) && near(a.z, b.z, eps);
}

// The compiler's projection, re-derived here because compile_iw8.cpp keeps its functions file-local.
struct RefAxes {
    Vec3 s, t;
};
RefAxes refDominant(const Vec3& n) {
    const double ax = std::fabs(n.x), ay = std::fabs(n.y), az = std::fabs(n.z);
    if (az >= ax && az >= ay) return {{1, 0, 0}, {0, -1, 0}};
    if (ax >= ay) return {{0, 1, 0}, {0, 0, -1}};
    return {{1, 0, 0}, {0, 0, -1}};
}
std::array<double, 2> refUV(const Vec3& p, const Vec3& nIn, const TexParams& tp) {
    const Vec3 n = normalize(nIn);
    const RefAxes ax = refDominant(n);
    const double rad = tp.rot * kPiT / 180.0;
    const double c = std::cos(rad), sn = std::sin(rad);
    const double a = dot(p, ax.s), b = dot(p, ax.t);
    const double S = a * c - b * sn, T = a * sn + b * c;
    return {S / tp.width + tp.xoff / tp.width, T / tp.height + tp.yoff / tp.height};
}
Vec3 refTangent(const Vec3& nIn, const TexParams& tp) {
    const Vec3 n = normalize(nIn);
    const RefAxes ax = refDominant(n);
    const double rad = tp.rot * kPiT / 180.0;
    const double c = std::cos(rad), sn = std::sin(rad);
    const Vec3 g = ax.s * c - ax.t * sn;
    Vec3 tan = g - n * dot(g, n);
    if (length(tan) < 1e-9) {
        const Vec3 r = std::fabs(n.x) < 0.9 ? Vec3{1, 0, 0} : Vec3{0, 1, 0};
        tan = r - n * dot(r, n);
    }
    return normalize(tan);
}

// Random points on the plane through `origin` with normal `n`.
std::vector<Vec3> randomFacePoints(std::mt19937& rng, const Vec3& origin, const Vec3& n, size_t count) {
    std::uniform_real_distribution<double> d(-200.0, 200.0);
    const Vec3 helper = std::fabs(n.z) < 0.9 ? Vec3{0, 0, 1} : Vec3{1, 0, 0};
    const Vec3 e1 = normalize(cross(n, helper));
    const Vec3 e2 = cross(n, e1);
    std::vector<Vec3> out;
    for (size_t i = 0; i < count; ++i) out.push_back(origin + e1 * d(rng) + e2 * d(rng));
    return out;
}

// A brush face's normal from its three points (outward for Radiant maps).
Vec3 faceNormal(const Primitive& p, size_t face) {
    const BrushFace& f = p.faces[face];
    return normalize(planeFromPoints(f.p[0], f.p[1], f.p[2])->n);
}

// Square winding on the z = 0 plane: u spans 0..100, v spans 0..50 at unit scale.
std::vector<Vec3> squareWinding() { return {{0, 0, 0}, {100, 0, 0}, {100, 50, 0}, {0, 50, 0}}; }

}  // namespace

TEST(tex_face_uv_axial_hand_computed) {
    // Top face, base axes s = (1,0,0), t = (0,-1,0). P = (100, 50, 0): a = 100, b = -50.
    const TexParams tp{64, 32, 10, 5, 0};
    const auto uv = faceUV({0, 0, 1}, tp, {100, 50, 0});
    CHECK(near(uv[0], 110.0 / 64.0, 1e-12));  // (100 + 10) / 64
    CHECK(near(uv[1], -45.0 / 32.0, 1e-12));  // (-50 + 5) / 32
    CHECK(nearVec(faceTangent({0, 0, 1}, tp), {1, 0, 0}));
    // Wall facing +X: s = (0,1,0), t = (0,0,-1). P = (0, 30, -20): a = 30, b = 20.
    const auto wall = faceUV({1, 0, 0}, TexParams{64, 64, 0, 0, 0}, {0, 30, -20});
    CHECK(near(wall[0], 30.0 / 64.0, 1e-12));
    CHECK(near(wall[1], 20.0 / 64.0, 1e-12));
}

TEST(tex_face_uv_rot45_hand_computed) {
    // rot = 45: S = a cos - b sin, T = a sin + b cos. P = (100, 0, 0): a = 100, b = 0.
    const TexParams tp{128, 128, 0, 0, 45};
    const auto uv = faceUV({0, 0, 1}, tp, {100, 0, 0});
    const double expect = 100.0 * std::sqrt(0.5) / 128.0;
    CHECK(near(uv[0], expect, 1e-12));
    CHECK(near(uv[1], expect, 1e-12));
    // Rotated base axes: s' = s cos - t sin = (c, sn, 0), t' = s sin + t cos = (sn, -c, 0) with c = sn = 1/sqrt2.
    const Vec3 tan = faceTangent({0, 0, 1}, tp);
    CHECK(nearVec(tan, {std::sqrt(0.5), std::sqrt(0.5), 0}, 1e-12));
    const TexBasis tb = texBasis({0, 0, 1}, tp);
    CHECK(nearVec(tb.s, {std::sqrt(0.5), std::sqrt(0.5), 0}, 1e-12));
    CHECK(nearVec(tb.t, {std::sqrt(0.5), -std::sqrt(0.5), 0}, 1e-12));
}

TEST(tex_face_uv_and_tangent_match_compiler) {
    // Sample faces with a normal that is not normalized and rotations that are not axial.
    const std::vector<Vec3> normals = {{0, 0, 1}, {1, 0, 0}, {0, -1, 0}, {0.3, -0.5, 0.8}, {-0.9, 0.1, 0.2}, {2, 2, 2}};
    const std::vector<TexParams> params = {{64, 32, 5, -7, 30}, {128, -128, -6372.8945, -434, 180}, {100, 50, 0, 0, -135}};
    std::mt19937 rng(20260508u);
    std::uniform_real_distribution<double> d(-500.0, 500.0);
    for (const Vec3& n : normals)
        for (const TexParams& tp : params) {
            CHECK(nearVec(faceTangent(n, tp), refTangent(n, tp), 1e-12));
            for (int i = 0; i < 20; ++i) {
                const Vec3 p{d(rng), d(rng), d(rng)};
                const auto got = faceUV(n, tp, p);
                const auto ref = refUV(p, n, tp);
                CHECK(near(got[0], ref[0], 1e-12));
                CHECK(near(got[1], ref[1], 1e-12));
            }
        }
}

TEST(tex_basis_is_orthonormal_and_in_face_plane) {
    const std::vector<Vec3> normals = {{0, 0, 1}, {1, 0, 0}, {0, 1, 0}, {0.3, -0.5, 0.8}};
    for (const Vec3& n : normals) {
        const Vec3 nn = normalize(n);
        for (double rot : {0.0, 33.0, -120.0}) {
            const TexBasis b = texBasis(n, TexParams{64, 64, 0, 0, rot});
            CHECK(near(length(b.s), 1.0, 1e-12));
            CHECK(near(length(b.t), 1.0, 1e-12));
            CHECK(near(dot(b.s, b.t), 0.0, 1e-12));
            // Only axis-aligned faces have a base pair that lies in the face plane. For an oblique face the
            // pair is the projection axes of the dominant component (as in the compiler); faceTangent is in-plane.
            if (std::fabs(nn.x) == 1.0 || std::fabs(nn.y) == 1.0 || std::fabs(nn.z) == 1.0) {
                CHECK(near(dot(b.s, nn), 0.0, 1e-12));
                CHECK(near(dot(b.t, nn), 0.0, 1e-12));
            }
            CHECK(near(dot(faceTangent(n, TexParams{64, 64, 0, 0, rot}), nn), 0.0, 1e-12));
        }
    }
}

TEST(tex_lock_translation_keeps_uv_and_only_offsets) {
    const Vec3 n = normalize(Vec3{0.2, 0.5, 0.84});
    const TexParams old{64, -32, 10, -5, 17};
    std::mt19937 rng(7);
    const auto pts = randomFacePoints(rng, {10, 20, 30}, n, 10);
    const Vec3 d{37, -12, 5};
    const auto xf = [&](const Vec3& p) { return p + d; };
    const auto got = lockTexture(n, old, {pts[0], pts[1], pts[2]}, xf);
    CHECK(got.has_value());
    if (!got) return;
    for (const Vec3& p : pts) {
        const auto before = faceUV(n, old, p);
        const auto after = faceUV(n, *got, xf(p));
        CHECK(near(before[0], after[0], 1e-6));
        CHECK(near(before[1], after[1], 1e-6));
    }
    CHECK(got->width == old.width);
    CHECK(got->height == old.height);
    CHECK(got->rot == old.rot);
    CHECK(!near(got->xoff, old.xoff, 1e-6) || !near(got->yoff, old.yoff, 1e-6));  // the offsets did move
}

TEST(tex_lock_wall_rotated_90_about_z_keeps_uv) {
    // Wall at x = 50 (normal +X). Rotate +90 degrees about Z: (x, y, z) -> (-y, x, z). New normal +Y.
    const Vec3 n{1, 0, 0};
    const TexParams old{64, 32, 3, -4, 20};
    std::mt19937 rng(11);
    const auto pts = randomFacePoints(rng, {50, 0, 0}, n, 10);
    const auto xf = [](const Vec3& p) { return Vec3{-p.y, p.x, p.z}; };
    const auto got = lockTexture(n, old, {pts[0], pts[1], pts[2]}, xf);
    CHECK(got.has_value());
    if (!got) return;
    CHECK(nearVec(xf(n), {0, 1, 0}, 1e-12));
    for (const Vec3& p : pts) {
        const auto before = faceUV(n, old, p);
        const auto after = faceUV({0, 1, 0}, *got, xf(p));
        CHECK(near(before[0], after[0], 1e-6));
        CHECK(near(before[1], after[1], 1e-6));
    }
}

TEST(tex_lock_uniform_scale_doubles_size) {
    const Vec3 n{0, 0, 1};
    const TexParams old{64, 32, 3, -4, 0};
    std::mt19937 rng(13);
    const auto pts = randomFacePoints(rng, {0, 0, 30}, n, 10);
    const auto xf = [](const Vec3& p) { return p * 2.0; };
    const auto got = lockTexture(n, old, {pts[0], pts[1], pts[2]}, xf);
    CHECK(got.has_value());
    if (!got) return;
    CHECK(near(got->width, 128, 1e-9));
    CHECK(near(got->height, 64, 1e-9));
    for (const Vec3& p : pts) {
        const auto before = faceUV(n, old, p);
        const auto after = faceUV(n, *got, xf(p));
        CHECK(near(before[0], after[0], 1e-6));
        CHECK(near(before[1], after[1], 1e-6));
    }
}

TEST(tex_lock_mirror_keeps_uv_and_sign_of_width) {
    // Mirror across x = 0 on a top face. The uv must survive; the rotation becomes 180 and the height flips sign.
    const Vec3 n{0, 0, 1};
    const TexParams old{64, 32, 10, 5, 0};
    std::mt19937 rng(17);
    const auto pts = randomFacePoints(rng, {0, 0, 0}, n, 10);
    const auto xf = [](const Vec3& p) { return Vec3{-p.x, p.y, p.z}; };
    const auto got = lockTexture(n, old, {pts[0], pts[1], pts[2]}, xf);
    CHECK(got.has_value());
    if (!got) return;
    CHECK(got->width > 0);
    for (const Vec3& p : pts) {
        const auto before = faceUV(n, old, p);
        const auto after = faceUV(n, *got, xf(p));
        CHECK(near(before[0], after[0], 1e-6));
        CHECK(near(before[1], after[1], 1e-6));
    }
}

TEST(tex_lock_rejects_shear) {
    // Shear x' = x + 0.5 y on a top face: the uv axes stop being orthogonal.
    const Vec3 n{0, 0, 1};
    const TexParams old{64, 64, 0, 0, 0};
    const std::array<Vec3, 3> pts = {Vec3{0, 0, 0}, Vec3{64, 0, 0}, Vec3{0, 64, 0}};
    const auto xf = [](const Vec3& p) { return Vec3{p.x + 0.5 * p.y, p.y, p.z}; };
    CHECK(!lockTexture(n, old, pts, xf).has_value());
    // Collinear points cannot fit an affine map.
    const std::array<Vec3, 3> line = {Vec3{0, 0, 0}, Vec3{10, 0, 0}, Vec3{20, 0, 0}};
    CHECK(!lockTexture(n, old, line, [](const Vec3& p) { return p; }).has_value());
}

TEST(tex_fit_texture_one_repeat_spans_integer_interval) {
    const Vec3 n{0, 0, 1};
    const TexParams tp{64, 64, 0, 0, 0};
    const auto win = squareWinding();
    const TexParams fit = fitTexture(n, tp, win, 1, 1);
    // u spans 100 units and v spans 50 units, so one repeat is 100 x 50 texture units.
    CHECK(near(fit.width, 100, 1e-9));
    CHECK(near(fit.height, 50, 1e-9));
    double umin = 1e30, umax = -1e30, vmin = 1e30, vmax = -1e30;
    for (const Vec3& p : win) {
        const auto uv = faceUV(n, fit, p);
        umin = std::min(umin, uv[0]);
        umax = std::max(umax, uv[0]);
        vmin = std::min(vmin, uv[1]);
        vmax = std::max(vmax, uv[1]);
    }
    CHECK(near(umin, std::round(umin), 1e-9));
    CHECK(near(umax - umin, 1.0, 1e-9));
    CHECK(near(vmax - vmin, 1.0, 1e-9));
    // Two repeats across u.
    const TexParams two = fitTexture(n, tp, win, 2, 1);
    const auto a = faceUV(n, two, {0, 0, 0});
    const auto b = faceUV(n, two, {100, 0, 0});
    CHECK(near(b[0] - a[0], 2.0, 1e-9));
}

TEST(tex_justify_left_puts_min_u_on_integer) {
    const Vec3 n{0, 0, 1};
    const TexParams tp{64, 64, 10.5, 5, 0};
    const auto win = squareWinding();
    const TexParams j = justifyTexture(n, tp, win, Justify::Left);
    double umin = 1e30, umax = -1e30;
    for (const Vec3& p : win) {
        const double u = faceUV(n, j, p)[0];
        umin = std::min(umin, u);
        umax = std::max(umax, u);
    }
    CHECK(near(umin, std::round(umin), 1e-9));
    CHECK(near(umax - umin, 100.0 / 64.0, 1e-9));  // justify moves, it does not scale
    CHECK(near(j.width, 64, 1e-12));
    CHECK(near(j.yoff, 5, 1e-12));                 // no change in v for Left
    // Right justify puts the max u on an integer.
    const TexParams r = justifyTexture(n, tp, win, Justify::Right);
    double rmax = -1e30;
    for (const Vec3& p : win) rmax = std::max(rmax, faceUV(n, r, p)[0]);
    CHECK(near(rmax, std::round(rmax), 1e-9));
}

TEST(tex_natural_texture_resets_size_only) {
    const TexParams t = naturalTexture(TexParams{200, 300, 7, -9, 45});
    CHECK(near(t.width, 64, 1e-12));
    CHECK(near(t.height, 64, 1e-12));
    CHECK(near(t.xoff, 7, 1e-12));
    CHECK(near(t.yoff, -9, 1e-12));
    CHECK(near(t.rot, 45, 1e-12));
}

TEST(tex_params_of_face_reads_five_fields) {
    Primitive p = *makeBrushPrimitive(makeBox(AABB{{-64, -64, -64}, {64, 64, 64}}, " caulk 64 32 10 -5 90 0 lightmap_gray 16384 16384 0 0 0 0"), "\n");
    const auto tp = texParamsOfFace(p, 0);
    CHECK(tp.has_value());
    if (tp) {
        CHECK(near(tp->width, 64, 1e-12));
        CHECK(near(tp->height, 32, 1e-12));
        CHECK(near(tp->xoff, 10, 1e-12));
        CHECK(near(tp->yoff, -5, 1e-12));
        CHECK(near(tp->rot, 90, 1e-12));
    }
    CHECK(!texParamsOfFace(p, 99).has_value());
}

TEST(tex_apply_changes_only_five_tokens) {
    Primitive p = *makeBrushPrimitive(makeBox(AABB{{-64, -64, -64}, {64, 64, 64}}, " caulk 64 64 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0"), "\n");
    const size_t face = 4;  // any face; the suffix is the same on all of them
    const std::string oldSuffix = p.faces[face].suffix;
    const std::string oldLine = p.lines[p.faces[face].line].text;
    const size_t head = oldLine.size() - oldSuffix.size();
    CHECK(applyTexParams(p, face, TexParams{100, 50, 7, -3, 90}));
    const std::string want = " caulk 100 50 7 -3 90 0 lightmap_gray 16384 16384 0 0 0 0";
    CHECK(p.faces[face].suffix == want);
    CHECK(p.lines[p.faces[face].line].text == oldLine.substr(0, head) + want);
    // Other faces are untouched.
    CHECK(p.faces[0].suffix == oldSuffix);
    CHECK(!applyTexParams(p, 99, TexParams{}));
    // Round trip through the reader.
    const auto back = texParamsOfFace(p, face);
    CHECK(back.has_value());
    if (back) CHECK(near(back->rot, 90, 1e-12) && near(back->xoff, 7, 1e-12));
}
