// Full map capability pass regression tests: patch/terrain collision, per-face brush materials,
// nav blockers, extended local-light keys, custom sound pools, audio zones (stock aliases, slabs,
// blends, PA speakers), prefab behaviour transforms, auto-caulk transparency, FX sprite atlases,
// light-grid local light illumination, Hardpoint detection, Play mode parity, and patch decals.
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "audio_zones.h"
#include "auto_caulk.h"
#include "brush_ops.h"
#include "compile_iw8.h"
#include "custom_sounds.h"
#include "fx_author.h"
#include "gamemode_templates.h"
#include "iw8_contents.h"
#include "json_lite.h"
#include "light_grid.h"
#include "map_gametypes.h"
#include "map_text.h"
#include "own_decals.h"
#include "play_behaviour.h"
#include "prefab.h"
#include "ray_bvh.h"
#include "script_check.h"
#include "test_harness.h"

using namespace iw8r;
namespace fs = std::filesystem;

namespace {

MapFile parseOrFail(const std::string& text) {
    Id next = 1;
    const ParseResult pr = parseMap(text, next);
    CHECK(pr.map.has_value());
    return pr.map ? *pr.map : MapFile{};
}

std::string boxBrushText(const AABB& b, const std::string& mat) {
    auto p = makeBrushPrimitive(makeBox(b, " " + mat + " 128 128 0 0 0 0"), "\n");
    return p ? p->text() : std::string();
}

std::string readTextFile(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

}  // namespace

TEST(capability_compile_patch_collision_per_face_materials_nav_blockers_and_lights) {
    // One solid floor brush with mixed face materials (concrete + wood top), one Bezier patch,
    // one clip_nav blocker, and one tube/shadow-tuned spot light.
    std::string mapText =
        "iwmap 4\n"
        "{\n"
        "\"classname\" \"worldspawn\"\n"
        "{\n"
        " ( -256 -256 -32 ) ( -256 256 -32 ) ( -256 -256 0 ) concrete_wall 128 128 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0\n"
        " ( 256 -256 -32 ) ( 256 -256 0 ) ( 256 256 -32 ) concrete_wall 128 128 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0\n"
        " ( -256 -256 -32 ) ( -256 -256 0 ) ( 256 -256 -32 ) concrete_wall 128 128 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0\n"
        " ( -256 256 -32 ) ( 256 256 -32 ) ( -256 256 0 ) concrete_wall 128 128 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0\n"
        " ( -256 -256 -32 ) ( 256 -256 -32 ) ( -256 256 -32 ) concrete_wall 128 128 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0\n"
        " ( -256 -256 0 ) ( -256 256 0 ) ( 256 -256 0 ) wood_plank 128 128 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0\n"
        "}\n" +
        boxBrushText({{-64, -64, 0}, {64, 64, 64}}, "clip_nav") +
        "{\n"
        " mesh\n"
        " {\n"
        "  grass_ground\n"
        "  lightmap_gray\n"
        "  3 3 16 8\n"
        "  (\n"
        "   v -128 -128 16 t 0 0 0 0\n"
        "   v -128 0 16 t 0 64 0 0\n"
        "   v -128 128 16 t 0 128 0 0\n"
        "  )\n"
        "  (\n"
        "   v 0 -128 16 t 64 0 0 0\n"
        "   v 0 0 24 t 64 64 0 0\n"
        "   v 0 128 16 t 64 128 0 0\n"
        "  )\n"
        "  (\n"
        "   v 128 -128 16 t 128 0 0 0\n"
        "   v 128 0 16 t 128 64 0 0\n"
        "   v 128 128 16 t 128 128 0 0\n"
        "  )\n"
        " }\n"
        "}\n"
        "}\n"
        "{\n"
        "\"classname\" \"light_spot\"\n"
        "\"origin\" \"0 0 128\"\n"
        "\"angles\" \"90 0 0\"\n"
        "\"radius\" \"320\"\n"
        "\"iw8r_bulb_length\" \"24 0 0\"\n"
        "\"iw8r_shadows\" \"1\"\n"
        "\"iw8r_dynamic_shadows\" \"1\"\n"
        "\"iw8r_shadow_softness\" \"0.75\"\n"
        "\"iw8r_shadow_area\" \"0.003\"\n"
        "\"iw8r_shadow_near_bias\" \"6\"\n"
        "\"iw8r_up\" \"0 1 0\"\n"
        "}\n";

    const MapFile map = parseOrFail(mapText);
    const fs::path outDir = fs::temp_directory_path() / "iw8r_cap_test_compile";
    std::error_code ec;
    fs::remove_all(outDir, ec);

    CompileOptions opt;
    opt.mapId = "mp_captest";
    CompileReport rep;
    std::string cerr;
    CHECK(compileIw8Extraction(map, outDir.string(), opt, rep, &cerr));

    // Verify collision JSON carries patch collision mesh, per-face materials, and nav_blockers.
    const std::string colText = readTextFile(outDir / "extracted" / "maps" / "mp" / "mp_captest.d3dbsp.replay-collision.json");
    std::string jerr;
    const auto col = json::parse(colText, &jerr);
    CHECK(col.has_value());
    if (col) {
        const json::Value* verts = col->get("vertices");
        const json::Value* tris = col->get("triangles");
        const json::Value* tcont = col->get("triangle_contents");
        const json::Value* tsurf = col->get("triangle_surfaces");
        CHECK(verts && verts->arr.size() >= 9);
        CHECK(tris && !tris->arr.empty());
        CHECK(tcont && tcont->arr.size() == tris->arr.size());
        CHECK(tsurf && tsurf->arr.size() == tris->arr.size());
        // Grass surface type in Replay is 10.
        CHECK(tsurf && !tsurf->arr.empty() && *tsurf->arr[0].arr[0].number() == 10.0);

        const json::Value* mats = col->get("materials");
        CHECK(mats && mats->arr.size() == 29);
        const json::Value* brushes = col->get("brushes");
        CHECK(brushes && brushes->arr.size() == 1);
        if (brushes && !brushes->arr.empty()) {
            const json::Value& b0 = brushes->arr[0];
            CHECK(b0.get("planes") && b0.get("planes")->arr.size() == 6);
            CHECK(b0.get("side_materials") && b0.get("side_materials")->arr.size() == 6);
            CHECK(b0.get("axial_materials") && b0.get("axial_materials")->arr.size() == 2);
            // +Z face is wood_plank (IW3 surface type 21), -Z is concrete (5).
            CHECK(*b0.get("axial_materials")->arr[1].arr[2].number() == 21.0);
            CHECK(*b0.get("axial_materials")->arr[0].arr[2].number() == 5.0);
        }

        const json::Value* blockers = col->get("nav_blockers");
        CHECK(blockers && blockers->arr.size() == 1);
    }

    // Verify comworld JSON carries tube light and shadow tuning keys.
    const std::string cwText = readTextFile(outDir / "extracted" / "maps" / "mp" / "mp_captest.d3dbsp.replay-comworld.json");
    const auto cw = json::parse(cwText, &jerr);
    CHECK(cw.has_value());
    if (cw) {
        const json::Value* lights = cw->get("primary_lights");
        CHECK(lights && lights->arr.size() == 3);
        if (lights && lights->arr.size() == 3) {
            const json::Value& l = lights->arr[2];
            CHECK(l.get("bulb_length") && *l.get("bulb_length")->arr[0].number() == 24.0);
            CHECK(l.get("needs_dynamic_shadows") && l.get("needs_dynamic_shadows")->b);
            CHECK(l.get("shadow_softness") && *l.get("shadow_softness")->number() == 0.75);
            CHECK(l.get("shadow_area") && *l.get("shadow_area")->number() == 0.003);
            CHECK(l.get("shadow_near_plane_bias") && *l.get("shadow_near_plane_bias")->number() == 6.0);
            CHECK(l.get("authored_up") && l.get("authored_up")->b);
            CHECK(l.get("up") && *l.get("up")->arr[1].number() == 1.0);
        }
    }

    fs::remove_all(outDir, ec);
}

TEST(capability_custom_sounds_multi_variant_and_audio_zones_blends) {
    CustomSoundAlias a;
    a.name = "custom_footstep";
    a.distMin = 80;
    a.distMax = 800;
    a.variants.push_back({"step_01.wav", 0.8, 1.0, 0.9, 1.1, 0.6, 0.2, 0.1, 15, "step_sub"});
    a.variants.push_back({"step_02.wav", 0.7, 0.9, 0.95, 1.05, 0.4, 0.0, 0.0, 0, ""});
    std::string err;
    const auto parsed = json::parse(customSoundAliasJson(a), &err);
    CHECK(parsed.has_value());
    if (parsed) {
        const json::Value* arr = parsed->get("aliases");
        CHECK(arr && arr->arr.size() == 2);
        CHECK(*arr->arr[0].get("probability")->number() == 0.6);
        CHECK(*arr->arr[0].get("lfe_percentage")->number() == 0.2);
        CHECK(*arr->arr[0].get("center_percentage")->number() == 0.1);
        CHECK(*arr->arr[0].get("start_delay")->number() == 15.0);
        CHECK(arr->arr[0].get("secondary")->str == "step_sub");
    }

    // Audio zones: blend node + PA speaker + exterior flag.
    const std::string mapText =
        "iwmap 4\n"
        "{\n\"classname\" \"worldspawn\"\n\"iw8r_reverb\" \"iw8_ext_open_space_med_1\"\n}\n"
        "{\n\"classname\" \"trigger_multiple\"\n\"iw8r_audio_zone\" \"room_a\"\n\"iw8r_audio_interior\" \"1\"\n" +
        boxBrushText({{0, 0, 0}, {128, 128, 128}}, "trigger") +
        "}\n"
        "{\n\"classname\" \"script_origin\"\n\"origin\" \"0 64 64\"\n\"iw8r_audio_blend\" \"mp_t room_a\"\n"
        "\"iw8r_audio_pa_speaker\" \"pa_lobby\"\n}\n";
    const MapFile map = parseOrFail(mapText);
    const AudioSetup s = readAudioSetup(map, "mp_t", {{-512, -512, -64}, {512, 512, 256}}, nullptr);
    CHECK(s.blendNodes.size() == 1);
    CHECK(s.paSpeakers.size() == 1 && s.paSpeakers[0].name == "pa_lobby");
    const auto trig = json::parse(clientTriggersJson(s), &err);
    CHECK(trig.has_value());
    if (trig) {
        CHECK(trig->get("blend_nodes") && trig->get("blend_nodes")->arr.size() == 1);
        CHECK(trig->get("pa_speakers") && trig->get("pa_speakers")->arr.size() == 1);
    }
}

TEST(capability_prefab_behaviour_key_transforms) {
    const std::string pfText =
        "iwmap 4\n"
        "{\n\"classname\" \"worldspawn\"\n}\n"
        "{\n\"classname\" \"script_brushmodel\"\n\"iw8r_door\" \"slide 100 0 0 1.5 2\"\n\"iw8r_hinge\" \"10 0 0\"\n\"iw8r_lift\" \"64 2 1\"\n" +
        boxBrushText({{0, 0, 0}, {32, 32, 64}}, "concrete_wall") +
        "}\n"
        "{\n\"classname\" \"trigger_radius\"\n\"origin\" \"0 0 0\"\n\"radius\" \"50\"\n\"height\" \"72\"\n\"iw8r_jump\" \"200 0 300\"\n}\n";
    const MapFile pf = parseOrFail(pfText);
    PrefabInstance inst;
    inst.origin = {100, 200, 0};
    inst.angles = {0, 90, 0};  // +X rotates to +Y
    inst.scale = 2.0;
    const PrefabContents pc = instantiatePrefab(pf, inst, "\n");
    CHECK(pc.entities.size() == 2);
    auto findKey = [](const std::vector<std::pair<std::string, std::string>>& kvs, const std::string& k) -> std::string {
        for (const auto& [kk, vv] : kvs)
            if (kk == k) return vv;
        return {};
    };
    const std::string door = findKey(pc.entities[0].first, "iw8r_door");
    const std::string hinge = findKey(pc.entities[0].first, "iw8r_hinge");
    const std::string lift = findKey(pc.entities[0].first, "iw8r_lift");
    double dx = 0, dy = 0, dz = 0, t1 = 0, t2 = 0;
    CHECK(std::sscanf(door.c_str(), "slide %lf %lf %lf %lf %lf", &dx, &dy, &dz, &t1, &t2) == 5);
    CHECK(std::fabs(dx) < 1e-3 && std::fabs(dy - 200.0) < 1e-3 && std::fabs(dz) < 1e-3 && std::fabs(t1 - 1.5) < 1e-3);
    double hx = 0, hy = 0, hz = 0;
    CHECK(std::sscanf(hinge.c_str(), "%lf %lf %lf", &hx, &hy, &hz) == 3);
    CHECK(std::fabs(hx - 100.0) < 1e-3 && std::fabs(hy - 220.0) < 1e-3);
    double lz = 0;
    CHECK(std::sscanf(lift.c_str(), "%lf", &lz) == 1 && std::fabs(lz - 128.0) < 1e-3);

    const std::string rad = findKey(pc.entities[1].first, "radius");
    const std::string hgt = findKey(pc.entities[1].first, "height");
    const std::string jmp = findKey(pc.entities[1].first, "iw8r_jump");
    CHECK(std::fabs(std::atof(rad.c_str()) - 100.0) < 1e-3);
    CHECK(std::fabs(std::atof(hgt.c_str()) - 144.0) < 1e-3);
    double jx = 0, jy = 0, jz = 0;
    CHECK(std::sscanf(jmp.c_str(), "%lf %lf %lf", &jx, &jy, &jz) == 3);
    CHECK(std::fabs(jx) < 1e-3 && std::fabs(jy - 400.0) < 1e-3 && std::fabs(jz - 600.0) < 1e-3);
}

TEST(capability_auto_caulk_fx_atlas_light_grid_local_and_decals) {
    // Auto-caulk must NOT caulk a solid face touching a glass brush.
    const std::string glassMap =
        "iwmap 4\n{\n\"classname\" \"worldspawn\"\n" +
        boxBrushText({{0, 0, 0}, {64, 64, 64}}, "concrete_wall") +
        boxBrushText({{64, 0, 0}, {72, 64, 64}}, "iw8r_glass") +
        "}\n";
    Document doc;
    std::string derr;
    CHECK(doc.loadFromText(glassMap, "glass_test.map", &derr));
    const size_t changed = autoCaulk(doc, false);
    CHECK(changed == 0);

    // Authored FX sprite atlas rows/cols/fps round-trip and IW3 emission.
    AuthoredFx fx;
    fx.name = "iw8r/fire_atlas";
    AuthoredFxElement el;
    el.name = "flames";
    el.image = "flames.png";
    el.atlasRows = 4;
    el.atlasCols = 4;
    el.atlasFps = 24;
    fx.elements.push_back(el);
    AuthoredFx parsedFx;
    std::string ferr;
    CHECK(parseAuthoredFx(authoredFxJson(fx), parsedFx, &ferr));
    CHECK(parsedFx.elements.size() == 1 && parsedFx.elements[0].atlasRows == 4 &&
          parsedFx.elements[0].atlasCols == 4 && parsedFx.elements[0].atlasFps == 24);
    const std::string matJ = authoredFxMaterialJson(parsedFx, 0);
    CHECK(matJ.find("\"rows\":4,\"columns\":4") != std::string::npos);
    const std::string iw3J = authoredFxIw3Json(parsedFx);
    CHECK(iw3J.find("\"entry_count\":16") != std::string::npos && iw3J.find("\"fps\":24") != std::string::npos);

    // Light grid local lights illuminate an enclosed room where sun & sky are blocked.
    RayBvh bvh;
    std::vector<RayBvh::Triangle> tris;
    // Closed box [-128..128]^3
    const Vec3 c[8] = {{-128, -128, -128}, {128, -128, -128}, {128, 128, -128}, {-128, 128, -128},
                       {-128, -128, 128},  {128, -128, 128},  {128, 128, 128},  {-128, 128, 128}};
    const int f[12][3] = {{0, 2, 1}, {0, 3, 2}, {4, 5, 6}, {4, 6, 7}, {0, 1, 5}, {0, 5, 4},
                          {2, 3, 7}, {2, 7, 6}, {1, 2, 6}, {1, 6, 5}, {3, 0, 4}, {3, 4, 7}};
    for (const auto& t : f) tris.push_back({c[t[0]], c[t[1]], c[t[2]], 0});
    bvh.build(std::move(tris));
    std::vector<LightGridLocalLight> lls;
    LightGridLocalLight lamp;
    lamp.origin = {0, 0, 64};
    lamp.color = {1.0, 0.8, 0.4};
    lamp.radius = 256.0;
    lls.push_back(lamp);
    std::array<std::array<float, 3>, 56> sNoLight{}, sWithLight{};
    lightGridSamplesAt(bvh, {}, {0, 0, 1}, {1, 1, 1}, {0.3, 0.3, 0.4}, 1024.0, {0, 0, 0}, sNoLight, nullptr);
    lightGridSamplesAt(bvh, {}, {0, 0, 1}, {1, 1, 1}, {0.3, 0.3, 0.4}, 1024.0, {0, 0, 0}, sWithLight, &lls);
    double sumWith = 0, sumWithout = 0;
    for (int i = 0; i < 56; ++i) {
        sumWith += sWithLight[i][0] + sWithLight[i][1] + sWithLight[i][2];
        sumWithout += sNoLight[i][0] + sNoLight[i][1] + sNoLight[i][2];
    }
    CHECK(sumWith > sumWithout + 50.0);

    // Own decal volume projects onto Bezier patches and carries opacity/draw_order.
    const std::string decalMap =
        "iwmap 4\n{\n\"classname\" \"worldspawn\"\n"
        "{\n mesh\n {\n  concrete_wall\n  lightmap_gray\n  3 3 16 8\n"
        "  (\n   v -64 -64 0 t 0 0 0 0\n   v -64 0 0 t 0 64 0 0\n   v -64 64 0 t 0 128 0 0\n  )\n"
        "  (\n   v 0 -64 0 t 64 0 0 0\n   v 0 0 0 t 64 64 0 0\n   v 0 64 0 t 64 128 0 0\n  )\n"
        "  (\n   v 64 -64 0 t 128 0 0 0\n   v 64 0 0 t 128 64 0 0\n   v 64 64 0 t 128 128 0 0\n  )\n"
        " }\n}\n}\n"
        "{\n\"classname\" \"iw8r_decal\"\n\"origin\" \"0 0 0\"\n\"angles\" \"90 0 0\"\n"
        "\"iw8r_size\" \"64 64 32\"\n\"material\" \"decal_test\"\n\"opacity\" \"0.5\"\n\"feather\" \"0.25\"\n\"draw_order\" \"2\"\n}\n";
    const MapFile dm = parseOrFail(decalMap);
    const auto projected = projectOwnDecals(dm);
    CHECK(!projected.empty());
    if (!projected.empty()) {
        CHECK(projected[0].material == "decal_test");
        CHECK(std::fabs(projected[0].opacity - 0.5) < 1e-6);
        CHECK(projected[0].drawOrder == 2);
    }

    // Script check: rotating_door snippet uses a script_origin + linkto instead of dropped origin brushes.
    bool sawRotatingDoor = false;
    for (const ScriptSnippet& sn : scriptSnippets()) {
        if (std::string(sn.id) == "rotating_door") {
            sawRotatingDoor = true;
            CHECK(std::string(sn.text).find("Give the script_brushmodel an origin brush") == std::string::npos);
            CHECK(std::string(sn.text).find("script_origin") != std::string::npos);
            CHECK(std::string(sn.text).find("linkto") != std::string::npos);
        }
    }
    CHECK(sawRotatingDoor);
}
