// Log links (roadmap 7.13): references found in Replay and zonetool log lines, and what they resolve to in a map.
// The lines are copied from out\compile-nm\compile-log.txt and out\snippet-test\res\compile-log.txt.
#include <algorithm>
#include <string>
#include <vector>

#include "log_links.h"
#include "map_text.h"
#include "test_harness.h"

using namespace iw8r;

namespace {

// The first reference of this kind and value, or nullptr.
const LogRef* findRef(const std::vector<LogRef>& refs, LogRef::Kind kind, const std::string& value) {
    for (const LogRef& r : refs)
        if (r.kind == kind && r.value == value) return &r;
    return nullptr;
}

// The text a reference covers in its line.
std::string spanText(const std::string& line, const LogRef& r) { return line.substr(r.begin, r.end - r.begin); }

// Parsed map of a text (the test fails if it does not parse).
MapFile parsedMap(const std::string& text) {
    Id next = 1;
    ParseResult pr = parseMap(text, next);
    CHECK(pr.map.has_value());
    return pr.map ? *pr.map : MapFile{};
}

// One axis-aligned box brush with the given material (same shape as the trigger template test helper).
std::string boxBrush(const std::string& mat) {
    auto f = [&](const std::string& a, const std::string& b, const std::string& c) {
        return " ( " + a + " ) ( " + b + " ) ( " + c + " ) " + mat + " 128 128 0 0 0 0\n";
    };
    const std::string X0 = "0", Y0 = "0", Z0 = "0", X1 = "64", Y1 = "64", Z1 = "64";
    std::string t = "{\n";
    t += f(X1 + " " + Y1 + " " + Z0, X0 + " " + Y1 + " " + Z0, X0 + " " + Y0 + " " + Z0);
    t += f(X0 + " " + Y0 + " " + Z1, X0 + " " + Y1 + " " + Z1, X1 + " " + Y1 + " " + Z1);
    t += f(X0 + " " + Y0 + " " + Z0, X0 + " " + Y0 + " " + Z1, X1 + " " + Y0 + " " + Z1);
    t += f(X1 + " " + Y0 + " " + Z0, X1 + " " + Y0 + " " + Z1, X1 + " " + Y1 + " " + Z1);
    t += f(X1 + " " + Y1 + " " + Z0, X1 + " " + Y1 + " " + Z1, X0 + " " + Y1 + " " + Z1);
    t += f(X0 + " " + Y1 + " " + Z0, X0 + " " + Y1 + " " + Z1, X0 + " " + Y0 + " " + Z1);
    return t + "}\n";
}

// Entities in map.entities order: 0 worldspawn (with a brush), 1 info_player_start, 2 script_model, 3 misc_model,
// 4 fx_origin, 5 sound_emitter, 6 trigger with a targetname.
const char* kSmallMapHead = "iwmap 4\n{\n\"classname\" \"worldspawn\"\n";

std::string smallMap() {
    std::string t = kSmallMapHead;
    t += boxBrush("tm2co/me_curb_stone_01");
    t += "}\n";
    t += "{\n\"classname\" \"info_player_start\"\n\"origin\" \"0 0 0\"\n}\n";
    t += "{\n\"classname\" \"script_model\"\n\"model\" \"iw8r_dev_crate\"\n\"origin\" \"100 0 0\"\n}\n";
    t += "{\n\"classname\" \"misc_model\"\n\"model\" \"attachment_vm_angledgrip_mike14_v11\"\n\"origin\" \"200 0 0\"\n}\n";
    t += "{\n\"classname\" \"fx_origin\"\n\"fx\" \"vfx/code/tread/heli_dust_default\"\n\"origin\" \"0 100 0\"\n}\n";
    t += "{\n\"classname\" \"sound_emitter\"\n\"soundalias\" \"mp_alarm_loop\"\n\"origin\" \"0 200 0\"\n}\n";
    t += "{\n\"classname\" \"trigger_multiple\"\n\"targetname\" \"gate_north\"\n\"origin\" \"0 300 0\"\n}\n";
    return t;
}

}  // namespace

TEST(log_links_entity_number_span) {
    const std::string line = "warning: LEAK from info_player_start (entity 1); Tools > Leak Check draws the path. The build continues (one DPVS cell).";
    const auto refs = findLogRefs(line);
    const LogRef* e = findRef(refs, LogRef::Entity, "1");
    CHECK(e != nullptr);
    if (e) {
        CHECK(spanText(line, *e) == "entity 1");
        CHECK(e->begin == line.find("entity 1"));
    }
    CHECK(refs.size() == 1);

    const std::string hash = "warning: entity #12: brush skipped (not a bounded convex solid)";
    const LogRef* h = findRef(findLogRefs(hash), LogRef::Entity, "12");
    CHECK(h != nullptr);
    if (h) CHECK(spanText(hash, *h) == "entity #12");

    // "entities" (a count) and "entity" without a number are not references.
    CHECK(findLogRefs("warning: 3 mp_tdm_spawn entities, 2 entity classes").empty());
}

TEST(log_links_script_position) {
    const std::string line = "preflight error script: maps/mp/mp_snip.gsc:193: unknown function 'nosuchbuiltin_xyz()': not a builtin, not defined in this script, not a converter name";
    const auto refs = findLogRefs(line);
    const LogRef* s = findRef(refs, LogRef::ScriptLine, "maps/mp/mp_snip.gsc");
    CHECK(s != nullptr);
    if (s) {
        CHECK(s->line == 193);
        CHECK(spanText(line, *s) == "maps/mp/mp_snip.gsc:193");
    }
    // The bare file name, without a line, opens the file.
    const std::string inc = "warning: map script includes mp_snip.gsc (user_main runs after the load script)";
    const LogRef* f = findRef(findLogRefs(inc), LogRef::File, "mp_snip.gsc");
    CHECK(f != nullptr);
    if (f) CHECK(spanText(inc, *f) == "mp_snip.gsc");
    // A Windows path with a line keeps its drive.
    const std::string win = "error: E:/IW8-Radiant/samples/x/maps/mp/x.gsc:7 bad call";
    const LogRef* w = findRef(findLogRefs(win), LogRef::ScriptLine, "E:/IW8-Radiant/samples/x/maps/mp/x.gsc");
    CHECK(w != nullptr);
    if (w) CHECK(w->line == 7);
}

TEST(log_links_model_names) {
    const std::string quoted = "warning: model 'automotive_truck_loading_ramp_short' on 1 entity: stock xmodels are not exported by the x64-zt route yet, key left out";
    const LogRef* q = findRef(findLogRefs(quoted), LogRef::Model, "automotive_truck_loading_ramp_short");
    CHECK(q != nullptr);
    if (q) CHECK(spanText(quoted, *q) == "automotive_truck_loading_ramp_short");

    const std::string misc = "warning: misc_model entity with model 'attachment_vm_angledgrip_mike14_v11' (1): not a custom model in the model folders; stock xmodel export is not supported yet, skipped";
    const auto mrefs = findLogRefs(misc);
    CHECK(findRef(mrefs, LogRef::Model, "attachment_vm_angledgrip_mike14_v11") != nullptr);
    CHECK(findRef(mrefs, LogRef::Entity, "1") == nullptr);  // "(1)" is a count here, not an entity number
    CHECK(findRef(mrefs, LogRef::Model, "entity") == nullptr);

    const std::string bare = "warning: model iw8r_dev_crate: generated 36 vertex normal(s) (the file has no vn for them)";
    const LogRef* b = findRef(findLogRefs(bare), LogRef::Model, "iw8r_dev_crate");
    CHECK(b != nullptr);
    if (b) CHECK(spanText(bare, *b) == "iw8r_dev_crate");

    const std::string script = "warning: gameplay: script_model iw8r_dev_crate is built as a static prop (no gameobject, exploder or targeting entity); use misc_model for decor";
    CHECK(findRef(findLogRefs(script), LogRef::Model, "iw8r_dev_crate") != nullptr);
}

TEST(log_links_materials_and_images) {
    const std::string mats = "warning: 3 editor material(s) compiled to custom materials (doc 21): tm2co/ee_trim_beam_airport_concrete, tm2co/me_curb_stone_01, tm2co/me_plaster_barrier_fence_wall";
    const auto refs = findLogRefs(mats);
    CHECK(findRef(refs, LogRef::Material, "tm2co/ee_trim_beam_airport_concrete") != nullptr);
    const LogRef* m = findRef(refs, LogRef::Material, "tm2co/me_curb_stone_01");
    CHECK(m != nullptr);
    if (m) CHECK(spanText(mats, *m) == "tm2co/me_curb_stone_01");  // the comma after it is not part of the name
    CHECK(findRef(refs, LogRef::Material, "tm2co/me_plaster_barrier_fence_wall") != nullptr);

    const std::string quoted = "[info] write_material: native 2D material 'compass_map_mp_iw8r_dev_sho' (image compass_map_mp_iw8r_dev_sho)";
    const auto qrefs = findLogRefs(quoted);
    const LogRef* qm = findRef(qrefs, LogRef::Material, "compass_map_mp_iw8r_dev_sho");
    CHECK(qm != nullptr);
    if (qm) CHECK(spanText(quoted, *qm) == "compass_map_mp_iw8r_dev_sho");
    const LogRef* img = findRef(qrefs, LogRef::Image, "compass_map_mp_iw8r_dev_sho");
    CHECK(img != nullptr);
    if (img) CHECK(spanText(quoted, *img) == "compass_map_mp_iw8r_dev_sho");

    // A stock path shape in a quoted keyword form.
    const std::string stock = "stock model material 'tm/furniture/cafe_chair' has no cached colour image: grey this time (2 mesh(es))";
    CHECK(findRef(findLogRefs(stock), LogRef::Material, "tm/furniture/cafe_chair") != nullptr);
}

TEST(log_links_effects_and_sounds) {
    const std::string fx = "[warn] iw3: script effect vfx/code/tread/heli_dust_default has no extracted IW3 FX graph";
    const auto refs = findLogRefs(fx);
    const LogRef* e = findRef(refs, LogRef::Effect, "vfx/code/tread/heli_dust_default");
    CHECK(e != nullptr);
    if (e) CHECK(spanText(fx, *e) == "vfx/code/tread/heli_dust_default");
    CHECK(refs.size() == 1);

    // The converter's translated target (after "->", prefixed with "@...@") is not a second reference.
    const std::string tr = "[info] iw3: script translation: effect vfx/core/expl/spark_death -> @mw120r_iw3fx@vfx/core/expl/spark_death";
    const auto trefs = findLogRefs(tr);
    CHECK(trefs.size() == 1);
    CHECK(findRef(trefs, LogRef::Effect, "vfx/core/expl/spark_death") != nullptr);

    // "6 effect placement(s)" is a count, not an effect name.
    CHECK(findLogRefs("warning: 6 effect placement(s) in the map script (plays only when x64-zt compiles it: Tool Paths gsc_tool)").empty());

    const std::string snd = "warning: soundalias 'mp_alarm_loop' on 1 sound_emitter";
    const LogRef* s = findRef(findLogRefs(snd), LogRef::Sound, "mp_alarm_loop");
    CHECK(s != nullptr);
    if (s) CHECK(spanText(snd, *s) == "mp_alarm_loop");
}

TEST(log_links_targetname_and_files) {
    const std::string tn = "warning: targetname 'gate_north' has no matching trigger";
    const LogRef* t = findRef(findLogRefs(tn), LogRef::Targetname, "gate_north");
    CHECK(t != nullptr);
    if (t) CHECK(spanText(tn, *t) == "gate_north");

    const std::string map = "[warn] dumpsrc: wrote E:/IW8-Radiant/samples/dev/mp_iw8r_dev_sho.map";
    const LogRef* f = findRef(findLogRefs(map), LogRef::File, "E:/IW8-Radiant/samples/dev/mp_iw8r_dev_sho.map");
    CHECK(f != nullptr);
    if (f) CHECK(spanText(map, *f) == "E:/IW8-Radiant/samples/dev/mp_iw8r_dev_sho.map");

    // A .colmap is not a .map.
    CHECK(findRef(findLogRefs("[warn] dumpsrc: no .colmap at prepared/maps/mp/mp_iw8r_dev_sho.d3dbsp.colmap"), LogRef::File,
                  "prepared/maps/mp/mp_iw8r_dev_sho.d3dbsp.colmap") == nullptr);
}

TEST(log_links_ordinary_lines_have_no_refs) {
    // Twenty ordinary lines from the two compile logs and the editor's progress output.
    const std::vector<std::string> lines = {
        "-- IW8 extraction (native compiler)",
        "stock models: 17 built from the ACTS model cache, 1 not cached yet (left out this time; build again in a moment)",
        "stock textures: 44 material(s) built with their colour image",
        "custom materials: 49 in the library",
        "surfaces 63, triangles 752, collision brushes 92, entities 68, spawns 12",
        "bounds -2592 -3616 -64 .. 2592 3616 784",
        "sky: sky_mp_aniyah_tac built (2048x2048 HDR (dxgi 95) -> 1024 cube faces, exposure 1.02443 (mean 0.136594, p99.5 0.878539))",
        "warning: 22 placed model(s) got a solid collision box (players and bullets)",
        "[info] timing: iw3/read brush and static models 0.0s",
        "[info] iw3: moved X from alpha to red in 0 of 43 DXT5 normal map(s)",
        "[info] iw3: source atlas 8192x8192, 8 columns, 1024-pixel material tiles, 9 mips",
        "[info] collision: built native PhysicsAsset 'mw120r/mp_iw8r_dev_sho/smodel_5' (128 vertices, 168 triangles, surface 0, 11020 bytes)",
        "[info] iw3: normalized 764 triangles, 96 collision hulls, 8 brush models, 22 static models and 0 dynamic models",
        "[info] iw3: preserved 48 source materials and 0 authored lightmaps",
        "[info] navigation: 474x368 columns at 8.0 units, 174432 floor cells, 167638 walkable after radius erosion and reachability (0 spawn(s) off the grid)",
        "[info] local light 2: IW3 color 1.000 1.000 1.000, reference 192.0 of radius 384 -> intensity 18432.0",
        "[info] iw3: 23 world material(s), 0 with a detail map",
        "[info] assets: 0 materials, 1 images, 0 models, 0 model surfaces, 0 failures",
        "[info] iw8 xsurface(dump) 'mw120r/mp_iw8r_dev_sho/smodel_0_lod0': wrote XModelSurfs (8 surf(s), 39055 verts, 44526 tris, 701 tri-clusters, shared blob 1488496 bytes; 8 rigid-run(s), 0 blend-word(s))",
        "[info] entities: converted 54 source entities",
    };
    CHECK(lines.size() == 20);
    for (const std::string& l : lines) {
        const auto refs = findLogRefs(l);
        if (!refs.empty()) std::printf("  unexpected ref in: %s\n", l.c_str());
        CHECK(refs.empty());
    }
}

TEST(log_links_resolve_entities) {
    const MapFile map = parsedMap(smallMap());
    CHECK(map.entities.size() == 7);
    if (map.entities.size() != 7) return;

    // Entity numbers are indices into map.entities (the editor's convention).
    LogRef byIndex;
    byIndex.kind = LogRef::Entity;
    byIndex.value = "1";
    CHECK(resolveLogRef(map, byIndex) == std::vector<Id>{map.entities[1].id});

    LogRef outOfRange = byIndex;
    outOfRange.value = "99";
    CHECK(resolveLogRef(map, outOfRange).empty());

    // Entity::id is used only when the flag is set.
    LogRef byId = byIndex;
    byId.value = std::to_string(map.entities[3].id);
    byId.entityIsId = true;
    CHECK(resolveLogRef(map, byId) == std::vector<Id>{map.entities[3].id});

    LogRef tn;
    tn.kind = LogRef::Targetname;
    tn.value = "gate_north";
    CHECK(resolveLogRef(map, tn) == std::vector<Id>{map.entities[6].id});

    LogRef model;
    model.kind = LogRef::Model;
    model.value = "iw8r_dev_crate";
    CHECK(resolveLogRef(map, model) == std::vector<Id>{map.entities[2].id});
    model.value = "attachment_vm_angledgrip_mike14_v11";
    CHECK(resolveLogRef(map, model) == std::vector<Id>{map.entities[3].id});

    LogRef mat;
    mat.kind = LogRef::Material;
    mat.value = "TM2CO/me_curb_stone_01";  // materials compare case-insensitively
    CHECK(resolveLogRef(map, mat) == std::vector<Id>{map.entities[0].id});
    mat.value = "tm2co/absent";
    CHECK(resolveLogRef(map, mat).empty());

    LogRef fx;
    fx.kind = LogRef::Effect;
    fx.value = "vfx/code/tread/heli_dust_default";
    CHECK(resolveLogRef(map, fx) == std::vector<Id>{map.entities[4].id});

    LogRef snd;
    snd.kind = LogRef::Sound;
    snd.value = "mp_alarm_loop";
    CHECK(resolveLogRef(map, snd) == std::vector<Id>{map.entities[5].id});

    // Files, script positions and images resolve to nothing.
    LogRef script;
    script.kind = LogRef::ScriptLine;
    script.value = "maps/mp/mp_snip.gsc";
    script.line = 193;
    CHECK(resolveLogRef(map, script).empty());
    LogRef img;
    img.kind = LogRef::Image;
    img.value = "iw8r_dev_crate";
    CHECK(resolveLogRef(map, img).empty());
}

TEST(log_links_resolve_real_lines) {
    // A real compile line resolved against the map it came from: "entity 1" is the info_player_start.
    const MapFile map = parsedMap(smallMap());
    const std::string line = "warning: LEAK from info_player_start (entity 1); Tools > Leak Check draws the path.";
    const auto refs = findLogRefs(line);
    const LogRef* e = findRef(refs, LogRef::Entity, "1");
    CHECK(e != nullptr);
    if (e) CHECK(resolveLogRef(map, *e) == std::vector<Id>{map.entities[1].id});
}
