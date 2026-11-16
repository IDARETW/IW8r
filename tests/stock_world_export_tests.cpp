// OBJ export of stock world triangles (src/core/stock_world_export.h) on a synthetic StockWorld: region filtering,
// the centre offset, one usemtl group per material, sky skipping, and index / vertex consistency.
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "geometry.h"
#include "stock_world.h"
#include "stock_world_export.h"
#include "test_harness.h"

using namespace iw8r;

namespace {

ImportedMesh mesh(const std::string& material, std::vector<Vec3> pos, std::vector<uint32_t> idx) {
    ImportedMesh m;
    m.material = material;
    m.pos = std::move(pos);
    m.idx = std::move(idx);
    m.normal.assign(m.pos.size(), Vec3{0, 0, 1});
    m.uv.assign(m.pos.size(), std::array<double, 2>{0, 0});
    return m;
}

// Synthetic world. Region below is [-10, 110] x [-10, 110] x [-10, 10], centre (50, 50, 0).
//   tw/floor   : (0,1,2) all inside; (0,3,4) has v0 inside, v3 and v4 far away -> kept; (3,4,5) all outside -> dropped.
//   tw/sky_mp_shipment : one triangle inside (skipped when skipSky).
//   tw/wall    : every triangle outside -> the mesh is absent from the output.
//   tw/ghost   : one vertex inside -> kept.
StockWorld world() {
    StockWorld w;
    ImportedMesh floor = mesh("tw/floor",
                              {{0, 0, 0}, {100, 0, 0}, {0, 100, 0}, {1000, 1000, 0}, {1100, 1000, 0}, {1000, 1100, 0}},
                              {0, 1, 2, 0, 3, 4, 3, 4, 5});
    for (auto& uv : floor.uv) uv = {0.25, 0.75};
    w.meshes.push_back(floor);
    w.meshes.push_back(mesh("tw/sky_mp_shipment", {{0, 0, 0}, {100, 0, 0}, {0, 100, 0}}, {0, 1, 2}));
    w.meshes.push_back(mesh("tw/wall", {{2000, 0, 0}, {2100, 0, 0}, {2000, 100, 0}}, {0, 1, 2}));
    w.meshes.push_back(mesh("tw/ghost", {{5, 5, 0}, {2000, 0, 0}, {2000, 2000, 0}}, {0, 1, 2}));
    return w;
}

AABB region() {
    AABB b;
    b.add(Vec3{-10, -10, -10});
    b.add(Vec3{110, 110, 10});
    return b;
}

// Number of lines that start with `prefix` (a full token, e.g. "v " or "f ").
size_t countLines(const std::string& text, const std::string& prefix) {
    std::istringstream in(text);
    std::string line;
    size_t n = 0;
    while (std::getline(in, line))
        if (line.compare(0, prefix.size(), prefix) == 0) ++n;
    return n;
}

bool hasLine(const std::string& text, const std::string& line) {
    std::istringstream in(text);
    std::string l;
    while (std::getline(in, l))
        if (l == line) return true;
    return false;
}

// Largest vertex index used by any "f" line (the first of a/b/c triples).
uint32_t maxFaceIndex(const std::string& text) {
    std::istringstream in(text);
    std::string l;
    uint32_t best = 0;
    while (std::getline(in, l)) {
        if (l.compare(0, 2, "f ") != 0) continue;
        std::istringstream fs(l.substr(2));
        std::string tok;
        while (fs >> tok) {
            const uint32_t v = uint32_t(std::stoul(tok.substr(0, tok.find('/'))));
            if (v > best) best = v;
        }
    }
    return best;
}

}  // namespace

TEST(stock_world_export_region_filter) {
    const StockWorld w = world();
    size_t tris = 0;
    std::vector<std::string> mats;
    const std::string obj = stockWorldToObj(w, region(), false, &tris, &mats);
    // floor: 2 triangles, sky: 1, wall: 0, ghost: 1.
    CHECK(tris == 4);
    CHECK(countLines(obj, "f ") == 4);
    CHECK(mats == (std::vector<std::string>{"tw/floor", "tw/sky_mp_shipment", "tw/ghost"}));
    // Floor vertices used: v0..v4 (five); v5 belongs only to the dropped triangle and is not written.
    CHECK(countLines(obj, "v ") == 5 + 3 + 3);
    CHECK(maxFaceIndex(obj) == countLines(obj, "v "));

    // A region that holds nothing gives no faces and no groups.
    AABB far;
    far.add(Vec3{-5000, -5000, -5000});
    far.add(Vec3{-4000, -4000, -4000});
    size_t none = 99;
    std::vector<std::string> noMats{"stale"};
    const std::string empty = stockWorldToObj(w, far, false, &none, &noMats);
    CHECK(none == 0);
    CHECK(noMats.empty());
    CHECK(countLines(empty, "f ") == 0);
    CHECK(countLines(empty, "usemtl ") == 0);

    // An invalid (default) region keeps nothing either.
    size_t invalid = 99;
    stockWorldToObj(w, AABB(), false, &invalid, nullptr);
    CHECK(invalid == 0);
}

TEST(stock_world_export_offset_and_groups) {
    const StockWorld w = world();
    size_t tris = 0;
    std::vector<std::string> mats;
    Vec3 off{-1, -1, -1};
    const std::string obj = stockWorldToObj(w, region(), true, &tris, &mats, &off);
    // The region centre is returned, and the origin of the export is that point.
    CHECK(off.x == 50 && off.y == 50 && off.z == 0);
    // v0 (0,0,0) moves to (-50,-50,0); the far floor vertex (1000,1000,0) to (950,950,0).
    CHECK(hasLine(obj, "v -50.000000 -50.000000 0.000000"));
    CHECK(hasLine(obj, "v 950.000000 950.000000 0.000000"));
    CHECK(!hasLine(obj, "v 0.000000 0.000000 0.000000"));

    // One group and one usemtl per material, in mesh order, with the sky skipped.
    CHECK(countLines(obj, "usemtl ") == 2);
    CHECK(countLines(obj, "g ") == 2);
    CHECK(hasLine(obj, "usemtl tw/floor"));
    CHECK(hasLine(obj, "usemtl tw/ghost"));
    CHECK(mats == (std::vector<std::string>{"tw/floor", "tw/ghost"}));
    CHECK(tris == 3);

    // Attributes from the mesh: uv written as stored (vt), normal as vn.
    CHECK(hasLine(obj, "vt 0.250000 0.750000"));
    CHECK(hasLine(obj, "vn 0.000000 0.000000 1.000000"));
    CHECK(countLines(obj, "vt ") == countLines(obj, "v "));
    CHECK(countLines(obj, "vn ") == countLines(obj, "v "));

    // Face indices stay consistent across groups (OBJ indices are global to the file).
    CHECK(maxFaceIndex(obj) == countLines(obj, "v "));
    // The floor group numbers its 5 vertices from 1; the ghost group continues from 6 (one shared index space).
    CHECK(hasLine(obj, "f 1/1/1 2/2/2 3/3/3"));
    CHECK(hasLine(obj, "f 1/1/1 4/4/4 5/5/5"));
    CHECK(hasLine(obj, "f 6/6/6 7/7/7 8/8/8"));
}

TEST(stock_world_export_sky_skip) {
    const StockWorld w = world();
    size_t kept = 0;
    std::vector<std::string> mats;
    const std::string withSky = stockWorldToObj(w, region(), false, &kept, &mats);
    CHECK(kept == 4);
    CHECK(hasLine(withSky, "usemtl tw/sky_mp_shipment"));

    const std::string noSky = stockWorldToObj(w, region(), true, &kept, &mats);
    CHECK(kept == 3);
    CHECK(!hasLine(noSky, "usemtl tw/sky_mp_shipment"));
    for (const std::string& m : mats) CHECK(m.find("sky") == std::string::npos);

    // A material named "sky" without the tw/ prefix is also a sky dome; "skyline" is still matched by the rule
    // (starts with sky), so a stock name like that is skipped too. A name that merely contains "sky" is kept.
    StockWorld sky;
    sky.meshes.push_back(mesh("sky_dome", {{0, 0, 0}, {10, 0, 0}, {0, 10, 0}}, {0, 1, 2}));
    sky.meshes.push_back(mesh("tw/cloud_sky", {{0, 0, 0}, {10, 0, 0}, {0, 10, 0}}, {0, 1, 2}));
    size_t n = 0;
    stockWorldToObj(sky, region(), true, &n, nullptr);
    CHECK(n == 1);
}

TEST(stock_world_export_names_and_empty_groups) {
    StockWorld w;
    w.meshes.push_back(mesh("", {{0, 0, 0}, {10, 0, 0}, {0, 10, 0}}, {0, 1, 2}));
    w.meshes.push_back(mesh("tw/a b", {{1, 1, 0}, {10, 0, 0}, {0, 10, 0}}, {0, 1, 2}));
    ImportedMesh bad = mesh("tw/bad", {{0, 0, 0}, {10, 0, 0}, {0, 10, 0}}, {0, 1, 7});  // index out of range
    w.meshes.push_back(bad);
    size_t tris = 0;
    std::vector<std::string> mats;
    const std::string obj = stockWorldToObj(w, region(), false, &tris, &mats);
    // Empty material becomes "default", spaces become underscores, the out-of-range triangle is dropped.
    CHECK(hasLine(obj, "usemtl default"));
    CHECK(hasLine(obj, "usemtl tw/a_b"));
    CHECK(mats == (std::vector<std::string>{"default", "tw/a_b"}));
    CHECK(tris == 2);
    CHECK(maxFaceIndex(obj) == countLines(obj, "v "));
}

TEST(stock_world_export_offset_is_centre_even_without_triangles) {
    StockWorld w;
    Vec3 off{9, 9, 9};
    stockWorldToObj(w, region(), false, nullptr, nullptr, &off);
    CHECK(off.x == 50 && off.y == 50 && off.z == 0);
}

// 11.8 on the real mp_shipment cache (skipped without it): a region of the stock world exports with texture
// coordinates and stock material groups, so the copy builds textured. Also writes the OBJ to the test folder
// (stock_world_export/stock_mp_shipment_part.obj) for a build check.
TEST(stock_world_export_real_cache_has_uvs_and_materials) {
    const char* local = std::getenv("LOCALAPPDATA");
    if (!local) return;
    namespace fs = std::filesystem;
    const fs::path dir = fs::path(local) / "IW8 Radiant" / "IW8 Radiant" / "world_cache" / "mp_shipment";
    std::error_code ec;
    if (!fs::exists(dir, ec)) return;
    StockWorld w;
    std::string err;
    CHECK(readStockWorld(dir.string(), w, &err));
    if (w.meshes.empty()) return;
    // Centre on a vertex of the largest non-sky mesh (the world bounds include the sky dome).
    const ImportedMesh* big = nullptr;
    for (const auto& m : w.meshes)
        if (m.material.find("sky") == std::string::npos && !m.idx.empty() && (!big || m.idx.size() > big->idx.size())) big = &m;
    CHECK(big != nullptr);
    if (!big) return;
    const Vec3 c = big->pos[big->idx[0]];
    AABB region;
    region.add(c - Vec3{384, 384, 256});
    region.add(c + Vec3{384, 384, 256});
    size_t tris = 0;
    std::vector<std::string> mats;
    Vec3 offset;
    const std::string obj = stockWorldToObj(w, region, true, &tris, &mats, &offset);
    CHECK(tris > 100);
    CHECK(!mats.empty());
    size_t vt = 0, nonZero = 0;
    std::istringstream in(obj);
    std::string line;
    while (std::getline(in, line))
        if (line.rfind("vt ", 0) == 0) {
            ++vt;
            if (line != "vt 0 0") ++nonZero;
        }
    CHECK(vt > 0 && nonZero * 2 > vt);  // most vertices carry real texture coordinates
    for (const auto& m : mats) CHECK(!m.empty() && m[0] != ',');
    const fs::path out = fs::path(test::tmpDir()) / "stock_world_export";
    fs::create_directories(out, ec);
    std::ofstream(out / "stock_mp_shipment_part.obj") << obj;
    std::ofstream(out / "stock_mp_shipment_part.offset.txt") << offset.x << " " << offset.y << " " << offset.z << "\n";
}
