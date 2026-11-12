// CSG merge, 3-point clipper, face extrude and edge drag (src/core/brush_ops2.h).
#include <cmath>
#include <string>
#include <vector>

#include "brush_ops.h"
#include "brush_ops2.h"
#include "test_harness.h"

using namespace iw8r;

namespace {

const char* kTail = " caulk 64 64 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0";
const char* kWallA = " wall_a 64 64 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0";
const char* kWallB = " wall_b 32 32 8 0 0 0 lightmap_gray 16384 16384 0 0 0 0";
const char* kTopTile = " tile_top 64 64 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0";

AABB box3(Vec3 a, Vec3 b) {
    AABB r;
    r.add(a);
    r.add(b);
    return r;
}

bool sameBounds(const AABB& a, const AABB& b, double eps) {
    return std::fabs(a.mins.x - b.mins.x) < eps && std::fabs(a.mins.y - b.mins.y) < eps &&
           std::fabs(a.mins.z - b.mins.z) < eps && std::fabs(a.maxs.x - b.maxs.x) < eps &&
           std::fabs(a.maxs.y - b.maxs.y) < eps && std::fabs(a.maxs.z - b.maxs.z) < eps;
}

// First face whose outward normal is `n`, or nullptr.
const BrushFaceSpec* faceWithNormal(const ConvexBrush& b, const Vec3& n) {
    for (const auto& f : b)
        if (dot(f.plane.n, n) > 0.999999) return &f;
    return nullptr;
}

// True when every vertex of every face lies inside every face plane.
bool isConvexBrush(const ConvexBrush& b) {
    for (const Winding& w : brushWindings(b))
        for (const Vec3& p : w)
            for (const auto& f : b)
                if (dot(f.plane.n, p) - f.plane.d > 1e-6) return false;
    return true;
}

}  // namespace

TEST(ops2_merge_touching_boxes_into_one_box) {
    const ConvexBrush a = makeBox(box3({0, 0, 0}, {64, 64, 64}), kWallA);
    const ConvexBrush b = makeBox(box3({64, 0, 0}, {128, 64, 64}), kWallB);
    auto m = mergeBrushes({a, b});
    CHECK(m.has_value());
    if (!m) return;
    CHECK(m->size() == 6);
    CHECK(isValidBrush(*m));
    CHECK(sameBounds(brushBounds(*m), box3({0, 0, 0}, {128, 64, 64}), 1e-9));
    CHECK(std::fabs(brushVolume(*m) - 128.0 * 64 * 64) < 1e-6);
    // Faces keep the suffix of the source face with the same plane.
    const BrushFaceSpec* plusX = faceWithNormal(*m, {1, 0, 0});
    const BrushFaceSpec* minusX = faceWithNormal(*m, {-1, 0, 0});
    CHECK(plusX && plusX->suffix == kWallB);
    CHECK(minusX && minusX->suffix == kWallA);
    // A single brush merges to itself.
    auto single = mergeBrushes({a});
    CHECK(single.has_value() && std::fabs(brushVolume(*single) - 64.0 * 64 * 64) < 1e-6);
    CHECK(!mergeBrushes({}).has_value());
}

TEST(ops2_merge_refuses_gap_and_l_shape) {
    const ConvexBrush a = makeBox(box3({0, 0, 0}, {64, 64, 64}), kTail);
    // Gap of 16 units along x: the hull bridges it, so it is not the union.
    const ConvexBrush far = makeBox(box3({80, 0, 0}, {144, 64, 64}), kTail);
    CHECK(!mergeBrushes({a, far}).has_value());
    // L-shape in plan: the hull cuts off a corner that is not covered.
    const ConvexBrush wide = makeBox(box3({64, 0, 0}, {128, 32, 64}), kTail);
    CHECK(!mergeBrushes({a, wide}).has_value());
}

TEST(ops2_clip_plane_three_points_keeps_side) {
    const Vec3 a{0, 0, 0}, b{64, 0, 0}, c{0, 64, 0};  // the plane z == 0
    auto above = clipPlaneFrom3Points(a, b, c, {0, 0, 100});
    CHECK(above.has_value());
    if (above) {
        CHECK(dot(above->n, Vec3{0, 0, 100}) - above->d < 0);  // keepSide is inside
        CHECK(std::fabs(above->n.z + 1) < 1e-9);                 // kept half-space is z >= 0
        // Clipping a 64 cube with it keeps the top half.
        const ConvexBrush cube = makeBox(box3({-32, -32, -32}, {32, 32, 32}), kTail);
        auto kept = clipBrush(cube, *above, kWallB);
        CHECK(kept.has_value());
        if (kept) CHECK(sameBounds(brushBounds(*kept), box3({-32, -32, 0}, {32, 32, 32}), 1e-9));
    }
    auto below = clipPlaneFrom3Points(a, b, c, {0, 0, -100});
    CHECK(below.has_value() && dot(below->n, Vec3{0, 0, -100}) - below->d < 0);
    CHECK(below.has_value() && std::fabs(below->n.z - 1) < 1e-9);
    // Degenerate: collinear points, or the keep point on the plane.
    CHECK(!clipPlaneFrom3Points(a, {32, 0, 0}, {64, 0, 0}, {0, 0, 100}).has_value());
    CHECK(!clipPlaneFrom3Points(a, b, c, {5, 5, 0}).has_value());
}

TEST(ops2_extrude_top_face_by_32) {
    ConvexBrush cube = makeBox(box3({0, 0, 0}, {64, 64, 64}), kTail);
    size_t top = cube.size();
    for (size_t i = 0; i < cube.size(); ++i) {
        if (dot(cube[i].plane.n, Vec3{0, 0, 1}) > 0.5) {
            top = i;
            cube[i].suffix = kTopTile;
        }
    }
    CHECK(top < cube.size());
    if (top >= cube.size()) return;

    auto e = extrudeFace(cube, top, 32);
    CHECK(e.has_value());
    if (!e) return;
    CHECK(isValidBrush(*e));
    CHECK(e->size() == 6);
    CHECK(sameBounds(brushBounds(*e), box3({0, 0, 64}, {64, 64, 96}), 1e-9));
    CHECK(std::fabs(brushVolume(*e) - 64.0 * 64 * 32) < 1e-6);
    CHECK(isConvexBrush(*e));
    // The new top carries the source top material.
    const BrushFaceSpec* newTop = faceWithNormal(*e, {0, 0, 1});
    CHECK(newTop && newTop->suffix == kTopTile);
    CHECK(newTop && std::fabs(newTop->plane.d - 96) < 1e-9);
    // The extruded brush can be written as a brush primitive.
    CHECK(makeBrushPrimitive(*e, "\n").has_value());

    CHECK(!extrudeFace(cube, top, 0).has_value());
    CHECK(!extrudeFace(cube, top, -8).has_value());
    CHECK(!extrudeFace(cube, cube.size(), 8).has_value());
}

TEST(ops2_box_has_twelve_edges) {
    const ConvexBrush cube = makeBox(box3({0, 0, 0}, {64, 64, 64}), kTail);
    const auto edges = brushEdges(cube);
    CHECK(edges.size() == 12);
    for (const auto& e : edges) CHECK(std::fabs(length(e.b - e.a) - 64.0) < 1e-9);
}

TEST(ops2_move_top_edge_sideways_makes_wedge) {
    const ConvexBrush cube = makeBox(box3({0, 0, 0}, {64, 64, 64}), kTail);
    // The top edge on the front side (y == 0, z == 64).
    BrushEdge front{};
    bool found = false;
    for (const auto& e : brushEdges(cube))
        if (e.a.z == 64 && e.b.z == 64 && e.a.y == 0 && e.b.y == 0) {
            front = e;
            found = true;
        }
    CHECK(found);
    if (!found) return;

    auto wedge = moveBrushEdge(cube, front, {0, 32, 0});
    CHECK(wedge.has_value());
    if (wedge) {
        CHECK(isValidBrush(*wedge));
        CHECK(wedge->size() == 6);
        CHECK(isConvexBrush(*wedge));
        CHECK(sameBounds(brushBounds(*wedge), box3({0, 0, 0}, {64, 64, 64}), 1e-9));
        // Cross-section is the trapezoid (0,0),(64,0),(64,64),(32,64) in y-z: area 3072.
        CHECK(std::fabs(brushVolume(*wedge) - 3072.0 * 64) < 1e-3);
        CHECK(brushEdges(*wedge).size() == 12);
    }
    // Dragged onto the opposite edge (y == 64): the top face collapses.
    CHECK(!moveBrushEdge(cube, front, {0, 64, 0}).has_value());
    // Dragged through it: the top face flips.
    CHECK(!moveBrushEdge(cube, front, {0, 96, 0}).has_value());
    // An edge that is not part of the brush is refused.
    CHECK(!moveBrushEdge(cube, BrushEdge{{0, 0, 0}, {1, 1, 1}}, {0, 8, 0}).has_value());
}
