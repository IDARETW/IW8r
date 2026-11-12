// Patch tool tests: caps, weld, redisperse, split, terrain, noise, smoothing and raise/lower (src/core/patch_ops2.cpp).
#include <cmath>
#include <optional>
#include <string>
#include <vector>

#include "map_text.h"
#include "patch_ops.h"
#include "patch_ops2.h"
#include "patch_tess.h"
#include "test_harness.h"

using namespace iw8r;

namespace {

constexpr double kEps = 1e-6;

// Puts a primitive text into a worldspawn and reads it back.
std::optional<Primitive> parseOnePrim(const std::string& primText) {
    const std::string text = "iwmap 4\n// entity 0\n{\n\"classname\" \"worldspawn\"\n" + primText + "}\n";
    Id next = 1;
    const ParseResult r = parseMap(text, next);
    if (!r.map || r.map->entities.empty()) return std::nullopt;
    for (const auto& it : r.map->entities[0].items)
        if (it.isPrimitive()) return *it.prim();
    return std::nullopt;
}

Primitive patchPrim(const PatchGrid& g) {
    auto p = parseOnePrim(makePatchPrimitive(g, "\n", "// patch").text());
    return p ? *p : Primitive{};
}

bool samePos(const PatchGrid& a, const PatchGrid& b) {
    if (a.rows != b.rows || a.cols != b.cols || a.pos.size() != b.pos.size() || a.st.size() != b.st.size())
        return false;
    for (size_t i = 0; i < a.pos.size(); ++i) {
        if (length(a.pos[i] - b.pos[i]) > kEps) return false;
        if (std::fabs(a.st[i][0] - b.st[i][0]) > kEps || std::fabs(a.st[i][1] - b.st[i][1]) > kEps) return false;
    }
    return true;
}

// Writes the grid as patch text, reparses it as a primitive and checks the grid comes back unchanged.
bool reparseOk(const PatchGrid& g) {
    Primitive p = makePatchPrimitive(g, "\r\n", "// t");
    reparsePrimitive(p);
    if (p.kind != PrimKind::Patch) return false;
    const auto back = patchGrid(p);
    if (!back) return false;
    return samePos(*back, g) && patchPrim(g).kind == PrimKind::Patch;
}

size_t at(const PatchGrid& g, int r, int c) { return static_cast<size_t>(r) * static_cast<size_t>(g.cols) + static_cast<size_t>(c); }

AABB boxOf(const Vec3& lo, const Vec3& hi) {
    AABB b;
    b.add(lo);
    b.add(hi);
    return b;
}

double zVariance(const PatchGrid& g) {
    double mean = 0;
    for (const Vec3& p : g.pos) mean += p.z;
    mean /= static_cast<double>(g.pos.size());
    double var = 0;
    for (const Vec3& p : g.pos) var += (p.z - mean) * (p.z - mean);
    return var / static_cast<double>(g.pos.size());
}

}  // namespace

TEST(patch_ops2_cap_cylinder_closes_both_ends) {
    const PatchGrid cyl = makeCylinderPatch(boxOf({-64, -32, 0}, {64, 32, 128}), "cyl");
    const auto caps = capPatch(cyl);
    CHECK(caps.size() == 2);
    if (caps.size() != 2) return;

    for (int end = 0; end < 2; ++end) {
        const PatchGrid& cap = caps[static_cast<size_t>(end)];
        const int ringRow = end == 0 ? 0 : 2;
        CHECK(cap.rows == 3 && cap.cols == 9);
        CHECK(cap.keyword == "mesh");
        CHECK(cap.material == "cyl");
        CHECK(reparseOk(cap));

        // Row 0 of the cap holds the cylinder's end ring. The bottom cap is inverted to face outward, which
        // reverses the column order, so each ring point is matched anywhere in the row.
        for (int k = 0; k < 9; ++k) {
            bool inRow = false;
            for (int c = 0; c < 9; ++c)
                if (length(cap.pos[at(cap, 0, c)] - cyl.pos[at(cyl, ringRow, k)]) < kEps) inRow = true;
            CHECK(inRow);
        }
        // Closed ring: the first and last columns coincide.
        CHECK(length(cap.pos[at(cap, 0, 0)] - cap.pos[at(cap, 0, 8)]) < kEps);
        // The last row collapses to the ring centre, which lies on the ring's plane.
        const Vec3 centre = cap.pos[at(cap, 2, 0)];
        CHECK(std::fabs(centre.z - cyl.pos[at(cyl, ringRow, 0)].z) < kEps);
        for (int k = 0; k < 9; ++k) CHECK(length(cap.pos[at(cap, 2, k)] - centre) < kEps);

        // Tessellation (mesh: control points, no smoothing) contains the ring and the centre.
        const Primitive p = patchPrim(cap);
        const PatchMesh m = tessellatePrimitive(p, 1);
        CHECK(!m.points.empty());
        CHECK(m.normals.size() == m.points.size());
        for (int k = 0; k < 9; ++k) {
            const Vec3 ring = cyl.pos[at(cyl, ringRow, k)];
            bool found = false;
            for (const Vec3& q : m.points)
                if (length(q - ring) < 1e-6) found = true;
            CHECK(found);
        }
        // Faces point away from the cylinder: bottom cap down (-z), top cap up (+z).
        if (!m.normals.empty()) {
            const double outward = end == 0 ? -1.0 : 1.0;
            CHECK(outward * m.normals[0].z > 0.5);
        }
    }
}

TEST(patch_ops2_cap_rejects_small_grids) {
    CHECK(capPatch(PatchGrid{}).empty());
    PatchGrid one = makeFlatPatch(boxOf({0, 0, 0}, {64, 64, 0}), 2, 2, 2, "m");
    one.rows = 1;
    one.pos.resize(2);
    one.st.resize(2);
    CHECK(capPatch(one).empty());
}

TEST(patch_ops2_weld_shared_edge_gives_3x5) {
    const PatchGrid a = makeFlatPatch(boxOf({0, 0, 0}, {64, 64, 0}), 2, 3, 3, "m");
    const PatchGrid b = makeFlatPatch(boxOf({64, 0, 0}, {128, 64, 0}), 2, 3, 3, "m");
    const auto w = weldPatches(a, b);
    CHECK(w.has_value());
    if (!w) return;
    CHECK(w->rows == 3 && w->cols == 5);
    CHECK(reparseOk(*w));
    // Columns 0..2 are a, columns 3..4 are b's columns 1..2 (the shared column comes from a).
    for (int r = 0; r < 3; ++r)
        for (int k = 0; k < 5; ++k) {
            const Vec3 expect = k <= 2 ? a.pos[at(a, r, k)] : b.pos[at(b, r, k - 2)];
            CHECK(length(w->pos[at(*w, r, k)] - expect) < kEps);
        }
    // Facing follows a (a flat +Z patch).
    const PatchMesh m = tessellatePrimitive(patchPrim(*w), 1);
    CHECK(!m.normals.empty() && m.normals[0].z > 0.5);
}

TEST(patch_ops2_weld_row_edge_gives_5x3) {
    const PatchGrid a = makeFlatPatch(boxOf({0, 0, 0}, {64, 64, 0}), 2, 3, 3, "m");
    const PatchGrid above = makeFlatPatch(boxOf({0, 64, 0}, {64, 128, 0}), 2, 3, 3, "m");
    const auto w = weldPatches(a, above);
    CHECK(w.has_value());
    if (!w) return;
    CHECK(w->rows == 5 && w->cols == 3);
    CHECK(reparseOk(*w));
    // Rows 0..2 are a, rows 3..4 are above's rows 1..2.
    for (int c = 0; c < 3; ++c)
        for (int r = 0; r < 5; ++r) {
            const Vec3 expect = r <= 2 ? a.pos[at(a, r, c)] : above.pos[at(above, r - 2, c)];
            CHECK(length(w->pos[at(*w, r, c)] - expect) < kEps);
        }
    // The other order works too: the patch above welded under a.
    const auto w2 = weldPatches(above, a);
    CHECK(w2.has_value() && w2->rows == 5 && w2->cols == 3);
}

TEST(patch_ops2_weld_reversed_edge_and_flipped_grid) {
    const PatchGrid a = makeFlatPatch(boxOf({0, 0, 0}, {64, 64, 0}), 2, 3, 3, "m");
    const PatchGrid b = makeFlatPatch(boxOf({64, 0, 0}, {128, 64, 0}), 2, 3, 3, "m");

    // b with its rows reversed: its shared column runs the other way.
    PatchGrid bRev = b;
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c) bRev.pos[at(bRev, r, c)] = b.pos[at(b, 2 - r, c)];
    const auto w = weldPatches(a, bRev);
    CHECK(w.has_value());
    if (w) {
        CHECK(w->rows == 3 && w->cols == 5);
        for (int r = 0; r < 3; ++r)
            for (int k = 3; k <= 4; ++k) CHECK(length(w->pos[at(*w, r, k)] - b.pos[at(b, r, k - 2)]) < kEps);
    }

    // invertPatch(b) reverses the columns, so the shared edge is its last column.
    const auto w2 = weldPatches(a, invertPatch(b));
    CHECK(w2.has_value() && w2->rows == 3 && w2->cols == 5);
}

TEST(patch_ops2_weld_refuses_non_shared_edges) {
    const PatchGrid a = makeFlatPatch(boxOf({0, 0, 0}, {64, 64, 0}), 2, 3, 3, "m");

    // Far away: no edge in common.
    CHECK(!weldPatches(a, makeFlatPatch(boxOf({200, 0, 0}, {264, 64, 0}), 2, 3, 3, "m")).has_value());

    // Edge offset by 0.5 units: outside the default tolerance, inside a larger one.
    const PatchGrid off = makeFlatPatch(boxOf({64.5, 0, 0}, {128.5, 64, 0}), 2, 3, 3, "m");
    CHECK(!weldPatches(a, off).has_value());
    CHECK(weldPatches(a, off, 1.0).has_value());

    // Different edge lengths.
    CHECK(!weldPatches(a, makeFlatPatch(boxOf({64, 0, 0}, {128, 64, 0}), 2, 5, 3, "m")).has_value());

    // Different keywords.
    PatchGrid curve = makeFlatPatch(boxOf({64, 0, 0}, {128, 64, 0}), 2, 3, 3, "m");
    curve.keyword = "curve";
    CHECK(!weldPatches(a, curve).has_value());
}

TEST(patch_ops2_redisperse_keeps_corners_and_spaces_evenly) {
    const AABB box = boxOf({0, 0, 0}, {40, 20, 0});

    // Row 1: straight along X at y = 10, with uneven x. Ends are 0 and 10, so the even spacing is 2.5.
    PatchGrid g = makeFlatPatch(box, 2, 3, 5, "m");
    const double xs[5] = {0, 1, 5, 6, 10};
    for (int c = 0; c < 5; ++c) g.pos[at(g, 1, c)] = {xs[c], 10, 0};
    const PatchGrid rows = redisperse(g, true, false);
    CHECK(rows.rows == 3 && rows.cols == 5);
    for (int r : {0, 2})
        for (int c : {0, 4}) CHECK(length(rows.pos[at(rows, r, c)] - g.pos[at(g, r, c)]) < kEps);
    for (int c = 0; c < 5; ++c) {
        CHECK(std::fabs(rows.pos[at(rows, 1, c)].x - 2.5 * c) < 1e-6);
        CHECK(std::fabs(rows.pos[at(rows, 1, c)].y - 10) < kEps);
    }
    CHECK(reparseOk(rows));

    // Column 2: straight along Y at x = 20 with y = 0, 3, 20. The interior point moves to the middle, y = 10.
    PatchGrid h = makeFlatPatch(box, 2, 3, 5, "m");
    h.pos[at(h, 1, 2)].y = 3;
    const PatchGrid cols = redisperse(h, false, true);
    CHECK(std::fabs(cols.pos[at(cols, 1, 2)].y - 10) < 1e-6);
    CHECK(std::fabs(cols.pos[at(cols, 1, 2)].x - 20) < kEps);
    for (int r : {0, 2})
        for (int c : {0, 4}) CHECK(length(cols.pos[at(cols, r, c)] - h.pos[at(h, r, c)]) < kEps);
}

TEST(patch_ops2_split_round_trip_with_weld) {
    const PatchGrid g = makeFlatPatch(boxOf({0, 0, 0}, {64, 128, 0}), 2, 5, 3, "m");

    const auto rowParts = splitPatch(g, true, 2);
    CHECK(rowParts.size() == 2);
    if (rowParts.size() == 2) {
        CHECK(rowParts[0].rows == 3 && rowParts[0].cols == 3);
        CHECK(rowParts[1].rows == 3 && rowParts[1].cols == 3);
        CHECK(reparseOk(rowParts[0]) && reparseOk(rowParts[1]));
        const auto w = weldPatches(rowParts[0], rowParts[1]);
        CHECK(w.has_value());
        if (w) CHECK(samePos(*w, g));
    }

    const PatchGrid h = makeFlatPatch(boxOf({0, 0, 0}, {128, 64, 0}), 2, 3, 5, "m");
    const auto colParts = splitPatch(h, false, 2);
    CHECK(colParts.size() == 2);
    if (colParts.size() == 2) {
        CHECK(colParts[0].rows == 3 && colParts[0].cols == 3);
        CHECK(colParts[1].rows == 3 && colParts[1].cols == 3);
        const auto w = weldPatches(colParts[0], colParts[1]);
        CHECK(w.has_value());
        if (w) CHECK(samePos(*w, h));
    }

    // Invalid split points: odd index, too close to an end, or an even total.
    CHECK(splitPatch(g, true, 1).empty());
    CHECK(splitPatch(g, true, 3).empty());
    CHECK(splitPatch(makeFlatPatch(boxOf({0, 0, 0}, {64, 128, 0}), 2, 4, 3, "m"), true, 2).empty());
}

TEST(patch_ops2_terrain_grid_size_and_z) {
    const AABB box = boxOf({-128, -64, -16}, {128, 64, 48});
    const PatchGrid t = makeTerrainPatch(box, 5, 9, "tm/ground");
    CHECK(t.rows == 5 && t.cols == 9);
    CHECK(t.pos.size() == 45 && t.st.size() == 45);
    CHECK(t.keyword == "mesh");
    CHECK(t.material == "tm/ground");
    for (const Vec3& p : t.pos) {
        CHECK(std::fabs(p.z - (-16)) < kEps);
        CHECK(p.x >= -128 - kEps && p.x <= 128 + kEps);
        CHECK(p.y >= -64 - kEps && p.y <= 64 + kEps);
    }
    CHECK(length(t.pos[0] - Vec3{-128, -64, -16}) < kEps);
    CHECK(length(t.pos[44] - Vec3{128, 64, -16}) < kEps);
    // Texture by world size: 256 x 128 units over 512 units per repeat.
    CHECK(std::fabs(t.st[44][0] - 0.5) < kEps && std::fabs(t.st[44][1] - 0.25) < kEps);
    CHECK(reparseOk(t));

    CHECK(makeTerrainPatch(box, 1, 9, "m").rows == 0);
}

TEST(patch_ops2_noise_deterministic_and_bounded) {
    const PatchGrid base = makeTerrainPatch(boxOf({-128, -64, -16}, {128, 64, 48}), 9, 9, "m");

    const PatchGrid n1 = applyNoise(base, 4.0, 7);
    CHECK(samePos(n1, applyNoise(base, 4.0, 7)));
    CHECK(!samePos(n1, applyNoise(base, 4.0, 8)));
    CHECK(samePos(applyNoise(base, 0.0, 7), base));

    bool moved = false;
    for (size_t i = 0; i < base.pos.size(); ++i) {
        const double dz = n1.pos[i].z - base.pos[i].z;
        CHECK(std::fabs(dz) <= 4.0 + kEps);
        if (std::fabs(dz) > 0.1) moved = true;
        CHECK(n1.pos[i].x == base.pos[i].x && n1.pos[i].y == base.pos[i].y);  // zOnly leaves x, y alone
    }
    CHECK(moved);
    CHECK(reparseOk(n1));

    // Full 3D noise also moves x and y, each within the amplitude.
    const PatchGrid n3 = applyNoise(base, 4.0, 7, false);
    bool movedX = false;
    for (size_t i = 0; i < base.pos.size(); ++i) {
        CHECK(std::fabs(n3.pos[i].x - base.pos[i].x) <= 4.0 + kEps);
        CHECK(std::fabs(n3.pos[i].y - base.pos[i].y) <= 4.0 + kEps);
        CHECK(std::fabs(n3.pos[i].z - base.pos[i].z) <= 4.0 + kEps);
        if (std::fabs(n3.pos[i].x - base.pos[i].x) > 0.1) movedX = true;
    }
    CHECK(movedX);
}

TEST(patch_ops2_smooth_reduces_variance_keeps_border) {
    PatchGrid g = makeFlatPatch(boxOf({0, 0, 0}, {96, 96, 0}), 2, 7, 7, "m");
    for (int r = 1; r < 6; ++r)
        for (int c = 1; c < 6; ++c) g.pos[at(g, r, c)].z = 10.0 * ((r * 7 + c * 13) % 5) - 20.0;

    const PatchGrid s = smoothPatch(g, 3);
    CHECK(zVariance(s) < zVariance(g));
    for (int r = 0; r < 7; ++r)
        for (int c = 0; c < 7; ++c) {
            const size_t k = at(g, r, c);
            const bool border = r == 0 || r == 6 || c == 0 || c == 6;
            if (border) CHECK(s.pos[k] == g.pos[k]);
            CHECK(std::fabs(s.pos[k].x - g.pos[k].x) < kEps && std::fabs(s.pos[k].y - g.pos[k].y) < kEps);
        }
    CHECK(samePos(smoothPatch(g, 0), g));
    CHECK(reparseOk(s));
}

TEST(patch_ops2_raise_lower_only_inside_radius) {
    const PatchGrid base = makeFlatPatch(boxOf({0, 0, 0}, {128, 128, 0}), 2, 9, 9, "m");  // 16 unit spacing, z = 0
    const Vec3 centre{64, 64, 0};
    const double radius = 40, amount = 10;

    const PatchGrid r = raiseLower(base, centre, radius, amount);
    for (size_t i = 0; i < base.pos.size(); ++i) {
        const double d = std::hypot(base.pos[i].x - centre.x, base.pos[i].y - centre.y);
        CHECK(r.pos[i].x == base.pos[i].x && r.pos[i].y == base.pos[i].y);
        if (d >= radius) {
            CHECK(r.pos[i].z == base.pos[i].z);
        } else {
            CHECK(r.pos[i].z > 0.0 && r.pos[i].z <= amount + kEps);
        }
    }
    CHECK(std::fabs(r.pos[at(r, 4, 4)].z - amount) < kEps);  // centre point gets the full amount
    CHECK(reparseOk(r));

    // Lowering, and the no-op cases.
    CHECK(std::fabs(raiseLower(base, centre, radius, -5).pos[at(base, 4, 4)].z + 5) < kEps);
    CHECK(samePos(raiseLower(base, centre, 0, amount), base));
    CHECK(samePos(raiseLower(base, centre, radius, 0), base));
}
