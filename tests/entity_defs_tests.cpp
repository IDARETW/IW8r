// Entity definitions (src/core/entity_defs.*): the real data file loads, the stock classes are covered,
// and validate() reports missing required keys, bad value types and classes the build drops.
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "entity_defs.h"
#include "json_lite.h"
#include "test_harness.h"

using namespace iw8r;
namespace fs = std::filesystem;

namespace {

// Finds a repo-relative file by walking up from the working directory (tests run from the repo or from bin/<mode>).
std::string repoFile(const std::string& rel) {
    fs::path p = fs::current_path();
    for (int i = 0; i < 8; ++i) {
        if (fs::exists(p / rel)) return (p / rel).string();
        if (!p.has_parent_path() || p.parent_path() == p) break;
        p = p.parent_path();
    }
    return rel;
}

// The definitions, loaded once from the real data file.
const EntityDefs& defs() {
    static EntityDefs d;
    static const bool loaded = [] {
        std::string err;
        const bool ok = d.load(repoFile("data/iw8_entity_defs.json"), &err);
        if (!ok) std::printf("  load failed: %s\n", err.c_str());
        return ok;
    }();
    (void)loaded;
    return d;
}

Entity makeEntity(std::initializer_list<std::pair<const char*, const char*>> kvs) {
    Entity e;
    for (const auto& [k, v] : kvs) {
        EntityItem it;
        it.v = makeKeyValue(k, v, "\n");
        e.items.push_back(std::move(it));
    }
    return e;
}

bool hasMsg(const std::vector<std::string>& msgs, const std::string& needle) {
    for (const auto& m : msgs)
        if (m.find(needle) != std::string::npos) return true;
    return false;
}

}  // namespace

TEST(entity_defs_loads_real_data) {
    CHECK(defs().classNames().size() >= 46);
    const auto names = defs().classNames();
    CHECK(std::is_sorted(names.begin(), names.end()));
}

TEST(entity_defs_mp_tdm_spawn_box_and_keys) {
    const ClassDef* c = defs().find("mp_tdm_spawn");
    CHECK(c != nullptr);
    if (!c) return;
    CHECK(c->kind == "point");
    CHECK(c->box.mins.x == -16 && c->box.mins.y == -16 && c->box.mins.z == 0);
    CHECK(c->box.maxs.x == 16 && c->box.maxs.y == 16 && c->box.maxs.z == 72);
    CHECK(c->buildAccepted);
    const KeyDef* origin = c->key("origin");
    const KeyDef* angles = c->key("angles");
    CHECK(origin && origin->type == "vec3" && origin->required);
    CHECK(angles && angles->type == "angles");
}

TEST(entity_defs_worldspawn_exists) {
    const ClassDef* c = defs().find("worldspawn");
    CHECK(c != nullptr);
    if (c) CHECK(c->kind == "brush");
}

TEST(entity_defs_validate_flags_spawn_without_origin) {
    const Entity e = makeEntity({{"classname", "mp_tdm_spawn"}, {"angles", "0 90 0"}});
    const auto msgs = defs().validate(e);
    CHECK(hasMsg(msgs, "missing required key 'origin'"));
}

TEST(entity_defs_validate_flags_bad_vec3_on_script_model) {
    const Entity bad = makeEntity({{"classname", "script_model"}, {"model", "metal_chain_01_wrapped"}, {"origin", "1 2"}});
    const auto msgs = defs().validate(bad);
    CHECK(hasMsg(msgs, "'origin'") && hasMsg(msgs, "vec3"));

    const Entity good = makeEntity({{"classname", "script_model"}, {"model", "metal_chain_01_wrapped"}, {"origin", "1 2 3"}});
    CHECK(defs().validate(good).empty());
}

TEST(entity_defs_validate_flags_a_class_the_build_drops) {
    // export_volume (an infil animation volume) is not accepted by the converter (research 42); reflection probes are
    // built (world.json probes) and must not be flagged.
    const Entity e = makeEntity({{"classname", "export_volume"}, {"origin", "0 0 0"}});
    CHECK(hasMsg(defs().validate(e), "not built"));
    const ClassDef* c = defs().find("export_volume");
    CHECK(c != nullptr && !c->buildAccepted);
    const ClassDef* probe = defs().find("reflection_probe");
    CHECK(probe != nullptr && probe->buildAccepted);
}

TEST(entity_defs_validate_checks_int_float_bool_and_model_index) {
    const Entity hurt = makeEntity({{"classname", "trigger_hurt"}, {"dmg", "lots"}, {"model", "*4"}});
    const auto hurtMsgs = defs().validate(hurt);
    CHECK(hasMsg(hurtMsgs, "'dmg'") && hasMsg(hurtMsgs, "integer"));
    CHECK(!hasMsg(hurtMsgs, "'model'"));

    const Entity radius = makeEntity({{"classname", "trigger_radius"}, {"origin", "0 0 0"}, {"radius", "big"}, {"nophysics", "maybe"}});
    const auto radiusMsgs = defs().validate(radius);
    CHECK(hasMsg(radiusMsgs, "'radius'") && hasMsg(radiusMsgs, "number"));
    CHECK(hasMsg(radiusMsgs, "'nophysics'") && hasMsg(radiusMsgs, "bool"));
}

TEST(entity_defs_every_stock_class_present) {
    std::ifstream f(repoFile("data/iw8_entity_classes.json"), std::ios::binary);
    CHECK(static_cast<bool>(f));
    if (!f) return;
    std::stringstream ss;
    ss << f.rdbuf();
    std::string err;
    const auto root = json::parse(ss.str(), &err);
    CHECK(root.has_value());
    if (!root) return;
    const json::Value* classes = root->get("classes");
    CHECK(classes != nullptr);
    if (!classes) return;
    int missing = 0;
    for (const auto& [name, unused] : classes->obj) {
        (void)unused;
        if (!defs().find(name)) {
            std::printf("  missing stock class: %s\n", name.c_str());
            ++missing;
        }
    }
    CHECK(missing == 0);
}

TEST(entity_defs_value_types_and_colours_are_valid) {
    const std::set<std::string> types = {"string", "int", "float", "vec3", "angles", "bool", "targetname", "model", "material"};
    int badTypes = 0, badColours = 0;
    for (const std::string& n : defs().classNames()) {
        const ClassDef* c = defs().find(n);
        for (double v : c->color)
            if (v < 0 || v > 1) ++badColours;
        if (c->kind != "point" && c->kind != "brush") ++badColours;
        for (const KeyDef& k : c->keys)
            if (!types.count(k.type)) ++badTypes;
    }
    CHECK(badTypes == 0);
    CHECK(badColours == 0);
}
