// Generator tests: rope/wire patches, mesh painter scatter and drop to floor (src/core/generators.cpp).
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "brush_ops.h"
#include "generators.h"
#include "geometry.h"
#include "leak_check.h"
#include "map_text.h"
#include "patch_ops.h"
#include "test_harness.h"

using namespace iw8r;

namespace {

namespace fs = std::filesystem;

constexpr double kEps = 1e-6;
constexpr double kPi = 3.14159265358979323846;

// Worldspawn holding the given primitive texts (each ends with a newline).
std::optional<MapFile> mapFromPrims(const std::vector<std::string>& prims) {
    std::string text = "iwmap 4\n// entity 0\n{\n\"classname\" \"worldspawn\"\n";
    for (const std::string& p : prims) text += p;
    text += "}\n";
    Id next = 1;
    ParseResult r = parseMap(text, next);
    if (!r.map) return std::nullopt;
    return std::move(*r.map);
}

std::optional<std::string> readSample() {
    fs::path p = fs::current_path();
    for (int up = 0; up < 6; ++up, p = p.parent_path()) {
        const fs::path cand = p / "tests" / "data" / "iw8_sample_room.map";
        if (fs::exists(cand)) {
            std::string text;
            if (readFile(cand.string(), text)) return text;
            return std::nullopt;
        }
    }
    return std::nullopt;
}

// Point inside a convex XY winding (either orientation), with tolerance.
bool insideXY(const Winding& w, const Vec3& q) {
    double area2 = 0;
    for (size_t i = 0; i < w.size(); ++i) {
        const Vec3& a = w[i];
        const Vec3& b = w[(i + 1) % w.size()];
        area2 += a.x * b.y - b.x * a.y;
    }
    const double sign = area2 >= 0 ? 1.0 : -1.0;
    for (size_t i = 0; i < w.size(); ++i) {
        const Vec3& a = w[i];
        const Vec3& b = w[(i + 1) % w.size()];
        const double cross = (b.x - a.x) * (q.y - a.y) - (b.y - a.y) * (q.x - a.x);
        if (sign * cross < -1e-5 * std::max(1.0, length(b - a))) return false;
    }
    return true;
}

// Up axis (0,0,1) rotated by Ry(pitch) * Rx(roll) * Rz(yaw), the convention documented in generators.h.
Vec3 upFromAngles(const Vec3& angles) {
    const double x = angles.x * kPi / 180.0, y = angles.y * kPi / 180.0, z = angles.z * kPi / 180.0;
    (void)y;  // yaw about the up axis leaves it unchanged
    const Vec3 rx{0, -std::sin(z), std::cos(z)};
    return {std::cos(x) * rx.x + std::sin(x) * rx.z, rx.y, -std::sin(x) * rx.x + std::cos(x) * rx.z};
}

// Checks that a placement lies on an upward face of a solid brush within the slope limit and the circle.
// Returns the matching face's normal through `normalOut` when found.
bool onUpwardFace(const std::vector<SolidBrush>& solids, const Placement& p, const Vec3& center, double radius,
                  double maxSlopeDeg, Vec3* normalOut = nullptr) {
    const double dx = p.origin.x - center.x, dy = p.origin.y - center.y;
    if (std::sqrt(dx * dx + dy * dy) > radius + kEps) return false;
    for (const SolidBrush& b : solids) {
        const std::vector<Winding> ws = brushWindings(b.planes);
        for (size_t i = 0; i < b.planes.size() && i < ws.size(); ++i) {
            const Plane& pl = b.planes[i].plane;
            if (ws[i].size() < 3 || pl.n.z <= 0) continue;
            const double slope = std::acos(std::min(1.0, pl.n.z)) * 180.0 / kPi;
            if (slope > maxSlopeDeg + 1e-6) continue;
            if (std::fabs(dot(pl.n, p.origin) - pl.d) > 1e-5) continue;
            if (!insideXY(ws[i], p.origin)) continue;
            if (normalOut) *normalOut = pl.n;
            return true;
        }
    }
    return false;
}

bool samePlacements(const std::vector<Placement>& a, const std::vector<Placement>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i].origin != b[i].origin || a[i].angles != b[i].angles || a[i].scale != b[i].scale) return false;
    }
    return true;
}

}  // namespace

TEST(gen_rope_curve_endpoints_and_sag) {
    const Vec3 a{0, 0, 100}, b{400, 0, 100};
    const std::vector<Vec3> pts = ropeCurve(a, b, 50, 8);
    CHECK(pts.size() == 9);
    if (pts.size() != 9) return;
    CHECK(pts.front() == a);
    CHECK(pts.back() == b);
    size_t lowest = 0;
    for (size_t i = 1; i < pts.size(); ++i)
        if (pts[i].z < pts[lowest].z) lowest = i;
    CHECK(lowest == 4);
    CHECK(std::fabs(pts[lowest].z - 50.0) < kEps);  // 100 - sag
    CHECK(std::fabs(pts[lowest].x - 200.0) < kEps);
    // Zero sag is a straight line.
    const std::vector<Vec3> flat = ropeCurve(a, b, 0, 4);
    CHECK(flat.size() == 5);
    for (const Vec3& p : flat) CHECK(std::fabs(p.z - 100.0) < kEps);
    // Non-positive segment counts give the two endpoints.
    CHECK(ropeCurve(a, b, 50, 0).size() == 2);
}

TEST(gen_rope_patch_reparses) {
    const Vec3 a{0, 0, 128}, b{256, 64, 128};
    const double radius = 4;
    const PatchGrid g = ropePatch(a, b, 30, radius, 4, "tm2co/rope_mat");
    CHECK(g.rows == 5);
    CHECK(g.cols == 9);
    CHECK(g.pos.size() == 45);
    CHECK(g.st.size() == g.pos.size());
    if (g.pos.size() != 45) return;

    const std::vector<Vec3> curve = ropeCurve(a, b, 30, 4);
    for (int r = 0; r < g.rows; ++r) {
        for (int c = 0; c < g.cols; ++c) {
            const double d = length(g.pos[static_cast<size_t>(r) * g.cols + c] - curve[static_cast<size_t>(r)]);
            CHECK(std::fabs(d - radius) < 1e-6);
        }
        // The last column closes the ring on the first one.
        CHECK(length(g.pos[static_cast<size_t>(r) * g.cols] - g.pos[static_cast<size_t>(r) * g.cols + 8]) < 1e-9);
    }

    const std::string text = "iwmap 4\n// entity 0\n{\n\"classname\" \"worldspawn\"\n" + makePatchPrimitive(g, "\n", "// rope").text() + "}\n";
    Id next = 1;
    const ParseResult pr = parseMap(text, next);
    CHECK(pr.map.has_value());
    if (!pr.map) return;
    const Primitive* prim = nullptr;
    for (const EntityItem& it : pr.map->entities[0].items)
        if (it.isPrimitive()) prim = it.prim();
    CHECK(prim != nullptr);
    if (!prim) return;
    CHECK(prim->kind == PrimKind::Patch);
    const std::optional<PatchGrid> back = patchGrid(*prim);
    CHECK(back.has_value());
    if (!back) return;
    CHECK(back->rows == g.rows && back->cols == g.cols);
    CHECK(back->material == "tm2co/rope_mat");
    CHECK(back->keyword == "curve");
    if (back->pos.size() == g.pos.size())
        for (size_t i = 0; i < g.pos.size(); ++i) CHECK(length(back->pos[i] - g.pos[i]) < 1e-6);

    // Degenerate arguments give an empty grid.
    CHECK(ropePatch(a, b, 30, radius, 1, "m").rows == 0);
    CHECK(ropePatch(a, b, 30, 0, 4, "m").rows == 0);
}

TEST(gen_scatter_fixture_deterministic_and_valid) {
    const std::optional<std::string> text = readSample();
    CHECK(text.has_value());
    if (!text) return;
    Id next = 1;
    const ParseResult pr = parseMap(*text, next);
    CHECK(pr.map.has_value());
    if (!pr.map) return;
    const MapFile& map = *pr.map;
    const std::vector<SolidBrush> solids = collidingBrushes(map, true);

    ScatterOptions opt;
    opt.radius = 256;
    opt.count = 20;
    opt.maxSlopeDeg = 40;
    opt.minSpacing = 24;
    opt.seed = 7;
    const Vec3 center{0, 0, 0};

    const std::vector<Placement> a = scatterOnBrushes(map, center, opt);
    const std::vector<Placement> b = scatterOnBrushes(map, center, opt);
    CHECK(!a.empty());
    CHECK(a.size() == 20);
    CHECK(samePlacements(a, b));

    ScatterOptions other = opt;
    other.seed = 8;
    CHECK(!samePlacements(a, scatterOnBrushes(map, center, other)));

    for (const Placement& p : a) {
        CHECK(onUpwardFace(solids, p, center, opt.radius, opt.maxSlopeDeg));
        CHECK(p.scale >= opt.minScale - kEps && p.scale <= opt.maxScale + kEps);
        CHECK(p.angles.x == 0 && p.angles.z == 0);  // alignToSurface is off
        CHECK(p.angles.y >= 0 && p.angles.y < 360.0);
    }
    for (size_t i = 0; i < a.size(); ++i)
        for (size_t j = i + 1; j < a.size(); ++j) {
            const double dx = a[i].origin.x - a[j].origin.x, dy = a[i].origin.y - a[j].origin.y;
            CHECK(std::sqrt(dx * dx + dy * dy) >= opt.minSpacing - kEps);
        }

    // A tight slope limit keeps placements on flat tops only.
    ScatterOptions flat = opt;
    flat.maxSlopeDeg = 0.5;
    flat.count = 10;
    const std::vector<Placement> f = scatterOnBrushes(map, center, flat);
    CHECK(!f.empty());
    for (const Placement& p : f) {
        CHECK(onUpwardFace(solids, p, center, flat.radius, flat.maxSlopeDeg));
        // Flat tops only: the floor (z = 0) and the pillar top (z = 96).
        CHECK(std::fabs(p.origin.z) < kEps || std::fabs(p.origin.z - 96.0) < kEps);
    }

    // Bad options give nothing.
    ScatterOptions bad = opt;
    bad.radius = 0;
    CHECK(scatterOnBrushes(map, center, bad).empty());
    bad = opt;
    bad.minScale = 2;
    bad.maxScale = 1;
    CHECK(scatterOnBrushes(map, center, bad).empty());
}

TEST(gen_scatter_aligns_to_tilted_face) {
    // A box whose top is tilted 30 degrees about X: its top face is the only upward face the scatter may use.
    const AABB box{{-64, -64, -16}, {64, 64, 16}};
    ConvexBrush brush = makeBox(box, " tm2co/me_curb_stone_01 128 128 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0");
    brush = rotateBrush(brush, 0, 30, {0, 0, 0});
    const std::optional<Primitive> prim = makeBrushPrimitive(brush, "\n");
    CHECK(prim.has_value());
    if (!prim) return;
    const std::optional<MapFile> map = mapFromPrims({prim->text()});
    CHECK(map.has_value());
    if (!map) return;
    const std::vector<SolidBrush> solids = collidingBrushes(*map, true);
    CHECK(solids.size() == 1);

    ScatterOptions opt;
    opt.radius = 40;
    opt.count = 12;
    opt.maxSlopeDeg = 45;
    opt.randomYaw = false;
    opt.alignToSurface = true;
    opt.seed = 3;
    const std::vector<Placement> pl = scatterOnBrushes(*map, {0, 0, 0}, opt);
    CHECK(pl.size() == 12);
    for (const Placement& p : pl) {
        Vec3 n;
        CHECK(onUpwardFace(solids, p, {0, 0, 0}, opt.radius, opt.maxSlopeDeg, &n));
        // The model's up axis, rotated by the placement angles, must be the surface normal.
        const Vec3 up = upFromAngles(p.angles);
        CHECK(length(up - n) < 1e-6);
        CHECK(std::fabs(p.angles.y) < kEps);  // randomYaw is off
    }
}

TEST(gen_drop_to_floor_fixture) {
    const std::optional<std::string> text = readSample();
    CHECK(text.has_value());
    if (!text) return;
    Id next = 1;
    const ParseResult pr = parseMap(*text, next);
    CHECK(pr.map.has_value());
    if (!pr.map) return;
    const MapFile& map = *pr.map;

    // Above the floor slab (top at z = 0), clear of the pillar at the origin.
    const std::optional<Vec3> landed = dropToFloor(map, {200, 0, 50});
    CHECK(landed.has_value());
    if (landed) {
        CHECK(std::fabs(landed->z - 0.0) < kEps);
        CHECK(std::fabs(landed->x - 200.0) < kEps && std::fabs(landed->y) < kEps);
    }

    // Above the pillar top (z = 96) the highest top below the point wins over the floor.
    const std::optional<Vec3> onPillar = dropToFloor(map, {0, 0, 200});
    CHECK(onPillar.has_value());
    if (onPillar) CHECK(std::fabs(onPillar->z - 96.0) < kEps);

    // Too far below the floor top for maxDrop.
    CHECK(!dropToFloor(map, {200, 0, 5000}, 4096).has_value());
    CHECK(dropToFloor(map, {200, 0, 5000}, 6000).has_value());

    // Off the map: nothing below.
    CHECK(!dropToFloor(map, {100000, 100000, 50}).has_value());
    CHECK(!dropToFloor(map, {std::nan(""), 0, 50}).has_value());
}
