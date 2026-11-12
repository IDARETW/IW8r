// Spline tools (roadmap 2.26): Catmull-Rom sampling, and walls, posts and pipes built along a path (src/core/spline_tools.cpp).
#include <cmath>
#include <string>
#include <vector>

#include "brush_ops.h"
#include "geometry.h"
#include "map_text.h"
#include "spline_tools.h"
#include "test_harness.h"

using namespace iw8r;

namespace {

constexpr double kEps = 1e-6;

bool near(double a, double b, double eps = kEps) { return std::fabs(a - b) <= eps; }

bool nearV(const Vec3& a, const Vec3& b, double eps = kEps) {
    return near(a.x, b.x, eps) && near(a.y, b.y, eps) && near(a.z, b.z, eps);
}

// True when some sample lies on the control point.
bool hasSample(const std::vector<Vec3>& s, const Vec3& p) {
    for (const Vec3& q : s)
        if (nearV(q, p)) return true;
    return false;
}

// Every brush has one non-empty winding per face and a valid convex solid.
bool allValid(const std::vector<Primitive>& prims) {
    for (const Primitive& p : prims) {
        const auto ws = faceWindingsOf(p);
        if (ws.empty() || ws.size() != p.faces.size()) return false;
        for (const Winding& w : ws)
            if (w.size() < 3) return false;
        if (!isValidBrush(brushFromPrimitive(p))) return false;
    }
    return true;
}

AABB unionBounds(const std::vector<Primitive>& prims) {
    AABB box;
    for (const Primitive& p : prims) box.add(brushBounds(brushFromPrimitive(p)));
    return box;
}

// Point inside some brush of the list (planes compared with a small tolerance, so boundary points count).
bool insideAny(const std::vector<Primitive>& prims, const Vec3& q) {
    for (const Primitive& p : prims) {
        bool in = true;
        for (const BrushFaceSpec& f : brushFromPrimitive(p))
            if (dot(f.plane.n, q) > f.plane.d + 1e-4) {
                in = false;
                break;
            }
        if (in) return true;
    }
    return false;
}

std::vector<Vec3> straight3() { return {{0, 0, 0}, {128, 0, 0}, {256, 0, 0}}; }

}  // namespace

TEST(spline_sample_passes_through_control_points) {
    const std::vector<Vec3> cp = {{0, 0, 0}, {200, 0, 0}, {200, 200, 0}, {0, 200, 100}};
    const auto s = sampleSpline(cp, 40, false);
    CHECK(!s.empty());
    for (const Vec3& p : cp) CHECK(hasSample(s, p));
    CHECK(nearV(s.front(), cp.front()));
    CHECK(nearV(s.back(), cp.back()));
}

TEST(spline_sample_spacing_straight_exact) {
    // Three collinear points 300 apart, target 100: three pieces per span, so every gap is exactly 100.
    const std::vector<Vec3> cp = {{0, 0, 0}, {300, 0, 0}, {600, 0, 0}};
    const auto s = sampleSpline(cp, 100, false);
    CHECK(s.size() == 7);
    for (size_t i = 1; i < s.size(); ++i) CHECK(near(length(s[i] - s[i - 1]), 100));
}

TEST(spline_sample_spacing_curved_near_target) {
    const std::vector<Vec3> cp = {{0, 0, 0}, {200, 0, 0}, {200, 200, 0}, {0, 200, 100}};
    const double target = 40;
    const auto s = sampleSpline(cp, target, false);
    CHECK(s.size() >= cp.size());
    // Chords are a little shorter than arcs, so the lower bound is loose. The upper bound is strict.
    for (size_t i = 1; i < s.size(); ++i) {
        const double g = length(s[i] - s[i - 1]);
        CHECK(g <= target + kEps);
        CHECK(g >= 0.5 * target);
    }
}

TEST(spline_sample_closed_loop) {
    const std::vector<Vec3> cp = {{0, 0, 0}, {256, 0, 0}, {256, 256, 0}, {0, 256, 0}};
    const auto s = sampleSpline(cp, 64, true);
    CHECK(s.size() >= cp.size());
    for (const Vec3& p : cp) CHECK(hasSample(s, p));
    // The ring is not closed by a repeated first point.
    CHECK(length(s.back() - s.front()) > 1e-3);
}

TEST(spline_bad_arguments_return_empty) {
    const std::vector<Vec3> two = {{0, 0, 0}, {100, 0, 0}};
    CHECK(sampleSpline({{0, 0, 0}}, 10, false).empty());
    CHECK(sampleSpline(two, 0, false).empty());
    CHECK(sampleSpline(two, 10, true).empty());  // a closed loop needs 3 points
    CHECK(wallAlongSpline(straight3(), 0, 64, "metal", false).empty());
    CHECK(wallAlongSpline(straight3(), 16, 64, "", false).empty());
    CHECK(wallAlongSpline(straight3(), 16, 64, "bad name", false).empty());
    CHECK(postsAlongSpline(straight3(), 0, 8, 48, "metal").empty());
    CHECK(pipeAlongSpline(straight3(), 8, 3, "metal").empty());
    CHECK(pipeAlongSpline(straight3(), 8, 17, "metal").empty());
    CHECK(pipeAlongSpline(straight3(), 0, 8, "metal").empty());
    CHECK(!pipeAlongSpline(straight3(), 8, 4, "metal").empty());
}

TEST(wall_straight_is_contiguous) {
    const auto prims = wallAlongSpline(straight3(), 16, 64, "metal", false);
    CHECK(prims.size() == 2);
    CHECK(allValid(prims));
    const AABB all = unionBounds(prims);
    CHECK(nearV(all.mins, {0, -8, 0}));
    CHECK(nearV(all.maxs, {256, 8, 64}));
    if (prims.size() == 2) {
        const AABB a = brushBounds(brushFromPrimitive(prims[0]));
        const AABB b = brushBounds(brushFromPrimitive(prims[1]));
        CHECK(near(a.maxs.x, 128));
        CHECK(near(b.mins.x, 128));
    }
}

TEST(wall_l_corner_has_no_gap) {
    const std::vector<Vec3> pts = {{0, 0, 0}, {256, 0, 0}, {256, 256, 0}};
    const auto prims = wallAlongSpline(pts, 16, 64, "metal", false);
    CHECK(prims.size() == 2);
    CHECK(allValid(prims));
    const AABB all = unionBounds(prims);
    CHECK(nearV(all.mins, {0, -8, 0}));
    CHECK(nearV(all.maxs, {264, 256, 64}));
    // The outside corner of the L (mitre line) is covered. A flat-capped joint would leave it empty.
    CHECK(insideAny(prims, {264, -8, 32}));
    CHECK(insideAny(prims, {200, 4, 32}));
    CHECK(insideAny(prims, {260, 200, 32}));
    CHECK(insideAny(prims, {248, 8, 32}));  // inside corner
    CHECK(!insideAny(prims, {100, 50, 32}));  // open side of the L
}

TEST(wall_closed_square_covers_all_corners) {
    const std::vector<Vec3> pts = {{0, 0, 0}, {256, 0, 0}, {256, 256, 0}, {0, 256, 0}};
    const auto prims = wallAlongSpline(pts, 16, 64, "metal", true);
    CHECK(prims.size() == 4);
    CHECK(allValid(prims));
    CHECK(insideAny(prims, {264, -8, 32}));
    CHECK(insideAny(prims, {264, 264, 32}));
    CHECK(insideAny(prims, {-8, 264, 32}));
    CHECK(insideAny(prims, {-8, -8, 32}));
}

TEST(wall_sloped_brushes_are_valid_and_follow_height) {
    const std::vector<Vec3> pts = {{0, 0, 0}, {128, 0, 64}, {256, 0, 128}};
    const auto prims = wallAlongSpline(pts, 16, 64, "metal", false);
    CHECK(prims.size() == 2);
    CHECK(allValid(prims));
    const AABB all = unionBounds(prims);
    CHECK(near(all.mins.z, 0, 1e-4));
    CHECK(near(all.maxs.z, 192, 1e-4));  // top of the last point: 128 + height 64
}

TEST(wall_drops_duplicate_points) {
    const std::vector<Vec3> pts = {{0, 0, 0}, {0, 0, 0}, {128, 0, 0}, {256, 0, 0}};
    const auto prims = wallAlongSpline(pts, 16, 64, "metal", false);
    CHECK(prims.size() == 2);
    CHECK(allValid(prims));
}

TEST(posts_count_is_floor_length_over_spacing_plus_one) {
    const std::vector<Vec3> line = {{0, 0, 0}, {1000, 0, 0}};
    const auto a = postsAlongSpline(line, 128, 8, 48, "metal");
    CHECK(a.size() == 8);  // floor(1000 / 128) + 1
    CHECK(allValid(a));
    const auto b = postsAlongSpline({{0, 0, 0}, {1024, 0, 0}}, 128, 8, 48, "metal");
    CHECK(b.size() == 9);  // the end post lands on the last point
    CHECK(allValid(b));
    // Each post is a square of side 8 at x = 128 k, from the point's z up by the height.
    if (a.size() >= 2) {
        const AABB p1 = brushBounds(brushFromPrimitive(a[1]));
        CHECK(near(p1.mins.x, 124));
        CHECK(near(p1.maxs.x, 132));
        CHECK(near(p1.mins.z, 0));
        CHECK(near(p1.maxs.z, 48));
    }
}

TEST(posts_follow_arc_length_around_a_corner) {
    // Length 512 with a corner at 256: posts at 0, 128, 256 (the corner), 384 and 512.
    const std::vector<Vec3> pts = {{0, 0, 0}, {256, 0, 0}, {256, 256, 0}};
    const auto prims = postsAlongSpline(pts, 128, 8, 48, "metal");
    CHECK(prims.size() == 5);
    CHECK(allValid(prims));
    if (prims.size() == 5) {
        const AABB corner = brushBounds(brushFromPrimitive(prims[2]));
        CHECK(near(corner.mins.x, 252));
        CHECK(near(corner.mins.y, -4));
        const AABB last = brushBounds(brushFromPrimitive(prims[4]));
        CHECK(near(last.maxs.y, 260));
    }
}

TEST(pipe_face_count_is_sides_plus_two) {
    const std::vector<Vec3> pts = {{0, 0, 0}, {100, 0, 0}, {200, 0, 0}};
    for (int sides : {4, 8, 16}) {
        const auto prims = pipeAlongSpline(pts, 8, sides, "metal");
        CHECK(prims.size() == 2);
        for (const Primitive& p : prims) CHECK(p.faces.size() == static_cast<size_t>(sides + 2));
        CHECK(allValid(prims));
    }
}

TEST(pipe_straight_bounds_and_corner_validity) {
    const std::vector<Vec3> line = {{0, 0, 0}, {100, 0, 0}, {200, 0, 0}};
    const auto prims = pipeAlongSpline(line, 8, 4, "metal");
    const AABB all = unionBounds(prims);
    CHECK(near(all.mins.x, 0));
    CHECK(near(all.maxs.x, 200));
    // Square section with vertices on the radius-8 circle, so its flat sides sit at 8 cos(45 deg) on the axes.
    CHECK(near(all.maxs.y, 8 * std::cos(3.14159265358979323846 / 4), 1e-4));
    CHECK(near(all.mins.y, -8 * std::cos(3.14159265358979323846 / 4), 1e-4));

    const std::vector<Vec3> bend = {{0, 0, 0}, {100, 0, 0}, {100, 100, 0}};
    const auto bent = pipeAlongSpline(bend, 8, 8, "metal");
    CHECK(bent.size() == 2);
    CHECK(allValid(bent));

    const std::vector<Vec3> sloped = {{0, 0, 0}, {100, 0, 50}, {200, 50, 50}};
    CHECK(allValid(pipeAlongSpline(sloped, 8, 6, "metal")));
}
