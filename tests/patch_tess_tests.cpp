// Patch tessellation tests (src/core/patch_tess.cpp).
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "map_text.h"
#include "patch_tess.h"
#include "test_harness.h"

using namespace iw8r;

namespace {

constexpr double kEps = 1e-9;

// Control point helper: row r, column c.
Vec3 pt(int r, int c, double z = 0) { return {c * 10.0, r * 10.0, z}; }

std::vector<Vec3> grid(int rows, int cols, double (*zOf)(int, int) = nullptr) {
    std::vector<Vec3> g;
    for (int r = 0; r < rows; ++r)
        for (int c = 0; c < cols; ++c) g.push_back(pt(r, c, zOf ? zOf(r, c) : 0.0));
    return g;
}

double bumpZ(int r, int c) { return std::sin(0.7 * r) * 3.0 + std::cos(0.9 * c) * 2.0 + 0.1 * r * c; }

bool sameMesh(const PatchMesh& a, const PatchMesh& b) {
    if (a.width != b.width || a.height != b.height) return false;
    if (a.indices != b.indices) return false;
    if (a.points.size() != b.points.size() || a.normals.size() != b.normals.size()) return false;
    for (size_t i = 0; i < a.points.size(); ++i)
        if (a.points[i] != b.points[i]) return false;
    for (size_t i = 0; i < a.normals.size(); ++i)
        if (a.normals[i] != b.normals[i]) return false;
    return true;
}

std::string num(double v) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.17g", v);
    return buf;
}

// Builds a one-brush map holding a patch. Groups are control rows; each group lists `cols` vertices.
std::string patchMapText(const std::string& keyword, const std::vector<Vec3>& ctrl, int rows, int cols) {
    std::string s =
        "iwmap 4\n"
        "// entity 0\n"
        "{\n"
        "\"classname\" \"worldspawn\"\n"
        "// brush 0\n"
        "{\n"
        " " + keyword + "\n"
        " {\n"
        "  me_test_patch\n"
        "  lightmap_gray\n"
        "  " + std::to_string(rows) + " " + std::to_string(cols) + " 16 8\n";
    for (int r = 0; r < rows; ++r) {
        s += "  (\n";
        for (int c = 0; c < cols; ++c) {
            const Vec3& p = ctrl[static_cast<size_t>(r) * cols + c];
            s += "\tv " + num(p.x) + " " + num(p.y) + " " + num(p.z) + " t 0 0 0 0\n";
        }
        s += "  )\n";
    }
    s += " }\n}\n}\n";
    return s;
}

const Primitive* firstPatch(const ParseResult& r) {
    if (!r.map || r.map->entities.empty()) return nullptr;
    for (const auto& it : r.map->entities[0].items)
        if (it.isPrimitive() && it.prim()->kind == PrimKind::Patch) return it.prim();
    return nullptr;
}

}  // namespace

TEST(patch_tess_flat_curve_stays_in_plane) {
    const auto ctrl = grid(3, 3);
    for (int subdiv : {1, 2, 4}) {
        const PatchMesh m = tessellatePatch(ctrl, 3, 3, "curve", subdiv);
        CHECK(m.points.size() == static_cast<size_t>((subdiv + 1) * (subdiv + 1)));
        CHECK(m.width == subdiv + 1 && m.height == subdiv + 1);
        bool flat = true;
        for (const auto& p : m.points) flat = flat && std::fabs(p.z) < kEps;
        CHECK(flat);
    }
}

TEST(patch_tess_curve_corners_exact) {
    const auto ctrl = grid(3, 3, bumpZ);
    const PatchMesh m = tessellatePatch(ctrl, 3, 3, "curve", 4);
    CHECK(m.points.front() == ctrl[0]);
    CHECK(m.points[m.width - 1] == ctrl[2]);
    CHECK(m.points[static_cast<size_t>(m.height - 1) * m.width] == ctrl[6]);
    CHECK(m.points.back() == ctrl[8]);
}

TEST(patch_tess_arch_midpoint_matches_bezier) {
    // Middle row lifted to z = 8: the arch midpoint is 0.25*a + 0.5*b + 0.25*c over the middle column.
    std::vector<Vec3> ctrl;
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c) ctrl.push_back(pt(r, c, r == 1 ? 8.0 : 0.0));
    const Vec3 a = ctrl[1], b = ctrl[4], c = ctrl[7];
    const Vec3 expect = a * 0.25 + b * 0.5 + c * 0.25;
    CHECK(std::fabs(expect.z - 4.0) < kEps);

    const PatchMesh m = tessellatePatch(ctrl, 3, 3, "curve", 2);
    const Vec3 mid = m.points[1 * m.width + 1];
    CHECK(std::fabs(mid.x - expect.x) < kEps);
    CHECK(std::fabs(mid.y - expect.y) < kEps);
    CHECK(std::fabs(mid.z - expect.z) < kEps);
}

TEST(patch_tess_5x3_shares_seam) {
    // Rows 0..2 and 2..4 are two sub-patches sharing row 2; one column of sub-patches.
    const auto ctrl = grid(5, 3, bumpZ);
    const int subdiv = 3;
    const PatchMesh m = tessellatePatch(ctrl, 5, 3, "curve", subdiv);
    CHECK(m.width == subdiv + 1);
    CHECK(m.height == 2 * subdiv + 1);
    CHECK(m.points.size() == static_cast<size_t>((2 * subdiv + 1) * (subdiv + 1)));

    // No two grid points coincide, so the seam row is not emitted twice.
    bool duplicate = false;
    for (size_t i = 0; i < m.points.size(); ++i)
        for (size_t j = i + 1; j < m.points.size(); ++j)
            if (length(m.points[i] - m.points[j]) < kEps) duplicate = true;
    CHECK(!duplicate);

    // The seam row (point row == subdiv) starts at control row 2, which both sub-patches share.
    const Vec3 seamStart = m.points[static_cast<size_t>(subdiv) * m.width];
    CHECK(std::fabs(seamStart.x - ctrl[2 * 3 + 0].x) < kEps);
    CHECK(std::fabs(seamStart.y - ctrl[2 * 3 + 0].y) < kEps);
    CHECK(std::fabs(seamStart.z - ctrl[2 * 3 + 0].z) < kEps);
}

TEST(patch_tess_mesh_2x2_two_triangles) {
    const auto ctrl = grid(2, 2, bumpZ);
    const PatchMesh m = tessellatePatch(ctrl, 2, 2, "mesh", 8);
    CHECK(m.points.size() == 4);
    CHECK(m.indices.size() == 6);  // two triangles, no smoothing regardless of subdiv
    CHECK(m.points[0] == ctrl[0] && m.points[3] == ctrl[3]);
}

TEST(patch_tess_invalid_curve_dims_triangulate_control_grid) {
    // Even dimensions are not a valid biquadratic curve: fall back to the raw control grid.
    const auto ctrl = grid(4, 3, bumpZ);
    const PatchMesh m = tessellatePatch(ctrl, 4, 3, "curve", 4);
    CHECK(m.points == ctrl);
    CHECK(m.indices.size() == static_cast<size_t>(2 * 3 * 2) * 3);
}

TEST(patch_tess_indices_and_normals_valid) {
    const auto ctrl = grid(5, 5, bumpZ);
    for (const char* kw : {"curve", "mesh"}) {
        const PatchMesh m = tessellatePatch(ctrl, 5, 5, kw, 4);
        CHECK(!m.indices.empty());
        CHECK(m.indices.size() % 3 == 0);
        bool inRange = true;
        for (uint32_t idx : m.indices) inRange = inRange && idx < m.points.size();
        CHECK(inRange);
        CHECK(m.normals.size() == m.points.size());
        bool unit = true;
        for (const auto& n : m.normals) unit = unit && std::fabs(length(n) - 1.0) < kEps;
        CHECK(unit);
    }
}

TEST(patch_tess_flat_normals_point_up) {
    const PatchMesh m = tessellatePatch(grid(3, 3), 3, 3, "curve", 2);
    bool up = true;
    for (const auto& n : m.normals) up = up && std::fabs(n.z - 1.0) < kEps && std::fabs(n.x) < kEps && std::fabs(n.y) < kEps;
    CHECK(up);
}

TEST(patch_tess_bad_input_is_empty) {
    CHECK(tessellatePatch(grid(3, 3), 3, 4, "curve", 2).points.empty());  // size mismatch
    CHECK(tessellatePatch({}, 0, 0, "mesh", 2).points.empty());
}

TEST(patch_tess_primitive_matches_direct) {
    const auto ctrl = grid(5, 3, bumpZ);
    const std::string text = patchMapText("curve", ctrl, 5, 3);
    Id nextId = 1;
    const ParseResult parsed = parseMap(text, nextId);
    CHECK(parsed.map.has_value());
    const Primitive* prim = firstPatch(parsed);
    CHECK(prim != nullptr);
    if (!prim) return;
    CHECK(prim->patchKeyword == "curve");
    CHECK(prim->verts.size() == ctrl.size());

    const PatchMesh fromText = tessellatePrimitive(*prim, 3);
    const PatchMesh direct = tessellatePatch(ctrl, 5, 3, "curve", 3);
    CHECK(!fromText.points.empty());
    CHECK(sameMesh(fromText, direct));
}

TEST(patch_tess_primitive_mesh_from_text) {
    const auto ctrl = grid(2, 2, bumpZ);
    const std::string text = patchMapText("mesh", ctrl, 2, 2);
    Id nextId = 1;
    const ParseResult parsed = parseMap(text, nextId);
    const Primitive* prim = firstPatch(parsed);
    CHECK(prim != nullptr);
    if (!prim) return;
    const PatchMesh m = tessellatePrimitive(*prim, 4);
    CHECK(m.indices.size() == 6);
    CHECK(m.points == ctrl);
}
