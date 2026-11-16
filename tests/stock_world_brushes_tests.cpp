#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include "brush_ops.h"
#include "stock_world.h"
#include "stock_world_brushes.h"
#include "tex_projection.h"
#include "test_harness.h"

using namespace iw8r;

TEST(stock_world_brushes_triangle_prism_keeps_uvs) {
    StockWorld w;
    ImportedMesh m;
    m.material = "tw/ground_dirt_01";
    m.pos = {Vec3{0, 0, 0}, Vec3{128, 0, 0}, Vec3{0, 128, 8}};
    m.uv = {{0.0, 1.0}, {2.0, 1.0}, {0.0, -1.0}};  // ImportedMesh convention (v up)
    m.idx = {0, 1, 2};
    w.meshes.push_back(m);
    ImportedMesh sky;
    sky.material = "tw/sky_test";
    sky.pos = m.pos;
    sky.idx = {0, 1, 2};
    w.meshes.push_back(sky);
    AABB region;
    region.add(Vec3{-10, -10, -10});
    region.add(Vec3{10, 10, 10});
    const StockBrushResult r = stockWorldToBrushes(w, region, 4, 100, "\n");
    CHECK(r.triangles == 1 && r.brushes.size() == 1 && r.skipped == 0 && r.materials.size() == 1);
    if (r.brushes.size() != 1) return;
    const Primitive& p = r.brushes[0];
    CHECK(p.faces.size() == 5 && isValidBrush(brushFromPrimitive(p)));
    // The top face has the stock material; its projection reproduces the triangle's texture coordinates (v flipped).
    size_t top = p.faces.size();
    for (size_t f = 0; f < p.faces.size(); ++f)
        if (p.faces[f].material() == "tw/ground_dirt_01") top = f;
    CHECK(top < p.faces.size());
    if (top >= p.faces.size()) return;
    const auto tp = texParamsOfFace(p, top);
    CHECK(tp.has_value());
    if (!tp) return;
    const Vec3 n = normalize(cross(m.pos[1] - m.pos[0], m.pos[2] - m.pos[0]));
    for (int i = 0; i < 3; ++i) {
        const auto uv = faceUV(n, *tp, m.pos[size_t(i)]);
        CHECK(std::fabs(uv[0] - m.uv[size_t(i)][0]) < 0.02 && std::fabs(uv[1] - (1 - m.uv[size_t(i)][1])) < 0.02);
    }
    const AABB b = brushBounds(brushFromPrimitive(p));
    CHECK(b.valid() && b.maxs.z <= 8.01 && b.mins.z < 0);  // extends behind the (upward-facing) triangle
    // A cap stops early and says so.
    const StockBrushResult capped = stockWorldToBrushes(w, region, 4, 0, "\n");
    CHECK(capped.capped && capped.brushes.empty());
}

TEST(stock_world_brushes_real_cache_region) {
    const char* local = std::getenv("LOCALAPPDATA");
    if (!local) return;
    namespace fs = std::filesystem;
    const fs::path dir = fs::path(local) / "IW8 Radiant" / "IW8 Radiant" / "world_cache" / "mp_shipment";
    std::error_code ec;
    if (!fs::exists(dir, ec)) return;
    StockWorld w;
    CHECK(readStockWorld(dir.string(), w));
    const ImportedMesh* big = nullptr;
    for (const auto& m : w.meshes)
        if (m.material.find("sky") == std::string::npos && !m.idx.empty() && (!big || m.idx.size() > big->idx.size())) big = &m;
    if (!big) return;
    const Vec3 c = big->pos[big->idx[0]];
    AABB region;
    region.add(c - Vec3{256, 256, 128});
    region.add(c + Vec3{256, 256, 128});
    const StockBrushResult r = stockWorldToBrushes(w, region, 2, 5000, "\n");
    CHECK(r.triangles > 20 && r.brushes.size() > 10 && r.brushes.size() + r.skipped == r.triangles);
    size_t valid = 0;
    for (const auto& p : r.brushes) valid += isValidBrush(brushFromPrimitive(p));
    CHECK(valid == r.brushes.size());
    // For a build check: the sample room plus these brushes in a func_group (tmp/stock_world_brushes/mp_iw8r_stockbrush.map).
    std::string room;
    fs::path sample;
    for (fs::path p = fs::current_path(); sample.empty() && !p.empty() && p != p.parent_path(); p = p.parent_path())
        if (fs::exists(p / "tests" / "data" / "iw8_sample_room.map", ec)) sample = p / "tests" / "data" / "iw8_sample_room.map";
    if (std::FILE* f = sample.empty() ? nullptr : std::fopen(sample.string().c_str(), "rb")) {
        char buf[65536];
        size_t got;
        while ((got = std::fread(buf, 1, sizeof buf, f)) > 0) room.append(buf, got);
        std::fclose(f);
    }
    if (!room.empty()) {
        std::string text = room + "{\n\"classname\" \"func_group\"\n";
        for (const auto& p : r.brushes) text += p.text();
        text += "}\n";
        const fs::path out = fs::path(test::tmpDir()) / "stock_world_brushes";
        fs::create_directories(out, ec);
        std::ofstream(out / "mp_iw8r_stockbrush.map", std::ios::binary) << text;
    }
}
