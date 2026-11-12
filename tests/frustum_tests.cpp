// Frustum culling (roadmap 3.20): plane construction matches the editor camera, box/point tests, distance culling.
#include <cmath>
#include <cstddef>
#include <random>
#include <vector>

#include "frustum.h"
#include "test_harness.h"

using namespace iw8r;

namespace {

constexpr double kNear = 4.0;        // editor viewProjection near (camera.cpp:23)
constexpr double kFar = 131072.0;    // editor viewProjection far (camera.cpp:23)
constexpr double kPiT = 3.14159265358979323846;

AABB box(double x0, double y0, double z0, double x1, double y1, double z1) {
    AABB b;
    b.mins = Vec3(x0, y0, z0);
    b.maxs = Vec3(x1, y1, z1);
    return b;
}

struct View {
    Vec3 eye;
    double yaw = 0, pitch = 0, fov = 90, aspect = 16.0 / 9.0;
};

// Independent projection used as the oracle. It re-derives the basis from the angles (right = (sin y, -cos y, 0)
// instead of a cross product), maps the point to view space and applies a D3D-style perspective with x/y scaled by
// 1/tan(fov/2) (horizontal fov) and aspect/tan(fov/2), depth remapped to [0, 1]. Inside means |x| <= w, |y| <= w and
// 0 <= z <= w in clip space, with w = view depth.
bool projectsInside(const View& v, const Vec3& p, double nearZ = kNear, double farZ = kFar) {
    const double y = v.yaw * kPiT / 180.0, pt = v.pitch * kPiT / 180.0;
    const Vec3 fwd{std::cos(pt) * std::cos(y), std::cos(pt) * std::sin(y), std::sin(pt)};
    const Vec3 right{std::sin(y), -std::cos(y), 0.0};
    const Vec3 up{-std::sin(pt) * std::cos(y), -std::sin(pt) * std::sin(y), std::cos(pt)};
    const Vec3 d = p - v.eye;
    const double xv = d.x * right.x + d.y * right.y + d.z * right.z;
    const double yv = d.x * up.x + d.y * up.y + d.z * up.z;
    const double zv = d.x * fwd.x + d.y * fwd.y + d.z * fwd.z;
    const double tx = std::tan(v.fov * kPiT / 360.0);
    if (zv <= 0) return false;
    const double xc = xv / tx;                    // x_clip, w = zv
    const double yc = yv * v.aspect / tx;         // y_clip, w = zv
    const double A = farZ / (farZ - nearZ), B = -nearZ * farZ / (farZ - nearZ);
    const double zc = zv * A + B;                 // z_clip, w = zv
    return std::fabs(xc) <= zv && std::fabs(yc) <= zv && zc >= 0 && zc <= zv;
}

Frustum frustumOf(const View& v) { return makeFrustum(v.eye, v.yaw, v.pitch, v.fov, v.aspect, kNear, kFar); }

bool visibleBox(const View& v, const AABB& b) { return frustumIntersectsAabb(frustumOf(v), b); }

}  // namespace

TEST(frustum_boxes_in_front_visible_behind_culled) {
    View v{Vec3(0, 0, 0)};  // looks down +X
    CHECK(visibleBox(v, box(100, -10, -10, 120, 10, 10)));
    CHECK(visibleBox(v, box(5000, -10, -10, 5020, 10, 10)));
    CHECK(!visibleBox(v, box(-200, -10, -10, -100, 10, 10)));
    CHECK(!visibleBox(v, box(-5000, -10, -10, -4900, 10, 10)));
}

TEST(frustum_horizontal_fov_left_right) {
    // fov 90 horizontal: at x = 100 the view spans |y| <= 100 (yaw 0: right is -Y, left is +Y).
    View v{Vec3(0, 0, 0)};
    v.aspect = 1.0;
    CHECK(visibleBox(v, box(100, 60, -5, 110, 80, 5)));
    CHECK(visibleBox(v, box(100, -60, -5, 110, -40, 5)));
    CHECK(!visibleBox(v, box(100, 120, -5, 110, 140, 5)));
    CHECK(!visibleBox(v, box(100, -150, -5, 110, -120, 5)));
}

TEST(frustum_vertical_fov_up_down_uses_aspect) {
    // Aspect 2 (wide): vertical half-slope is tan(45) / 2 = 0.5, so at x = 100 the view spans |z| <= 50.
    View v{Vec3(0, 0, 0)};
    v.aspect = 2.0;
    CHECK(visibleBox(v, box(100, -5, 20, 110, 5, 40)));
    CHECK(visibleBox(v, box(100, -5, -40, 110, 5, -20)));
    CHECK(!visibleBox(v, box(100, -5, 60, 110, 5, 80)));
    CHECK(!visibleBox(v, box(100, -5, -80, 110, 5, -60)));
    // Aspect 1: the vertical span grows to |z| <= 100, so the same box at z 60..80 is now visible.
    v.aspect = 1.0;
    CHECK(visibleBox(v, box(100, -5, 60, 110, 5, 80)));
}

TEST(frustum_pitch_and_yaw) {
    // Pitch 45 (looking up): the point along forward is visible, a point straight above the eye at that depth is not.
    View up{Vec3(0, 0, 0)};
    up.pitch = 45;
    up.aspect = 1.0;
    CHECK(visibleBox(up, box(99, -1, 99, 101, 1, 101)));
    CHECK(!visibleBox(up, box(-101, -1, 299, -99, 1, 301)));   // far above the 45 degree top edge
    CHECK(!visibleBox(up, box(100, -1, -200, 110, 1, -180)));  // below and behind
    // Pitch -45 (looking down) mirrors it.
    View down = up;
    down.pitch = -45;
    CHECK(visibleBox(down, box(99, -1, -101, 101, 1, -99)));
    CHECK(!visibleBox(down, box(-10, -1, 200, 10, 1, 220)));

    // Yaw 90 looks down +Y; right is +X. A point 200 to the right at depth 100 is outside a 90 fov.
    View north{Vec3(0, 0, 0)};
    north.yaw = 90;
    north.aspect = 1.0;
    CHECK(visibleBox(north, box(-10, 90, -5, 10, 110, 5)));
    CHECK(visibleBox(north, box(30, 90, -5, 40, 110, 5)));
    CHECK(!visibleBox(north, box(200, 90, -5, 210, 110, 5)));
    CHECK(!visibleBox(north, box(-10, -110, -5, 10, -90, 5)));
}

TEST(frustum_yaw_wraps) {
    // Yaw -90 and 270 describe the same camera.
    const Vec3 eye(10, 20, 30);
    const Frustum a = makeFrustum(eye, -90, 10, 75, 1.5, kNear, kFar);
    const Frustum b = makeFrustum(eye, 270, 10, 75, 1.5, kNear, kFar);
    for (int i = 0; i < 6; ++i) {
        CHECK(std::fabs(a.planes[i].d - b.planes[i].d) < 1e-6);
        CHECK(length(a.planes[i].n - b.planes[i].n) < 1e-9);
    }
}

TEST(frustum_huge_box_containing_eye_visible) {
    View v{Vec3(12, -34, 56)};
    CHECK(visibleBox(v, box(-1e6, -1e6, -1e6, 1e6, 1e6, 1e6)));
    CHECK(frustumContainsPoint(frustumOf(v), v.eye + Vec3(20, 0, 0)));
}

TEST(frustum_boxes_straddling_planes_visible) {
    View v{Vec3(0, 0, 0)};
    v.aspect = 1.0;
    // Straddles the near plane (x = 4).
    CHECK(visibleBox(v, box(2, -1, -1, 6, 1, 1)));
    // Straddles the right edge (|y| = x): corner (150, -110) is inside, corner (50, -90) is outside.
    CHECK(visibleBox(v, box(50, -110, -1, 150, -90, 1)));
    // Straddles the far plane.
    CHECK(visibleBox(v, box(kFar - 10, -1, -1, kFar + 10, 1, 1)));
    // Entirely beyond the far plane.
    CHECK(!visibleBox(v, box(kFar + 10, -1, -1, kFar + 20, 1, 1)));
    // Entirely outside the right edge at this depth.
    CHECK(!visibleBox(v, box(50, -200, -1, 60, -150, 1)));
}

TEST(frustum_point_containment) {
    View v{Vec3(0, 0, 0)};
    v.aspect = 1.0;
    const Frustum fr = frustumOf(v);
    CHECK(frustumContainsPoint(fr, Vec3(100, 0, 0)));
    CHECK(!frustumContainsPoint(fr, Vec3(2, 0, 0)));     // before near
    CHECK(!frustumContainsPoint(fr, Vec3(-100, 0, 0)));  // behind
    CHECK(!frustumContainsPoint(fr, Vec3(100, 150, 0)));
    CHECK(!frustumContainsPoint(fr, Vec3(100, 0, 150)));
}

TEST(frustum_visible_indices_and_stats) {
    View v{Vec3(0, 0, 0)};
    v.aspect = 1.0;
    const Frustum fr = frustumOf(v);
    std::vector<AABB> boxes = {
        box(100, -5, -5, 110, 5, 5),      // 0 visible
        box(-300, -5, -5, -290, 5, 5),    // 1 behind
        box(200, 300, -5, 210, 310, 5),   // 2 left, outside
        box(50, -5, -5, 60, 5, 5),        // 3 visible
        AABB{},                           // 4 invalid
    };
    CullStats stats;
    const std::vector<std::size_t> vis = visibleIndices(fr, boxes, &stats);
    CHECK((vis == std::vector<std::size_t>{0, 3}));
    CHECK(stats.tested == 5);
    CHECK(stats.culled == 3);
    // Stats accumulate and a null pointer is allowed.
    visibleIndices(fr, boxes, &stats);
    CHECK(stats.tested == 10 && stats.culled == 6);
    CHECK(visibleIndices(fr, {}).empty());
}

TEST(frustum_distance_culling) {
    const Vec3 eye(0, 0, 0);
    CHECK(beyondDrawDistance(eye, box(100, -5, -5, 200, 5, 5), 99));
    CHECK(!beyondDrawDistance(eye, box(100, -5, -5, 200, 5, 5), 100));  // touching the sphere is kept
    CHECK(!beyondDrawDistance(eye, box(100, -5, -5, 200, 5, 5), 200));
    CHECK(!beyondDrawDistance(eye, box(-5, -5, -5, 5, 5, 5), 1));       // eye inside
    // Nearest point is the corner (30, 40, 0): distance 50.
    CHECK(beyondDrawDistance(eye, box(30, 40, 0, 31, 41, 1), 49.9));
    CHECK(!beyondDrawDistance(eye, box(30, 40, 0, 31, 41, 1), 50.0));
    CHECK(!beyondDrawDistance(eye, AABB{}, 1.0));
}

TEST(frustum_random_no_false_culls) {
    // Property: a box with any corner that projects inside the view must be reported visible. The oracle is the
    // independent projection above. Also checks point containment against the same oracle.
    std::mt19937 rng(20260523u);
    std::uniform_real_distribution<double> unit(0.0, 1.0);
    auto rnd = [&](double lo, double hi) { return lo + (hi - lo) * unit(rng); };

    std::size_t boxesChecked = 0, insideBoxes = 0, culledBoxes = 0, pointsChecked = 0;
    for (int cam = 0; cam < 20; ++cam) {
        View v;
        v.eye = Vec3(rnd(-2000, 2000), rnd(-2000, 2000), rnd(-500, 500));
        v.yaw = rnd(-720, 720);
        v.pitch = rnd(-85, 85);
        v.fov = rnd(30, 120);
        v.aspect = rnd(0.5, 3.0);
        const Frustum fr = frustumOf(v);

        for (int i = 0; i < 100; ++i) {
            const Vec3 c(rnd(-3000, 3000), rnd(-3000, 3000), rnd(-1000, 1000));
            const Vec3 h(rnd(1, 500), rnd(1, 500), rnd(1, 500));
            const AABB b = box(c.x - h.x, c.y - h.y, c.z - h.z, c.x + h.x, c.y + h.y, c.z + h.z);
            ++boxesChecked;

            bool anyCornerInside = false;
            for (int k = 0; k < 8; ++k) {
                const Vec3 corner((k & 1) ? b.maxs.x : b.mins.x, (k & 2) ? b.maxs.y : b.mins.y,
                                  (k & 4) ? b.maxs.z : b.mins.z);
                if (projectsInside(v, corner)) anyCornerInside = true;
            }
            const bool visible = frustumIntersectsAabb(fr, b);
            if (anyCornerInside) {
                ++insideBoxes;
                CHECK(visible);
            }
            if (!visible) ++culledBoxes;
        }

        for (int i = 0; i < 100; ++i) {
            const Vec3 p(rnd(-3000, 3000), rnd(-3000, 3000), rnd(-1000, 1000));
            ++pointsChecked;
            CHECK(frustumContainsPoint(fr, p) == projectsInside(v, p));
        }
    }
    CHECK(boxesChecked == 2000);
    CHECK(insideBoxes > 200);   // the property is exercised, not vacuous
    CHECK(culledBoxes > 200);   // and culling really happens
    CHECK(pointsChecked == 2000);
}
