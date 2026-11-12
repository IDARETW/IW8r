// IW8 extraction compiler: placed model collision as convex hulls (roadmap 4.20). An L-shaped model made of two
// separate box meshes (one material each, so two connected components) gives two hull brushes; with hulls off it
// gives one oriented box that fills the L's empty corner.
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "brush_ops.h"
#include "compile_iw8.h"
#include "json_lite.h"
#include "map_text.h"
#include "test_harness.h"

using namespace iw8r;
namespace fs = std::filesystem;

namespace {

constexpr const char* kId = "mp_hull_t";
constexpr double kEps = 0.01;  // tolerance of the containment checks (map units)

// Scratch folder under the test temp dir, emptied first.
std::string scratch(const std::string& name) {
    const fs::path p = fs::path(test::tmpDir()) / "model_collision_hull" / name;
    std::error_code ec;
    fs::remove_all(p, ec);
    return p.string();
}

void writeText(const fs::path& p, const std::string& text) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f << text;
}

std::string readText(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

json::Value loadJson(const fs::path& p, bool& ok) {
    std::string err;
    auto v = json::parse(readText(p), &err);
    ok = v.has_value();
    if (!ok) std::printf("  json error in %s: %s\n", p.string().c_str(), err.c_str());
    return ok ? *v : json::Value{};
}

std::string boxBrush(const AABB& b, const char* material) {
    const auto prim = makeBrushPrimitive(makeBox(b, std::string(" ") + material + " 128 128 0 0 0 0"), "\n");
    return prim ? prim->text() : std::string();
}

// Two floor boxes (world brushes 0 and 1) plus the spawns the converter needs, then the given entities.
std::string worldText(const std::string& entities) {
    std::string t = "iwmap 4\n{\n\"classname\" \"worldspawn\"\n";
    t += boxBrush({{0, 0, -16}, {256, 256, 0}}, "world/a");
    t += boxBrush({{900, 0, -16}, {1156, 256, 0}}, "world/b");
    t += "}\n";
    t += "{\n\"classname\" \"mp_tdm_spawn\"\n\"origin\" \"64 128 16\"\n\"angles\" \"0 0 0\"\n}\n";
    t += "{\n\"classname\" \"mp_tdm_spawn\"\n\"origin\" \"1000 128 16\"\n\"angles\" \"0 180 0\"\n}\n";
    t += entities;
    return t;
}

// Two box meshes in OBJ (Y-up) units, 64 per unit step. Arm A: x 0..256, y 0..64, z 0..64. Arm B: x 0..64,
// y 0..64, z 64..192. Together they form an L in the XZ plane; the notch x 64..256, z 64..192 is empty.
// Each box is its own usemtl group, so the importer makes one mesh (one connected component) per arm.
void appendBox(std::string& obj, const char* material, const Vec3& lo, const Vec3& hi, int& base) {
    obj += std::string("usemtl ") + material + "\n";
    for (int c = 0; c < 8; ++c) {
        const Vec3 p{(c & 1) ? hi.x : lo.x, (c & 2) ? hi.y : lo.y, (c & 4) ? hi.z : lo.z};
        std::ostringstream v;
        v << "v " << p.x << " " << p.y << " " << p.z << "\n";
        obj += v.str();
    }
    // Corner index c = x + 2y + 4z. Six quads, each split into two triangles.
    const int quads[6][4] = {{0, 1, 3, 2}, {4, 5, 7, 6}, {0, 1, 5, 4}, {2, 3, 7, 6}, {0, 2, 6, 4}, {1, 3, 7, 5}};
    for (const auto& q : quads) {
        const int a = base + q[0] + 1, b = base + q[1] + 1, c = base + q[2] + 1, d = base + q[3] + 1;
        obj += "f " + std::to_string(a) + " " + std::to_string(b) + " " + std::to_string(c) + "\n";
        obj += "f " + std::to_string(a) + " " + std::to_string(c) + " " + std::to_string(d) + "\n";
    }
    base += 8;
}

std::string lShapeObj() {
    std::string obj = "# L-shaped test model: two box meshes\n";
    int base = 0;
    appendBox(obj, "armA", {0, 0, 0}, {256, 64, 64}, base);
    appendBox(obj, "armB", {0, 0, 64}, {64, 64, 192}, base);
    return obj;
}

// Vertices of an arm (OBJ units): its eight corners, which are every vertex of that box mesh.
std::vector<Vec3> armCorners(const Vec3& lo, const Vec3& hi) {
    std::vector<Vec3> out;
    for (int c = 0; c < 8; ++c) out.push_back(Vec3{(c & 1) ? hi.x : lo.x, (c & 2) ? hi.y : lo.y, (c & 4) ? hi.z : lo.z});
    return out;
}

// Placement of the model: origin (128, 128, 0), angles "0 90 0". With anglesToAxis the axes are forward (0,1,0),
// left (-1,0,0), up (0,0,1), so a model point (x, y, z) (Y-up) lands at origin + (z, x, y) in map space.
Vec3 worldOf(const Vec3& p) { return Vec3{128.0 + p.z, 128.0 + p.x, p.y}; }

// A point in the L's empty notch (OBJ units), in map space.
const Vec3 kNotchWorld = worldOf(Vec3{160, 32, 128});

// Compiles the L map with the given model collision mode. col receives the written collision json.
bool compileL(bool hulls, const std::string& out, CompileReport& rep, json::Value& col) {
    const fs::path models = fs::path(test::tmpDir()) / "model_collision_hull" / "models";
    writeText(models / "t_lshape.obj", lShapeObj());
    const std::string entities =
        "{\n\"classname\" \"misc_model\"\n\"model\" \"t_lshape\"\n\"origin\" \"128 128 0\"\n\"angles\" \"0 90 0\"\n}\n";
    Id next = 1;
    ParseResult pr = parseMap(worldText(entities), next);
    if (!pr.map) {
        std::printf("  map parse failed\n");
        return false;
    }
    CompileOptions opt;
    opt.mapId = kId;
    opt.modelRoots = {models.string()};
    opt.modelCollisionHulls = hulls;
    std::string err;
    if (!compileIw8Extraction(*pr.map, out, opt, rep, &err)) {
        std::printf("  compile error: %s\n", err.c_str());
        return false;
    }
    bool ok = false;
    col = loadJson(fs::path(out) / "extracted" / "maps" / "mp" / (std::string(kId) + ".d3dbsp.replay-collision.json"), ok);
    return ok;
}

// Planes of a collision brush as written: [nx, ny, nz, d] with dot(n, x) <= d, n outward.
std::vector<Plane> planesOf(const json::Value& brush) {
    std::vector<Plane> out;
    const json::Value* pl = brush.get("planes");
    if (!pl) return out;
    for (const auto& p : pl->arr) {
        if (p.arr.size() != 4) continue;
        out.push_back(Plane{Vec3{p.arr[0].number().value_or(0.0), p.arr[1].number().value_or(0.0), p.arr[2].number().value_or(0.0)},
                            p.arr[3].number().value_or(0.0)});
    }
    return out;
}

bool inside(const std::vector<Plane>& planes, const Vec3& p) {
    if (planes.empty()) return false;
    for (const Plane& pl : planes)
        if (dot(pl.n, p) > pl.d + kEps) return false;
    return true;
}

// Every point of the set is inside the brush (within kEps).
bool allInside(const std::vector<Plane>& planes, const std::vector<Vec3>& modelPoints) {
    for (const Vec3& q : modelPoints)
        if (!inside(planes, worldOf(q))) return false;
    return true;
}

bool hasWarning(const CompileReport& rep, const std::string& needle) {
    for (const auto& w : rep.warnings)
        if (w.find(needle) != std::string::npos) return true;
    return false;
}

const Vec3 kArmLo{0, 0, 0}, kArmHi{256, 64, 64};
const Vec3 kBLo{0, 0, 64}, kBHi{64, 64, 192};

}  // namespace

// Hull mode: the L is two convex brushes after the two floor boxes. Each hull holds exactly one arm (every corner of
// its box mesh inside its planes, within kEps) and leaves the notch outside.
TEST(model_collision_hull_l_shape_gives_two_brushes) {
    const std::string out = scratch("hulls");
    CompileReport rep;
    json::Value col;
    if (!compileL(true, out, rep, col)) {
        CHECK(false);
        return;
    }
    const json::Value* brushes = col.get("brushes");
    CHECK(brushes && brushes->arr.size() == 4);  // 2 floor boxes + 2 hulls
    CHECK(rep.collisionBrushes == 4);
    CHECK(hasWarning(rep, "2 convex hull brush(es), 0 as a solid box"));
    if (!brushes || brushes->arr.size() != 4) return;

    const std::vector<Vec3> armA = armCorners(kArmLo, kArmHi);
    const std::vector<Vec3> armB = armCorners(kBLo, kBHi);
    bool coversA = false, coversB = false;
    for (size_t bi = 2; bi < 4; ++bi) {
        const std::vector<Plane> pl = planesOf(brushes->arr[bi]);
        CHECK(pl.size() >= 4);
        CHECK(!inside(pl, kNotchWorld));  // the empty corner of the L stays open
        const bool a = allInside(pl, armA), b = allInside(pl, armB);
        CHECK(a != b);  // a hull of exactly one arm
        coversA |= a;
        coversB |= b;
    }
    CHECK(coversA && coversB);
}

// Box mode (modelCollisionHulls off): one oriented box for the whole L, which fills the notch.
TEST(model_collision_box_mode_gives_one_brush) {
    const std::string out = scratch("boxes");
    CompileReport rep;
    json::Value col;
    if (!compileL(false, out, rep, col)) {
        CHECK(false);
        return;
    }
    const json::Value* brushes = col.get("brushes");
    CHECK(brushes && brushes->arr.size() == 3);  // 2 floor boxes + 1 box
    CHECK(rep.collisionBrushes == 3);
    CHECK(hasWarning(rep, "placed model(s) got a solid collision box (hulls off"));
    if (!brushes || brushes->arr.size() != 3) return;

    const std::vector<Plane> pl = planesOf(brushes->arr[2]);
    CHECK(pl.size() == 6);
    std::vector<Vec3> all = armCorners(kArmLo, kArmHi);
    const std::vector<Vec3> armB = armCorners(kBLo, kBHi);
    all.insert(all.end(), armB.begin(), armB.end());
    CHECK(allInside(pl, all));
    CHECK(inside(pl, kNotchWorld));  // the box covers the notch the hulls leave open
}
