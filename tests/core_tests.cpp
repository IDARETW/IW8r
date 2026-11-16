// Core tests for the IW8 Radiant edit-save-reopen loop.
//   iw8r_tests [--samples <dir>] [--tmp <dir>]
// IW8 fixtures: --iw8-sample <iw8_sample_room.map> (default: <repo>/samples/iw8) and --iw8-ents <ACTS map_ents asset.json>
// (default: the stock mp_hideout dump). --corpus <dir> is an optional Radiant text-grammar corpus (any folder of .map files).
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "document.h"
#include "iw8_dump.h"
#include "iw8_mapents.h"
#include "test_harness.h"

using namespace iw8r;
namespace fs = std::filesystem;

namespace {

using test::tests;
std::string g_samples = "E:\\CoD4\\game_build\\Call of Duty 4 Modern Warfare\\map_source";  // grammar corpus only
std::string g_iw8Sample;
std::string g_iw8Ents = "E:\\mw124-conversion-builds\\_ents_stock\\hideout\\mw19replay\\mp_hideout\\assets\\map_ents\\maps\\mp\\mp_hideout.d3dbsp.0.asset.json";
std::string& g_tmp = test::tmpDir();

// A small scene with everything the round trip must preserve: layers, comments, a brush with
// contents/layer lines, a patch with colour + texture tokens, a point entity, a brush entity,
// an unknown top-level line and CRLF endings.
const char* kScene =
    "iwmap 4\r\n"
    "\"000_Global\" flags active\r\n"
    "\"The Map\" flags \r\n"
    "// entity 0\r\n"
    "{\r\n"
    "\"classname\" \"worldspawn\"\r\n"
    "\"_color\" \"0.6 0.7 1.000000\"\r\n"
    "// brush 0\r\n"
    "{\r\n"
    " layer \"000_Global/Floors\"\r\n"
    " contents detail;\r\n"
    " ( 64 64 0 ) ( 0 64 0 ) ( 0 0 0 ) caulk 64 64 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0\r\n"
    " ( 0 0 32 ) ( 0 64 32 ) ( 64 64 32 ) ch_tile_floor05 64 64 0 -512 0 0 lightmap_gray 16384 16384 0 0 0 0\r\n"
    " ( 0 0 32 ) ( 64 0 32 ) ( 64 0 0 ) caulk 64 64 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0\r\n"
    " ( 64 64 32 ) ( 0 64 32 ) ( 0 64 0 ) caulk 64 64 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0\r\n"
    " ( 64 0 32 ) ( 64 64 32 ) ( 64 64 0 ) caulk 64 64 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0\r\n"
    " ( 0 64 32 ) ( 0 0 32 ) ( 0 0 0 ) caulk 64 64 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0\r\n"
    "}\r\n"
    "// brush 1\r\n"
    "{\r\n"
    " mesh\r\n"
    " {\r\n"
    "  toolFlags splitGeo;\r\n"
    "  me_cinderblock_wall2\r\n"
    "  lightmap_gray\r\n"
    "  2 2 16 8\r\n"
    "  (\r\n"
    "\tv -263.5 -1007.5 137.5 c 255 255 255 0 t 0 0 -62.96875 -8.59375\r\n"
    "\tv -264 -1007.5 137 t 0 -5.6568542 -62.96875 -8.5625\r\n"
    "  )\r\n"
    "  (\r\n"
    "\tv -263.5 -1000 137.5 t 0 0 -62.96875 -8.59375\r\n"
    "\tv -264 -1000 137 t 0 -5.6568542 -62.96875 -8.5625\r\n"
    "  )\r\n"
    " }\r\n"
    "}\r\n"
    "}\r\n"
    "// entity 1\r\n"
    "{\r\n"
    "\"origin\" \"-64 128 16\"\r\n"
    "\"angles\" \"0 90 0\"\r\n"
    "\"classname\" \"mp_tdm_spawn\"\r\n"
    "}\r\n"
    "// entity 2\r\n"
    "{\r\n"
    "\"classname\" \"trigger_multiple\"\r\n"
    "\"targetname\" \"door_trig\"\r\n"
    "// brush 0\r\n"
    "{\r\n"
    " ( 144 16 0 ) ( 128 16 0 ) ( 128 0 0 ) trigger 64 64 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0\r\n"
    " ( 128 0 16 ) ( 128 16 16 ) ( 144 16 16 ) trigger 64 64 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0\r\n"
    " ( 128 0 16 ) ( 144 0 16 ) ( 144 0 0 ) trigger 64 64 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0\r\n"
    " ( 144 16 16 ) ( 128 16 16 ) ( 128 16 0 ) trigger 64 64 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0\r\n"
    " ( 144 0 16 ) ( 144 16 16 ) ( 144 16 0 ) trigger 64 64 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0\r\n"
    " ( 128 16 16 ) ( 128 0 16 ) ( 128 0 0 ) trigger 64 64 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0\r\n"
    "}\r\n"
    "}\r\n";

Document sceneDoc() {
    Document d;
    std::string err;
    if (!d.loadFromText(kScene, "scene.map", &err)) std::printf("  scene load error: %s\n", err.c_str());
    return d;
}

const EntityItem* nthPrim(const Entity& e, size_t n) {
    size_t k = 0;
    for (const auto& it : e.items)
        if (it.isPrimitive() && k++ == n) return &it;
    return nullptr;
}

TEST(parse_scene_structure) {
    Document d = sceneDoc();
    const auto& m = d.map();
    CHECK(m.entities.size() == 3);
    CHECK(m.eol == "\r\n");
    CHECK(m.header == "iwmap 4\r\n\"000_Global\" flags active\r\n\"The Map\" flags \r\n");
    CHECK(m.entities[0].prefix == "// entity 0\r\n");
    CHECK(m.entities[0].isWorldspawn());
    const Primitive* brush = nthPrim(m.entities[0], 0)->prim();
    const Primitive* patch = nthPrim(m.entities[0], 1)->prim();
    CHECK(brush->kind == PrimKind::Brush);
    CHECK(brush->faces.size() == 6);
    CHECK(brush->faces[1].material() == "ch_tile_floor05");
    CHECK(patch->kind == PrimKind::Patch);
    CHECK(patch->patchKeyword == "mesh");
    CHECK(patch->patchMaterial == "me_cinderblock_wall2");
    CHECK(patch->verts.size() == 4);
    CHECK(patch->verts[3].row == 1 && patch->verts[3].col == 1);
    CHECK(m.entities[1].origin().has_value() && m.entities[1].origin()->x == -64);
    CHECK(d.text() == kScene);
}

TEST(brush_geometry_box) {
    Document d = sceneDoc();
    const Primitive* brush = nthPrim(d.map().entities[0], 0)->prim();
    const auto planes = brush->planes();
    CHECK(planes.size() == 6);
    const auto w = buildBrushWindings(planes);
    size_t faces = 0;
    for (const auto& f : w) faces += (f.size() == 4);
    CHECK(faces == 6);
    const AABB b = brush->bounds();
    CHECK(std::fabs(b.mins.x) < 1e-6 && std::fabs(b.maxs.x - 64) < 1e-6);
    CHECK(std::fabs(b.mins.z) < 1e-6 && std::fabs(b.maxs.z - 32) < 1e-6);
    // Ray picking straight down onto the top face.
    Ray r{{32, 32, 1000}, {0, 0, -1}};
    auto t = rayConvex(r, planes);
    CHECK(t.has_value() && std::fabs(*t - 968) < 1e-6);
    Ray miss{{200, 32, 1000}, {0, 0, -1}};
    CHECK(!rayConvex(miss, planes).has_value());
}

TEST(move_brush_on_grid_preserves_fields) {
    Document d = sceneDoc();
    const Entity& w = d.map().entities[0];
    d.setGrid(16);
    d.select({w.id, nthPrim(w, 0)->id}, false);
    CHECK(d.selection().size() == 1);
    const Vec3 delta = snapToGrid(Vec3{19, -7, 0}, d.grid());  // -> 16 0 0
    CHECK(delta == Vec3(16, 0, 0));
    CHECK(d.translateSelection(delta));
    const Primitive* brush = nthPrim(d.map().entities[0], 0)->prim();
    CHECK(brush->dirty);
    CHECK(brush->lines[3].text == " contents detail;");
    CHECK(brush->lines[4].text == " ( 80 64 0 ) ( 16 64 0 ) ( 16 0 0 ) caulk 64 64 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0");
    CHECK(brush->lines[2].text == " layer \"000_Global/Floors\"");
    CHECK(brush->lines[2].eol == "\r\n");
    // Everything else is untouched.
    const MapDiff diff = diffMaps(sceneDoc().map(), d.map());
    CHECK(diff.changes.size() == 1);
    CHECK(diff.changes[0].kind == "brush-moved");
    CHECK(diff.changes[0].detail == "16 0 0");
}

TEST(undo_redo_is_byte_exact) {
    Document d = sceneDoc();
    const Entity& w = d.map().entities[0];
    d.select({w.id, nthPrim(w, 0)->id}, false);
    CHECK(d.translateSelection({0, 0, 8}));
    const std::string moved = d.text();
    CHECK(moved != kScene);
    CHECK(d.modified());
    CHECK(d.undo());
    CHECK(d.text() == kScene);
    CHECK(!d.modified());
    CHECK(d.redo());
    CHECK(d.text() == moved);
    CHECK(!d.redo());
    CHECK(d.undo());
    CHECK(!d.undo());
    CHECK(d.text() == kScene);
}

TEST(entity_property_edit_and_undo) {
    Document d = sceneDoc();
    const Id spawn = d.map().entities[1].id;
    CHECK(d.setKeyValue(spawn, "angles", "0 180 0"));
    CHECK(*d.findEntity(spawn)->value("angles") == "0 180 0");
    CHECK(!d.setKeyValue(spawn, "angles", "0 180 0"));  // no-op is not a history entry
    CHECK(d.setKeyValue(spawn, "script_noteworthy", "editor_test"));
    const std::string edited = d.text();
    CHECK(edited.find("\"angles\" \"0 180 0\"\r\n") != std::string::npos);
    CHECK(edited.find("\"script_noteworthy\" \"editor_test\"\r\n\"classname\"") == std::string::npos);  // inserted after last kv
    CHECK(edited.find("\"classname\" \"mp_tdm_spawn\"\r\n\"script_noteworthy\" \"editor_test\"\r\n}") != std::string::npos);
    CHECK(d.history().size() == 2);
    CHECK(d.undo());
    CHECK(d.findEntity(spawn)->value("script_noteworthy") == nullptr);
    CHECK(d.undo());
    CHECK(d.text() == kScene);
    CHECK(d.redo() && d.redo());
    CHECK(d.text() == edited);
    CHECK(d.removeKey(spawn, "script_noteworthy"));
    CHECK(!d.removeKey(spawn, "classname"));
    CHECK(d.undo());
    CHECK(d.text() == edited);
}

TEST(point_entity_move_updates_origin) {
    Document d = sceneDoc();
    const Id spawn = d.map().entities[1].id;
    d.select({spawn, kNoId}, false);
    CHECK(d.translateSelection({8, -8, 0}));
    CHECK(*d.findEntity(spawn)->value("origin") == "-56 120 16");
    CHECK(d.undo());
    CHECK(d.text() == kScene);
}

TEST(brush_entity_selects_as_unit) {
    Document d = sceneDoc();
    const Entity& trig = d.map().entities[2];
    d.select({trig.id, nthPrim(trig, 0)->id}, false);
    CHECK(d.selection().size() == 1);
    CHECK(d.inspectedEntity() == trig.id);
    CHECK(d.translateSelection({0, 0, 16}));
    CHECK(d.undo());
    CHECK(d.text() == kScene);
}

TEST(patch_move_keeps_colour_and_uv_tokens) {
    Document d = sceneDoc();
    const Entity& w = d.map().entities[0];
    d.select({w.id, nthPrim(w, 1)->id}, false);
    CHECK(d.translateSelection({0, 0, 4}));
    const Primitive* patch = nthPrim(d.map().entities[0], 1)->prim();
    bool found = false;
    for (const auto& l : patch->lines)
        if (l.text == "\tv -263.5 -1007.5 141.5 c 255 255 255 0 t 0 0 -62.96875 -8.59375") found = true;
    CHECK(found);
    const MapDiff diff = diffMaps(sceneDoc().map(), d.map());
    CHECK(diff.changes.size() == 1 && diff.changes[0].kind == "patch-moved" && diff.changes[0].detail == "0 0 4");
}

TEST(drag_commits_single_transaction) {
    Document d = sceneDoc();
    const Entity& w = d.map().entities[0];
    d.select({w.id, nthPrim(w, 0)->id}, false);
    d.beginDrag();
    d.updateDrag({8, 0, 0});
    d.updateDrag({16, 0, 0});
    d.updateDrag({24, 8, 0});
    CHECK(d.history().empty());
    CHECK(d.endDrag(true));
    CHECK(d.history().size() == 1);
    const MapDiff diff = diffMaps(sceneDoc().map(), d.map());
    CHECK(diff.changes.size() == 1 && diff.changes[0].detail == "24 8 0");
    // Cancelled drag restores the exact text.
    const std::string before = d.text();
    d.beginDrag();
    d.updateDrag({64, 64, 64});
    CHECK(!d.endDrag(false));
    CHECK(d.text() == before);
    CHECK(d.undo());
    CHECK(d.text() == kScene);
}

TEST(delete_and_undo) {
    Document d = sceneDoc();
    const Entity& trig = d.map().entities[2];
    d.select({trig.id, nthPrim(trig, 0)->id}, false);
    CHECK(d.deleteSelection());
    CHECK(d.map().entities.size() == 2);  // brush entity with no brushes left is removed
    CHECK(d.undo());
    CHECK(d.text() == kScene);
    const Entity& w = d.map().entities[0];
    d.select({w.id, nthPrim(w, 1)->id}, false);
    CHECK(d.deleteSelection());
    CHECK(d.map().entities.size() == 3);
    CHECK(d.undo());
    CHECK(d.text() == kScene);
}

TEST(clone_and_invert_selection) {
    Document d = sceneDoc();
    const Entity& w = d.map().entities[0];
    const Id spawn = d.map().entities[1].id;
    d.select({w.id, nthPrim(w, 0)->id}, false);
    d.select({spawn, kNoId}, true);
    CHECK(d.cloneSelection({16, 16, 0}));
    CHECK(d.map().entities.size() == 4);
    CHECK(d.selection().size() == 2);
    const Entity& copy = d.map().entities[3];
    CHECK(copy.classname() == "mp_tdm_spawn");
    CHECK(*copy.value("origin") == "-48 144 16");
    const MapDiff diff = diffMaps(sceneDoc().map(), d.map());
    CHECK(!diff.structureEqual);
    CHECK(d.undo());
    CHECK(d.text() == kScene);
    d.deselectAll();
    d.invertSelection();
    CHECK(d.selection().size() == 4);  // 2 world prims, spawn, trigger brush
}

TEST(redo_branch_is_discarded_after_new_edit) {
    Document d = sceneDoc();
    const Entity& w = d.map().entities[0];
    d.select({w.id, nthPrim(w, 0)->id}, false);
    CHECK(d.translateSelection({8, 0, 0}));
    CHECK(d.undo());
    CHECK(d.translateSelection({0, 8, 0}));
    CHECK(!d.canRedo());
    CHECK(d.history().size() == 1);
}

TEST(undo_limit_trims_oldest) {
    Document d = sceneDoc();
    d.undoLimit = 4;
    const Entity& w = d.map().entities[0];
    d.select({w.id, nthPrim(w, 0)->id}, false);
    for (int i = 0; i < 10; ++i) CHECK(d.translateSelection({1, 0, 0}));
    CHECK(d.history().size() == 4);
    int n = 0;
    while (d.undo()) ++n;
    CHECK(n == 4);
    CHECK(d.modified());  // saved state fell off the history
}

TEST(lf_and_no_trailing_newline) {
    const std::string lf = "iwmap 4\n{\n\"classname\" \"worldspawn\"\n}";
    Document d;
    std::string err;
    CHECK(d.loadFromText(lf, "lf.map", &err));
    CHECK(d.map().eol == "\n");
    CHECK(d.text() == lf);
    CHECK(d.setKeyValue(d.map().entities[0].id, "_color", "1 1 1"));
    CHECK(d.text() == "iwmap 4\n{\n\"classname\" \"worldspawn\"\n\"_color\" \"1 1 1\"\n}");
}

TEST(malformed_input_reports_line) {
    Document d;
    std::string err;
    CHECK(!d.loadFromText("iwmap 4\r\n{\r\n\"classname\" \"worldspawn\"\r\n{\r\n ( 0 0 0 ) ( 0 1 0 ) ( 1 1 0 ) caulk\r\n", "bad.map", &err));
    CHECK(err.find("bad.map:") == 0);
}

TEST(save_reopen_roundtrip_on_disk) {
    Document d = sceneDoc();
    const Entity& w = d.map().entities[0];
    d.setGrid(8);
    d.select({w.id, nthPrim(w, 0)->id}, false);
    CHECK(d.translateSelection({8, 8, 0}));
    CHECK(d.setKeyValue(d.map().entities[1].id, "angles", "0 270 0"));
    const std::string out = (fs::path(g_tmp) / "scene_edited.map").string();
    std::string err;
    CHECK(d.saveAs(out, &err));
    CHECK(!d.modified());
    Document r;
    CHECK(r.load(out, &err));
    CHECK(r.text() == d.text());
    const MapDiff diff = diffMaps(sceneDoc().map(), r.map());
    CHECK(diff.changes.size() == 2);
    bool moved = false, kv = false;
    for (const auto& c : diff.changes) {
        moved |= c.kind == "brush-moved" && c.detail == "8 8 0";
        kv |= c.kind == "kv-changed" && c.detail == "\"angles\": \"0 90 0\" -> \"0 270 0\"";
    }
    CHECK(moved && kv);
}

// ---------------------------------------------------------------- IW8

// IW8 runtime entity-string spelling: canonical ids, LF endings, NUL terminator, plus the
// `0 name "value"` script-string form.
const char kIw8Ents =
    0;  // placeholder to keep the raw string below readable
const std::string kIw8EntString = std::string(
    "{\n212 \"worldspawn\"\n2553 \"1 1 1\"\n}\n"
    "{\n80 \"0 90 0\"\n212 \"script_model\"\n709 \"-904.5 1145 320\"\n652 \"automotive_truck_loading_ramp_short\"\n}\n"
    "{\n212 \"script_origin\"\n709 \"512 512 256\"\n0 my_custom_key \"keep\"\n1072 \"auto598\"\n}\n") + std::string(1, '\0');

TEST(iw8_canonical_keys_resolve_and_edit_in_place) {
    (void)kIw8Ents;
    Document d;
    std::string err;
    CHECK(d.loadFromText(kIw8EntString, "mp_test.d3dbsp.ents", &err));
    CHECK(d.text() == kIw8EntString);
    CHECK(d.map().eol == "\n");
    CHECK(d.map().trailer == std::string(1, '\0'));
    const Entity& model = d.map().entities[1];
    CHECK(model.classname() == "script_model");
    CHECK(model.usesCanonicalKeys());
    CHECK(model.origin().has_value() && model.origin()->x == -904.5);
    CHECK(*model.value("model") == "automotive_truck_loading_ramp_short");
    const Entity& org = d.map().entities[2];
    const KeyValueLine* custom = nullptr;
    for (const auto& it : org.items)
        if (const auto* kv = it.kv(); kv && kv->form == KeyForm::ScriptString) custom = kv;
    CHECK(custom && custom->key == "my_custom_key" && custom->value == "keep");
    CHECK(*org.value("targetname") == "auto598");

    // Move on grid: the origin keeps its canonical spelling.
    d.setGrid(16);
    d.select({model.id, kNoId}, false);
    CHECK(d.translateSelection(snapToGrid(Vec3{17, 0, -3}, d.grid())));
    CHECK(d.text().find("\n709 \"-888.5 1145 320\"\n") != std::string::npos);
    // New key is written as a canonical id (845 = script_noteworthy), existing ones in place.
    CHECK(d.setKeyValue(model.id, "script_noteworthy", "editor_test"));
    CHECK(d.text().find("652 \"automotive_truck_loading_ramp_short\"\n845 \"editor_test\"\n}") != std::string::npos);
    CHECK(d.setKeyValue(org.id, "my_custom_key", "changed"));
    CHECK(d.text().find("\n0 my_custom_key \"changed\"\n") != std::string::npos);
    // Undo everything -> byte-identical runtime string, NUL included.
    while (d.undo()) {}
    CHECK(d.text() == kIw8EntString);
}

TEST(regenerated_numbers_have_no_float_noise) {
    CHECK(formatNumber(0.1 + 16) == "16.1");
    CHECK(formatNumber(-94.0001 + 16) == "-78.0001");
    CHECK(formatNumber(1.0 / 3.0) == "0.333333");
    CHECK(formatNumber(-0.0000001) == "0");
    CHECK(formatNumber(-662.88159) == "-662.88159");
    CHECK(formatNumber(131072) == "131072");
    CHECK(formatFloat32(-1382.28f) == "-1382.28");
}

TEST(iw8_sample_room_edit_save_reopen) {
    if (g_iw8Sample.empty() || !fs::exists(g_iw8Sample)) {
        test::skipf("  SKIP IW8 sample not found (run tools/make_iw8_sample_room.py)\n");
        CHECK(false);
        return;
    }
    std::string original;
    CHECK(readFile(g_iw8Sample, original));
    Document d;
    std::string err;
    CHECK(d.load(g_iw8Sample, &err));
    CHECK(d.text() == original);
    // 1. Move the pillar brush (worldspawn primitive 5) on the 16-unit grid.
    Entity* w = d.worldspawn();
    CHECK(w != nullptr);
    const EntityItem* pillar = nthPrim(*w, 5);
    CHECK(pillar && pillar->prim()->faces.size() == 6);
    d.setGrid(16);
    d.select({w->id, pillar->id}, false);
    CHECK(d.translateSelection(snapToGrid(Vec3{30, -9, 0}, d.grid())));  // -> 32 -16 0
    // 2. Change an entity property: rotate the allies start spawn.
    Id spawn = kNoId;
    for (const auto& e : d.map().entities)
        if (e.classname() == "mp_tdm_spawn_allies_start") spawn = e.id;
    CHECK(spawn != kNoId);
    CHECK(d.setKeyValue(spawn, "angles", "0 90 0"));
    const std::string edited = d.text();
    // 3. Undo both, redo both.
    CHECK(d.undo() && d.undo());
    CHECK(d.text() == original);
    CHECK(d.redo() && d.redo());
    CHECK(d.text() == edited);
    // 4. Save a copy and reopen it.
    const std::string copy = (fs::path(g_tmp) / "iw8_sample_room_edited.map").string();
    CHECK(d.saveAs(copy, &err));
    Document r;
    CHECK(r.load(copy, &err));
    CHECK(r.text() == edited);
    // 5. Exactly two changes; untouched geometry, materials and unknown fields survive.
    Document o;
    CHECK(o.load(g_iw8Sample, &err));
    const MapDiff diff = diffMaps(o.map(), r.map());
    CHECK(diff.structureEqual);
    CHECK(diff.changes.size() == 2);
    bool moved = false, rotated = false;
    for (const auto& c : diff.changes) {
        moved |= c.kind == "brush-moved" && c.detail == "32 -16 0";
        rotated |= c.kind == "kv-changed" && c.detail == "\"angles\": \"0 45 0\" -> \"0 90 0\"";
    }
    CHECK(moved && rotated);
    CHECK(edited.find("layer \"000_Global/Floors\"") != std::string::npos);
    CHECK(edited.find(" contents detail;") != std::string::npos);
    CHECK(edited.find("  toolFlags splitGeo;") != std::string::npos);
    CHECK(edited.find("\"editor_note\" \"unknown keys must survive\"") != std::string::npos);
    const Primitive* movedPillar = nthPrim(*r.worldspawn(), 5)->prim();
    for (const auto& f : movedPillar->faces) CHECK(f.material() == "tm2co/ee_trim_beam_airport_concrete");
}

TEST(iw8_stock_mapents_hideout) {
    if (!fs::exists(g_iw8Ents)) {
        test::skipf("  SKIP stock IW8 MapEnts dump not found: %s\n", g_iw8Ents.c_str());
        return;
    }
    const fs::path strings = fs::path(g_iw8Ents).parent_path().parent_path().parent_path().parent_path().parent_path() / "script_strings.json";
    Iw8MapEntsDump dump;
    std::string err;
    CHECK(loadActsMapEnts(g_iw8Ents, fs::exists(strings) ? strings.string() : "", dump, &err));
    if (!err.empty()) std::printf("  %s\n", err.c_str());
    CHECK(dump.profile == "replay-1.20");
    CHECK(dump.assetName == "maps/mp/mp_hideout.d3dbsp");
    CHECK(dump.entityString.size() == dump.numEntityChars);
    CHECK(dump.spawns.size() == 501);
    CHECK(!dump.spawns.empty() && dump.spawns[0].name == "mp_dm_spawn_start");
    Document d;
    CHECK(d.loadFromText(dump.entityString, "mp_hideout.ents", &err));
    CHECK(d.text() == dump.entityString);
    CHECK(d.map().entities.size() == 2660);
    // Move the first script_model 16 units on X and tag it.
    Id model = kNoId;
    Vec3 before;
    for (const auto& e : d.map().entities)
        if (e.classname() == "script_model") {
            model = e.id;
            before = *e.origin();
            break;
        }
    CHECK(model != kNoId);
    d.setGrid(16);
    d.select({model, kNoId}, false);
    CHECK(d.translateSelection({16, 0, 0}));
    CHECK(d.setKeyValue(model, "script_noteworthy", "iw8radiant_test"));
    const std::string out = (fs::path(g_tmp) / "mp_hideout_edited.ents").string();
    CHECK(d.saveAs(out, &err));
    Document r;
    CHECK(r.load(out, &err));
    CHECK(r.text() == d.text());
    CHECK(r.findEntity(r.map().entities[0].id) != nullptr);
    Document o;
    CHECK(o.loadFromText(dump.entityString, "orig", &err));
    const MapDiff diff = diffMaps(o.map(), r.map());
    CHECK(diff.changes.size() == 2);
    size_t identicalEntities = 0;
    for (size_t i = 0; i < o.map().entities.size(); ++i) identicalEntities += o.map().entities[i].text() == r.map().entities[i].text();
    CHECK(identicalEntities == 2659);
    CHECK(r.text().back() == '\0');
    std::printf("  hideout: %zu entities, %zu spawns, origin %s -> moved +16 x, 2659 entities byte-identical\n",
                d.map().entities.size(), dump.spawns.size(), formatNumber(before.x).c_str());
}

// x64-zt IW8D map_ents dump of stock mp_shipment (made by `zonetool -game iw8 dumpzone mp_shipment` into
// out/m2-x64zt): byte-exact container round trip, then an entity edit written back with every other record kept.
std::string g_iw8dShipment = "E:\\IW8-Radiant\\out\\m2-x64zt\\dump\\mp_shipment\\map_ents\\maps\\mp\\mp_shipment.d3dbsp.iw8";

TEST(iw8d_mapents_roundtrip_and_writeback) {
    std::string raw, err;
    if (!readFile(g_iw8dShipment, raw)) {
        test::skipf("  SKIP x64-zt dump not found: %s\n", g_iw8dShipment.c_str());
        return;
    }
    Iw8Dump d;
    CHECK(parseIw8Dump(raw, d, &err));
    CHECK(d.type == kIw8MapEntsType);
    CHECK(serializeIw8Dump(d) == raw);
    std::string ents;
    CHECK(mapEntsEntityString(d, ents, &err));
    Document doc;
    CHECK(doc.loadFromText(ents, "mp_shipment.ents", &err));
    CHECK(doc.text() == ents);
    Id model = kNoId;
    for (const auto& e : doc.map().entities)
        if (e.classname() == "script_model") { model = e.id; break; }
    CHECK(model != kNoId);
    doc.select({model, kNoId}, false);
    CHECK(doc.translateSelection({16, 0, 0}));
    CHECK(setMapEntsEntityString(d, doc.text(), &err));
    Iw8Dump back;
    CHECK(parseIw8Dump(serializeIw8Dump(d), back, &err));
    std::string written;
    CHECK(mapEntsEntityString(back, written, &err));
    CHECK(written == doc.text());
    Iw8Dump orig;
    parseIw8Dump(raw, orig, &err);
    size_t same = 0;
    for (size_t i = 0; i < orig.records.size(); ++i)
        same += i != 0 && i != 2 && orig.records[i].bytes == back.records[i].bytes && orig.records[i].kind == back.records[i].kind &&
                orig.records[i].value == back.records[i].value && orig.records[i].delta == back.records[i].delta;
    CHECK(same == orig.records.size() - 2);
    std::printf("  mp_shipment map_ents: %zu records, entity string %zu bytes; edit written back, %zu other records unchanged\n",
                orig.records.size(), written.size(), same);
}

// The outward-normal convention must hold on real Radiant data: every face of every brush in
// mp_test.map produces a winding, and the windings close into a solid with positive volume.
TEST(radiant_grammar_corpus_brushes_close) {
    const std::string p = (fs::path(g_samples) / "mp_test.map").string();
    Document d;
    std::string err;
    if (!fs::exists(p)) {
        test::skipf("  SKIP %s not found\n", p.c_str());
        return;
    }
    CHECK(d.load(p, &err));
    size_t brushes = 0, closed = 0;
    for (const auto& e : d.map().entities)
        for (const auto& it : e.items) {
            const Primitive* pr = it.prim();
            if (!pr || pr->kind != PrimKind::Brush) continue;
            ++brushes;
            const auto w = buildBrushWindings(pr->planes());
            size_t withPoly = 0;
            for (const auto& f : w) withPoly += f.size() >= 3;
            const AABB b = pr->bounds();
            if (withPoly == pr->faces.size() && b.valid() && b.maxs.x > b.mins.x && b.maxs.y > b.mins.y && b.maxs.z > b.mins.z) ++closed;
        }
    std::printf("  %zu/%zu brushes closed\n", closed, brushes);
    CHECK(brushes > 0);
    CHECK(closed == brushes);
}

// Every real sample map must round-trip byte for byte, and a move + undo must be exact.
TEST(radiant_grammar_corpus_roundtrip) {
    if (!fs::exists(g_samples)) {
        test::skipf("  SKIP samples folder not found: %s\n", g_samples.c_str());
        return;
    }
    size_t files = 0;
    for (const auto& entry : fs::recursive_directory_iterator(g_samples)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".map") continue;
        const std::string p = entry.path().string();
        std::string text;
        CHECK(readFile(p, text));
        Document d;
        std::string err;
        const bool ok = d.loadFromText(text, p, &err);
        if (!ok) std::printf("  parse error: %s\n", err.c_str());
        CHECK(ok);
        if (!ok) continue;
        CHECK(d.text() == text);
        size_t unknownPrims = 0, prims = 0;
        for (const auto& e : d.map().entities)
            for (const auto& it : e.items)
                if (const Primitive* pr = it.prim()) {
                    ++prims;
                    unknownPrims += pr->kind == PrimKind::Unknown;
                }
        if (unknownPrims) std::printf("  note: %s has %zu/%zu unclassified primitives\n", p.c_str(), unknownPrims, prims);
        // Move the first worldspawn primitive (if any) and undo.
        if (Entity* w = d.worldspawn()) {
            for (const auto& it : w->items)
                if (it.isPrimitive()) {
                    d.select({w->id, it.id}, false);
                    break;
                }
            if (!d.selection().empty()) {
                CHECK(d.translateSelection({16, 0, 0}));
                CHECK(d.text() != text);
                CHECK(d.undo());
                CHECK(d.text() == text);
            }
        }
        ++files;
        std::printf("  ok   %-60s %9zu bytes  %6zu prims\n", entry.path().filename().string().c_str(), text.size(), prims);
    }
    CHECK(files > 0);
}

}  // namespace

int main(int argc, char** argv) {
    for (int i = 1; i + 1 < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--samples" || a == "--corpus") g_samples = argv[++i];
        else if (a == "--iw8-sample") g_iw8Sample = argv[++i];
        else if (a == "--iw8-ents") g_iw8Ents = argv[++i];
        else if (a == "--tmp") g_tmp = argv[++i];
    }
    if (g_tmp.empty()) g_tmp = (fs::temp_directory_path() / "iw8r_tests").string();
    if (g_iw8Sample.empty()) {
        // <repo>/bin/<mode>/iw8r_tests.exe -> <repo>/tests/data/iw8_sample_room.map
        fs::path p = fs::absolute(argv[0]).parent_path();
        for (int up = 0; up < 3 && g_iw8Sample.empty(); ++up, p = p.parent_path())
            if (fs::exists(p / "tests" / "data" / "iw8_sample_room.map")) g_iw8Sample = (p / "tests" / "data" / "iw8_sample_room.map").string();
    }
    {
        // Full IW8 key table when generated (tools/gen_iw8_keytable.py); built-in essentials otherwise.
        fs::path p = fs::absolute(argv[0]).parent_path();
        for (int up = 0; up < 3; ++up, p = p.parent_path())
            if (fs::exists(p / "data" / "generated" / "iw8_canonical_keys.tsv")) {
                KeyTable::global().loadTsv((p / "data" / "generated" / "iw8_canonical_keys.tsv").string());
                break;
            }
    }
    fs::create_directories(g_tmp);
    for (const auto& t : tests()) {
        test::current() = t.name;
        const int before = test::failures();
        std::printf("[ RUN  ] %s\n", t.name);
        t.fn();
        std::printf("[ %s ] %s\n", test::failures() == before ? " OK " : "FAIL", t.name);
    }
    std::printf("\n%d checks, %d failures, %zu tests\n", test::checks(), test::failures(), tests().size());
    if (test::skips())
        std::printf("%d test part(s) SKIPPED: a fixture is missing on this machine (search the output for SKIP)\n", test::skips());
    return test::failures() ? 1 : 0;
}
