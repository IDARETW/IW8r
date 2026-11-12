// IW8 extraction compiler: reflection_probe entities, misc_model static models and the worldspawn sky key (doc 29).
#include <algorithm>
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

constexpr const char* kId = "mp_world_t";

// Scratch folder under the test temp dir, emptied first.
std::string scratch(const std::string& name) {
    const fs::path p = fs::path(test::tmpDir()) / "compile_world" / name;
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

// Two floor boxes, far apart and in different materials, plus the spawns the converter needs.
std::string worldText(const std::string& worldspawnExtra, const std::string& entities) {
    std::string t = "iwmap 4\n{\n\"classname\" \"worldspawn\"\n" + worldspawnExtra;
    t += boxBrush({{0, 0, -16}, {256, 256, 0}}, "world/a");
    t += boxBrush({{900, 0, -16}, {1156, 256, 0}}, "world/b");
    t += "}\n";
    t += "{\n\"classname\" \"mp_tdm_spawn\"\n\"origin\" \"64 128 16\"\n\"angles\" \"0 0 0\"\n}\n";
    t += "{\n\"classname\" \"mp_tdm_spawn\"\n\"origin\" \"1000 128 16\"\n\"angles\" \"0 180 0\"\n}\n";
    t += entities;
    return t;
}

const char* kProbeA = "{\n\"classname\" \"reflection_probe\"\n\"origin\" \"-100 128 64\"\n}\n";
const char* kProbeB = "{\n\"classname\" \"reflection_probe\"\n\"origin\" \"1000 128 64\"\n}\n";

// Compiles map text into a fresh folder. Returns false (and prints why) on failure.
bool compileText(const std::string& text, const std::string& out, CompileReport& rep, const std::vector<std::string>& modelRoots = {}) {
    Id next = 1;
    ParseResult pr = parseMap(text, next);
    if (!pr.map) {
        std::printf("  map parse failed\n");
        return false;
    }
    CompileOptions opt;
    opt.mapId = kId;
    opt.modelRoots = modelRoots;
    std::string err;
    const bool ok = compileIw8Extraction(*pr.map, out, opt, rep, &err);
    if (!ok) std::printf("  compile error: %s\n", err.c_str());
    return ok;
}

bool hasWarning(const CompileReport& rep, const std::string& needle) {
    for (const auto& w : rep.warnings)
        if (w.find(needle) != std::string::npos) return true;
    return false;
}

fs::path extracted(const std::string& out) { return fs::path(out) / "extracted"; }
fs::path worldPath(const std::string& out) { return extracted(out) / "maps" / "mp" / (std::string(kId) + ".d3dbsp.replay-world.json"); }

// Centre of a surface's vertex bounds, read back from world.json.
bool surfaceCentre(const json::Value& surf, double c[3]) {
    const json::Value* verts = surf.get("vertices");
    if (!verts || verts->arr.empty()) return false;
    double lo[3] = {1e30, 1e30, 1e30}, hi[3] = {-1e30, -1e30, -1e30};
    for (const auto& v : verts->arr) {
        const json::Value* p = v.get("position");
        if (!p || p->arr.size() != 3) return false;
        for (int k = 0; k < 3; ++k) {
            const double x = p->arr[k].number().value_or(0.0);
            lo[k] = std::min(lo[k], x);
            hi[k] = std::max(hi[k], x);
        }
    }
    for (int k = 0; k < 3; ++k) c[k] = 0.5 * (lo[k] + hi[k]);
    return true;
}

double d2(const double a[3], const double b[3]) {
    return (a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]) + (a[2] - b[2]) * (a[2] - b[2]);
}

}  // namespace

// Two reflection_probe entities: two probes in map order, one DDS each, every surface on its nearest probe.
TEST(compile_world_two_reflection_probes_nearest_assignment) {
    const std::string out = scratch("probes");
    CompileReport rep;
    if (!compileText(worldText("", std::string(kProbeA) + kProbeB), out, rep)) {
        CHECK(false);
        return;
    }
    CHECK(rep.reflectionProbes == 3);  // default probe 0 + two entity probes (x64-zt drops source probe 0)
    CHECK(fs::exists(extracted(out) / "images" / (std::string(kId) + "_probe.dds")));
    CHECK(fs::exists(extracted(out) / "images" / (std::string(kId) + "_probe1.dds")));
    CHECK(fs::exists(extracted(out) / "images" / (std::string(kId) + "_probe2.dds")));

    bool ok = false;
    const json::Value world = loadJson(worldPath(out), ok);
    CHECK(ok);
    if (!ok) return;
    const json::Value* probes = world.get("reflection_probes");
    CHECK(probes && probes->arr.size() == 3);
    if (!probes || probes->arr.size() != 3) return;
    CHECK(probes->arr[0].get("image")->string() && *probes->arr[0].get("image")->string() == std::string(kId) + "_probe");
    CHECK(probes->arr[1].get("image")->string() && *probes->arr[1].get("image")->string() == std::string(kId) + "_probe1");
    CHECK(probes->arr[2].get("image")->string() && *probes->arr[2].get("image")->string() == std::string(kId) + "_probe2");
    const json::Value* o0 = probes->arr[1].get("origin");
    const json::Value* o1 = probes->arr[2].get("origin");
    CHECK(o0 && o0->arr.size() == 3 && o0->arr[0].number() == -100.0 && o0->arr[2].number() == 64.0);
    CHECK(o1 && o1->arr.size() == 3 && o1->arr[0].number() == 1000.0);

    const double origins[2][3] = {{-100, 128, 64}, {1000, 128, 64}};
    const json::Value* surfs = world.get("surfaces");
    CHECK(surfs && !surfs->arr.empty());
    bool seen[2] = {false, false};
    bool allNearest = true;
    if (surfs) {
        for (const auto& s : surfs->arr) {
            double c[3];
            const json::Value* rp = s.get("reflection_probe");
            if (!rp || !surfaceCentre(s, c)) {
                allNearest = false;
                continue;
            }
            const size_t got = size_t(rp->number().value_or(99)) - 1;  // entity probes are 1 and 2
            if (got > 1) {
                allNearest = false;
                continue;
            }
            seen[got] = true;
            const size_t want = d2(c, origins[0]) <= d2(c, origins[1]) ? 0 : 1;
            allNearest &= got == want;
        }
    }
    CHECK(allNearest);
    CHECK(seen[0] && seen[1]);  // the west box takes probe 1, the east box probe 2

    const json::Value* cells = world.get("dpvs") ? world.get("dpvs")->get("cells") : nullptr;
    CHECK(cells && cells->arr.size() == 1);
    if (cells && cells->arr.size() == 1) {
        const json::Value* cp = cells->arr[0].get("reflection_probes");
        CHECK(cp && cp->arr.size() == 3 && cp->arr[0].number() == 0.0 && cp->arr[2].number() == 2.0);
    }

    const std::string ents = readText(extracted(out) / "maps" / "mp" / (std::string(kId) + ".d3dbsp.ents"));
    CHECK(ents.find("reflection_probe") == std::string::npos);
}

// No reflection_probe entity: the default probe is the only one, as before (doc 15 §7).
TEST(compile_world_default_probe_without_entities) {
    const std::string out = scratch("default_probe");
    CompileReport rep;
    CHECK(compileText(worldText("", ""), out, rep));
    CHECK(rep.reflectionProbes == 1);
    CHECK(fs::exists(extracted(out) / "images" / (std::string(kId) + "_probe.dds")));
    bool ok = false;
    const json::Value world = loadJson(worldPath(out), ok);
    const json::Value* probes = ok ? world.get("reflection_probes") : nullptr;
    CHECK(probes && probes->arr.size() == 1);
    if (probes && probes->arr.size() == 1)
        CHECK(probes->arr[0].get("image")->string() && *probes->arr[0].get("image")->string() == std::string(kId) + "_probe");
}

// A custom misc_model (OBJ in a model folder) becomes one models[] placement; angles 0 90 0 give the axes
// forward (0,1,0), left (-1,0,0), up (0,0,1), as x64-zt AnglesToAxis does. modelscale sets the placement scale.
// A stock misc_model name is skipped with a warning.
TEST(compile_world_custom_misc_model_placement) {
    const std::string out = scratch("misc");
    const fs::path models = fs::path(test::tmpDir()) / "compile_world" / "misc_models";
    writeText(models / "t_wedge.obj", "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");
    const std::string entities =
        "{\n\"classname\" \"misc_model\"\n\"model\" \"t_wedge\"\n\"origin\" \"256 128 0\"\n\"angles\" \"0 90 0\"\n"
        "\"modelscale\" \"2\"\n}\n" +
        std::string("{\n\"classname\" \"misc_model\"\n\"model\" \"mp_crate_stock\"\n\"origin\" \"0 0 0\"\n}\n");
    CompileReport rep;
    if (!compileText(worldText("", entities), out, rep, {models.string()})) {
        CHECK(false);
        return;
    }
    CHECK(rep.staticModels == 1);
    CHECK(rep.customModels == 1);
    CHECK(hasWarning(rep, "mp_crate_stock"));
    CHECK(fs::exists(extracted(out) / "xmodel" / "t_wedge.json"));

    bool ok = false;
    const json::Value world = loadJson(worldPath(out), ok);
    const json::Value* models_ = ok ? world.get("models") : nullptr;
    CHECK(models_ && models_->arr.size() == 1);
    if (!models_ || models_->arr.size() != 1) return;
    const json::Value& m = models_->arr[0];
    CHECK(m.get("model")->string() && *m.get("model")->string() == "t_wedge");
    const json::Value* org = m.get("origin");
    CHECK(org && org->arr.size() == 3 && org->arr[0].number() == 256.0 && org->arr[1].number() == 128.0);
    CHECK(m.get("scale") && m.get("scale")->number() == 2.0);
    CHECK(m.get("reflection_probe") && m.get("reflection_probe")->number() == 0.0);
    const json::Value* axis = m.get("axis");
    const double want[3][3] = {{0, 1, 0}, {-1, 0, 0}, {0, 0, 1}};
    bool axisOk = axis && axis->arr.size() == 3;
    for (size_t r = 0; axisOk && r < 3; ++r) {
        axisOk = axis->arr[r].arr.size() == 3;
        for (size_t c = 0; axisOk && c < 3; ++c)
            axisOk = std::fabs(axis->arr[r].arr[c].number().value_or(99.0) - want[r][c]) < 1e-6;
    }
    CHECK(axisOk);

    // The model never goes to .ents (x64-zt projects misc_model away, doc 15 §3.1).
    const std::string ents = readText(extracted(out) / "maps" / "mp" / (std::string(kId) + ".d3dbsp.ents"));
    CHECK(ents.find("misc_model") == std::string::npos);
}

// The worldspawn sky name is not written: the converter needs images/<name>.dds for a sky, so world.sky stays "".
TEST(compile_world_sky_key_stays_empty_without_files) {
    {
        const std::string out = scratch("sky_named");
        CompileReport rep;
        CHECK(compileText(worldText("\"skyname\" \"sky_mp_test\"\n", ""), out, rep));
        CHECK(hasWarning(rep, "sky_mp_test"));
        bool ok = false;
        const json::Value world = loadJson(worldPath(out), ok);
        CHECK(ok && world.get("sky") && world.get("sky")->string() && world.get("sky")->string()->empty());
    }
    {
        const std::string out = scratch("sky_none");
        CompileReport rep;
        CHECK(compileText(worldText("", ""), out, rep));
        CHECK(!hasWarning(rep, "sky"));
        bool ok = false;
        const json::Value world = loadJson(worldPath(out), ok);
        CHECK(ok && world.get("sky") && world.get("sky")->string() && world.get("sky")->string()->empty());
    }
}

// Probe volumes (5.5): reflection_probe mins/maxs (relative to the origin) become world.json probe "volume".
TEST(compile_world_probe_authored_volume) {
    const char* text =
        "iwmap 4\n{\n\"classname\" \"worldspawn\"\n{\n"
        " ( 256 256 -16 ) ( -256 256 -16 ) ( -256 -256 -16 ) tm2co/me_curb_stone_01 128 128 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0\n"
        " ( -256 -256 0 ) ( -256 256 0 ) ( 256 256 0 ) tm2co/me_curb_stone_01 128 128 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0\n"
        " ( -256 -256 0 ) ( 256 -256 0 ) ( 256 -256 -16 ) tm2co/me_curb_stone_01 128 128 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0\n"
        " ( 256 256 0 ) ( -256 256 0 ) ( -256 256 -16 ) tm2co/me_curb_stone_01 128 128 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0\n"
        " ( 256 -256 0 ) ( 256 256 0 ) ( 256 256 -16 ) tm2co/me_curb_stone_01 128 128 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0\n"
        " ( -256 256 0 ) ( -256 -256 0 ) ( -256 -256 -16 ) tm2co/me_curb_stone_01 128 128 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0\n"
        "}\n}\n"
        "{\n\"classname\" \"reflection_probe\"\n\"origin\" \"0 0 64\"\n\"mins\" \"-128 -64 -64\"\n\"maxs\" \"128 64 96\"\n}\n"
        "{\n\"classname\" \"reflection_probe\"\n\"origin\" \"100 0 64\"\n}\n"
        "{\n\"classname\" \"mp_tdm_spawn\"\n\"origin\" \"0 0 16\"\n}\n";
    Id next = 1;
    ParseResult pr = parseMap(text, next);
    CHECK(pr.map.has_value());
    if (!pr.map) return;
    const fs::path out = fs::path(iw8r::test::tmpDir()) / "probe_volume";
    std::error_code ec;
    fs::remove_all(out, ec);
    CompileOptions opt;
    opt.mapId = "mp_probevol";
    CompileReport rep;
    std::string err;
    CHECK(compileIw8Extraction(*pr.map, out.string(), opt, rep, &err));
    std::string world;
    CHECK(readFile((out / "extracted" / "maps" / "mp" / "mp_probevol.d3dbsp.replay-world.json").string(), world));
    CHECK(world.find("\"origin\":[0,0,64],\"volume\":[[-128,-64,0],[128,64,160]]") != std::string::npos);
    const size_t second = world.find("\"origin\":[100,0,64]");
    CHECK(second != std::string::npos && world.compare(second + 20, 9, ",\"volume\"") != 0);  // no keys: no volume
}

// Probe blend (5.5): priority and feather keys reach world.json; feather is written as 1 / distance per axis.
TEST(compile_world_probe_priority_feather) {
    const std::string out = scratch("probeblend");
    CompileReport rep;
    const std::string probe = "{\n\"classname\" \"reflection_probe\"\n\"origin\" \"128 128 64\"\n\"mins\" \"-64 -64 -32\"\n"
                              "\"maxs\" \"64 64 96\"\n\"priority\" \"20\"\n\"feather\" \"32 32 8\"\n}\n";
    const std::string bad = "{\n\"classname\" \"reflection_probe\"\n\"origin\" \"1000 128 64\"\n\"feather\" \"-1\"\n}\n";
    if (!compileText(worldText("", probe + bad), out, rep)) {
        CHECK(false);
        return;
    }
    bool ok = false;
    const json::Value world = loadJson(worldPath(out), ok);
    CHECK(ok);
    if (!ok) return;
    const json::Value* probes = world.get("reflection_probes");
    CHECK(probes && probes->arr.size() == 3);
    if (!probes || probes->arr.size() != 3) return;
    const json::Value* pr = probes->arr[1].get("priority");
    const json::Value* fe = probes->arr[1].get("feather");
    CHECK(pr && pr->number() == 20.0);
    CHECK(fe && fe->arr.size() == 3 && fe->arr[0].number() == 1.0 / 32 && fe->arr[2].number() == 1.0 / 8);
    CHECK(!probes->arr[2].get("feather") && !probes->arr[2].get("priority"));
    CHECK(hasWarning(rep, "feather '-1' needs one or three positive distances"));
}

// Prefabs are expanded at build time (misc_prefab is editor-only): brushes become world geometry (func_group), entities
// are kept, prefabs inside prefabs too.
TEST(compile_world_prefabs_expanded) {
    const std::string out = scratch("prefabbuild");
    const fs::path dir = fs::path(out).parent_path() / "prefabbuild_src";
    fs::create_directories(dir / "prefabs");
    auto write = [](const fs::path& p, const std::string& t) {
        std::ofstream f(p, std::ios::binary);
        f << t;
    };
    // lamp.map: a pole brush and a light; post.map: a base brush plus a nested lamp prefab.
    write(dir / "prefabs" / "lamp.map", "iwmap 4\n{\n\"classname\" \"worldspawn\"\n" + boxBrush({{-4, -4, 0}, {4, 4, 128}}, "world/a") +
                                             "}\n{\n\"classname\" \"light\"\n\"origin\" \"0 0 120\"\n}\n");
    write(dir / "prefabs" / "post.map", "iwmap 4\n{\n\"classname\" \"worldspawn\"\n" + boxBrush({{-16, -16, 0}, {16, 16, 16}}, "world/b") +
                                             "}\n{\n\"classname\" \"misc_prefab\"\n\"model\" \"lamp.map\"\n\"origin\" \"0 0 16\"\n}\n");
    const std::string text = worldText("", "{\n\"classname\" \"misc_prefab\"\n\"model\" \"prefabs/post.map\"\n\"origin\" \"128 128 0\"\n}\n");
    Id next = 1;
    ParseResult pr = parseMap(text, next);
    CHECK(pr.map.has_value());
    if (!pr.map) return;
    CompileOptions opt;
    opt.mapId = kId;
    opt.mapDir = dir.string();
    CompileReport rep;
    std::string err;
    CHECK(compileIw8Extraction(*pr.map, out, opt, rep, &err));
    CHECK(hasWarning(rep, "1 prefab(s) expanded into the build"));
    CHECK(!hasWarning(rep, "misc_prefab"));
    bool ok = false;
    const json::Value world = loadJson(worldPath(out), ok);
    CHECK(ok);
    if (!ok) return;
    // The nested lamp pole reaches z = 16 + 128 = 144 at x 124..132: some surface vertex must be there.
    bool pole = false;
    if (const json::Value* surfs = world.get("surfaces"))
        for (const auto& s : surfs->arr)
            if (const json::Value* verts = s.get("vertices"))
                for (const auto& v : verts->arr)
                    if (const json::Value* p = v.get("position"); p && p->arr.size() == 3 && p->arr[2].number().value_or(0) > 143.9 &&
                                                                  p->arr[0].number().value_or(0) > 123.9 && p->arr[0].number().value_or(0) < 132.1)
                        pole = true;
    CHECK(pole);
    // The nested light is a primary light.
    std::string comworld;
    readFile((extracted(out) / "maps" / "mp" / (std::string(kId) + ".d3dbsp.replay-comworld.json")).string(), comworld);
    CHECK(comworld.find("[128,128,136]") != std::string::npos);
}

// 5.22 (research 54): misc_model lod_scale and no_shadow reach the placement; a value off the LOD table warns.
TEST(compile_world_misc_model_lod_scale_and_no_shadow) {
    const std::string out = scratch("smflags");
    const fs::path models = fs::path(test::tmpDir()) / "compile_world" / "misc_models";
    writeText(models / "t_wedge.obj", "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");
    const std::string entities =
        "{\n\"classname\" \"misc_model\"\n\"model\" \"t_wedge\"\n\"origin\" \"0 0 0\"\n\"lod_scale\" \"2\"\n\"no_shadow\" \"1\"\n}\n"
        "{\n\"classname\" \"misc_model\"\n\"model\" \"t_wedge\"\n\"origin\" \"64 0 0\"\n\"lod_scale\" \"3.3\"\n}\n";
    CompileReport rep;
    if (!compileText(worldText("", entities), out, rep, {models.string()})) {
        CHECK(false);
        return;
    }
    bool ok = false;
    const json::Value world = loadJson(worldPath(out), ok);
    const json::Value* models_ = ok ? world.get("models") : nullptr;
    CHECK(models_ && models_->arr.size() == 2);
    if (!models_ || models_->arr.size() != 2) return;
    const json::Value& a = models_->arr[0];
    CHECK(a.get("lod_scale") && a.get("lod_scale")->number() == 2.0);
    CHECK(a.get("shadow_lights") && a.get("shadow_lights")->arr.empty());
    const json::Value& b = models_->arr[1];
    CHECK(!b.get("lod_scale") && !b.get("shadow_lights"));  // 3.3 is not a table value: left at 1 (omitted)
    CHECK(hasWarning(rep, "lod_scale"));
}

// 4.8 (research 54): misc_model ground_lighting writes the converter's lighting block (mins / maxs, model_flags 1,
// the packed colour); other placements keep the grid.
TEST(compile_world_misc_model_ground_lighting) {
    const std::string out = scratch("groundlit");
    const fs::path models = fs::path(test::tmpDir()) / "compile_world" / "misc_models_gl";
    writeText(models / "t_wedge.obj", "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");
    const std::string entities =
        "{\n\"classname\" \"misc_model\"\n\"model\" \"t_wedge\"\n\"origin\" \"8 0 0\"\n\"ground_lighting\" \"1 0.5 0\"\n}\n"
        "{\n\"classname\" \"misc_model\"\n\"model\" \"t_wedge\"\n\"origin\" \"64 0 0\"\n}\n";
    CompileReport rep;
    if (!compileText(worldText("", entities), out, rep, {models.string()})) {
        CHECK(false);
        return;
    }
    bool ok = false;
    const json::Value world = loadJson(worldPath(out), ok);
    const json::Value* models_ = ok ? world.get("models") : nullptr;
    CHECK(models_ && models_->arr.size() == 2);
    if (!models_ || models_->arr.size() != 2) return;
    const json::Value& a = models_->arr[0];
    CHECK(a.get("model_flags") && a.get("model_flags")->number() == 1);
    CHECK(a.get("ground_lighting") && a.get("ground_lighting")->number() == double(0xFF0080FFu));
    CHECK(a.get("mins") && a.get("maxs"));
    const json::Value& b = models_->arr[1];
    CHECK(!b.get("model_flags") && !b.get("mins"));
}

// 8.11 (research 57): a painted patch writes per-vertex colours into the world JSON (the converter reads `color`);
// an unpainted patch writes none.
TEST(compile_world_patch_vertex_colours) {
    auto patch = [](const char* c00) {
        return std::string(" {\n mesh\n {\n  world/a\n  lightmap_gray\n  2 2 16 8\n  (\n\tv 300 0 0 ") + c00 +
               "t 0 0 0 0\n\tv 300 0 64 t 0 -64 0 4\n  )\n  (\n\tv 364 0 0 t 64 0 4 0\n\tv 364 0 64 t 64 -64 4 4\n  )\n }\n }\n";
    };
    for (int painted = 0; painted < 2; ++painted) {
        const std::string out = scratch(painted ? "vcolour" : "vcolour_none");
        CompileReport rep;
        if (!compileText(worldText(patch(painted ? "c 255 0 0 255 " : ""), ""), out, rep)) {
            CHECK(false);
            return;
        }
        const std::string text = readText(worldPath(out));
        if (painted) {
            CHECK(text.find("\"color\":[255,0,0,255]") != std::string::npos);  // the painted corner
            CHECK(text.find("\"color\":[255,255,255,255]") != std::string::npos);  // the far corner stays white
        } else {
            CHECK(text.find("\"lightmap_uv\":[0,0],\"color\"") == std::string::npos);
        }
    }
}

// Patch texture coordinates reach the build: file st / 1024 (patch_ops kPatchStUnits), tessellated like the positions.
TEST(compile_world_patch_uses_its_st) {
    const std::string patch = " {\n mesh\n {\n  world/a\n  lightmap_gray\n  2 2 16 8\n  (\n\tv 300 0 0 t 0 0 0 0\n"
                              "\tv 300 0 64 t 0 -64 0 4\n  )\n  (\n\tv 364 0 0 t 64 0 4 0\n\tv 364 0 64 t 64 -64 4 4\n  )\n }\n }\n";
    const std::string out = scratch("patch_st");
    CompileReport rep;
    if (!compileText(worldText(patch, ""), out, rep)) {
        CHECK(false);
        return;
    }
    const std::string text = readText(worldPath(out));
    CHECK(text.find("\"uv\":[0.0625,0]") != std::string::npos);       // t 64 0 at (364 0 0)
    CHECK(text.find("\"uv\":[0.0625,-0.0625]") != std::string::npos);  // t 64 -64 at (364 0 64)
}
