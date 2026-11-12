// Custom model import (OBJ) and the x64-zt extraction writer (xmodel/<name>.json, .replay.json, materials/).
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "json_lite.h"
#include "model_import.h"
#include "test_harness.h"

using namespace iw8r;
namespace fs = std::filesystem;

namespace {

fs::path scratch(const std::string& name) {
    const fs::path dir = fs::path(test::tmpDir()) / "model_import" / name;
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    return dir;
}

void writeFile(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << text;
}

std::string readFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

bool near(double a, double b, double eps = 1e-5) { return std::fabs(a - b) <= eps; }

const json::Value* member(const json::Value* v, const char* key) { return v ? v->get(key) : nullptr; }

// Parses a JSON file under the extraction root; null when missing or malformed.
std::optional<json::Value> parseFile(const fs::path& path) {
    std::string err;
    auto value = json::parse(readFile(path), &err);
    if (!value) std::printf("    parse error in %s: %s\n", path.string().c_str(), err.c_str());
    return value;
}

// A unit cube of half-size 8 in Y-up OBJ coordinates. Each face has its own four vertices, UVs and normal.
// Corners run counter-clockwise seen from outside (the OBJ convention).
std::string cubeObj(const std::string& material) {
    struct Face {
        double n[3];
        double c[4][3];
    };
    const Face faces[6] = {
        {{0, 0, 1}, {{-8, -8, 8}, {8, -8, 8}, {8, 8, 8}, {-8, 8, 8}}},
        {{0, 0, -1}, {{-8, 8, -8}, {8, 8, -8}, {8, -8, -8}, {-8, -8, -8}}},
        {{1, 0, 0}, {{8, -8, -8}, {8, 8, -8}, {8, 8, 8}, {8, -8, 8}}},
        {{-1, 0, 0}, {{-8, -8, -8}, {-8, -8, 8}, {-8, 8, 8}, {-8, 8, -8}}},
        {{0, 1, 0}, {{-8, 8, -8}, {-8, 8, 8}, {8, 8, 8}, {8, 8, -8}}},
        {{0, -1, 0}, {{-8, -8, -8}, {8, -8, -8}, {8, -8, 8}, {-8, -8, 8}}},
    };
    std::ostringstream out;
    out << "mtllib cube.mtl\n# cube\n";
    for (int f = 0; f < 6; ++f)
        for (int c = 0; c < 4; ++c)
            out << "v " << faces[f].c[c][0] << " " << faces[f].c[c][1] << " " << faces[f].c[c][2] << "\n";
    for (int f = 0; f < 6; ++f) out << "vn " << faces[f].n[0] << " " << faces[f].n[1] << " " << faces[f].n[2] << "\n";
    out << "vt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\n";
    out << "usemtl " << material << "\n";
    for (int f = 0; f < 6; ++f) {
        out << "f";
        for (int c = 0; c < 4; ++c) {
            const int v = f * 4 + c + 1;
            out << " " << v << "/" << (c + 1) << "/" << (f + 1);
        }
        out << "\n";
    }
    return out.str();
}

}  // namespace

TEST(model_import_obj_quad_tri_usemtl) {
    const fs::path dir = scratch("quad_tri");
    writeFile(dir / "quad_tri.obj",
              "mtllib quad_tri.mtl\n"
              "v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\nv 2 0 0\n"
              "vt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\n"
              "vn 0 0 1\n"
              "usemtl wood\n"
              "f 1/1/1 2/2/1 3/3/1 4/4/1\n"
              "usemtl metal\n"
              "f 2/2/1 5/1/1 3/3/1\n");
    writeFile(dir / "quad_tri.mtl", "newmtl wood\nKd 0.5 0.3 0.1\nmap_Kd wood_diffuse.png\n\nnewmtl metal\nKd 1 1 1\n");

    ImportedModel m;
    std::string err;
    CHECK(importObj((dir / "quad_tri.obj").string(), m, &err));
    CHECK(err.empty());
    CHECK(m.name == "quad_tri");
    CHECK(m.meshes.size() == 2);
    if (m.meshes.size() != 2) return;

    const ImportedMesh& wood = m.meshes[0];
    CHECK(wood.material == "wood");
    CHECK(wood.pos.size() == 4);
    CHECK((wood.idx == std::vector<uint32_t>{0, 1, 2, 0, 2, 3}));  // quad fan-triangulated
    CHECK(wood.pos.size() == wood.normal.size() && wood.pos.size() == wood.uv.size());
    CHECK(near(wood.pos[1].x, 1.0) && near(wood.pos[1].y, 0.0));
    CHECK(near(wood.pos[2].x, 1.0) && near(wood.pos[2].y, 1.0));
    CHECK(near(wood.normal[3].z, 1.0));
    CHECK(near(wood.uv[2][0], 1.0) && near(wood.uv[2][1], 1.0));

    const ImportedMesh& metal = m.meshes[1];
    CHECK(metal.material == "metal");
    CHECK(metal.pos.size() == 3);
    CHECK((metal.idx == std::vector<uint32_t>{0, 1, 2}));
    CHECK(near(metal.pos[1].x, 2.0));  // v5 (2,0,0)
    CHECK(near(metal.uv[1][0], 0.0) && near(metal.uv[1][1], 0.0));  // vt 1 is (0,0)
    CHECK(near(metal.normal[2].z, 1.0));

    CHECK(near(m.bounds.mins.x, 0.0) && near(m.bounds.maxs.x, 2.0));
    CHECK(near(m.bounds.maxs.y, 1.0));

    // mtl map_Kd is a hint only.
    bool hinted = false;
    for (const auto& w : m.warnings)
        if (w.find("wood_diffuse.png") != std::string::npos) hinted = true;
    CHECK(hinted);
}

TEST(model_import_obj_negative_indices) {
    const fs::path dir = scratch("negative");
    writeFile(dir / "neg.obj", "v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\nf -4 -3 -2 -1\n");
    ImportedModel m;
    std::string err;
    CHECK(importObj((dir / "neg.obj").string(), m, &err));
    CHECK(m.meshes.size() == 1);
    if (m.meshes.empty()) return;
    CHECK((m.meshes[0].idx == std::vector<uint32_t>{0, 1, 2, 0, 2, 3}));
    CHECK(m.meshes[0].material.empty());
    CHECK(m.meshes[0].pos.size() == 4);
}

TEST(model_import_obj_generated_normals) {
    const fs::path dir = scratch("gen_normals");
    // Counter-clockwise seen from +Z, so the generated normal is +Z.
    writeFile(dir / "tri.obj", "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");
    ImportedModel m;
    std::string err;
    CHECK(importObj((dir / "tri.obj").string(), m, &err));
    CHECK(m.meshes.size() == 1);
    if (m.meshes.empty()) return;
    CHECK(m.meshes[0].normal.size() == 3);
    for (const auto& n : m.meshes[0].normal) CHECK(near(n.x, 0.0) && near(n.y, 0.0) && near(n.z, 1.0));
    bool warned = false;
    for (const auto& w : m.warnings)
        if (w.find("generated 3 vertex normal") != std::string::npos) warned = true;
    CHECK(warned);
    // uv missing: zeros, reported.
    CHECK(m.meshes[0].uv.size() == 3 && m.meshes[0].uv[1][0] == 0.0);
}

TEST(model_import_obj_errors) {
    const fs::path dir = scratch("errors");
    ImportedModel m;
    std::string err;
    CHECK(!importObj((dir / "missing.obj").string(), m, &err));
    CHECK(!err.empty());

    writeFile(dir / "bad_index.obj", "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 9\n");
    err.clear();
    CHECK(!importObj((dir / "bad_index.obj").string(), m, &err));
    CHECK(err.find("bad_index.obj:4") != std::string::npos);

    writeFile(dir / "empty.obj", "v 0 0 0\n");
    err.clear();
    CHECK(!importObj((dir / "empty.obj").string(), m, &err));

    err.clear();
    CHECK(!importGltf((dir / "model.glb").string(), m, &err));
    CHECK(err.find("OBJ") != std::string::npos);
}

TEST(model_import_write_extraction_cube) {
    const fs::path dir = scratch("cube");
    writeFile(dir / "src" / "cube_test.obj", cubeObj("cube_wood"));
    ImportedModel m;
    std::string err;
    CHECK(importObj((dir / "src" / "cube_test.obj").string(), m, &err));
    CHECK(m.meshes.size() == 1);
    CHECK(m.meshes.empty() || m.meshes[0].pos.size() == 24);

    const fs::path extracted = dir / "extracted";
    ModelExportOptions opt;
    opt.materialPrefix = "iw8r/";
    CHECK(writeExtractionModel(m, extracted.string(), opt, &err));
    CHECK(err.empty());
    CHECK(fs::is_regular_file(extracted / "xmodel" / "cube_test.json"));
    CHECK(fs::is_regular_file(extracted / "xmodel" / "cube_test.replay.json"));
    CHECK(fs::is_regular_file(extracted / "materials" / "iw8r" / "cube_wood.json"));

    // xmodel table.
    const auto table = parseFile(extracted / "xmodel" / "cube_test.json");
    CHECK(table.has_value());
    if (!table) return;
    CHECK(member(&*table, "type") && member(&*table, "type")->string() && *member(&*table, "type")->string() == "rigid");
    CHECK(member(&*table, "collLod") && member(&*table, "collLod")->number() && *member(&*table, "collLod")->number() == 0);
    const json::Value* lods = member(&*table, "lods");
    CHECK(lods && lods->arr.size() == 1);
    if (!lods || lods->arr.size() != 1) return;
    CHECK(member(&lods->arr[0], "distance") && *member(&lods->arr[0], "distance")->number() == 0);

    // geometry.
    const auto geo = parseFile(extracted / "xmodel" / "cube_test.replay.json");
    CHECK(geo.has_value());
    if (!geo) return;
    CHECK(member(&*geo, "schema") && *member(&*geo, "schema")->number() == 1);
    CHECK(member(&*geo, "name") && *member(&*geo, "name")->string() == "cube_test");
    const json::Value* glods = member(&*geo, "lods");
    CHECK(glods && glods->arr.size() == 1);
    if (!glods || glods->arr.empty()) return;
    const json::Value* surfaces = member(&glods->arr[0], "surfaces");
    CHECK(surfaces && surfaces->arr.size() == 1);
    if (!surfaces || surfaces->arr.empty()) return;
    const json::Value& surface = surfaces->arr[0];
    CHECK(member(&surface, "material") && *member(&surface, "material")->string() == "iw8r/cube_wood");
    CHECK(member(&surface, "deformed") && member(&surface, "deformed")->b == false);
    CHECK(member(&surface, "reflection_probe") && *member(&surface, "reflection_probe")->number() == 0);

    const json::Value* verts = member(&surface, "vertices");
    const json::Value* idx = member(&surface, "indices");
    CHECK(verts && verts->arr.size() == 24);
    CHECK(idx && idx->arr.size() == 36);
    if (!verts || !idx || verts->arr.size() != 24 || idx->arr.size() != 36) return;

    // Positions are Radiant Z-up: the Y-up cube's top (+Y) becomes +Z, and its extent is 8 on each axis.
    double maxZ = -1e9, minZ = 1e9, maxAbs = 0;
    bool unitNormals = true, tangentsOk = true, signsOk = true;
    std::vector<std::array<double, 3>> pos(24), nrm(24);
    for (std::size_t i = 0; i < 24; ++i) {
        const json::Value& v = verts->arr[i];
        const json::Value* p = member(&v, "position");
        const json::Value* n = member(&v, "normal");
        const json::Value* t = member(&v, "tangent");
        const json::Value* s = member(&v, "binormal_sign");
        if (!p || !n || !t || !s || p->arr.size() != 3 || n->arr.size() != 3 || t->arr.size() != 3) {
            CHECK(false);
            return;
        }
        for (int k = 0; k < 3; ++k) {
            pos[i][k] = *p->arr[k].number();
            nrm[i][k] = *n->arr[k].number();
            maxAbs = std::max(maxAbs, std::fabs(pos[i][k]));
        }
        maxZ = std::max(maxZ, pos[i][2]);
        minZ = std::min(minZ, pos[i][2]);
        const double nl = std::sqrt(nrm[i][0] * nrm[i][0] + nrm[i][1] * nrm[i][1] + nrm[i][2] * nrm[i][2]);
        if (!near(nl, 1.0, 1e-4)) unitNormals = false;
        const double tx = *t->arr[0].number(), ty = *t->arr[1].number(), tz = *t->arr[2].number();
        const double tl = std::sqrt(tx * tx + ty * ty + tz * tz);
        const double tdot = tx * nrm[i][0] + ty * nrm[i][1] + tz * nrm[i][2];
        if (!near(tl, 1.0, 1e-4) || !near(tdot, 0.0, 1e-4)) tangentsOk = false;
        if (!near(std::fabs(*s->number()), 1.0)) signsOk = false;
    }
    CHECK(near(maxZ, 8.0) && near(minZ, -8.0));
    CHECK(near(maxAbs, 8.0));
    CHECK(unitNormals);
    CHECK(tangentsOk);
    CHECK(signsOk);
    // The vertex at OBJ (0,8,0) region: the top face normal is +Z after the axis change.
    bool topNormal = false;
    for (std::size_t i = 0; i < 24; ++i)
        if (near(pos[i][2], 8.0) && near(nrm[i][2], 1.0)) topNormal = true;
    CHECK(topNormal);

    // Winding: each triangle is clockwise seen from outside (cross(b-a, c-a) against the vertex normal).
    bool allCw = true;
    bool indicesInRange = true;
    for (std::size_t t = 0; t < 36; t += 3) {
        const auto i0 = static_cast<std::size_t>(*idx->arr[t].number());
        const auto i1 = static_cast<std::size_t>(*idx->arr[t + 1].number());
        const auto i2 = static_cast<std::size_t>(*idx->arr[t + 2].number());
        if (i0 >= 24 || i1 >= 24 || i2 >= 24) {
            indicesInRange = false;
            continue;
        }
        const auto& a = pos[i0];
        const auto& b = pos[i1];
        const auto& c = pos[i2];
        const double e1[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
        const double e2[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
        const double cr[3] = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]};
        const double d = cr[0] * nrm[i0][0] + cr[1] * nrm[i0][1] + cr[2] * nrm[i0][2];
        if (!(d < 0)) allCw = false;
    }
    CHECK(indicesInRange);
    CHECK(allCw);

    // The material file is the grey placeholder.
    const auto mat = parseFile(extracted / "materials" / "iw8r" / "cube_wood.json");
    CHECK(mat.has_value());
    if (mat) {
        CHECK(member(&*mat, "techniqueSet") && *member(&*mat, "techniqueSet")->string() == "iw8r_grey");
        CHECK(member(&*mat, "textures") && member(&*mat, "textures")->arr.size() == 2);
    }

    // An existing material file is kept.
    writeFile(extracted / "materials" / "iw8r" / "cube_wood.json", R"({"techniqueSet":"kept"})");
    CHECK(writeExtractionModel(m, extracted.string(), opt, &err));
    const auto kept = parseFile(extracted / "materials" / "iw8r" / "cube_wood.json");
    CHECK(kept && member(&*kept, "techniqueSet") && *member(&*kept, "techniqueSet")->string() == "kept");
}

TEST(model_import_write_lods_and_limits) {
    const fs::path dir = scratch("lods");
    writeFile(dir / "src" / "lod_cube.obj", cubeObj("mat"));
    ImportedModel m;
    std::string err;
    CHECK(importObj((dir / "src" / "lod_cube.obj").string(), m, &err));
    if (m.meshes.empty()) return;

    // Two LODs: one surface each, the same geometry repeated.
    ModelExportOptions opt;
    opt.lodDistances = {0, 500};
    CHECK(writeExtractionModel(m, (dir / "two").string(), opt, &err));
    const auto table = parseFile(dir / "two" / "xmodel" / "lod_cube.json");
    CHECK(table && member(&*table, "lods") && member(&*table, "lods")->arr.size() == 2);
    const auto geo = parseFile(dir / "two" / "xmodel" / "lod_cube.replay.json");
    CHECK(geo && member(&*geo, "lods") && member(&*geo, "lods")->arr.size() == 2);
    if (geo && member(&*geo, "lods") && member(&*geo, "lods")->arr.size() == 2)
        CHECK(*member(&member(&*geo, "lods")->arr[1], "distance")->number() == 500);

    // Non-decreasing distances only.
    ModelExportOptions bad;
    bad.lodDistances = {500, 0};
    CHECK(!writeExtractionModel(m, (dir / "bad_lod").string(), bad, &err));
    CHECK(!err.empty());

    // Names must be file-safe.
    ImportedModel named = m;
    named.name = "../escape";
    err.clear();
    CHECK(!writeExtractionModel(named, (dir / "name").string(), ModelExportOptions{}, &err));
    CHECK(!fs::exists(dir / "escape.json"));

    // Surface budget: 255 across all LODs (here 1 surface x 6 LODs passes, 1 x 256 is out of range).
    ModelExportOptions six;
    six.lodDistances = {0, 1, 2, 3, 4, 5};
    CHECK(writeExtractionModel(m, (dir / "six").string(), six, &err));
    ImportedModel many = m;
    many.meshes.assign(256, m.meshes[0]);
    ModelExportOptions one;
    err.clear();
    CHECK(!writeExtractionModel(many, (dir / "many").string(), one, &err));
    CHECK(err.find("255") != std::string::npos);

    // Scale applies to positions.
    ModelExportOptions scaled;
    scaled.scale = 2.0;
    CHECK(writeExtractionModel(m, (dir / "scaled").string(), scaled, &err));
    const auto sg = parseFile(dir / "scaled" / "xmodel" / "lod_cube.replay.json");
    if (sg) {
        bool hasSixteen = false;
        for (const auto& v : member(&member(&member(&*sg, "lods")->arr[0], "surfaces")->arr[0], "vertices")->arr) {
            const auto z = *member(&v, "position")->arr[2].number();
            if (near(z, 16.0)) hasSixteen = true;
        }
        CHECK(hasSixteen);
    }
}

// A non-symmetric triangle (points up Y in OBJ) tells the axis change apart: Y-up -> Z-up maps (x,y,z) to (x,-z,y).
TEST(model_import_write_axis_and_scale) {
    const fs::path dir = scratch("axis");
    writeFile(dir / "src" / "tri_axis.obj", "v 0 0 0\nv 1 0 0\nv 0 1 0\nvn 0 0 1\nf 1//1 2//1 3//1\n");
    ImportedModel m;
    std::string err;
    CHECK(importObj((dir / "src" / "tri_axis.obj").string(), m, &err));
    if (m.meshes.empty()) return;

    const auto checkTri = [&](const fs::path& extracted, const double expectY,
                              const double expectZ, const double nY, const double nZ) {
        const auto geo = parseFile(extracted / "xmodel" / "tri_axis.replay.json");
        CHECK(geo.has_value());
        if (!geo) return;
        const json::Value* verts = member(&member(&member(&*geo, "lods")->arr[0], "surfaces")->arr[0], "vertices");
        CHECK(verts && verts->arr.size() == 3);
        if (!verts || verts->arr.size() < 3) return;
        const json::Value* p = member(&verts->arr[2], "position");
        const json::Value* n = member(&verts->arr[2], "normal");
        CHECK(p && n);
        if (!p || !n) return;
        CHECK(near(*p->arr[0].number(), 0.0));
        CHECK(near(*p->arr[1].number(), expectY));
        CHECK(near(*p->arr[2].number(), expectZ));
        CHECK(near(*n->arr[0].number(), 0.0));
        CHECK(near(*n->arr[1].number(), nY));
        CHECK(near(*n->arr[2].number(), nZ));
    };

    ModelExportOptions defaults;  // zUp = true, scale = 1
    CHECK(writeExtractionModel(m, (dir / "zup").string(), defaults, &err));
    // OBJ (0,1,0) -> Radiant (0,0,1); OBJ normal (0,0,1) -> Radiant (0,-1,0).
    checkTri(dir / "zup", 0.0, 1.0, -1.0, 0.0);

    ModelExportOptions raw;
    raw.zUp = false;
    CHECK(writeExtractionModel(m, (dir / "raw").string(), raw, &err));
    checkTri(dir / "raw", 1.0, 0.0, 0.0, 1.0);

    ModelExportOptions big;
    big.scale = 4.0;
    CHECK(writeExtractionModel(m, (dir / "big").string(), big, &err));
    checkTri(dir / "big", 0.0, 4.0, -1.0, 0.0);  // scale moves positions only; the normal stays unit
}
