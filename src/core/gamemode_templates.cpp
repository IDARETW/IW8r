// IW8 Radiant Suite - game-mode object templates (roadmap 6.2). See gamemode_templates.h and docs/research/32_gamemode_objects.md.
#include "gamemode_templates.h"

#include "brush_ops.h"
#include "map_text.h"

namespace iw8r {

namespace {

using KV = std::pair<std::string, std::string>;

// Trigger face suffix: the "trigger" tool material, as brush_flags_tests and the trigger hull path expect.
const char* kTriggerSuffix = " trigger 128 128 0 0 0 0";

TemplateEntity point(const std::string& cls, Vec3 offset, std::vector<KV> keys) {
    TemplateEntity e;
    e.classname = cls;
    e.offset = offset;
    e.keys = std::move(keys);
    return e;
}

TemplateEntity brushTrigger(const std::string& cls, const AABB& box, std::vector<KV> keys) {
    TemplateEntity e;
    e.classname = cls;
    e.hasBrush = true;
    e.brush = box;
    e.keys = std::move(keys);
    return e;
}

AABB boxAt(double x, double y, double halfX, double halfY, double z0, double z1) {
    return AABB{{x - halfX, y - halfY, z0}, {x + halfX, y + halfY, z1}};
}

std::vector<GameModeTemplate> buildTemplates() {
    std::vector<GameModeTemplate> out;

    // Domination: three flags, each a radius trigger (stock values: radius 120, height 128, nophysics). The flag prop
    // is UNKNOWN in the stock zones, so it is not written. Spacing is a layout choice.
    {
        GameModeTemplate t;
        t.id = "dom_flags";
        t.name = "Domination Flags (3)";
        t.description = "Three Domination capture triggers (script_gameobjectname \"dom onslaught\", labels _a/_b/_c).";
        const char* labels[] = {"_a", "_b", "_c"};
        for (int i = 0; i < 3; ++i)
            t.entities.push_back(point("trigger_radius", {384.0 * i, 0, 32},
                                       {{"footprint_mask_group", "default"}, {"height", "128"}, {"nophysics", "true"},
                                        {"radius", "120"}, {"script_gameobjectname", "dom onslaught"},
                                        {"script_label", labels[i]}, {"targetname", "flag_primary"}}));
        out.push_back(t);
    }

    // Hardpoint: five zone brushes (stock shipment has five; backlot2 has thirteen). Zone volume is a placeholder box.
    {
        GameModeTemplate t;
        t.id = "hardpoint_zones";
        t.name = "Hardpoint Zones (5)";
        t.description = "Five Hardpoint zone trigger brushes (script_gameobjectname \"hardpoint\", labels 1-5). Resize the boxes.";
        for (int i = 0; i < 5; ++i) {
            const double x = 512.0 * i;
            t.entities.push_back(brushTrigger("trigger_multiple", boxAt(x, 0, 96, 96, 0, 128),
                                              {{"footprint_mask_group", "default"}, {"script_gameobjectname", "hardpoint"},
                                               {"script_label", std::to_string(i + 1)}, {"targetname", "hardpoint_zone"}}));
        }
        out.push_back(t);
    }

    // Search & Destroy: two labelled bomb sites (trigger_use_touch brushes), the bomb and its pickup trigger.
    {
        GameModeTemplate t;
        t.id = "sd_sites";
        t.name = "Search & Destroy Sites and Bomb";
        t.description = "Sites A and B laid out as on stock mp_shipment (plant trigger -> site crate -> defuse trigger, plus the "
                        "destroyed crate as an exploder), the bomb model (script_gameobjectname \"sd\") and its pickup trigger.";
        // Per site, as stock (research 40, mp_shipment pf44 / pf46): the plant trigger targets the crate, the crate
        // targets the defuse trigger (obj_bombzone.gsc reads that chain), and the destroyed crate (targetname exploder)
        // shares the crate's script_exploder.
        const auto site = [&t](const char* label, const char* tag, double x) {
            const std::string crate = std::string("sd_site") + tag + "_crate", defuse = std::string("sd_site") + tag + "_defuse",
                              boom = std::string("sd_site") + tag + "_boom";
            t.entities.push_back(brushTrigger("trigger_use_touch", boxAt(x, 0, 80, 80, 0, 96),
                                              {{"footprint_mask_group", "default"}, {"script_bombmode_original", "1"},
                                               {"script_gameobjectname", "bombzone"}, {"script_label", label},
                                               {"target", crate}, {"targetname", "bombzone"}}));
            t.entities.push_back(brushTrigger("trigger_use_touch", boxAt(x, 0, 80, 80, 0, 96),
                                              {{"footprint_mask_group", "default"}, {"script_gameobjectname", "bombzone"},
                                               {"targetname", defuse}}));
            t.entities.push_back(point("script_model", {x, 0, 12},
                                       {{"angles", "0 135 0"}, {"footprint_mask_group", "default"},
                                        {"model", "sd_equipment_bombsite_crate"}, {"script_exploder", boom},
                                        {"script_gameobjectname", "bombzone"}, {"spawnflags", "4"}, {"target", defuse},
                                        {"targetname", crate}}));
            t.entities.push_back(point("script_model", {x, 0, 12},
                                       {{"angles", "0 135 0"}, {"footprint_mask_group", "default"},
                                        {"model", "sd_equipment_bombsite_crate_d"}, {"script_exploder", boom},
                                        {"script_gameobjectname", "bombzone"}, {"spawnflags", "4"}, {"targetname", "exploder"}}));
        };
        site("_a", "a", 0);
        site("_b", "b", 1024);
        t.entities.push_back(point("script_model", {512, 0, 16},
                                   {{"angles", "0 45 0"}, {"footprint_mask_group", "default"},
                                    {"model", "offhand_wm_briefcase_bomb"}, {"script_gameobjectname", "sd"},
                                    {"spawnflags", "4"}, {"targetname", "sd_bomb"}}));
        t.entities.push_back(brushTrigger("trigger_multiple", boxAt(512, 0, 48, 48, 0, 64),
                                          {{"footprint_mask_group", "default"}, {"script_gameobjectname", "sd"},
                                           {"targetname", "sd_bomb_pickup_trig"}}));
        out.push_back(t);
    }

    // Capture the Flag: per team a flag model, a pickup radius trigger and a capture zone (stock: radius 64, height 128).
    // The axis team is the allies set turned 180 degrees and placed 1024 units along +X.
    {
        GameModeTemplate t;
        t.id = "ctf_flags";
        t.name = "Capture the Flag (2 teams)";
        t.description = "Allied and axis flag, pickup trigger and capture zone (script_gameobjectname \"ctf\").";
        for (int team = 0; team < 2; ++team) {
            const bool axis = team == 1;
            const std::string side = axis ? "axis" : "allies";
            const double x = axis ? 1024.0 : 0.0;
            const std::string angles = axis ? "0 180 0" : "";
            auto withAngles = [&](std::vector<KV> keys) {
                if (axis) keys.push_back({"angles", angles});
                return keys;
            };
            t.entities.push_back(point("trigger_radius", {x, 0, 32},
                                       withAngles({{"footprint_mask_group", "default"}, {"height", "128"},
                                                   {"radius", "64"}, {"script_gameobjectname", "ctf"},
                                                   {"target", "ctf_flag_" + side}, {"targetname", "ctf_trig_" + side}})));
            t.entities.push_back(point("script_model", {x, 0, 16},
                                       withAngles({{"footprint_mask_group", "default"}, {"model", "highway_flag0"},
                                                   {"script_gameobjectname", "ctf"}, {"spawnflags", "4"},
                                                   {"targetname", "ctf_flag_" + side}})));
            t.entities.push_back(point("trigger_radius", {x, 0, 32},
                                       withAngles({{"footprint_mask_group", "default"}, {"height", "128"},
                                                   {"radius", "64"}, {"script_gameobjectname", "ctf"},
                                                   {"targetname", "ctf_zone_" + side}})));
        }
        out.push_back(t);
    }

    // Spawn sets: mp_*_spawn* entities (replay_spawns.h / doc 26 section 3 route into spawnList). Counts follow
    // gameplay_checks.cpp (TDM: 2 generic + 1 allies start + 1 axis start; DOM and CTF: same pattern). Positions are a layout
    // choice. Angles: allies face +X, axis face -X.
    auto spawnSet = [&](const std::string& id, const std::string& name, const std::string& desc, const std::string& generic,
                        const std::string& allies, const std::string& axis, bool genericSpawns) {
        GameModeTemplate t;
        t.id = id;
        t.name = name;
        t.description = desc;
        if (genericSpawns) {
            t.entities.push_back(point(generic, {-256, 0, 0}, {{"angles", "0 0 0"}, {"footprint_mask_group", "default"}}));
            t.entities.push_back(point(generic, {256, 0, 0}, {{"angles", "0 180 0"}, {"footprint_mask_group", "default"}}));
        }
        t.entities.push_back(point(allies, {-512, 0, 0}, {{"angles", "0 0 0"}, {"footprint_mask_group", "default"}}));
        t.entities.push_back(point(axis, {512, 0, 0}, {{"angles", "0 180 0"}, {"footprint_mask_group", "default"}}));
        out.push_back(t);
    };
    spawnSet("spawns_tdm", "Spawn Set: TDM", "mp_tdm_spawn x2 plus one allies and one axis start.", "mp_tdm_spawn",
             "mp_tdm_spawn_allies_start", "mp_tdm_spawn_axis_start", true);
    spawnSet("spawns_dom", "Spawn Set: Domination", "mp_dom_spawn x2 plus one allies and one axis start.", "mp_dom_spawn",
             "mp_dom_spawn_allies_start", "mp_dom_spawn_axis_start", true);
    spawnSet("spawns_ctf", "Spawn Set: CTF", "mp_ctf_spawn x2 plus one allies and one axis start.", "mp_ctf_spawn",
             "mp_ctf_spawn_allies_start", "mp_ctf_spawn_axis_start", true);
    spawnSet("spawns_sd", "Spawn Set: Search & Destroy", "One attacker and one defender spawn.", "",
             "mp_sd_spawn_attacker", "mp_sd_spawn_defender", false);

    // Match cameras (6.19, research 49): the intermission point (required: playerlogic.gsc errors without it) and the
    // pre-match camera struct, keyed exactly as stock mp_shipment's (targetname camera_intro, a target, green colour).
    {
        GameModeTemplate t;
        t.id = "match_cameras";
        t.name = "Match Cameras";
        t.description = "Intermission camera and the pre-match camera struct (as stock); the build adds minimap corners when there are none.";
        t.entities.push_back(point("mp_global_intermission", {0, 0, 64}, {{"angles", "0 0 0"}, {"footprint_mask_group", "default"}}));
        t.entities.push_back(point("script_struct", {-256, 0, 96},
                                   {{"_color", "0 .5 0"}, {"angles", "10 0 0"}, {"target", "camera_intro_path"}, {"targetname", "camera_intro"}}));
        t.entities.push_back(point("script_struct", {256, 0, 128}, {{"angles", "0 180 0"}, {"targetname", "camera_intro_path"}}));
        out.push_back(t);
    }

    return out;
}

// Keys and classnames must not carry the quote character or a line break (the iwmap text would break).
bool clean(const std::string& s) { return s.find_first_of("\"\r\n") == std::string::npos; }

}  // namespace

const std::vector<GameModeTemplate>& gameModeTemplates() {
    static const std::vector<GameModeTemplate> templates = buildTemplates();
    return templates;
}

const GameModeTemplate* findGameModeTemplate(const std::string& id) {
    for (const GameModeTemplate& t : gameModeTemplates())
        if (t.id == id) return &t;
    return nullptr;
}

std::string templateMapText(const GameModeTemplate& t, const Vec3& origin, const std::string& eol) {
    std::string s = "iwmap 4" + eol;
    int index = 0;
    for (const TemplateEntity& e : t.entities) {
        s += "// entity " + std::to_string(index++) + eol;
        s += "{" + eol;
        s += "\"classname\" \"" + e.classname + "\"" + eol;
        if (e.hasBrush) {
            const AABB box{origin + e.brush.mins, origin + e.brush.maxs};
            const Vec3 c = box.center();
            s += "\"origin\" \"" + formatNumber(c.x) + " " + formatNumber(c.y) + " " + formatNumber(c.z) + "\"" + eol;
        } else {
            const Vec3 p = origin + e.offset;
            s += "\"origin\" \"" + formatNumber(p.x) + " " + formatNumber(p.y) + " " + formatNumber(p.z) + "\"" + eol;
        }
        for (const auto& [k, v] : e.keys) s += "\"" + k + "\" \"" + v + "\"" + eol;
        if (e.hasBrush) {
            const AABB box{origin + e.brush.mins, origin + e.brush.maxs};
            if (auto prim = makeBrushPrimitive(makeBox(box, kTriggerSuffix), eol)) s += prim->text();
        }
        s += "}" + eol;
    }
    return s;
}

std::vector<Id> insertGameModeTemplate(Document& doc, const GameModeTemplate& t, const Vec3& origin, std::string* err) {
    auto fail = [&](const std::string& msg) {
        if (err) *err = msg;
        return std::vector<Id>{};
    };
    if (doc.worldspawn() == nullptr) return fail("map has no worldspawn");
    for (const TemplateEntity& e : t.entities)
        if (!clean(e.classname)) return fail("bad classname in template " + t.id);

    // Parse the template text (ids from a local counter; the document assigns its own ids on insert).
    Id next = 1;
    ParseResult pr = parseMap(templateMapText(t, origin, doc.eol()), next);
    if (!pr.map) return fail("template " + t.id + " does not parse");

    struct Pending {
        std::vector<std::pair<std::string, std::string>> kvs;
        std::vector<Primitive> prims;
    };
    std::vector<Pending> pending;
    for (const Entity& e : pr.map->entities) {
        Pending p;
        for (const auto& it : e.items) {
            if (const KeyValueLine* kv = it.kv()) p.kvs.push_back({kv->key, kv->value});
            if (const Primitive* prim = it.prim()) p.prims.push_back(*prim);
        }
        pending.push_back(std::move(p));
    }

    const size_t before = doc.undoPosition();
    doc.beginGroup("Insert Game Mode: " + t.name);
    std::vector<Id> ids;
    for (auto& p : pending) ids.push_back(doc.addEntity(p.kvs, std::move(p.prims), "Insert Game Mode"));
    doc.endGroup();

    // The new entities stay selected (Document::addEntity selects each one in turn; keep them all).
    doc.deselectAll();
    for (Id id : ids)
        if (id != kNoId)
            for (const SelRef& r : doc.selectionUnit({id, kNoId})) doc.select(r, true);

    if (doc.undoPosition() != before + 1) {
        if (err) *err = "template " + t.id + " did not form one undo step";
        return ids;
    }
    return ids;
}

}  // namespace iw8r
