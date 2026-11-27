// Game-mode object templates (roadmap 6.2): table, iwmap text, and one-step insertion into a Document.
#include <set>
#include <string>
#include <vector>

#include "document.h"
#include "gameplay_checks.h"
#include "gamemode_templates.h"
#include "map_text.h"
#include "test_harness.h"

using namespace iw8r;

namespace {

const char* kRoomEmpty =
    "iwmap 4\r\n"
    "// entity 0\r\n"
    "{\r\n"
    "\"classname\" \"worldspawn\"\r\n"
    "}\r\n";

// Entities of a parsed template text, excluding the worldspawn-free header.
MapFile parseText(const std::string& text) {
    Id next = 1;
    ParseResult pr = parseMap(text, next);
    CHECK(pr.map.has_value());
    if (!pr.map) return {};
    return *pr.map;
}

std::string classOf(const Entity& e) { return e.classname(); }

// Count of entities with the classname; optional key=value filter.
size_t countClass(const MapFile& m, const std::string& cls, const std::string& key = "", const std::string& value = "") {
    size_t n = 0;
    for (const Entity& e : m.entities) {
        if (classOf(e) != cls) continue;
        if (!key.empty()) {
            const std::string* v = e.value(key);
            if (!v || *v != value) continue;
        }
        ++n;
    }
    return n;
}

size_t brushCount(const Entity& e) {
    size_t n = 0;
    for (const auto& it : e.items)
        if (const Primitive* p = it.prim())
            if (p->kind == PrimKind::Brush) ++n;
    return n;
}

// The classname sequence a template is expected to produce (excluding its header), in order.
std::vector<std::string> expectedClasses(const std::string& id) {
    if (id == "dom_flags") return {"trigger_radius", "trigger_radius", "trigger_radius"};
    if (id == "match_cameras") return {"mp_global_intermission", "script_struct", "script_struct"};
    if (id == "hardpoint_zones") return {"trigger_multiple", "trigger_multiple", "trigger_multiple", "trigger_multiple", "trigger_multiple"};
    if (id == "sd_sites")
        return {"trigger_use_touch", "trigger_use_touch", "script_model", "script_model", "trigger_use_touch", "trigger_use_touch",
                "script_model", "script_model", "script_model", "trigger_multiple"};
    if (id == "ctf_flags")
        return {"trigger_radius", "script_model", "trigger_radius", "trigger_radius", "script_model", "trigger_radius"};
    if (id == "spawns_tdm") return {"mp_tdm_spawn", "mp_tdm_spawn", "mp_tdm_spawn_allies_start", "mp_tdm_spawn_axis_start"};
    if (id == "spawns_dm") return {"mp_dm_spawn", "mp_dm_spawn", "mp_dm_spawn", "mp_dm_spawn"};
    if (id == "spawns_dom") return {"mp_dom_spawn", "mp_dom_spawn", "mp_dom_spawn_allies_start", "mp_dom_spawn_axis_start"};
    if (id == "spawns_koth") return {"mp_koth_spawn", "mp_koth_spawn", "mp_koth_spawn_allies_start", "mp_koth_spawn_axis_start"};
    if (id == "spawns_ctf") return {"mp_ctf_spawn", "mp_ctf_spawn", "mp_ctf_spawn_allies_start", "mp_ctf_spawn_axis_start"};
    if (id == "spawns_sd") return {"mp_sd_spawn_attacker", "mp_sd_spawn_defender"};
    return {};
}

}  // namespace

TEST(gamemode_template_table) {
    const auto& all = gameModeTemplates();
    CHECK(all.size() == 11);
    std::set<std::string> ids;
    for (const auto& t : all) {
        CHECK(!t.id.empty() && !t.name.empty() && !t.description.empty());
        CHECK(ids.insert(t.id).second);  // unique
        CHECK(!t.entities.empty());
        CHECK(findGameModeTemplate(t.id) == &t);
        CHECK(!expectedClasses(t.id).empty());
    }
    CHECK(findGameModeTemplate("no_such_template") == nullptr);
}

TEST(gamemode_template_text_parses_with_expected_classnames) {
    for (const auto& t : gameModeTemplates()) {
        const std::string text = templateMapText(t, Vec3{1024, 512, 0}, "\r\n");
        CHECK(text.rfind("iwmap 4\r\n", 0) == 0);
        const MapFile m = parseText(text);
        CHECK(m.entities.size() == t.entities.size());
        const auto want = expectedClasses(t.id);
        CHECK(m.entities.size() == want.size());
        for (size_t i = 0; i < m.entities.size() && i < want.size(); ++i) CHECK(classOf(m.entities[i]) == want[i]);
        // Lossless: parse then serialize returns the same text.
        CHECK(m.text() == text);
    }
}

TEST(gamemode_template_brush_triggers_have_one_brush) {
    for (const auto& t : gameModeTemplates()) {
        const MapFile m = parseText(templateMapText(t, Vec3{}, "\n"));
        for (size_t i = 0; i < t.entities.size() && i < m.entities.size(); ++i) {
            const Entity& e = m.entities[i];
            if (t.entities[i].hasBrush) {
                CHECK(brushCount(e) == 1);
                CHECK(e.origin().has_value());
            } else {
                CHECK(brushCount(e) == 0);
                CHECK(e.origin().has_value());
            }
        }
    }
}

TEST(gamemode_template_dom_flags_keys) {
    const MapFile m = parseText(templateMapText(*findGameModeTemplate("dom_flags"), Vec3{}, "\n"));
    CHECK(countClass(m, "trigger_radius", "script_gameobjectname", "dom onslaught") == 3);
    CHECK(countClass(m, "trigger_radius", "script_label", "_a") == 1);
    CHECK(countClass(m, "trigger_radius", "script_label", "_c") == 1);
    CHECK(countClass(m, "trigger_radius", "targetname", "flag_primary") == 3);
    CHECK(countClass(m, "trigger_radius", "radius", "120") == 3);
}

TEST(gamemode_template_hardpoint_and_sd_keys) {
    const MapFile hp = parseText(templateMapText(*findGameModeTemplate("hardpoint_zones"), Vec3{}, "\n"));
    CHECK(countClass(hp, "trigger_multiple", "script_gameobjectname", "hardpoint") == 5);
    CHECK(countClass(hp, "trigger_multiple", "script_label", "5") == 1);
    CHECK(countClass(hp, "trigger_multiple", "targetname", "hardpoint_zone") == 5);

    const MapFile sd = parseText(templateMapText(*findGameModeTemplate("sd_sites"), Vec3{}, "\n"));
    CHECK(countClass(sd, "trigger_use_touch", "script_gameobjectname", "bombzone") == 4);  // plant + defuse per site
    CHECK(countClass(sd, "trigger_use_touch", "script_bombmode_original", "1") == 2);
    // The stock chain (research 40): plant trigger -> site crate -> defuse trigger; the destroyed crate is an exploder.
    CHECK(countClass(sd, "trigger_use_touch", "target", "sd_sitea_crate") == 1);
    CHECK(countClass(sd, "script_model", "target", "sd_sitea_defuse") == 1);
    CHECK(countClass(sd, "trigger_use_touch", "targetname", "sd_sitea_defuse") == 1);
    CHECK(countClass(sd, "script_model", "targetname", "exploder") == 2);
    CHECK(countClass(sd, "script_model", "model", "sd_equipment_bombsite_crate") == 2);
    CHECK(countClass(sd, "script_model", "model", "offhand_wm_briefcase_bomb") == 1);
    CHECK(countClass(sd, "trigger_multiple", "targetname", "sd_bomb_pickup_trig") == 1);
}

TEST(gamemode_template_ctf_axis_is_turned_and_offset) {
    const MapFile m = parseText(templateMapText(*findGameModeTemplate("ctf_flags"), Vec3{100, 200, 0}, "\n"));
    CHECK(countClass(m, "trigger_radius", "script_gameobjectname", "ctf") == 4);
    CHECK(countClass(m, "script_model", "targetname", "ctf_flag_allies") == 1);
    CHECK(countClass(m, "script_model", "targetname", "ctf_flag_axis") == 1);
    // Axis flag: 1024 units along +X from the allies flag, turned 180 degrees.
    for (const Entity& e : m.entities) {
        if (e.value("targetname") && *e.value("targetname") == "ctf_flag_axis") {
            CHECK(e.origin().has_value() && e.origin()->x == 1124 && e.origin()->y == 200 && e.origin()->z == 16);
            CHECK(e.value("angles") && *e.value("angles") == "0 180 0");
        }
        if (e.value("targetname") && *e.value("targetname") == "ctf_flag_allies") {
            CHECK(e.origin().has_value() && e.origin()->x == 100 && e.origin()->y == 200 && e.origin()->z == 16);
            CHECK(e.value("angles") == nullptr);
        }
    }
}

TEST(gamemode_template_spawn_sets_follow_gameplay_checks) {
    // gameplay_checks.cpp: TDM needs 2 mp_tdm_spawn and one allies and one axis start.
    const MapFile tdm = parseText(templateMapText(*findGameModeTemplate("spawns_tdm"), Vec3{}, "\n"));
    CHECK(countClass(tdm, "mp_tdm_spawn") == 2);
    CHECK(countClass(tdm, "mp_tdm_spawn_allies_start") == 1);
    CHECK(countClass(tdm, "mp_tdm_spawn_axis_start") == 1);
    const MapFile sd = parseText(templateMapText(*findGameModeTemplate("spawns_sd"), Vec3{}, "\n"));
    CHECK(countClass(sd, "mp_sd_spawn_attacker") == 1);
    CHECK(countClass(sd, "mp_sd_spawn_defender") == 1);
}

TEST(gamemode_template_insert_is_one_undo_step) {
    Document doc;
    CHECK(doc.loadFromText(kRoomEmpty, "room.map", nullptr));
    const std::string before = doc.text();
    const size_t pos = doc.undoPosition();
    const size_t entities0 = doc.map().entities.size();

    const GameModeTemplate* sd = findGameModeTemplate("sd_sites");
    CHECK(sd != nullptr);
    std::string err;
    const auto ids = insertGameModeTemplate(doc, *sd, Vec3{256, 256, 0}, &err);
    CHECK(err.empty());
    CHECK(ids.size() == sd->entities.size());
    CHECK(doc.map().entities.size() == entities0 + sd->entities.size());
    CHECK(doc.undoPosition() == pos + 1);
    for (Id id : ids) {
        bool selected = false;
        for (const SelRef& r : doc.selection()) selected = selected || r.entity == id;
        CHECK(selected);
    }
    CHECK(doc.text() != before);

    // The inserted text matches the template text at the origin (the entity bodies, in order).
    const std::string tpl = templateMapText(*sd, Vec3{256, 256, 0}, doc.eol());
    const MapFile tm = parseText(tpl);
    CHECK(tm.entities.size() == sd->entities.size());
    for (size_t i = 0; i < ids.size(); ++i) {
        const Entity* e = doc.findEntity(ids[i]);
        CHECK(e != nullptr);
        if (e && i < tm.entities.size()) CHECK(e->classname() == tm.entities[i].classname());
    }

    doc.undo();
    CHECK(doc.undoPosition() == pos);
    CHECK(doc.text() == before);
}

TEST(gamemode_template_insert_rejects_map_without_worldspawn) {
    Document doc;
    CHECK(doc.loadFromText("iwmap 4\r\n", "empty.map", nullptr));
    std::string err;
    const auto ids = insertGameModeTemplate(doc, *findGameModeTemplate("dom_flags"), Vec3{}, &err);
    CHECK(ids.empty());
    CHECK(!err.empty());
    CHECK(doc.undoPosition() == 0);
}

TEST(killstreak_notes_list_missing_heli_path_and_airstrike) {
    Id next = 1;
    const auto bare = parseMap("iwmap 4\n{\n\"classname\" \"worldspawn\"\n}\n", next);
    CHECK(bare.map.has_value());
    if (!bare.map) return;
    CHECK(killstreakNotes(*bare.map).size() == 2);
    const auto full = parseMap(
        "iwmap 4\n{\n\"classname\" \"worldspawn\"\n}\n"
        "{\n\"classname\" \"script_struct_heli\"\n\"targetname\" \"ks_heli_entrance\"\n\"origin\" \"0 0 900\"\n}\n"
        "{\n\"classname\" \"script_struct_heli\"\n\"targetname\" \"ks_heli_goal\"\n\"origin\" \"512 0 900\"\n}\n"
        "{\n\"classname\" \"script_origin\"\n\"targetname\" \"airstrikeheight\"\n\"origin\" \"0 0 2560\"\n}\n", next);
    CHECK(full.map.has_value());
    if (full.map) CHECK(killstreakNotes(*full.map).empty());
}
