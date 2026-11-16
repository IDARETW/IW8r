// Convex collision for custom models (src/core/convex_hull.h): hulls, plane budgets, pieces and placed brushes.
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "brush_ops.h"
#include "convex_hull.h"
#include "geometry.h"
#include "glb_mesh.h"
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

// Walking Hideout in the play preview (2026-11-15) overflowed the stack in splitConcave: a long thin diagonal triangle
// rasterises to cells that touch only at their corners (separate 4-connected groups), every triangle's centre lies in
// one of them, and the "split" gave the same triangles back at the same depth forever.
TEST(convex_pieces_thin_diagonal_part_does_not_recurse_forever) {
    ImportedModel m;
    ImportedMesh mesh;
    const Vec3 c{100.5, 99.5, 0};
    mesh.pos = {Vec3{0, 0, 0}, Vec3{200, 200, 0}, c, c + Vec3{1, 0, 0}, c + Vec3{0, 1, 0}, c + Vec3{-1, 0, 0}, c + Vec3{0, -1, 0}};
    mesh.idx = {0, 1, 2, 2, 3, 4, 2, 4, 5, 2, 5, 6};
    m.meshes.push_back(mesh);
    const auto pieces = convexPieces(m, 8, 32);
    CHECK(!pieces.empty());
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

// Map-agent report 2026-11-14: a door frame is one connected mesh, and its one hull filled the doorway. A notched part
// is split (jambs, header), so the opening stays free and the frame still collides; a plain box stays one piece.
TEST(convex_pieces_keep_a_door_frame_open) {
    ImportedModel m;
    ImportedMesh mesh;
    addBox(mesh, Vec3{0, 0, 0}, Vec3{6, 6, 96});    // left jamb
    addBox(mesh, Vec3{6, 0, 88}, Vec3{42, 6, 96});  // header (shares the jambs' top corners: one connected mesh)
    addBox(mesh, Vec3{42, 0, 0}, Vec3{48, 6, 96});  // right jamb
    m.meshes.push_back(mesh);
    const auto pieces = convexPieces(m, 4, 24);
    CHECK(pieces.size() >= 2 && pieces.size() <= 4);
    const auto inside = [&](const Vec3& p) {
        for (const auto& pc : pieces) {
            bool in = true;
            for (const Plane& pl : pc)
                if (dot(pl.n, p) > pl.d - 1e-6) in = false;
            if (in) return true;
        }
        return false;
    };
    CHECK(!inside(Vec3{24, 3, 40}));  // the doorway
    CHECK(!inside(Vec3{8, 3, 80}));   // beside a jamb, under the header
    CHECK(!inside(Vec3{40, 3, 10}));
    CHECK(inside(Vec3{3, 3, 40}));    // the jambs and the header still collide
    CHECK(inside(Vec3{45, 3, 40}));
    CHECK(inside(Vec3{24, 3, 92}));
    std::vector<Vec3> all = mesh.pos;  // every point of the frame is inside some piece (within its planes)
    for (const Vec3& p : all) {
        bool covered = false;
        for (const auto& pc : pieces) covered = covered || containsAll(pc, {p}, 1e-6);
        CHECK(covered);
    }

    // The same frame in metres (a custom model's file units): split the same way.
    ImportedModel metres = m;
    for (Vec3& p : metres.meshes[0].pos) p = p * 0.0254;
    CHECK(convexPieces(metres, 4, 24).size() == pieces.size());

    // The stock frame of the report (tm/door_frame_wooden_panel_03_painted_mp, from the model cache when present; file
    // space is Y-up: map z is y). Its doorway at mid height is free, its jambs collide.
    const char* lad = std::getenv("LOCALAPPDATA");
    const std::string glb = std::string(lad ? lad : "") +
                            "/IW8 Radiant/IW8 Radiant/model_cache/glb/tm_door_frame_wooden_panel_03_painted_mp.glb";
    ImportedModel frame;
    GlbReadOptions ro;
    ro.scale = 1.0 / 0.0254;  // as the model cache reads it: glTF metres to inches, Y-up
    if (lad && readGlb(glb, frame, nullptr, ro)) {
        const auto fp = convexPieces(frame, 4, 24);
        const auto in = [&](const Vec3& p) {
            for (const auto& pc : fp) {
                bool all = true;
                for (const Plane& pl : pc)
                    if (dot(pl.n, p) > pl.d - 1e-6) all = false;
                if (all) return true;
            }
            return false;
        };
        CHECK(!fp.empty());
        CHECK(!in(Vec3{0, 50, 0}));
        CHECK(!in(Vec3{-20, 30, 0}));
        CHECK(!in(Vec3{20, 80, 0}));
        int jambs = 0, header = 0;  // some point across each jamb's width, and near the top, collides
        for (double x = 24; x <= 32; x += 0.5)
            for (double z = -6; z <= 8; z += 1) {
                jambs += in(Vec3{-x, 50, z}) ? 1 : 0;
                jambs += in(Vec3{x, 50, z}) ? 100 : 0;
            }
        for (double y = 88; y <= 100; y += 0.5)
            for (double z = -6; z <= 8; z += 1) header += in(Vec3{0, y, z}) ? 1 : 0;
        CHECK(jambs % 100 > 0 && jambs / 100 > 0);
        CHECK(header > 0);
        std::printf("  stock door frame: %zu pieces\n", fp.size());
    } else {
        std::printf("  (stock door frame not in the model cache: real-frame check skipped)\n");
    }

    ImportedModel box;
    ImportedMesh bm;
    addBox(bm, Vec3{0, 0, 0}, Vec3{64, 48, 96});
    box.meshes.push_back(bm);
    const auto one = convexPieces(box, 4, 24);
    CHECK(one.size() == 1);
    if (one.size() == 1) CHECK(one[0].size() == 6);
}
