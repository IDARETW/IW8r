// Convex collision for custom models (src/core/convex_hull.h): hulls, plane budgets, pieces and placed brushes.
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "brush_ops.h"
#include "convex_hull.h"
#include "geometry.h"
#include "model_import.h"
#include "test_harness.h"

using namespace iw8r;

namespace {

// True when every point is inside (or within tol of) every plane.
bool containsAll(const std::vector<Plane>& planes, const std::vector<Vec3>& pts, double tol) {
    for (const Plane& pl : planes)
        for (const Vec3& p : pts)
            if (dot(pl.n, p) > pl.d + tol) return false;
    return true;
}

// Triangulated axis-aligned box (8 corners, 12 triangles) appended to a mesh.
void addBox(ImportedMesh& mesh, const Vec3& lo, const Vec3& hi) {
    const uint32_t base = uint32_t(mesh.pos.size());
    for (int c = 0; c < 8; ++c)
        mesh.pos.push_back(Vec3{(c & 1) ? hi.x : lo.x, (c & 2) ? hi.y : lo.y, (c & 4) ? hi.z : lo.z});
    const uint32_t tris[12][3] = {{0, 2, 1}, {1, 2, 3}, {4, 5, 6}, {5, 7, 6}, {0, 4, 2}, {2, 4, 6},
                                  {1, 3, 5}, {3, 7, 5}, {0, 1, 4}, {1, 5, 4}, {2, 6, 3}, {3, 6, 7}};
    for (const auto& t : tris)
        for (uint32_t k : t) mesh.idx.push_back(base + k);
}

bool nearTo(double a, double b) { return std::fabs(a - b) < 1e-3; }

}  // namespace

TEST(convex_hull_cube_gives_six_planes) {
    std::vector<Vec3> pts;
    for (int pass = 0; pass < 2; ++pass)  // corners twice: duplicates must not change the hull
        for (int c = 0; c < 8; ++c)
            pts.push_back(Vec3{double(c & 1), double((c >> 1) & 1), double((c >> 2) & 1)});
    pts.push_back(Vec3{0.5, 0.5, 0.5});  // interior
    pts.push_back(Vec3{0.2, 0.7, 0.4});  // interior

    const auto planes = convexHullPlanes(pts);
    CHECK(planes.size() == 6);
    CHECK(containsAll(planes, pts, 1e-9));
    bool axisAligned = true;
    for (const Plane& pl : planes) {
        int big = 0;
        for (int i = 0; i < 3; ++i)
            if (std::fabs(pl.n[i]) > 0.5) ++big;
        if (big != 1) axisAligned = false;
    }
    CHECK(axisAligned);
}

TEST(convex_hull_sphere_simplifies_and_contains_points) {
    // Fibonacci sphere sample, radius 10.
    std::vector<Vec3> pts;
    const int n = 100;
    for (int i = 0; i < n; ++i) {
        const double z = 1.0 - 2.0 * (i + 0.5) / n;
        const double r = std::sqrt(1.0 - z * z);
        const double phi = i * 2.399963229728653;
        pts.push_back(Vec3{10.0 * r * std::cos(phi), 10.0 * r * std::sin(phi), 10.0 * z});
    }
    const auto planes = convexHullPlanes(pts);
    CHECK(planes.size() > 20);
    CHECK(containsAll(planes, pts, 1e-7));

    const auto simple = simplifyHull(planes, 20);
    CHECK(simple.size() <= 20);
    CHECK(simple.size() >= 4);
    CHECK(containsAll(simple, pts, 1e-7));
}

TEST(convex_hull_degenerate_input_is_empty) {
    std::vector<Vec3> coplanar;
    for (int x = 0; x < 3; ++x)
        for (int y = 0; y < 3; ++y) coplanar.push_back(Vec3{double(x), double(y), 0.0});
    CHECK(convexHullPlanes(coplanar).empty());

    std::vector<Vec3> collinear;
    for (int x = 0; x < 5; ++x) collinear.push_back(Vec3{double(x), 2.0, 3.0});
    CHECK(convexHullPlanes(collinear).empty());

    CHECK(convexHullPlanes({Vec3{0, 0, 0}, Vec3{1, 0, 0}, Vec3{0, 1, 0}}).empty());
    CHECK(convexHullPlanes(std::vector<Vec3>(10, Vec3{4, 4, 4})).empty());
}

TEST(convex_pieces_split_components_and_merge_to_budget) {
    ImportedModel m;
    ImportedMesh mesh;
    addBox(mesh, Vec3{0, 0, 0}, Vec3{1, 1, 1});
    addBox(mesh, Vec3{10, 0, 0}, Vec3{11, 1, 1});
    m.meshes.push_back(mesh);

    const auto two = convexPieces(m, 8, 32);
    CHECK(two.size() == 2);
    if (two.size() == 2) {
        CHECK(two[0].size() == 6);
        CHECK(two[1].size() == 6);
    }

    const auto one = convexPieces(m, 1, 32);
    CHECK(one.size() == 1);
    if (one.size() == 1) CHECK(containsAll(one[0], mesh.pos, 1e-7));
}

TEST(hull_brushes_apply_placement) {
    std::vector<Vec3> cube;
    for (int c = 0; c < 8; ++c) cube.push_back(Vec3{double(c & 1), double((c >> 1) & 1), double((c >> 2) & 1)});
    const std::vector<std::vector<Plane>> pieces{convexHullPlanes(cube)};

    // Identity placement: the unit cube again.
    const auto plain = hullBrushes(pieces, Vec3{}, Vec3{}, 1.0, "clip");
    CHECK(plain.size() == 1);
    if (plain.size() == 1) {
        const ConvexBrush b = brushFromPrimitive(plain[0]);
        CHECK(isValidBrush(b));
        const AABB bb = brushBounds(b);
        CHECK(nearTo(bb.mins.x, 0) && nearTo(bb.mins.y, 0) && nearTo(bb.mins.z, 0));
        CHECK(nearTo(bb.maxs.x, 1) && nearTo(bb.maxs.y, 1) && nearTo(bb.maxs.z, 1));
    }

    // Scale 2, yaw 90 (+X -> +Y), origin (100, 0, 0): x in [98, 100], y in [0, 2], z in [0, 2].
    const auto placed = hullBrushes(pieces, Vec3{100, 0, 0}, Vec3{0, 90, 0}, 2.0, "clip");
    CHECK(placed.size() == 1);
    if (placed.size() == 1) {
        const ConvexBrush b = brushFromPrimitive(placed[0]);
        CHECK(isValidBrush(b));
        const AABB bb = brushBounds(b);
        CHECK(nearTo(bb.mins.x, 98) && nearTo(bb.mins.y, 0) && nearTo(bb.mins.z, 0));
        CHECK(nearTo(bb.maxs.x, 100) && nearTo(bb.maxs.y, 2) && nearTo(bb.maxs.z, 2));
        bool clipFaces = false;
        for (const auto& line : placed[0].lines)
            if (line.text.find(" clip ") != std::string::npos) clipFaces = true;
        CHECK(clipFaces);
    }

    CHECK(hullBrushes(pieces, Vec3{}, Vec3{}, 0.0, "clip").empty());
}

// Map-agent report 2026-11-11 / 14: a corrugated metal awning (nearly coplanar points, many duplicates) crashed the build
// in quickHullFaces (a directed edge lost its twin). Such sheets give planes without throwing, and every piece of a
// model gets collision (a hull, else its box).
TEST(convex_hull_corrugated_sheet_does_not_throw) {
    ImportedModel m;
    ImportedMesh mesh;
    for (int y = 0; y <= 40; ++y)
        for (int x = 0; x <= 160; ++x) {
            // 128 x 32 units, ripples of 0.002 units: nearly flat; every point twice (the GLB's split vertices).
            const Vec3 p{x * 0.8, y * 0.8, 0.002 * std::sin(x * 0.7) + 1e-9 * (x % 3)};
            mesh.pos.push_back(p);
            mesh.pos.push_back(p);
        }
    for (size_t i = 0; i + 2 < mesh.pos.size(); i += 3) mesh.idx.insert(mesh.idx.end(), {uint32_t(i), uint32_t(i + 1), uint32_t(i + 2)});
    m.meshes.push_back(mesh);
    bool threw = false;
    std::vector<std::vector<Plane>> pieces;
    try {
        pieces = convexPieces(m, 4, 24);
    } catch (...) {
        threw = true;
    }
    CHECK(!threw && !pieces.empty());
    for (const auto& pc : pieces) CHECK(pc.size() >= 4);
}
