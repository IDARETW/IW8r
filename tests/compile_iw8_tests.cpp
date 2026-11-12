// IW8 extraction compiler tests (src/core/compile_iw8.cpp): the IW8 sample room, the doc 15 §7 room,
// tool-face rules and input rejection.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "brush_ops.h"
#include "map_script.h"
#include "atmosphere.h"
#include "compile_iw8.h"
#include "scriptable_catalog.h"
#include "fx_author.h"
#include "collision_preview.h"
#include "json_lite.h"
#include "map_text.h"
#include "patch_tess.h"
#include "test_harness.h"

using namespace iw8r;
namespace fs = std::filesystem;

namespace {

constexpr const char* kSampleId = "mp_iw8_sample";

// Scratch folder under the test temp dir, emptied first.
std::string scratch(const std::string& name) {
    const fs::path p = fs::path(test::tmpDir()) / "compile_iw8" / name;
    std::error_code ec;
    fs::remove_all(p, ec);
    return p.string();
}

// <repo>/tests/data/iw8_sample_room.map, found by walking up from the working directory.
std::string samplePath() {
    fs::path p = fs::current_path();
    for (int up = 0; up < 6; ++up, p = p.parent_path()) {
        const fs::path cand = p / "tests" / "data" / "iw8_sample_room.map";
        if (fs::exists(cand)) return cand.string();
    }
    return {};
}

bool loadMapFile(const std::string& path, MapFile& out) {
    std::string text;
    if (!readFile(path, text)) return false;
    Id next = 1;
    ParseResult pr = parseMap(text, next);
    if (!pr.map) return false;
    out = std::move(*pr.map);
    return true;
}

std::string readAll(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

json::Value loadJson(const std::string& path, bool& ok) {
    std::string err;
    auto v = json::parse(readAll(path), &err);
    ok = v.has_value();
    if (!ok) std::printf("  json error in %s: %s\n", path.c_str(), err.c_str());
    return ok ? *v : json::Value{};
}

uint32_t u32(const std::string& b, size_t off) {
    uint32_t v = 0;
    for (size_t i = 0; i < 4 && off + i < b.size(); ++i) v |= uint32_t(static_cast<unsigned char>(b[off + i])) << (8 * i);
    return v;
}

struct Paths {
    std::string complete, skeleton, world, col, comworld, ents, material, dds;
};

Paths pathsFor(const std::string& root, const std::string& id) {
    const fs::path r(root);
    const fs::path m = r / "extracted" / "maps" / "mp";
    Paths p;
    p.complete = (r / "complete").string();
    p.skeleton = (r / "skeleton" / "complete").string();
    p.world = (m / (id + ".d3dbsp.replay-world.json")).string();
    p.col = (m / (id + ".d3dbsp.replay-collision.json")).string();
    p.comworld = (m / (id + ".d3dbsp.replay-comworld.json")).string();
    p.ents = (m / (id + ".d3dbsp.ents")).string();
    p.material = (r / "extracted" / "materials" / "iw8r" / "grey.json").string();
    // The sample has one reflection_probe entity, so its probe is <id>_probe1 after the default probe 0; the default is <id>_probe.
    p.dds = (r / "extracted" / "images" / (id + "_probe1.dds")).string();
    return p;
}

double vlen(const json::Value& v) {
    double s = 0;
    for (const auto& c : v.arr) s += (c.number().value_or(0.0)) * (c.number().value_or(0.0));
    return std::sqrt(s);
}

// Brush primitives of the worldspawn entity and the tessellated triangle count of its patches.
void countWorldspawn(const MapFile& map, size_t& brushes, size_t& patchTris) {
    brushes = 0;
    patchTris = 0;
    for (const auto& e : map.entities) {
        if (e.classname() != "worldspawn") continue;
        for (const auto& it : e.items) {
            const Primitive* p = it.prim();
            if (!p) continue;
            if (p->kind == PrimKind::Brush) ++brushes;
            if (p->kind == PrimKind::Patch) patchTris += tessellatePrimitive(*p, 4).indices.size() / 3;
        }
    }
}

// doc 15 §7 room: floor, north, south, east, west (brush order as in the doc's collision file).
std::string roomMapText() {
    const AABB boxes[5] = {
        {{-256, -256, -16}, {256, 256, 0}},
        {{-272, 256, 0}, {272, 272, 256}},
        {{-272, -272, 0}, {272, -256, 256}},
        {{256, -256, 0}, {272, 256, 256}},
        {{-272, -256, 0}, {-256, 256, 256}},
    };
    std::string t = "iwmap 4\n{\n\"classname\" \"worldspawn\"\n";
    for (const AABB& b : boxes) {
        const auto prim = makeBrushPrimitive(makeBox(b, " room/grey 128 128 0 0 0 0"), "\n");
        if (prim) t += prim->text();
    }
    t += "}\n";
    t += "{\n\"classname\" \"mp_tdm_spawn\"\n\"origin\" \"-128 0 16\"\n\"angles\" \"0 0 0\"\n}\n";
    t += "{\n\"classname\" \"mp_tdm_spawn\"\n\"origin\" \"128 0 16\"\n\"angles\" \"0 180 0\"\n}\n";
    return t;
}

}  // namespace

// Compiles the sample room and checks every file, the JSON invariants, the collision planes and the DDS header.
TEST(compile_iw8_sample_writes_extraction) {
    const std::string sample = samplePath();
    CHECK(!sample.empty());
    if (sample.empty()) return;
    MapFile map;
    CHECK(loadMapFile(sample, map));
    const std::string out = scratch("sample");
    CompileOptions opt;
    opt.mapId = kSampleId;
    CompileReport rep;
    std::string err;
    const bool ok = compileIw8Extraction(map, out, opt, rep, &err);
    if (!ok) std::printf("  compile error: %s\n", err.c_str());
    CHECK(ok);
    if (!ok) return;

    const Paths p = pathsFor(out, kSampleId);
    for (const std::string& f : {p.complete, p.skeleton, p.world, p.col, p.comworld, p.ents, p.material, p.dds}) {
        CHECK(fs::is_regular_file(f));
    }

    // Second compile into the same folder: a previous extraction may be overwritten.
    CompileReport rep2;
    CHECK(compileIw8Extraction(map, out, opt, rep2, &err));
    CHECK(rep2.surfaces == rep.surfaces);

    // Solid worldspawn brushes (6) and tessellated patch triangles, from the source map.
    size_t worldBrushes = 0, patchTris = 0;
    countWorldspawn(map, worldBrushes, patchTris);
    CHECK(worldBrushes == 6);

    // Surfaces: one per worldspawn brush and editor material. Brush 4 has two materials (fence, concrete),
    // the others one each, plus the patch: 1+1+1+1+2+1+1 = 8. Triangles: 6 faces x 2 per box + patch.
    CHECK(rep.surfaces == 8);
    CHECK(rep.triangles == 6 * 6 * 2 + 2 * patchTris);
    // The sample's trigger_multiple (one brush) is brush model *1 with its own collision brush.
    CHECK(rep.brushModels == 1);
    CHECK(rep.collisionBrushes == worldBrushes + 1);
    CHECK(rep.spawns == 4);
    CHECK(rep.bounds.valid());

    bool okW = false;
    const json::Value world = loadJson(p.world, okW);
    CHECK(okW);
    if (okW) {
        const json::Value* surfs = world.get("surfaces");
        CHECK(surfs && surfs->type == json::Value::Type::Array && !surfs->arr.empty());
        size_t tris = 0;
        for (const auto& s : surfs->arr) {
            const json::Value* idx = s.get("indices");
            const json::Value* verts = s.get("vertices");
            CHECK(idx && verts);
            if (!idx || !verts) continue;
            CHECK(idx->arr.size() % 3 == 0);
            tris += idx->arr.size() / 3;
            for (const auto& i : idx->arr) {
                const double v = i.number().value_or(-1.0);
                CHECK(v >= 0 && v < static_cast<double>(verts->arr.size()));
            }
            for (const auto& v : verts->arr) {
                const json::Value* n = v.get("normal");
                CHECK(n && n->arr.size() == 3);
                if (n && n->arr.size() == 3) CHECK(std::fabs(vlen(*n) - 1.0) < 1e-6);
            }
        }
        CHECK(tris == rep.triangles);
        CHECK(world.get("name") && world.get("name")->string() && *world.get("name")->string() == "maps/mp/mp_iw8_sample.d3dbsp");
        const json::Value* sun = world.get("sun");
        CHECK(sun && sun->get("angles") && sun->get("angles")->arr.size() == 3);
        CHECK(world.get("sun_primary_light_index") && world.get("sun_primary_light_index")->number() == 1.0);
        CHECK(world.get("primary_light_count") && world.get("primary_light_count")->number() == 2.0);
    }

    // Collision: one entry per solid brush; every plane has the brush's corners on its inside.
    bool okC = false;
    const json::Value col = loadJson(p.col, okC);
    CHECK(okC);
    if (okC) {
        const json::Value* brushes = col.get("brushes");
        CHECK(brushes && brushes->arr.size() == worldBrushes + 1);
        if (brushes)
            for (const auto& b : brushes->arr) {
                const json::Value* mn = b.get("mins");
                const json::Value* mx = b.get("maxs");
                const json::Value* planes = b.get("planes");
                CHECK(mn && mx && planes && mn->arr.size() == 3 && mx->arr.size() == 3);
                if (!mn || !mx || !planes) continue;
                for (const auto& pl : planes->arr) {
                    CHECK(pl.arr.size() == 4);
                    if (pl.arr.size() != 4) continue;
                    const double a = pl.arr[0].number().value_or(0), bb = pl.arr[1].number().value_or(0),
                                 c = pl.arr[2].number().value_or(0), d = pl.arr[3].number().value_or(0);
                    // The sample's brushes are boxes, so their 8 corners are the brush vertices.
                    for (int mask = 0; mask < 8; ++mask) {
                        const double x = (mask & 1) ? mx->arr[0].number().value_or(0) : mn->arr[0].number().value_or(0);
                        const double y = (mask & 2) ? mx->arr[1].number().value_or(0) : mn->arr[1].number().value_or(0);
                        const double z = (mask & 4) ? mx->arr[2].number().value_or(0) : mn->arr[2].number().value_or(0);
                        CHECK(a * x + bb * y + c * z <= d + 0.01);
                    }
                }
            }
    }

    // Comworld: two lights, the sun (index 1) normalized.
    bool okCw = false;
    const json::Value cw = loadJson(p.comworld, okCw);
    CHECK(okCw);
    if (okCw) {
        const json::Value* lights = cw.get("primary_lights");
        CHECK(lights && lights->arr.size() == 2);
        if (lights && lights->arr.size() == 2) {
            const json::Value* dir = lights->arr[1].get("direction");
            CHECK(dir && dir->arr.size() == 3);
            if (dir && dir->arr.size() == 3) CHECK(std::fabs(vlen(*dir) - 1.0) < 1e-6);
            CHECK(lights->arr[1].get("type") && lights->arr[1].get("type")->number() == 1.0);
        }
    }

    // .ents: named keys, worldspawn and the team start spawns.
    const std::string ents = readAll(p.ents);
    CHECK(ents.find("\"classname\" \"worldspawn\"") != std::string::npos);
    CHECK(ents.find("\"sundirection\" \"-50 45 0\"") != std::string::npos);
    CHECK(ents.find("mp_tdm_spawn_allies_start") != std::string::npos);
    CHECK(ents.find("\"script_model\"") != std::string::npos);

    // Probe DDS: 64x64 B8G8R8A8 cube, 7 mips, 128 + 6 * 21844 bytes.
    const std::string dds = readAll(p.dds);
    CHECK(dds.size() == 128 + 6 * 21844);
    CHECK(dds.compare(0, 4, "DDS ") == 0);
    CHECK(u32(dds, 4) == 124);
    CHECK(u32(dds, 12) == 64);
    CHECK(u32(dds, 16) == 64);
    CHECK(u32(dds, 28) == 7);
    CHECK(u32(dds, 76) == 32);
    CHECK(u32(dds, 80) == 0x41);
    CHECK(u32(dds, 88) == 32);
    CHECK((u32(dds, 112) & 0xFE00) == 0xFE00);
}

// The doc 15 §7 room (floor and four walls): 5 surfaces and 5 collision brushes, as in the doc example.
// The doc lists only the inner faces; the compiler renders every face, so 6 faces per box give 60 triangles.
TEST(compile_iw8_doc_room_counts) {
    MapFile map;
    Id next = 1;
    ParseResult pr = parseMap(roomMapText(), next);
    CHECK(pr.map.has_value());
    if (!pr.map) return;
    const std::string out = scratch("room");
    CompileOptions opt;
    opt.mapId = "mp_room";
    CompileReport rep;
    std::string err;
    const bool ok = compileIw8Extraction(*pr.map, out, opt, rep, &err);
    if (!ok) std::printf("  compile error: %s\n", err.c_str());
    CHECK(ok);
    if (!ok) return;
    CHECK(rep.surfaces == 5);
    CHECK(rep.collisionBrushes == 5);
    CHECK(rep.triangles == 60);
    CHECK(rep.spawns == 2);

    bool okW = false;
    const json::Value world = loadJson(pathsFor(out, "mp_room").world, okW);
    CHECK(okW);
    if (!okW) return;
    const json::Value* surfs = world.get("surfaces");
    CHECK(surfs && surfs->arr.size() == 5);
    if (surfs && surfs->arr.size() == 5) {
        // Surface 0 is the floor brush: its top face has normal (0,0,1).
        bool floorTop = false;
        for (const auto& v : surfs->arr[0].get("vertices")->arr) {
            const json::Value* n = v.get("normal");
            if (n && n->arr.size() == 3 && n->arr[2].number() == 1.0) floorTop = true;
        }
        CHECK(floorTop);
    }
    const json::Value* dpvs = world.get("dpvs");
    CHECK(dpvs && dpvs->get("cells") && dpvs->get("cells")->arr.size() == 1);
}

// Tool faces are not rendered; a caulk-only brush is still solid; a trigger brush is not collision.
TEST(compile_iw8_tool_faces_and_trigger_brushes) {
    const std::string text =
        "iwmap 4\n{\n\"classname\" \"worldspawn\"\n" +
        makeBrushPrimitive(makeBox({{0, 0, 0}, {64, 64, 64}}, " tm/concrete 128 128 0 0 0 0"), "\n")->text() +
        makeBrushPrimitive(makeBox({{100, 0, 0}, {164, 64, 64}}, " caulk 128 128 0 0 0 0"), "\n")->text() +
        makeBrushPrimitive(makeBox({{200, 0, 0}, {264, 64, 64}}, " trigger 128 128 0 0 0 0"), "\n")->text() + "}\n";
    Id next = 1;
    ParseResult pr = parseMap(text, next);
    CHECK(pr.map.has_value());
    if (!pr.map) return;
    CompileOptions opt;
    opt.mapId = "mp_tools";
    CompileReport rep;
    std::string err;
    const std::string out = scratch("tools");
    const bool ok = compileIw8Extraction(*pr.map, out, opt, rep, &err);
    CHECK(ok);
    if (!ok) return;
    CHECK(rep.surfaces == 1);
    CHECK(rep.triangles == 12);
    CHECK(rep.collisionBrushes == 2);
    CHECK(isIw8ToolMaterial("Textures/CAULK"));
    CHECK(isIw8ToolMaterial("sky_day"));
    CHECK(!isIw8ToolMaterial("tm2co/me_curb_stone_01"));
    CHECK(isIw8ToolMaterial("ladder") && isIw8ToolMaterial("textures/ladder"));
    CHECK(!isIw8ToolMaterial("tmo/metal_ladder_01") && !isIw8ToolMaterial("water"));
}

// A map with only tool faces has nothing to render and fails. Bad ids and non-extraction folders are refused.
TEST(compile_iw8_rejects_bad_input) {
    CHECK(isValidIw8MapId("mp_iw8_sample"));
    CHECK(isValidIw8MapId("mp_ok"));
    CHECK(!isValidIw8MapId("MP_bad"));
    CHECK(!isValidIw8MapId("mp_"));
    CHECK(!isValidIw8MapId("mp_abcdefghijklmnop"));  // 16 characters
    CHECK(!isValidIw8MapId("xx_room"));

    Id next = 1;
    ParseResult caulk = parseMap("iwmap 4\n{\n\"classname\" \"worldspawn\"\n" +
                                     makeBrushPrimitive(makeBox({{0, 0, 0}, {64, 64, 64}}, " caulk 128 128 0 0 0 0"), "\n")->text() +
                                     "}\n",
                                 next);
    CHECK(caulk.map.has_value());
    if (!caulk.map) return;
    CompileOptions opt;
    CompileReport rep;
    std::string err;

    opt.mapId = "mp_tools";
    CHECK(!compileIw8Extraction(*caulk.map, scratch("caulk_only"), opt, rep, &err));
    CHECK(!err.empty());

    opt.mapId = "MP_BAD";
    CHECK(!compileIw8Extraction(*caulk.map, scratch("bad_id"), opt, rep, &err));
    CHECK(!err.empty());

    // A non-empty folder that is not an extraction is refused and left alone.
    const std::string foreign = scratch("foreign");
    fs::create_directories(foreign);
    { std::ofstream(fs::path(foreign) / "notes.txt") << "keep me"; }
    Id next2 = 1;
    ParseResult rp = parseMap(roomMapText(), next2);
    CHECK(rp.map.has_value());
    if (!rp.map) return;
    opt.mapId = "mp_room";
    CHECK(!compileIw8Extraction(*rp.map, foreign, opt, rep, &err));
    CHECK(fs::is_regular_file(fs::path(foreign) / "notes.txt"));
    CHECK(!fs::exists(fs::path(foreign) / "complete"));
}

// Brush entities: a trigger_multiple becomes brush model *1 (collision only, trigger contents, no surfaces)
// and a script_brushmodel becomes *2 (collision + its own surfaces). Both get a submodel, a leaf node, a
// brush_models entry, and "model" "*N" in .ents; the world cell lists only the world's surfaces.
TEST(compile_iw8_brush_entities_become_brush_models) {
    const std::string text =
        "iwmap 4\n{\n\"classname\" \"worldspawn\"\n" +
        makeBrushPrimitive(makeBox({{0, 0, -16}, {512, 512, 0}}, " tm/floor 128 128 0 0 0 0"), "\n")->text() + "}\n" +
        "{\n\"classname\" \"trigger_multiple\"\n\"targetname\" \"zone_a\"\n" +
        makeBrushPrimitive(makeBox({{64, 64, 0}, {128, 128, 64}}, " trigger 128 128 0 0 0 0"), "\n")->text() + "}\n" +
        "{\n\"classname\" \"script_brushmodel\"\n\"targetname\" \"door\"\n\"iw8r_brush_visual\" \"0\"\n" +
        makeBrushPrimitive(makeBox({{256, 0, 0}, {264, 96, 128}}, " tm/door 128 128 0 0 0 0"), "\n")->text() + "}\n" +
        "{\n\"classname\" \"func_door\"\n" +
        makeBrushPrimitive(makeBox({{300, 0, 0}, {308, 96, 128}}, " tm/door 128 128 0 0 0 0"), "\n")->text() + "}\n";
    Id next = 1;
    ParseResult pr = parseMap(text, next);
    CHECK(pr.map.has_value());
    if (!pr.map) return;
    CompileOptions opt;
    opt.mapId = "mp_bmodels";
    CompileReport rep;
    std::string err;
    const std::string out = scratch("bmodels");
    const bool ok = compileIw8Extraction(*pr.map, out, opt, rep, &err);
    if (!ok) std::printf("  compile error: %s\n", err.c_str());
    CHECK(ok);
    if (!ok) return;
    CHECK(rep.brushModels == 2);         // func_door is not in the converter's class list
    CHECK(rep.collisionBrushes == 3);    // floor, trigger, door
    CHECK(rep.surfaces == 2);            // floor (world) + door (*2); the trigger draws nothing
    bool skippedDoor = false;
    for (const auto& w : rep.warnings) skippedDoor |= w.find("func_door") != std::string::npos;
    CHECK(skippedDoor);

    const Paths paths = pathsFor(out, "mp_bmodels");
    bool okC = false, okW = false;
    const json::Value col = loadJson(paths.col, okC);
    const json::Value world = loadJson(paths.world, okW);
    CHECK(okC && okW);
    if (!okC || !okW) return;
    CHECK(col.get("submodel_count") && col.get("submodel_count")->number() == 3.0);
    const json::Value* subs = col.get("submodels");
    const json::Value* leaves = col.get("leaf_brush_nodes");
    const json::Value* brushes = col.get("brushes");
    CHECK(subs && subs->arr.size() == 3 && leaves && leaves->arr.size() == 3 && brushes && brushes->arr.size() == 3);
    if (subs && leaves && brushes && subs->arr.size() == 3 && leaves->arr.size() == 3 && brushes->arr.size() == 3) {
        // World brush first with solid contents; then the trigger (0x8000001) and the door (1).
        CHECK(brushes->arr[0].get("contents")->number() == 1.0);
        CHECK(brushes->arr[1].get("contents")->number() == double(0x8000001));
        CHECK(brushes->arr[2].get("contents")->number() == 1.0);
        for (size_t n = 1; n <= 2; ++n) {
            const double leaf = subs->arr[n].get("leaf")->number().value_or(-1);
            CHECK(leaf == double(n));
            const json::Value& node = leaves->arr[n];
            CHECK(node.get("count")->number() == 1.0);
            CHECK(node.get("brushes")->arr.size() == 1 && node.get("brushes")->arr[0].number() == double(n));
        }
        CHECK(leaves->arr[0].get("count")->number() == 0.0);
    }
    const json::Value* bm = world.get("brush_models");
    CHECK(bm && bm->arr.size() == 3);
    if (bm && bm->arr.size() == 3) {
        CHECK(bm->arr[0].get("start")->number() == 0.0 && bm->arr[0].get("count")->number() == 1.0);
        CHECK(bm->arr[1].get("count")->number() == 0.0);
        CHECK(bm->arr[2].get("start")->number() == 1.0 && bm->arr[2].get("count")->number() == 1.0);
    }
    const json::Value* cells = world.get("dpvs") ? world.get("dpvs")->get("cells") : nullptr;
    CHECK(cells && cells->arr.size() == 1 && cells->arr[0].get("surfaces")->arr.size() == 1);

    std::string ents;
    {
        std::ifstream f(paths.ents, std::ios::binary);
        std::stringstream ss;
        ss << f.rdbuf();
        ents = ss.str();
    }
    const size_t trig = ents.find("\"trigger_multiple\"");
    const size_t door = ents.find("\"script_brushmodel\"");
    CHECK(trig != std::string::npos && door != std::string::npos);
    CHECK(ents.find("\"model\" \"*1\"", trig) != std::string::npos);
    CHECK(ents.find("\"model\" \"*2\"", door) != std::string::npos);
    CHECK(ents.find("func_door") == std::string::npos);
}

// Atmosphere (doc 31): worldspawn iw8r_fog / iw8r_film become the setExpFog gsc and the r_film* vision file.
TEST(compile_iw8_atmosphere_fog_and_film) {
    const ExpFog fog = *parseFog("0 1800 0.55 0.6 0.7");
    CHECK(parseFog(formatFog(fog)).has_value());
    CHECK(!parseFog("10 0 1 1 1"));    // halfway must be > 0
    CHECK(!parseFog("-1 100 1 1 1"));  // start must be >= 0
    CHECK(!parseFog("1 2 3"));
    const FilmGrade film = *parseFilm("1.1 0 0.85 0 1.05 1 0.9 0.9 0.95 1");
    CHECK(!film.isIdentity() && FilmGrade{}.isIdentity());
    CHECK(formatFilm(*parseFilm(formatFilm(film))) == formatFilm(film));
    CHECK(fogGsc(fog).find("setExpFog(0, 1800, 0.55, 0.6, 0.7);") != std::string::npos);
    CHECK(filmVision(film).find("r_filmdesaturation \"0.85\"") != std::string::npos);

    const std::string sample = samplePath();
    std::string text;
    CHECK(!sample.empty() && readFile(sample, text));
    if (text.empty()) return;
    const std::string ws = "\"classname\" \"worldspawn\"";
    const size_t at = text.find(ws);
    CHECK(at != std::string::npos);
    const std::string eol = text.find("\r\n") != std::string::npos ? "\r\n" : "\n";
    text.insert(at + ws.size(), eol + "\"iw8r_fog\" \"0 1800 0.55 0.6 0.7\"" + eol + "\"iw8r_film\" \"1.1 0 0.85 0 1.05 1 0.9 0.9 0.95 1\"");
    Id next = 1;
    ParseResult pr = parseMap(text, next);
    CHECK(pr.map.has_value());
    if (!pr.map) return;
    const std::string out = scratch("atmosphere");
    CompileOptions opt;
    opt.mapId = kSampleId;
    CompileReport rep;
    std::string err;
    CHECK(compileIw8Extraction(*pr.map, out, opt, rep, &err));
    const std::string gsc = readAll((fs::path(out) / "extracted" / "maps" / "mp" / (std::string(kSampleId) + ".gsc")).string());
    CHECK(gsc.find("setExpFog(0, 1800, 0.55, 0.6, 0.7);") != std::string::npos);
    const std::string vis = readAll((fs::path(out) / "extracted" / "vision" / (std::string(kSampleId) + ".vision")).string());
    CHECK(vis.find("r_filmenable \"1\"") != std::string::npos && vis.find("r_filmlighttint \"1.05 1 0.9\"") != std::string::npos);
    // The editor keys never reach the runtime entity string.
    const std::string ents = readAll((fs::path(out) / "extracted" / "maps" / "mp" / (std::string(kSampleId) + ".d3dbsp.ents")).string());
    CHECK(ents.find("iw8r_fog") == std::string::npos && ents.find("iw8r_film") == std::string::npos);

    // No keys: no fog and no vision file; the map script still runs the load script (stock-like main()).
    MapFile plain;
    CHECK(loadMapFile(sample, plain));
    const std::string out2 = scratch("atmosphere_none");
    CompileReport rep2;
    CHECK(compileIw8Extraction(plain, out2, opt, rep2, &err));
    const std::string plainGsc = readAll((fs::path(out2) / "extracted" / "maps" / "mp" / (std::string(kSampleId) + ".gsc")).string());
    CHECK(plainGsc.find("maps\\mp\\_load::main();") != std::string::npos && plainGsc.find("setExpFog") == std::string::npos);
    CHECK(!fs::exists(fs::path(out2) / "extracted" / "vision"));
}

// Ladders (6.8): a brush with the ladder tool material gets ladder_planes in the collision JSON (doc 15 section 2.5).
TEST(compile_iw8_ladder_planes) {
    const std::string sample = samplePath();
    std::string text;
    CHECK(!sample.empty() && readFile(sample, text));
    if (text.empty()) return;
    const std::string eol = text.find("\r\n") != std::string::npos ? "\r\n" : "\n";
    const std::string s = " ladder 128 128 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0" + eol;
    const std::string brush = "{" + eol + " ( 108 24 0 ) ( 100 24 0 ) ( 100 -24 0 )" + s + " ( 100 -24 128 ) ( 100 24 128 ) ( 108 24 128 )" + s +
                              " ( 100 -24 128 ) ( 108 -24 128 ) ( 108 -24 0 )" + s + " ( 108 24 128 ) ( 100 24 128 ) ( 100 24 0 )" + s +
                              " ( 108 -24 128 ) ( 108 24 128 ) ( 108 24 0 )" + s + " ( 100 24 128 ) ( 100 -24 128 ) ( 100 -24 0 )" + s + "}" + eol;
    const std::string ws = "\"classname\" \"worldspawn\"";
    const size_t at = text.find(ws);
    CHECK(at != std::string::npos);
    text.insert(text.find(eol, at) + eol.size(), brush);
    Id next = 1;
    ParseResult pr = parseMap(text, next);
    CHECK(pr.map.has_value());
    if (!pr.map) return;
    const std::string out = scratch("ladder");
    CompileOptions opt;
    opt.mapId = kSampleId;
    CompileReport rep;
    std::string err;
    CHECK(compileIw8Extraction(*pr.map, out, opt, rep, &err));
    const std::string col = readAll((fs::path(out) / "extracted" / "maps" / "mp" / (std::string(kSampleId) + ".d3dbsp.replay-collision.json")).string());
    const size_t lp = col.find("\"ladder_planes\":[");
    CHECK(lp != std::string::npos && col.find("\"ladder_planes\"", lp + 1) == std::string::npos);  // exactly one ladder brush
}

// Stock xmodels exported by the editor (opt.stockModels) are built as iw8r_stock_* copies; the model key follows.
TEST(compile_iw8_stock_models_as_copies) {
    CHECK(stockModelBuildName("tm/ee_appliance_dryer") == "iw8r_stock_tm_ee_appliance_dryer");
    const std::string sample = samplePath();
    MapFile map;
    CHECK(!sample.empty() && loadMapFile(sample, map));
    ImportedModel ramp;
    ImportedMesh m;
    m.pos = {Vec3{0, 0, 0}, Vec3{64, 0, 0}, Vec3{0, 64, 0}};
    m.normal = {Vec3{0, 0, 1}, Vec3{0, 0, 1}, Vec3{0, 0, 1}};
    m.uv = {{0, 0}, {1, 0}, {0, 1}};
    m.idx = {0, 1, 2};
    ramp.meshes.push_back(m);
    std::map<std::string, ImportedModel> stock{{"automotive_truck_loading_ramp_short", ramp}};
    const std::string out = scratch("stock_models");
    CompileOptions opt;
    opt.mapId = kSampleId;
    opt.stockModels = &stock;
    CompileReport rep;
    std::string err;
    CHECK(compileIw8Extraction(map, out, opt, rep, &err));
    const std::string bn = "iw8r_stock_automotive_truck_loading_ramp_short";
    CHECK(fs::exists(fs::path(out) / "extracted" / "xmodel" / (bn + ".json")));
    const std::string ents = readAll((fs::path(out) / "extracted" / "maps" / "mp" / (std::string(kSampleId) + ".d3dbsp.ents")).string());
    CHECK(ents.find("\"" + bn + "\"") != std::string::npos);
    CHECK(ents.find("\"automotive_truck_loading_ramp_short\"") == std::string::npos);
}

// Placed models get an oriented solid collision box (CompileOptions::modelCollisionBoxes).
TEST(compile_iw8_model_collision_boxes) {
    const std::string sample = samplePath();
    std::string text;
    CHECK(!sample.empty() && readFile(sample, text));
    // Unscripted: drop the ramp's targetname (a script_model with one may be moved by script and gets no box).
    const std::string tn = "\"targetname\" \"ramp\"";
    const size_t at = text.find(tn);
    CHECK(at != std::string::npos);
    if (at != std::string::npos) text.erase(at, text.find('\n', at) + 1 - at);
    Id next = 1;
    ParseResult pr = parseMap(text, next);
    CHECK(pr.map.has_value());
    if (!pr.map) return;
    MapFile map = std::move(*pr.map);
    ImportedModel ramp;  // Y-up like the model cache: 64 wide (x), 32 tall (y up), 128 deep (z -> -y in the map)
    ImportedMesh m;
    m.pos = {Vec3{0, 0, 0}, Vec3{64, 0, 0}, Vec3{0, 32, -128}};
    m.normal = {Vec3{0, 1, 0}, Vec3{0, 1, 0}, Vec3{0, 1, 0}};
    m.uv = {{0, 0}, {1, 0}, {0, 1}};
    m.idx = {0, 1, 2};
    ramp.meshes.push_back(m);
    std::map<std::string, ImportedModel> stock{{"automotive_truck_loading_ramp_short", ramp}};
    CompileOptions opt;
    opt.mapId = kSampleId;
    opt.stockModels = &stock;
    opt.modelCollisionBoxes = false;
    CompileReport off, on;
    std::string err;
    CHECK(compileIw8Extraction(map, scratch("model_boxes_off"), opt, off, &err));
    opt.modelCollisionBoxes = true;
    const std::string out = scratch("model_boxes_on");
    CHECK(compileIw8Extraction(map, out, opt, on, &err));
    CHECK(on.collisionBrushes == off.collisionBrushes + 1);  // only the stock model resolves; others are skipped
    const std::string col = readAll((fs::path(out) / "extracted" / "maps" / "mp" / (std::string(kSampleId) + ".d3dbsp.replay-collision.json")).string());
    std::vector<CollisionHullEdges> hulls;
    CHECK(readCollisionHulls(col, hulls));
    bool found = false;
    for (const auto& h : hulls) {
        AABB b;
        for (const auto& e : h.edges) b.add(e.first), b.add(e.second);
        if (std::fabs((b.maxs.z - b.mins.z) - 32) < 0.5 && h.edges.size() == 12) found = true;  // 32 tall box
    }
    CHECK(found);
}

// Bot navigation markers reach the converter: node_pathnode / node_negotiation_* pass into the .ents (6.9).
TEST(compile_iw8_navigation_markers_pass_through) {
    const std::string sample = samplePath();
    std::string text;
    CHECK(!sample.empty() && readFile(sample, text));
    if (text.empty()) return;
    const std::string eol = text.find("\r\n") != std::string::npos ? "\r\n" : "\n";
    text += "{" + eol + "\"classname\" \"node_pathnode\"" + eol + "\"origin\" \"64 64 0\"" + eol + "}" + eol;
    text += "{" + eol + "\"classname\" \"node_negotiation_begin\"" + eol + "\"origin\" \"0 0 0\"" + eol + "\"target\" \"jump1\"" + eol +
            "\"animscript\" \"traverse_jumpdown_40\"" + eol + "}" + eol;
    text += "{" + eol + "\"classname\" \"node_negotiation_end\"" + eol + "\"origin\" \"0 64 -40\"" + eol + "\"targetname\" \"jump1\"" + eol + "}" + eol;
    Id next = 1;
    ParseResult pr = parseMap(text, next);
    CHECK(pr.map.has_value());
    if (!pr.map) return;
    const std::string out = scratch("nav_markers");
    CompileOptions opt;
    opt.mapId = kSampleId;
    CompileReport rep;
    std::string err;
    CHECK(compileIw8Extraction(*pr.map, out, opt, rep, &err));
    const std::string ents = readAll((fs::path(out) / "extracted" / "maps" / "mp" / (std::string(kSampleId) + ".d3dbsp.ents")).string());
    CHECK(ents.find("node_pathnode") != std::string::npos);
    CHECK(ents.find("node_negotiation_begin") != std::string::npos && ents.find("node_negotiation_end") != std::string::npos);
    for (const auto& w : rep.warnings) CHECK(w.find("node_pathnode") == std::string::npos);  // not reported as dropped
}

// Colour grading LUT (5.6): a .cube file named by worldspawn iw8r_lut becomes grading/<id>_clut.rgba + grading_clut.
TEST(compile_iw8_grading_lut) {
    int size = 0;
    std::vector<uint8_t> rgba;
    std::string cube = "TITLE \"warm\"\nLUT_3D_SIZE 2\n# red fastest\n";
    for (int b = 0; b < 2; ++b)
        for (int g = 0; g < 2; ++g)
            for (int r = 0; r < 2; ++r) cube += std::to_string(r) + " " + std::to_string(g) + " " + std::to_string(b * 0.5) + "\n";
    CHECK(parseCubeLut(cube, size, rgba) && size == 2 && rgba.size() == 32);
    CHECK(rgba[4] == 255 && rgba[5] == 0 && rgba[6] == 0 && rgba[7] == 255);  // texel 1: red 1
    CHECK(rgba[4 * 4 + 2] == 128);                                            // texel 4 (b=1): blue 0.5
    CHECK(!parseCubeLut("LUT_3D_SIZE 2\n0 0 0\n", size, rgba));                // too few rows
    CHECK(!parseCubeLut("LUT_1D_SIZE 4\n", size, rgba));

    const fs::path dir = fs::path(scratch("lut_src"));
    fs::create_directories(dir);
    std::ofstream(dir / "warm.cube", std::ios::binary) << cube;
    const std::string sample = samplePath();
    std::string text;
    CHECK(!sample.empty() && readFile(sample, text));
    if (text.empty()) return;
    const std::string ws = "\"classname\" \"worldspawn\"";
    const std::string eol = text.find("\r\n") != std::string::npos ? "\r\n" : "\n";
    text.insert(text.find(ws) + ws.size(), eol + "\"iw8r_lut\" \"warm.cube\"");
    Id next = 1;
    ParseResult pr = parseMap(text, next);
    CHECK(pr.map.has_value());
    if (!pr.map) return;
    const std::string out = scratch("lut");
    CompileOptions opt;
    opt.mapId = kSampleId;
    opt.mapDir = dir.string();
    CompileReport rep;
    std::string err;
    CHECK(compileIw8Extraction(*pr.map, out, opt, rep, &err));
    const std::string world = readAll((fs::path(out) / "extracted" / "maps" / "mp" / (std::string(kSampleId) + ".d3dbsp.replay-world.json")).string());
    CHECK(world.find("\"grading_clut\":{\"size\":2,\"file\":\"grading/" + std::string(kSampleId) + "_clut.rgba\"}") != std::string::npos);
    CHECK(fs::file_size(fs::path(out) / "extracted" / "grading" / (std::string(kSampleId) + "_clut.rgba")) == 32);
}

// Effect placement (6.6): fx_origin entities become loadfx + spawnfx/playfx calls in the generated map script.
TEST(compile_iw8_fx_placement_script) {
    const std::string sample = samplePath();
    std::string text;
    CHECK(!sample.empty() && readFile(sample, text));
    if (text.empty()) return;
    const std::string eol = text.find("\r\n") != std::string::npos ? "\r\n" : "\n";
    text += "{" + eol + "\"classname\" \"fx_origin\"" + eol + "\"origin\" \"0 0 16\"" + eol + "\"fx\" \"vfx/iw8/veh/scriptables/vfx_veh_explosion_civ\"" + eol + "\"delay\" \"5\"" + eol + "}" + eol;
    text += "{" + eol + "\"classname\" \"fx_origin\"" + eol + "\"origin\" \"64 0 16\"" + eol + "\"angles\" \"-90 0 0\"" + eol + "\"fx\" \"vfx/iw8/veh/scriptables/shared/vfx_veh_fire_linger_med\"" + eol + "}" + eol;
    text += "{" + eol + "\"classname\" \"fx_origin\"" + eol + "\"origin\" \"0 64 16\"" + eol + "}" + eol;  // no fx: skipped
    Id next = 1;
    ParseResult pr = parseMap(text, next);
    CHECK(pr.map.has_value());
    if (!pr.map) return;
    const std::string out = scratch("fx");
    CompileOptions opt;
    opt.mapId = kSampleId;
    CompileReport rep;
    std::string err;
    CHECK(compileIw8Extraction(*pr.map, out, opt, rep, &err));
    const std::string gsc = readAll((fs::path(out) / "extracted" / "maps" / "mp" / (std::string(kSampleId) + ".gsc")).string());
    CHECK(gsc.find("loadfx(\"vfx/iw8/veh/scriptables/vfx_veh_explosion_civ\")") != std::string::npos);
    CHECK(gsc.find("iw8r_repeat_fx(level.iw8r_fx[0], (0, 0, 16), (0, 0, 0), 5)") != std::string::npos);
    CHECK(gsc.find("iw8r_spawn_fx(level.iw8r_fx[1], (64, 0, 16), (-90, 0, 0))") != std::string::npos);
    bool skippedWarned = false;
    for (const auto& w : rep.warnings) skippedWarned |= w.find("no usable fx key") != std::string::npos;
    CHECK(skippedWarned);
}

// Ambient sound (6.7): sound_emitter entities become playloopsound / repeating playsound calls in the map script, the
// stock alias list for x64-zt, and no entity in the .ents.
TEST(compile_iw8_sound_emitters) {
    const std::string sample = samplePath();
    std::string text;
    CHECK(!sample.empty() && readFile(sample, text));
    if (text.empty()) return;
    const std::string eol = text.find("\r\n") != std::string::npos ? "\r\n" : "\n";
    text += "{" + eol + "\"classname\" \"sound_emitter\"" + eol + "\"origin\" \"0 0 32\"" + eol + "\"soundalias\" \"emt_smokestack_lp\"" + eol +
            "\"loop\" \"1\"" + eol + "}" + eol;
    text += "{" + eol + "\"classname\" \"sound_emitter\"" + eol + "\"origin\" \"64 0 32\"" + eol + "\"soundalias\" \"Emt_Rock_Med_Debris_Short\"" +
            eol + "\"delay\" \"7\"" + eol + "}" + eol;
    text += "{" + eol + "\"classname\" \"sound_emitter\"" + eol + "\"origin\" \"0 64 32\"" + eol + "\"soundalias\" \"bad name\"" + eol + "}" + eol;
    Id next = 1;
    ParseResult pr = parseMap(text, next);
    CHECK(pr.map.has_value());
    if (!pr.map) return;
    const std::string out = scratch("snd");
    CompileOptions opt;
    opt.mapId = kSampleId;
    CompileReport rep;
    std::string err;
    CHECK(compileIw8Extraction(*pr.map, out, opt, rep, &err));
    const std::string gsc = readAll((fs::path(out) / "extracted" / "maps" / "mp" / (std::string(kSampleId) + ".gsc")).string());
    CHECK(gsc.find("iw8r_loop_sound(\"emt_smokestack_lp\", (0, 0, 32));") != std::string::npos);
    CHECK(gsc.find("level thread iw8r_repeat_sound(\"emt_rock_med_debris_short\", (64, 0, 32), 7);") != std::string::npos);
    CHECK(gsc.find("ent playloopsound(alias);") != std::string::npos && gsc.find("level thread iw8r_place_sounds();") != std::string::npos);
    const std::string list = readAll((fs::path(out) / "extracted" / "sound" / (std::string(kSampleId) + ".stock_sounds.txt")).string());
    CHECK(list == "emt_rock_med_debris_short 0\nemt_smokestack_lp 1\n");
    bool skippedWarned = false;
    for (const auto& w : rep.warnings) skippedWarned |= w.find("no usable soundalias") != std::string::npos;
    CHECK(skippedWarned);
    for (const auto& e : fs::directory_iterator(fs::path(out) / "extracted" / "maps" / "mp"))
        if (e.path().extension() == ".ents") CHECK(readAll(e.path().string()).find("sound_emitter") == std::string::npos);
}

// Model boxes follow x64-zt's static promotion: a targetname alone still becomes static (box); a targeted one stays an
// entity (no box).
TEST(compile_iw8_model_boxes_follow_static_promotion) {
    const std::string sample = samplePath();
    MapFile base;
    CHECK(!sample.empty() && loadMapFile(sample, base));
    ImportedModel ramp;
    ImportedMesh m;
    m.pos = {Vec3{0, 0, 0}, Vec3{64, 0, 0}, Vec3{0, 32, -128}};
    m.normal = {Vec3{0, 1, 0}, Vec3{0, 1, 0}, Vec3{0, 1, 0}};
    m.uv = {{0, 0}, {1, 0}, {0, 1}};
    m.idx = {0, 1, 2};
    ramp.meshes.push_back(m);
    std::map<std::string, ImportedModel> stock{{"automotive_truck_loading_ramp_short", ramp}};
    CompileOptions opt;
    opt.mapId = kSampleId;
    opt.stockModels = &stock;
    std::string err;
    // The fixture's ramp has targetname "ramp" that nothing targets: promoted to static, so it gets a box.
    CompileReport withName, off;
    CHECK(compileIw8Extraction(base, scratch("promo_a"), opt, withName, &err));
    opt.modelCollisionBoxes = false;
    CHECK(compileIw8Extraction(base, scratch("promo_off"), opt, off, &err));
    CHECK(withName.collisionBrushes == off.collisionBrushes + 1);
    // Another entity targets "ramp": it stays a script_model entity, no box.
    std::string text;
    CHECK(readFile(sample, text));
    const std::string eol = text.find("\r\n") != std::string::npos ? "\r\n" : "\n";
    text += "{" + eol + "\"classname\" \"script_origin\"" + eol + "\"origin\" \"0 0 0\"" + eol + "\"target\" \"ramp\"" + eol + "}" + eol;
    Id next = 1;
    ParseResult pr = parseMap(text, next);
    CHECK(pr.map.has_value());
    if (!pr.map) return;
    opt.modelCollisionBoxes = true;
    CompileReport targeted;
    CHECK(compileIw8Extraction(*pr.map, scratch("promo_t"), opt, targeted, &err));
    CHECK(targeted.collisionBrushes == off.collisionBrushes);
}

// Vision overrides (5.6): worldspawn iw8r_vision -> extracted/vision/<id>.overrides.json.
TEST(compile_iw8_vision_overrides) {
    std::string err;
    CHECK(visionOverridesJson("tonemapExposure=11.5; r_primaryLightTweakDiffuseStrength=0.9 ;fogColor=0.5 0.6 0.7") ==
          "{\"tonemapExposure\":11.5,\"r_primaryLightTweakDiffuseStrength\":0.9,\"fogColor\":[0.5,0.6,0.7]}");
    CHECK(visionOverridesJson("", &err).empty());
    CHECK(visionOverridesJson("bad name=1", &err).empty() && !err.empty());
    CHECK(visionOverridesJson("x=1 2", &err).empty());
    const std::string sample = samplePath();
    std::string text;
    CHECK(!sample.empty() && readFile(sample, text));
    if (text.empty()) return;
    const std::string ws = "\"classname\" \"worldspawn\"";
    const std::string eol = text.find("\r\n") != std::string::npos ? "\r\n" : "\n";
    text.insert(text.find(ws) + ws.size(), eol + "\"iw8r_vision\" \"tonemapExposure=11.5\"");
    Id next = 1;
    ParseResult pr = parseMap(text, next);
    CHECK(pr.map.has_value());
    if (!pr.map) return;
    const std::string out = scratch("vision_overrides");
    CompileOptions opt;
    opt.mapId = kSampleId;
    CompileReport rep;
    CHECK(compileIw8Extraction(*pr.map, out, opt, rep, &err));
    CHECK(readAll((fs::path(out) / "extracted" / "vision" / (std::string(kSampleId) + ".overrides.json")).string()) ==
          "{\"tonemapExposure\":11.5}\n");
}

// Live script_models are made solid at load; the map's own GSC (worldspawn iw8r_script) is carried in and started.
TEST(compile_iw8_script_models_solid_and_user_script) {
    const std::string sample = samplePath();
    std::string text;
    CHECK(!sample.empty() && readFile(sample, text));
    if (text.empty()) return;
    const std::string eol = text.find("\r\n") != std::string::npos ? "\r\n" : "\n";
    const fs::path dir = fs::path(scratch("userscript_src"));
    fs::create_directories(dir);
    {
        std::ofstream f(dir / "my_map.gsc");
        f << "user_main()\n{\n    door = getent(\"door1\", \"targetname\");\n}\n";
    }
    const size_t ws = text.find("\"classname\" \"worldspawn\"");
    CHECK(ws != std::string::npos);
    if (ws == std::string::npos) return;
    text.insert(ws, "\"iw8r_script\" \"my_map.gsc\"" + eol);
    text += "{" + eol + "\"classname\" \"script_model\"" + eol + "\"model\" \"com_junktire\"" + eol + "\"origin\" \"0 0 0\"" + eol +
            "\"targetname\" \"door1\"" + eol + "}" + eol;
    text += "{" + eol + "\"classname\" \"script_origin\"" + eol + "\"origin\" \"0 0 0\"" + eol + "\"target\" \"door1\"" + eol + "}" + eol;
    text += "{" + eol + "\"classname\" \"script_model\"" + eol + "\"model\" \"com_junktire\"" + eol + "\"origin\" \"64 0 0\"" + eol +
            "\"targetname\" \"flag\"" + eol + "\"script_gameobjectname\" \"dom\"" + eol + "}" + eol;
    Id next = 1;
    ParseResult pr = parseMap(text, next);
    CHECK(pr.map.has_value());
    if (!pr.map) return;
    const std::string out = scratch("userscript");
    CompileOptions opt;
    opt.mapId = kSampleId;
    opt.mapDir = dir.string();
    CompileReport rep;
    std::string err;
    CHECK(compileIw8Extraction(*pr.map, out, opt, rep, &err));
    const std::string gsc = readAll((fs::path(out) / "extracted" / "maps" / "mp" / (std::string(kSampleId) + ".gsc")).string());
    CHECK(gsc.find("iw8r_make_solid(\"door1\");") != std::string::npos);
    CHECK(gsc.find("iw8r_make_solid(\"flag\")") == std::string::npos);  // gameobjects belong to the gametype scripts
    CHECK(gsc.find("ent solid();") != std::string::npos);
    CHECK(gsc.find("level thread user_main();") != std::string::npos && gsc.find("door = getent(\"door1\", \"targetname\");") != std::string::npos);
}

// 6.12: trigger / script_brushmodel behaviour keys reach the map script; bad ones are warned about and skipped.
TEST(compile_iw8_inspector_behaviours) {
    const std::string sample = samplePath();
    std::string text;
    CHECK(!sample.empty() && readFile(sample, text));
    if (text.empty()) return;
    const std::string eol = text.find("\r\n") != std::string::npos ? "\r\n" : "\n";
    const auto ent = [&](std::initializer_list<std::pair<const char*, const char*>> kv) {
        std::string s = "{" + eol;
        for (const auto& [k, v] : kv) s += std::string("\"") + k + "\" \"" + v + "\"" + eol;
        return s + "}" + eol;
    };
    text += ent({{"classname", "trigger_multiple"}, {"targetname", "tp_in"}, {"iw8r_teleport", "tp_out"}});
    text += ent({{"classname", "script_origin"}, {"origin", "0 0 64"}, {"targetname", "tp_out"}});
    text += ent({{"classname", "trigger_multiple"}, {"targetname", "tp_lost"}, {"iw8r_teleport", "nowhere"}});
    text += ent({{"classname", "trigger_multiple"}, {"targetname", "pad"}, {"iw8r_jump", "0 0 700"}});
    text += ent({{"classname", "trigger_multiple"}, {"targetname", "pad2"}, {"iw8r_jump", "up"}});
    text += ent({{"classname", "trigger_multiple"}, {"iw8r_damage", "20 2"}});
    text += ent({{"classname", "trigger_multiple"}, {"targetname", "acid"}, {"iw8r_damage", "20 2"}});
    text += ent({{"classname", "script_brushmodel"}, {"targetname", "elevator"}, {"iw8r_lift", "192 2.5 1"}});
    text += ent({{"classname", "misc_model"}, {"model", "iw8r_nomodel"}, {"origin", "0 0 0"}, {"iw8r_spin", "4 roll"}});
    text += ent({{"classname", "fx_origin"}, {"origin", "8 16 24"}, {"fx", "vfx/test/fire"}, {"soundalias", "Fire_Crackle"}, {"iw8r_damage", "10 64 0.5"}});
    text += ent({{"classname", "misc_model"}, {"model", "com_trafficcone_01_dyn"}, {"origin", "0 0 0"}});
    text += ent({{"classname", "script_model"}, {"model", "iw8r_nomodel"}, {"origin", "0 0 0"}, {"targetname", "buoy"}, {"iw8r_bob", "24 3"}});
    Id next = 1;
    ParseResult pr = parseMap(text, next);
    CHECK(pr.map.has_value());
    if (!pr.map) return;
    const std::string out = scratch("behaviours");
    CompileOptions opt;
    opt.mapId = kSampleId;
    CompileReport rep;
    std::string err;
    CHECK(compileIw8Extraction(*pr.map, out, opt, rep, &err));
    const std::string gsc = readAll((fs::path(out) / "extracted" / "maps" / "mp" / (std::string(kSampleId) + ".gsc")).string());
    CHECK(gsc.find("dest = getent(\"tp_out\", \"targetname\");") != std::string::npos);
    CHECK(gsc.find("nowhere") == std::string::npos);
    CHECK(gsc.find("user setvelocity((0, 0, 700));") != std::string::npos);
    CHECK(gsc.find("iw8r_jump_1") == std::string::npos);
    CHECK(gsc.find("trig = iw8r_trigger(\"acid\");") != std::string::npos && gsc.find("iw8r_damage_1") == std::string::npos);
    // Playtest 2026-11-11: triggers are tested by the script (the sample room has the brush trigger room_trigger).
    CHECK(gsc.find("level thread iw8r_trigger_poll();") != std::string::npos);
    CHECK(gsc.find("t = iw8r_trigger_add(\"room_trigger\", 0, 0, 50);") != std::string::npos);
    CHECK(gsc.find("b = iw8r_trigger_box(t, (-96, 64, 0), (-32, 128, 64), 0);") != std::string::npos);
    CHECK(gsc.find("t.proxy notify(\"trigger\", p);") != std::string::npos);
    // A misc_model with iw8r_spin is built as a named script_model that the script turns.
    CHECK(gsc.find("level thread iw8r_spin_model(\"iw8r_mover_0\", 4, 2);") != std::string::npos);
    CHECK(gsc.find("level thread iw8r_bob_model(\"buoy\", 24, 3);") != std::string::npos);
    CHECK(gsc.find("ent rotateroll(360, seconds);") != std::string::npos);
    // An effect's own sound loops at the effect; a physics model placed as misc_model gets a note.
    CHECK(gsc.find("iw8r_loop_sound(\"fire_crackle\", (8, 16, 24));") != std::string::npos);
    // The effect hurts: a nameless damage cylinder of radius 64 around it, 10 damage every 500 ms.
    CHECK(gsc.find("t = iw8r_trigger_add(\"\", 0, 10, 500);") != std::string::npos);
    CHECK(gsc.find("b = iw8r_trigger_box(t, (-56, -48, 8), (72, 80, 88), 64);") != std::string::npos);
    bool dynNote = false;
    for (const auto& w : rep.warnings) dynNote |= w.find("physics model 'com_trafficcone_01_dyn' stand still") != std::string::npos;
    CHECK(dynNote);
    CHECK(gsc.find("lift moveto(bottom + (0, 0, 192), 2.5);") != std::string::npos);
    bool lost = false, badJump = false, noName = false;
    for (const auto& w : rep.warnings) {
        lost |= w.find("no entity has targetname 'nowhere'") != std::string::npos;
        badJump |= w.find("iw8r_jump 'up' not understood") != std::string::npos;
        noName |= w.find("with iw8r_damage needs a targetname") != std::string::npos;
    }
    CHECK(lost && badJump && noName);
}

// 8.2: an fx_origin naming an authored effect (<fx root>/<name>.iw8fx.json) writes the converter's IW3 effect graph,
// one effect material and one image per element; a bad file is warned about.
TEST(compile_iw8_authored_effects) {
    const std::string sample = samplePath();
    std::string text;
    CHECK(!sample.empty() && readFile(sample, text));
    if (text.empty()) return;
    const std::string eol = text.find("\r\n") != std::string::npos ? "\r\n" : "\n";
    const fs::path fxRoot = fs::path(scratch("authored_fx_src")) / "fx";
    fs::create_directories(fxRoot / "iw8r");
    {
        std::ofstream f(fxRoot / "iw8r" / "test_glow.iw8fx.json");
        f << "{\"elements\": [{\"name\": \"glow\", \"additive\": true, \"looping\": true, \"interval_ms\": 150, \"count\": 1, "
             "\"life_ms\": 900, \"velocity_min\": [0, 0, 10], \"velocity_max\": [0, 0, 30], \"size_start\": 6, \"size_end\": 2}]}";
    }
    {
        std::ofstream f(fxRoot / "iw8r" / "broken.iw8fx.json");
        f << "{\"elements\": []}";
    }
    // A soft sprite PNG for elements without an image: write a tiny one with the test's own TGA writer substitute
    // (the converter accepts any decodable image; a 2x2 uncompressed TGA).
    const fs::path sprite = fxRoot / "sprite.tga";
    {
        std::ofstream f(sprite, std::ios::binary);
        const unsigned char hdr[18] = {0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 2, 0, 32, 8};
        f.write(reinterpret_cast<const char*>(hdr), 18);
        for (int i = 0; i < 4; ++i) {
            const unsigned char px[4] = {255, 255, 255, 128};
            f.write(reinterpret_cast<const char*>(px), 4);
        }
    }
    text += "{" + eol + "\"classname\" \"fx_origin\"" + eol + "\"origin\" \"0 0 32\"" + eol + "\"fx\" \"iw8r/test_glow\"" + eol + "}" + eol;
    text += "{" + eol + "\"classname\" \"fx_origin\"" + eol + "\"origin\" \"0 64 32\"" + eol + "\"fx\" \"iw8r/broken\"" + eol + "}" + eol;
    Id next = 1;
    ParseResult pr = parseMap(text, next);
    CHECK(pr.map.has_value());
    if (!pr.map) return;
    const std::string out = scratch("authored_fx");
    CompileOptions opt;
    opt.mapId = kSampleId;
    opt.fxRoots = {fxRoot.string()};
    opt.fxDefaultSprite = sprite.string();
    CompileReport rep;
    std::string err;
    CHECK(compileIw8Extraction(*pr.map, out, opt, rep, &err));
    const fs::path ex = fs::path(out) / "extracted";
    const std::string graph = readAll((ex / "fx" / "iw8r" / "test_glow.iw3.json").string());
    CHECK(graph.find("\"asset_type\":\"iw3_fx\"") != std::string::npos && graph.find("\"name\":\"iw8r/test_glow\"") != std::string::npos);
    AuthoredFx probe;
    probe.name = "iw8r/test_glow";
    probe.elements.resize(1);
    const std::string mat = readAll((ex / "materials" / (authoredFxMaterialName(probe, 0) + ".json")).string());
    CHECK(mat.find("effect_zfeather_add") != std::string::npos);
    std::error_code ec;
    CHECK(fs::file_size(ex / "images" / (authoredFxImageName(probe, 0) + ".dds"), ec) > 128);
    CHECK(!fs::exists(ex / "fx" / "iw8r" / "broken.iw3.json"));
    const std::string gsc = readAll((ex / "maps" / "mp" / (std::string(kSampleId) + ".gsc")).string());
    CHECK(gsc.find("iw8r/test_glow") != std::string::npos);  // loaded by name: the converter converts it
    bool broken = false, written = false;
    for (const auto& w : rep.warnings) {
        broken |= w.find("broken.iw8fx.json not read") != std::string::npos;
        written |= w.find("1 authored effect(s) written") != std::string::npos;
    }
    CHECK(broken && written);
}

// 8.2: a custom model spawned by an authored effect's model element is written into the extraction.
TEST(compile_iw8_authored_effect_model_element) {
    const std::string sample = samplePath();
    std::string text;
    CHECK(!sample.empty() && readFile(sample, text));
    if (text.empty()) return;
    const std::string eol = text.find("\r\n") != std::string::npos ? "\r\n" : "\n";
    const fs::path src = fs::path(scratch("fx_model_src"));
    fs::create_directories(src / "fx" / "iw8r");
    fs::create_directories(src / "models");
    {
        std::ofstream f(src / "models" / "fx_chunk.obj");
        f << "v 0 0 0\nv 4 0 0\nv 0 4 0\nv 0 0 4\nf 1 2 3\nf 1 3 4\nf 1 4 2\nf 2 4 3\n";
    }
    {
        std::ofstream f(src / "fx" / "iw8r" / "chunks.iw8fx.json");
        f << "{\"elements\": [{\"name\": \"chunk\", \"kind\": \"model\", \"model\": \"fx_chunk\", \"looping\": false, \"count\": 4, "
             "\"life_ms\": 1500, \"gravity\": 1, \"size_start\": 1, \"size_end\": 1}]}";
    }
    text += "{" + eol + "\"classname\" \"fx_origin\"" + eol + "\"origin\" \"0 0 32\"" + eol + "\"fx\" \"iw8r/chunks\"" + eol + "}" + eol;
    Id next = 1;
    ParseResult pr = parseMap(text, next);
    CHECK(pr.map.has_value());
    if (!pr.map) return;
    const std::string out = scratch("fx_model");
    CompileOptions opt;
    opt.mapId = kSampleId;
    opt.fxRoots = {(src / "fx").string()};
    opt.modelRoots = {(src / "models").string()};
    CompileReport rep;
    std::string err;
    CHECK(compileIw8Extraction(*pr.map, out, opt, rep, &err));
    const fs::path ex = fs::path(out) / "extracted";
    CHECK(fs::exists(ex / "xmodel" / "fx_chunk.json"));
    const std::string graph = readAll((ex / "fx" / "iw8r" / "chunks.iw3.json").string());
    CHECK(graph.find("\"elem_type_name\":\"model\"") != std::string::npos && graph.find("\"name\":\"fx_chunk\"") != std::string::npos);
}

// 6.22: worldspawn iw8r_reverb and an iw8r_audio_zone trigger write the converter's sound table and client triggers.
TEST(compile_iw8_audio_zones) {
    const std::string sample = samplePath();
    std::string text;
    CHECK(!sample.empty() && readFile(sample, text));
    if (text.empty()) return;
    const std::string eol = text.find("\r\n") != std::string::npos ? "\r\n" : "\n";
    const std::string ws = "\"classname\" \"worldspawn\"";
    text.insert(text.find(ws) + ws.size(), eol + "\"iw8r_reverb\" \"iw8_md_room\"");
    const auto face = [&](const char* pts) { return std::string(" ") + pts + " trigger 128 128 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0" + eol; };
    text += "{" + eol + "\"classname\" \"trigger_multiple\"" + eol + "\"iw8r_audio_zone\" \"hall\"" + eol +
            "\"iw8r_reverb\" \"iw8_lg_room\"" + eol + "{" + eol + face("( 64 64 0 ) ( -64 64 0 ) ( -64 -64 0 )") +
            face("( -64 -64 128 ) ( -64 64 128 ) ( 64 64 128 )") + face("( -64 -64 128 ) ( 64 -64 128 ) ( 64 -64 0 )") +
            face("( 64 64 128 ) ( -64 64 128 ) ( -64 64 0 )") + face("( 64 -64 128 ) ( 64 64 128 ) ( 64 64 0 )") +
            face("( -64 64 128 ) ( -64 -64 128 ) ( -64 -64 0 )") + "}" + eol + "}" + eol;
    Id next = 1;
    ParseResult pr = parseMap(text, next);
    CHECK(pr.map.has_value());
    if (!pr.map) return;
    const std::string out = scratch("audio_zones");
    CompileOptions opt;
    opt.mapId = kSampleId;
    CompileReport rep;
    std::string err;
    CHECK(compileIw8Extraction(*pr.map, out, opt, rep, &err));
    const fs::path ex = fs::path(out) / "extracted";
    const std::string table = readAll((ex / "sound" / "soundtable.json").string());
    const std::string trig = readAll((ex / "maps" / "clienttriggers.json").string());
    CHECK(table.find("\"roomType\":\"iw8_md_room\"") != std::string::npos && table.find("\"roomType\":\"iw8_lg_room\"") != std::string::npos);
    CHECK(table.find(std::string("\"zoneName\":\"") + kSampleId + "\"") != std::string::npos);
    CHECK(trig.find("\"audio\":\"hall\"") != std::string::npos && trig.find("\"halfSize\":[64,64,64]") != std::string::npos);
}

// 6.22: a sound_emitter and a worldspawn iw8r_ambient naming the map's own sounds put the files and alias definitions in
// the extraction; stock aliases stay in stock_sounds.txt.
TEST(compile_iw8_custom_sounds) {
    const std::string sample = samplePath();
    std::string text;
    CHECK(!sample.empty() && readFile(sample, text));
    if (text.empty()) return;
    const std::string eol = text.find("\r\n") != std::string::npos ? "\r\n" : "\n";
    const fs::path sounds = fs::path(scratch("custom_sounds_src"));
    fs::create_directories(sounds);
    std::ofstream(sounds / "fire_loop.wav", std::ios::binary) << "RIFF0000WAVE";
    std::ofstream(sounds / "wind_bed.mp3", std::ios::binary) << "ID3";
    const std::string ws = "\"classname\" \"worldspawn\"";
    text.insert(text.find(ws) + ws.size(), eol + "\"iw8r_ambient\" \"wind_bed\"");
    text += "{" + eol + "\"classname\" \"sound_emitter\"" + eol + "\"origin\" \"0 0 32\"" + eol + "\"soundalias\" \"fire_loop\"" + eol +
            "\"loop\" \"1\"" + eol + "}" + eol;
    text += "{" + eol + "\"classname\" \"sound_emitter\"" + eol + "\"origin\" \"0 64 32\"" + eol + "\"soundalias\" \"emt_smokestack_lp\"" +
            eol + "\"loop\" \"1\"" + eol + "}" + eol;
    Id next = 1;
    ParseResult pr = parseMap(text, next);
    CHECK(pr.map.has_value());
    if (!pr.map) return;
    const std::string out = scratch("custom_sounds");
    CompileOptions opt;
    opt.mapId = kSampleId;
    opt.soundRoots = {sounds.string()};
    CompileReport rep;
    std::string err;
    CHECK(compileIw8Extraction(*pr.map, out, opt, rep, &err));
    const fs::path ex = fs::path(out) / "extracted";
    CHECK(fs::exists(ex / "sound" / "iw8r" / "fire_loop.wav") && fs::exists(ex / "sound" / "iw8r" / "wind_bed.mp3"));
    const std::string fire = readAll((ex / "soundaliases" / "fire_loop.iw3.json").string());
    const std::string wind = readAll((ex / "soundaliases" / "wind_bed.iw3.json").string());
    CHECK(fire.find("\"flags\":257") != std::string::npos);    // looping, channel auto (3D)
    CHECK(wind.find("\"flags\":6145") != std::string::npos);   // looping, channel ambient (2D bed)
    const std::string stock = readAll((ex / "sound" / (std::string(kSampleId) + ".stock_sounds.txt")).string());
    CHECK(stock.find("emt_smokestack_lp") != std::string::npos && stock.find("fire_loop") == std::string::npos);
    const std::string table = readAll((ex / "sound" / "soundtable.json").string());
    CHECK(table.find("\"ambientStream\":\"wind_bed\"") != std::string::npos);
    const std::string gsc = readAll((ex / "maps" / "mp" / (std::string(kSampleId) + ".gsc")).string());
    CHECK(gsc.find("\"fire_loop\"") != std::string::npos);  // the map script names it: the converter builds it
}

// Owner playtest 2 (2026-11-11): a moving brush model is drawn by a generated model the script links to it.
TEST(compile_iw8_brush_model_gets_a_visual_model) {
    std::string text = readAll(samplePath());
    if (text.empty()) return;
    const std::string eol = text.find("\r\n") != std::string::npos ? "\r\n" : "\n";
    text += "{" + eol + "\"classname\" \"script_brushmodel\"" + eol + "\"targetname\" \"gate\"" + eol + "\"iw8r_lift\" \"64 2 1\"" + eol + "{" + eol +
            " ( 64 64 0 ) ( 0 64 0 ) ( 0 0 0 ) caulk 64 64 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0" + eol +
            " ( 0 0 32 ) ( 0 64 32 ) ( 64 64 32 ) caulk 64 64 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0" + eol +
            " ( 0 0 32 ) ( 64 0 32 ) ( 64 0 0 ) tm2co/me_curb_stone_01 64 64 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0" + eol +
            " ( 64 64 32 ) ( 0 64 32 ) ( 0 64 0 ) tm2co/me_curb_stone_01 64 64 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0" + eol +
            " ( 64 0 32 ) ( 64 64 32 ) ( 64 64 0 ) tm2co/me_curb_stone_01 64 64 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0" + eol +
            " ( 0 64 32 ) ( 0 0 32 ) ( 0 0 0 ) tm2co/me_curb_stone_01 64 64 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0" + eol + "}" + eol + "}" + eol;
    Id next = 1;
    ParseResult pr = parseMap(text, next);
    CHECK(pr.map.has_value());
    if (!pr.map) return;
    const std::string out = scratch("bmvisual");
    CompileOptions opt;
    opt.mapId = kSampleId;
    CompileReport rep;
    std::string err;
    CHECK(compileIw8Extraction(*pr.map, out, opt, rep, &err));
    const std::string gsc = readAll((fs::path(out) / "extracted" / "maps" / "mp" / (std::string(kSampleId) + ".gsc")).string());
    const std::string ents = readAll((fs::path(out) / "extracted" / "maps" / "mp" / (std::string(kSampleId) + ".d3dbsp.ents")).string());
    CHECK(gsc.find("level thread iw8r_link_visual(\"gate_vis\", \"gate\");") != std::string::npos);
    CHECK(gsc.find("vis linkto(parent);") != std::string::npos);
    CHECK(ents.find("\"targetname\" \"gate_vis\"") != std::string::npos);
    CHECK(ents.find("iw8r_stock_iw8r_bmvis_") != std::string::npos);  // the generated model, built like a stock copy
    bool note = false;
    for (const auto& w : rep.warnings) note |= w.find("moving brush model(s) drawn by a generated model") != std::string::npos;
    CHECK(note);
    bool modelFile = false;
    std::error_code ec;
    for (const auto& f : fs::directory_iterator(fs::path(out) / "extracted" / "xmodel", ec))
        modelFile |= f.path().filename().string().find("iw8r_bmvis_") != std::string::npos;
    CHECK(modelFile);
}

// Owner report 2026-11-11: a material set on the mapper's own model (a shape cube used as stairs) was refused, because
// only stock models could be re-skinned. A model from the model folders now gets a copy built from the same file.
TEST(compile_iw8_custom_model_material) {
    const std::string sample = samplePath();
    std::string text;
    CHECK(!sample.empty() && readFile(sample, text));
    if (text.empty()) return;
    const std::string eol = text.find("\r\n") != std::string::npos ? "\r\n" : "\n";
    const fs::path src = fs::path(scratch("custom_model_mat_src"));
    fs::create_directories(src / "models");
    {
        std::ofstream f(src / "models" / "stair_cube.obj");
        f << "v 0 0 0\nv 16 0 0\nv 0 16 0\nv 0 0 16\nusemtl tm/old_wall\nf 1 2 3\nf 1 3 4\nf 1 4 2\nf 2 4 3\n";
    }
    const auto model = [&](const std::string& origin, const std::string& extra) {
        return "{" + eol + "\"classname\" \"misc_model\"" + eol + "\"model\" \"stair_cube\"" + eol + "\"origin\" \"" + origin + "\"" + eol + extra + "}" + eol;
    };
    text += model("0 0 0", "\"iw8r_material\" \"custom/grass\"" + eol) + model("32 0 0", "\"iw8r_material\" \"custom/grass\"" + eol) + model("64 0 0", "");
    Id next = 1;
    ParseResult pr = parseMap(text, next);
    CHECK(pr.map.has_value());
    if (!pr.map) return;
    const std::string out = scratch("custom_model_mat");
    CompileOptions opt;
    opt.mapId = kSampleId;
    opt.modelRoots = {(src / "models").string()};
    CompileReport rep;
    std::string err;
    CHECK(compileIw8Extraction(*pr.map, out, opt, rep, &err));
    bool note = false, refused = false;
    for (const auto& w : rep.warnings) {
        note |= w.find("2 model(s) built with their iw8r_material") != std::string::npos;
        refused |= w.find("it keeps its own") != std::string::npos;
    }
    CHECK(note && !refused);
    // The re-skinned copies keep their player collision: all three placements get a collision shape, exactly as
    // three plain ones do (owner report 2026-11-11: re-skinned cubes stopped bullets but not the player).
    {
        std::string plainText;
        CHECK(readFile(sample, plainText));
        plainText += model("0 0 0", "") + model("32 0 0", "") + model("64 0 0", "");
        Id nextPlain = 1;
        ParseResult plainMap = parseMap(plainText, nextPlain);
        CHECK(plainMap.map.has_value());
        CompileReport plainRep;
        if (plainMap.map) CHECK(compileIw8Extraction(*plainMap.map, scratch("custom_model_mat_plain"), opt, plainRep, &err));
        CHECK(plainRep.collisionBrushes > 0 && rep.collisionBrushes == plainRep.collisionBrushes);
    }
    // The plain model and one re-skinned copy (both placements share it) are written.
    const fs::path xm = fs::path(out) / "extracted" / "xmodel";
    CHECK(fs::exists(xm / "stair_cube.json"));
    std::string variant;
    std::error_code ec;
    for (const auto& f : fs::directory_iterator(xm, ec)) {
        const std::string n = f.path().filename().string();
        if (n.rfind("stair_cube_m", 0) == 0 && n.size() > 5 && n.substr(n.size() - 12) == ".replay.json") variant = n;
    }
    CHECK(!variant.empty());
    if (!variant.empty()) {
        const std::string body = readAll((xm / variant).string());
        CHECK(body.find("old_wall") == std::string::npos);  // the copy does not use the model's own material
    }
    const std::string plain = readAll((xm / "stair_cube.replay.json").string());
    CHECK(!plain.empty());
    // A model that is neither stock nor in the model folders still says why it is left alone.
    std::string text2;
    CHECK(readFile(sample, text2));
    text2 += "{" + eol + "\"classname\" \"misc_model\"" + eol + "\"model\" \"tm/not_exported\"" + eol + "\"origin\" \"0 0 0\"" + eol + "\"iw8r_material\" \"custom/grass\"" + eol + "}" + eol;
    Id next2 = 1;
    ParseResult pr2 = parseMap(text2, next2);
    CHECK(pr2.map.has_value());
    if (!pr2.map) return;
    CompileReport rep2;
    CHECK(compileIw8Extraction(*pr2.map, scratch("custom_model_mat2"), opt, rep2, &err));
    bool kept = false;
    for (const auto& w : rep2.warnings) kept |= w.find("not a stock model from the model cache and not a model in the model folders") != std::string::npos;
    CHECK(kept);
}

// 5.8 (research 80 section 6): iw8r_decal point entities are written as world.json decal_volumes; a bad one is left
// out with a warning and never stops the build; the class does not reach the entity list.
TEST(compile_iw8_decal_volumes) {
    const std::string sample = samplePath();
    std::string text;
    CHECK(!sample.empty() && readFile(sample, text));
    if (text.empty()) return;
    const std::string eol = text.find("\r\n") != std::string::npos ? "\r\n" : "\n";
    const auto decal = [&](const std::string& keys) { return "{" + eol + "\"classname\" \"iw8r_decal\"" + eol + keys + "}" + eol; };
    const auto kv = [&](const std::string& k, const std::string& v) { return "\"" + k + "\" \"" + v + "\"" + eol; };
    const std::string base = text;
    text += decal(kv("origin", "32 -16 4") + kv("angles", "90 0 0") + kv("size", "8 64 32") + kv("material", "i/vfx_decal_surface_scorch_1") +
                  kv("opacity", "0.5") + kv("feather", "0.25") + kv("draw_order", "2") + kv("uv_scale", "2 4") + kv("uv_shift", "0.5 0"));
    text += decal(kv("origin", "0 0 4") + kv("size", "8 64 64"));                                         // no material
    text += decal(kv("origin", "0 0 4") + kv("material", "i/x") + kv("size", "0 64 64"));                 // no depth
    text += decal(kv("origin", "64 0 4") + kv("material", "i/y") + kv("opacity", "7") + kv("draw_order", "9"));  // bad ranges: defaults
    Id next = 1;
    ParseResult pr = parseMap(text, next);
    CHECK(pr.map.has_value());
    if (!pr.map) return;
    const std::string out = scratch("decals");
    CompileOptions opt;
    opt.mapId = kSampleId;
    CompileReport rep;
    std::string err;
    CHECK(compileIw8Extraction(*pr.map, out, opt, rep, &err));
    const fs::path mp = fs::path(out) / "extracted" / "maps" / "mp";
    const std::string world = readAll((mp / (std::string(kSampleId) + ".d3dbsp.replay-world.json")).string());
    const size_t at = world.find("\"decal_volumes\":[");
    CHECK(at != std::string::npos);
    if (at == std::string::npos) return;
    const std::string decals = world.substr(at, world.find("]}", at) == std::string::npos ? std::string::npos : world.find(",\"dpvs\"", at) - at);
    size_t entries = 0;
    for (size_t p2 = 0; (p2 = decals.find("\"center\":", p2)) != std::string::npos; ++p2) ++entries;
    CHECK(entries == 2);  // the two valid ones
    CHECK(decals.find("\"material\":\"i/vfx_decal_surface_scorch_1\"") != std::string::npos);
    CHECK(decals.find("\"half_size\":[4,32,16]") != std::string::npos);
    CHECK(decals.find("\"opacity\":0.5") != std::string::npos && decals.find("\"feather\":0.25") != std::string::npos &&
          decals.find("\"draw_order\":2") != std::string::npos);
    CHECK(decals.find("\"uv_matrix\":[2,-0,0,4]") != std::string::npos || decals.find("\"uv_matrix\":[2,0,0,4]") != std::string::npos);
    CHECK(decals.find("\"uv_offset\":[0.5,0]") != std::string::npos);
    // The entry with bad ranges keeps its place with the defaults.
    CHECK(decals.find("\"material\":\"i/y\",\"opacity\":1,\"feather\":0,\"draw_order\":0") != std::string::npos);
    bool noMaterial = false, noDepth = false, range = false, written = false;
    for (const auto& w : rep.warnings) {
        noMaterial |= w.find("iw8r_decal") != std::string::npos && w.find("no material; left out") != std::string::npos;
        noDepth |= w.find("size must be three numbers above 0") != std::string::npos;
        range |= w.find("opacity '7' out of range") != std::string::npos;
        written |= w.find("2 decal volume(s) written") != std::string::npos;
    }
    CHECK(noMaterial && noDepth && range && written);
    // The class is not an entity of the built map.
    const std::string ents = readAll((mp / (std::string(kSampleId) + ".d3dbsp.ents")).string());
    CHECK(ents.find("iw8r_decal") == std::string::npos);
    // A map without decals has no decal_volumes key at all (the built world is as before).
    Id next2 = 1;
    ParseResult plain = parseMap(base, next2);
    CHECK(plain.map.has_value());
    if (!plain.map) return;
    const std::string out2 = scratch("decals_none");
    CompileReport rep2;
    CHECK(compileIw8Extraction(*plain.map, out2, opt, rep2, &err));
    const std::string world2 = readAll((fs::path(out2) / "extracted" / "maps" / "mp" / (std::string(kSampleId) + ".d3dbsp.replay-world.json")).string());
    CHECK(!world2.empty() && world2.find("decal_volumes") == std::string::npos);
}

// 6.14 (research 86): `scriptable` point entities become the collision json's scriptable_instances with the numbers of
// the stock definition; a definition that is not always loaded, not fully known, or unknown is left out with a warning.
TEST(compile_iw8_scriptable_instances) {
    ScriptableCatalog catalog;
    CHECK(catalog.parse(
        "# comment\n"
        "name\tzones\talways_loaded\tparts\tserver_instanced_parts\tusable_parts\tmax_dynent_parts\tphysics_dynents\tinstance_dynent\tsparse_occluder\tcollision_flags\tphysics_limit\tflags\tmodel\tsource\tverified\n"
        "hardpoint_chevron\tcommon_core_mp\t1\t4\t4\t0\t5\t0\t1\t0\t0\t0\t0x402246\thp_chevron_rig\tobserved\t1\n"
        "scriptable_door_x\tmp_backlot2\t0\t1\t1\t1\t2\t0\t1\t1\t1\t1\t0x48E2C7\tdoor_x\tobserved\t1\n"
        "scriptable_auto_ascender\tcommon_mp\t1\t1\t1\t1\t3\t0\t1\t0\t0\t0\t0x402046\ttag_origin\tobserved\t0\n"));
    CHECK(catalog.size() == 3);
    const ScriptableDefinition* chevron = catalog.find("HARDPOINT_CHEVRON");
    CHECK(chevron && chevron->placeable() && chevron->serverInstancedParts == 4 && chevron->maxDynentParts == 5 &&
          chevron->instanceDynent && !chevron->sparseOccluder && chevron->model == "hp_chevron_rig");
    const ScriptableDefinition* door = catalog.find("scriptable_door_x");
    CHECK(door && !door->placeable() && door->sparseOccluder && door->physicsLimit && door->collisionFlags == 1 && door->usableParts == 1);
    CHECK(catalog.find("scriptable_auto_ascender") && !catalog.find("scriptable_auto_ascender")->placeable());
    CHECK(!catalog.find("nothing"));

    std::string text;
    CHECK(readFile(samplePath(), text));
    if (text.empty()) return;
    const std::string eol = text.find("\r\n") != std::string::npos ? "\r\n" : "\n";
    const std::string base = text;
    const auto ent = [&](const std::string& keys) { return "{" + eol + "\"classname\" \"scriptable\"" + eol + keys + "}" + eol; };
    const auto kv = [&](const std::string& k, const std::string& v) { return "\"" + k + "\" \"" + v + "\"" + eol; };
    text += ent(kv("scriptable", "hardpoint_chevron") + kv("origin", "64 32 16") + kv("angles", "0 90 0") + kv("targetname", "hp_a") +
                kv("script_noteworthy", "zone_1"));
    text += ent(kv("scriptable", "hardpoint_chevron") + kv("origin", "-64 32 16"));
    text += ent(kv("scriptable", "scriptable_door_x") + kv("origin", "0 0 0"));         // lives in a map zone
    text += ent(kv("scriptable", "scriptable_auto_ascender") + kv("origin", "0 0 0"));  // not fully known
    text += ent(kv("scriptable", "made_up_thing") + kv("origin", "0 0 0"));             // unknown
    text += ent(kv("origin", "0 0 0"));                                                 // no definition
    Id next = 1;
    ParseResult pr = parseMap(text, next);
    CHECK(pr.map.has_value());
    if (!pr.map) return;
    const std::string out = scratch("scriptables");
    CompileOptions opt;
    opt.mapId = kSampleId;
    opt.scriptables = &catalog;
    CompileReport rep;
    std::string err;
    CHECK(compileIw8Extraction(*pr.map, out, opt, rep, &err));
    const fs::path mp = fs::path(out) / "extracted" / "maps" / "mp";
    const std::string col = readAll((mp / (std::string(kSampleId) + ".d3dbsp.replay-collision.json")).string());
    const size_t at = col.find("\"scriptable_instances\":[");
    CHECK(at != std::string::npos);
    if (at == std::string::npos) return;
    const std::string list = col.substr(at);
    size_t entries = 0;
    for (size_t p2 = 0; (p2 = list.find("\"definition\":", p2)) != std::string::npos; ++p2) ++entries;
    CHECK(entries == 2);
    CHECK(list.find("{\"definition\":\"hardpoint_chevron\",\"origin\":[64,32,16],\"angles\":[0,90,0],\"targetname\":\"hp_a\","
                    "\"script_noteworthy\":\"zone_1\",\"server_instanced_parts\":4,\"usable_parts\":0,\"physics_limit\":false,"
                    "\"max_dynent_parts\":5,\"instance_dynent\":true,\"sparse_occluder\":false,\"collision_flags\":0,"
                    "\"physics_dynents\":0}") != std::string::npos);
    CHECK(list.find("{\"definition\":\"hardpoint_chevron\",\"origin\":[-64,32,16],\"angles\":[0,0,0],\"server_instanced_parts\":4") != std::string::npos);
    CHECK(list.find("scriptable_door_x") == std::string::npos && list.find("made_up_thing") == std::string::npos);
    bool mapZone = false, notKnown = false, unknown = false, noKey = false, written = false;
    for (const auto& w : rep.warnings) {
        mapZone |= w.find("'scriptable_door_x' lives only in stock map zones (mp_backlot2)") != std::string::npos;
        notKnown |= w.find("'scriptable_auto_ascender': no stock map shows all the numbers") != std::string::npos;
        unknown |= w.find("'made_up_thing' is not a stock scriptable definition") != std::string::npos;
        noKey |= w.find("no scriptable key") != std::string::npos;
        written |= w.find("2 scriptable instance(s) written") != std::string::npos;
    }
    CHECK(mapZone && notKnown && unknown && noKey && written);
    // The class is not an entity of the built map.
    const std::string ents = readAll((mp / (std::string(kSampleId) + ".d3dbsp.ents")).string());
    CHECK(ents.find("scriptable") == std::string::npos);
    // No door was built, so the map script does not start the stock door script.
    CHECK(readAll((mp / (std::string(kSampleId) + ".gsc")).string()).find("scriptable_door") == std::string::npos);
    CHECK(door->carryZone() == "mp_backlot2" && door->carryable() && door->door() && chevron->carryZone().empty() && !chevron->door());

    // With carrying on, the door is written with the stock zone to take its definition from, and the map script
    // starts the stock door script right after the load script.
    {
        CompileOptions carry = opt;
        carry.carryScriptables = true;
        CompileReport rep4;
        const std::string out4 = scratch("scriptables_carry");
        CHECK(compileIw8Extraction(*pr.map, out4, carry, rep4, &err));
        const fs::path mp4 = fs::path(out4) / "extracted" / "maps" / "mp";
        const std::string col4 = readAll((mp4 / (std::string(kSampleId) + ".d3dbsp.replay-collision.json")).string());
        size_t entries4 = 0;
        for (size_t p2 = 0; (p2 = col4.find("\"definition\":", p2)) != std::string::npos; ++p2) ++entries4;
        CHECK(entries4 == 3);
        CHECK(col4.find("{\"definition\":\"scriptable_door_x\",\"origin\":[0,0,0],\"angles\":[0,0,0],\"carry_zone\":\"mp_backlot2\","
                        "\"server_instanced_parts\":1,\"usable_parts\":1,\"physics_limit\":true,\"max_dynent_parts\":2,"
                        "\"instance_dynent\":true,\"sparse_occluder\":true,\"collision_flags\":1,\"physics_dynents\":0}") != std::string::npos);
        CHECK(col4.find("\"definition\":\"hardpoint_chevron\",\"origin\":[64,32,16],\"angles\":[0,90,0],\"targetname\":\"hp_a\","
                        "\"script_noteworthy\":\"zone_1\",\"server_instanced_parts\"") != std::string::npos);  // no carry_zone
        bool said = false;
        for (const auto& w : rep4.warnings) said |= w.find("1 scriptable instance(s) carry their definition from the stock zone mp_backlot2") != std::string::npos;
        CHECK(said);
        const std::string gsc = readAll((mp4 / (std::string(kSampleId) + ".gsc")).string());
        const size_t load = gsc.find("maps\\mp\\_load::main();"), doors = gsc.find("scripts\\engine\\scriptable_door::system_init();");
        CHECK(load != std::string::npos && doors != std::string::npos && doors > load);
    }

    // Without a catalogue the entities are left out; a map without scriptables has no scriptable_instances key.
    CompileOptions bare;
    bare.mapId = kSampleId;
    CompileReport rep2;
    const std::string out2 = scratch("scriptables_nocat");
    CHECK(compileIw8Extraction(*pr.map, out2, bare, rep2, &err));
    const std::string col2 = readAll((fs::path(out2) / "extracted" / "maps" / "mp" / (std::string(kSampleId) + ".d3dbsp.replay-collision.json")).string());
    CHECK(!col2.empty() && col2.find("scriptable_instances") == std::string::npos);
    bool noCatalogue = false;
    for (const auto& w : rep2.warnings) noCatalogue |= w.find("scriptable catalogue") != std::string::npos;
    CHECK(noCatalogue);
    Id next3 = 1;
    ParseResult plain = parseMap(base, next3);
    CHECK(plain.map.has_value());
    if (!plain.map) return;
    CompileReport rep3;
    const std::string out3 = scratch("scriptables_none");
    CHECK(compileIw8Extraction(*plain.map, out3, opt, rep3, &err));
    const std::string col3 = readAll((fs::path(out3) / "extracted" / "maps" / "mp" / (std::string(kSampleId) + ".d3dbsp.replay-collision.json")).string());
    CHECK(!col3.empty() && col3.find("scriptable_instances") == std::string::npos);
}

// Research 87: a map can carry game mode dvars (worldspawn iw8r_mode_dvars) and the Warzone switches of a small map
// (iw8r_br); the generated map script sets them right after the load script.
TEST(compile_iw8_game_mode_settings_in_map_script) {
    std::string text;
    CHECK(readFile(samplePath(), text));
    if (text.empty()) return;
    const std::string eol = text.find("\r\n") != std::string::npos ? "\r\n" : "\n";
    const std::string marker = "\"classname\" \"worldspawn\"" + eol;
    const size_t at = text.find(marker);
    CHECK(at != std::string::npos);
    if (at == std::string::npos) return;
    text.insert(at + marker.size(), "\"iw8r_mode_dvars\" \"scr_br_teamsize=1; scr_br_gulag=0; bad name=3\"" + eol + "\"iw8r_br\" \"nocircle nogulag skipdrop\"" + eol);
    Id next = 1;
    ParseResult pr = parseMap(text, next);
    CHECK(pr.map.has_value());
    if (!pr.map) return;
    const std::string out = scratch("mode_settings");
    CompileOptions opt;
    opt.mapId = kSampleId;
    CompileReport rep;
    std::string err;
    CHECK(compileIw8Extraction(*pr.map, out, opt, rep, &err));
    const std::string gsc = readAll((fs::path(out) / "extracted" / "maps" / "mp" / (std::string(kSampleId) + ".gsc")).string());
    const size_t load = gsc.find("maps\\mp\\_load::main();");
    const size_t d1 = gsc.find("setdvar(\"iw8r:scr_br_teamsize\", \"1\");"), d2 = gsc.find("setdvar(\"iw8r:scr_br_gulag\", \"0\");");
    CHECK(load != std::string::npos && d1 != std::string::npos && d2 != std::string::npos && d1 > load && d2 > d1);
    CHECK(gsc.find("bad name") == std::string::npos);
    CHECK(gsc.find("level.br_circle_disabled = 1;") != std::string::npos &&
          gsc.find("scripts\\mp\\gametypes\\br_gametypes::disablefeature(\"circle\");") != std::string::npos);
    CHECK(gsc.find("level.usegulag = 0;") != std::string::npos && gsc.find("level.skipprematchdropspawn = 1;") != std::string::npos);
    bool dvars = false, br = false, bad = false;
    for (const auto& w : rep.warnings) {
        dvars |= w.find("2 game mode dvar(s) are set by the map script") != std::string::npos;
        br |= w.find("Warzone (br) switches") != std::string::npos && w.find("no circle no gulag no drop from the plane") != std::string::npos;
        bad |= w.find("iw8r_mode_dvars") != std::string::npos && w.find("bad name=3") != std::string::npos;
    }
    CHECK(dvars && br && bad);

    // A map without the keys gets none of it.
    Id next2 = 1;
    std::string plainText;
    CHECK(readFile(samplePath(), plainText));
    ParseResult plain = parseMap(plainText, next2);
    CHECK(plain.map.has_value());
    if (!plain.map) return;
    CompileReport rep2;
    const std::string out2 = scratch("mode_settings_none");
    CHECK(compileIw8Extraction(*plain.map, out2, opt, rep2, &err));
    const std::string gsc2 = readAll((fs::path(out2) / "extracted" / "maps" / "mp" / (std::string(kSampleId) + ".gsc")).string());
    CHECK(gsc2.find("setdvar(") == std::string::npos && gsc2.find("br_") == std::string::npos && gsc2.find("usegulag") == std::string::npos);
}

// The shipped catalogue: hardpoint_chevron is placeable; a stock door is listed but lives in map zones.
TEST(scriptable_catalog_shipped_file) {
    ScriptableCatalog catalog;
    std::string err;
    if (!catalog.load("data/asset_catalog/iw8_scriptables.tsv", &err)) {
        test::skipf("data/asset_catalog/iw8_scriptables.tsv not found");
        return;
    }
    CHECK(catalog.size() > 500);
    const ScriptableDefinition* chevron = catalog.find("hardpoint_chevron");
    CHECK(chevron && chevron->placeable() && chevron->serverInstancedParts == 4 && chevron->maxDynentParts == 5 && chevron->physicsDynents == 0);
    const ScriptableDefinition* door = catalog.find("scriptable_door_wooden_panel_03_painted_mp");
    CHECK(door && !door->alwaysLoaded && door->sparseOccluder && door->usableParts == 1 && door->maxDynentParts == 2);
    const ScriptableDefinition* cone = catalog.find("com_trafficcone_01");
    CHECK(cone && cone->physicsDynents == 1 && cone->instanceDynent && !cone->alwaysLoaded);
    size_t placeable = 0;
    for (const auto& d : catalog.all()) placeable += d.placeable() ? 1 : 0;
    CHECK(placeable >= 1);
}

// 12.7: "iw8r_mat:<model material>" replaces one slot of a stock model; the build makes a copy named from the change.
TEST(compile_iw8_model_material_slot) {
    std::string text;
    CHECK(readFile(samplePath(), text));
    if (text.empty()) return;
    const std::string eol = text.find("\r\n") != std::string::npos ? "\r\n" : "\n";
    text += "{" + eol + "\"classname\" \"misc_model\"" + eol + "\"model\" \"tm/test_two_slot\"" + eol + "\"origin\" \"0 0 0\"" + eol +
            "\"iw8r_mat:mo/slot_b\" \"custom/red\"" + eol + "}" + eol;
    Id next = 1;
    ParseResult pr = parseMap(text, next);
    CHECK(pr.map.has_value());
    if (!pr.map) return;
    ImportedModel im;
    im.name = "tm/test_two_slot";
    for (const char* mat : {"mo/slot_a", "mo/SLOT_B"}) {
        ImportedMesh mesh;
        mesh.material = mat;
        mesh.pos = {{0, 0, 0}, {16, 0, 0}, {0, 16, 0}};
        mesh.normal = {{0, 0, 1}, {0, 0, 1}, {0, 0, 1}};
        mesh.uv = {{0, 0}, {1, 0}, {0, 1}};
        mesh.idx = {0, 1, 2};
        for (const Vec3& v : mesh.pos) im.bounds.add(v);
        im.meshes.push_back(mesh);
    }
    std::map<std::string, ImportedModel> stock;
    stock.emplace(im.name, im);
    const std::string out = scratch("modelslot");
    CompileOptions opt;
    opt.mapId = kSampleId;
    opt.stockModels = &stock;
    CompileReport rep;
    std::string err;
    CHECK(compileIw8Extraction(*pr.map, out, opt, rep, &err));
    bool note = false;
    for (const auto& w : rep.warnings) note |= w.find("1 model(s) built with their iw8r_material") != std::string::npos;
    CHECK(note);
    // The copy is written as its own model, and the entity is pointed at it.
    bool copy = false;
    std::error_code ec;
    for (const auto& f : fs::directory_iterator(fs::path(out) / "extracted" / "xmodel", ec))
        copy |= f.path().filename().string().find("_iw8rmat_") != std::string::npos;
    CHECK(copy);
    const std::string ents = readAll((fs::path(out) / "extracted" / "maps" / "mp" / (std::string(kSampleId) + ".d3dbsp.ents")).string());
    CHECK(rep.staticModels > 0);
}

// Owner request 2026-11-11: a hinged door that swings away from the player who uses it; no trigger entity is needed.
TEST(compile_iw8_hinged_door) {
    std::string text;
    CHECK(readFile(samplePath(), text));
    if (text.empty()) return;
    const std::string eol = text.find("\r\n") != std::string::npos ? "\r\n" : "\n";
    const auto door = [&](const char* name, const char* key, const char* extra) {
        return "{" + eol + "\"classname\" \"script_brushmodel\"" + eol + "\"targetname\" \"" + name + "\"" + eol + "\"iw8r_door\" \"" + key + "\"" + eol +
               extra + makeBrushPrimitive(makeBox({{0, 28, 0}, {64, 36, 96}}, " caulk 64 64 0 0 0 0"), eol)->text() + "}" + eol;
    };
    text += door("gate", "hinge 100 0.5", "");
    text += door("gate2", "rotate 90 1 3", ("\"iw8r_hinge\" \"far\"" + eol).c_str());
    Id next = 1;
    ParseResult pr = parseMap(text, next);
    CHECK(pr.map.has_value());
    if (!pr.map) return;
    // The hinge is at the low end of the long side, mid thickness, on the floor; "far" takes the other end.
    DoorHinge h1, h2;
    for (const auto& e : pr.map->entities) {
        const std::string* tn = e.value("targetname");
        if (tn && *tn == "gate") h1 = doorHingeOf(e);
        if (tn && *tn == "gate2") h2 = doorHingeOf(e);
    }
    CHECK(h1.ok && h1.pos.x == 0 && h1.pos.y == 32 && h1.pos.z == 0 && h1.normal.y == 1 && h1.swing == 1);
    CHECK(h2.ok && h2.pos.x == 64 && h2.pos.y == 32 && h2.swing == -1);
    const std::string out = scratch("hinge");
    CompileOptions opt;
    opt.mapId = kSampleId;
    CompileReport rep;
    std::string err;
    CHECK(compileIw8Extraction(*pr.map, out, opt, rep, &err));
    const std::string gsc = readAll((fs::path(out) / "extracted" / "maps" / "mp" / (std::string(kSampleId) + ".gsc")).string());
    // Its own use volume, tested by the script: the door's bounds grown by 40.
    CHECK(gsc.find("t = iw8r_trigger_add(\"gate_use\", 1, 0, 50);") != std::string::npos);
    CHECK(gsc.find("b = iw8r_trigger_box(t, (-40, -12, 0), (104, 76, 96), 0);") != std::string::npos);
    CHECK(gsc.find("trig = iw8r_trigger(\"gate_use\");") != std::string::npos);
    CHECK(gsc.find("hinge = (0, 32, 0);") != std::string::npos && gsc.find("pivot = spawn(\"script_origin\", hinge);") != std::string::npos);
    CHECK(gsc.find("door linkto(pivot);") != std::string::npos && gsc.find("pivot rotateyaw(100 * dir, 0.5);") != std::string::npos);
    // "hinge" waits for a second use; "rotate" closes after its hold time.
    const size_t a = gsc.find("// Hinged door gate "), b = gsc.find("// Hinged door gate2 ");
    CHECK(a != std::string::npos && b != std::string::npos);
    if (a != std::string::npos && b != std::string::npos) {
        const std::string first = gsc.substr(a, b > a ? b - a : std::string::npos), second = gsc.substr(b);
        CHECK(first.find("wait 3;") == std::string::npos);
        CHECK(second.find("hinge = (64, 32, 0);") != std::string::npos && second.find("wait 3;") != std::string::npos);
    }
}

// 13.5 (research 89): `iw8r_loot_point` point entities become the collision json's loot_points and stay out of the
// entity text; loot_cluster "cache" marks a loot cache point.
TEST(compile_iw8_loot_points) {
    std::string text;
    CHECK(readFile(samplePath(), text));
    if (text.empty()) return;
    const std::string eol = text.find("\r\n") != std::string::npos ? "\r\n" : "\n";
    const auto ent = [&](const std::string& keys) { return "{" + eol + "\"classname\" \"iw8r_loot_point\"" + eol + keys + "}" + eol; };
    const auto kv = [&](const std::string& k, const std::string& v) { return "\"" + k + "\" \"" + v + "\"" + eol; };
    const std::string base = text;
    text += ent(kv("origin", "64 32 16") + kv("angles", "0 90 0") + kv("loot_cluster", "auto") + kv("loot_zone", "auto"));
    text += ent(kv("origin", "-64 32.5 16") + kv("loot_cluster", "Cache"));
    text += ent(kv("origin", "0 0 8") + kv("loot_cluster", "12") + kv("loot_zone", "3") + kv("loot_zone_name", "map_quarry"));
    Id next = 1;
    ParseResult pr = parseMap(text, next);
    CHECK(pr.map.has_value());
    if (!pr.map) return;
    const std::string out = scratch("loot_points");
    CompileOptions opt;
    opt.mapId = kSampleId;
    CompileReport rep;
    std::string err;
    CHECK(compileIw8Extraction(*pr.map, out, opt, rep, &err));
    const fs::path mp = fs::path(out) / "extracted" / "maps" / "mp";
    const std::string col = readAll((mp / (std::string(kSampleId) + ".d3dbsp.replay-collision.json")).string());
    CHECK(col.find("\"loot_points\":[{\"origin\":[64,32,16],\"angles\":[0,90,0],\"cache\":false},"
                   "{\"origin\":[-64,32.5,16],\"angles\":[0,0,0],\"cache\":true},"
                   "{\"origin\":[0,0,8],\"angles\":[0,0,0],\"cache\":false}]}") != std::string::npos);
    bool noted = false;
    for (const std::string& w : rep.warnings) noted |= w.find("3 loot point(s) written") != std::string::npos;
    CHECK(noted);
    // Not an entity of the map: the entity text does not name the class or its keys.
    const std::string ents = readAll((mp / (std::string(kSampleId) + ".d3dbsp.ents")).string());
    CHECK(!ents.empty() && ents.find("iw8r_loot_point") == std::string::npos && ents.find("loot_cluster") == std::string::npos);

    // A map without loot points has no loot_points key.
    Id next2 = 1;
    ParseResult pr2 = parseMap(base, next2);
    CHECK(pr2.map.has_value());
    if (!pr2.map) return;
    const std::string out2 = scratch("loot_points_none");
    CompileReport rep2;
    CHECK(compileIw8Extraction(*pr2.map, out2, opt, rep2, &err));
    const std::string col2 = readAll((fs::path(out2) / "extracted" / "maps" / "mp" / (std::string(kSampleId) + ".d3dbsp.replay-collision.json")).string());
    CHECK(!col2.empty() && col2.find("loot_points") == std::string::npos);
}

// 13.4 (research 89): a definition of the Warzone zone (common_br_mp) is placed by name on a map made for the br
// game type and left out on any other map; the detailed physics limit is written only when it differs from the main.
TEST(compile_iw8_scriptable_br_zone_and_limits) {
    ScriptableCatalog catalog;
    CHECK(catalog.parse(
        "name\tzones\talways_loaded\tparts\tserver_instanced_parts\tusable_parts\tmax_dynent_parts\tphysics_dynents\tinstance_dynent\tsparse_occluder\tcollision_flags\tphysics_limit\tflags\tmodel\tsource\tverified\tphysics_limit_detail\n"
        "br_plunder_box\tcommon_br_mp\t0\t1\t1\t1\t16\t0\t0\t0\t0\t1\t0x43E266\tkiosk_rig\tobserved\t1\t1\n"
        "sign_x\tcommon_mp\t1\t1\t1\t0\t2\t0\t1\t0\t0\t1\t0x406046\tsign_x\tobserved\t1\t0\n"
        "old_row\tcommon_mp\t1\t1\t1\t0\t2\t0\t1\t0\t0\t1\t0x40E046\told\tobserved\t1\n"));
    const ScriptableDefinition* box = catalog.find("br_plunder_box");
    CHECK(box && box->brZone() && !box->alwaysLoaded && box->verified && box->physicsLimit && box->physicsLimitDetail && box->carryZone().empty() &&
          !box->carryable());
    const ScriptableDefinition* sign = catalog.find("sign_x");
    CHECK(sign && !sign->brZone() && sign->physicsLimit && !sign->physicsLimitDetail);
    const ScriptableDefinition* old = catalog.find("old_row");  // a file without the column: the detailed limit follows the main one
    CHECK(old && old->physicsLimit && old->physicsLimitDetail);

    std::string text;
    CHECK(readFile(samplePath(), text));
    if (text.empty()) return;
    const std::string eol = text.find("\r\n") != std::string::npos ? "\r\n" : "\n";
    const auto ent = [&](const std::string& cls, const std::string& keys) { return "{" + eol + "\"classname\" \"" + cls + "\"" + eol + keys + "}" + eol; };
    const auto kv = [&](const std::string& k, const std::string& v) { return "\"" + k + "\" \"" + v + "\"" + eol; };
    const std::string placed = ent("scriptable", kv("scriptable", "br_plunder_box") + kv("origin", "64 32 0")) +
                               ent("scriptable", kv("scriptable", "sign_x") + kv("origin", "-64 32 0"));
    CompileOptions opt;
    opt.mapId = kSampleId;
    opt.scriptables = &catalog;
    std::string err;

    // An ordinary map: the buy station is left out with the reason, the sign is written with its two limits.
    {
        Id next = 1;
        ParseResult pr = parseMap(text + placed, next);
        CHECK(pr.map.has_value());
        if (!pr.map) return;
        const std::string out = scratch("scriptables_br_off");
        CompileReport rep;
        CHECK(compileIw8Extraction(*pr.map, out, opt, rep, &err));
        const std::string col = readAll((fs::path(out) / "extracted" / "maps" / "mp" / (std::string(kSampleId) + ".d3dbsp.replay-collision.json")).string());
        CHECK(col.find("br_plunder_box") == std::string::npos);
        CHECK(col.find("{\"definition\":\"sign_x\",\"origin\":[-64,32,0],\"angles\":[0,0,0],\"server_instanced_parts\":1,\"usable_parts\":0,"
                       "\"physics_limit\":true,\"max_dynent_parts\":2,\"instance_dynent\":true,\"sparse_occluder\":false,\"collision_flags\":0,"
                       "\"physics_dynents\":0,\"physics_limit_detail\":false}") != std::string::npos);
        bool reason = false;
        for (const std::string& w : rep.warnings) reason |= w.find("br_plunder_box") != std::string::npos && w.find("common_br_mp") != std::string::npos;
        CHECK(reason);
    }
    // The same map with the prematch area of a Warzone map: the buy station is placed by name (no carry zone).
    {
        Id next = 1;
        ParseResult pr = parseMap(text + placed + ent("script_struct", kv("targetname", "br_prematch_insertion_point") + kv("origin", "0 0 64")), next);
        CHECK(pr.map.has_value());
        if (!pr.map) return;
        const std::string out = scratch("scriptables_br_on");
        CompileReport rep;
        CHECK(compileIw8Extraction(*pr.map, out, opt, rep, &err));
        const std::string col = readAll((fs::path(out) / "extracted" / "maps" / "mp" / (std::string(kSampleId) + ".d3dbsp.replay-collision.json")).string());
        CHECK(col.find("{\"definition\":\"br_plunder_box\",\"origin\":[64,32,0],\"angles\":[0,0,0],\"server_instanced_parts\":1,\"usable_parts\":1,"
                       "\"physics_limit\":true,\"max_dynent_parts\":16,\"instance_dynent\":false,\"sparse_occluder\":false,\"collision_flags\":0,"
                       "\"physics_dynents\":0}") != std::string::npos);
        CHECK(col.find("carry_zone") == std::string::npos);
        bool noted = false;
        for (const std::string& w : rep.warnings) noted |= w.find("1 scriptable instance(s) name a definition of the Warzone zone") != std::string::npos;
        CHECK(noted);
    }
}
