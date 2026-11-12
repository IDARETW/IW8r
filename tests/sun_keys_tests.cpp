// Sun and shadow worldspawn keys (roadmap 5.16): the key table and checkSunKeys warnings.
#include <string>
#include <vector>

#include "map_text.h"
#include "sun_keys.h"
#include "test_harness.h"

using namespace iw8r;

namespace {

// Parses a one-entity map whose only entity is the worldspawn with the given key/value pairs.
Entity worldspawnWith(const std::vector<std::pair<std::string, std::string>>& kv) {
    std::string text = "iwmap 4\n// entity 0\n{\n\"classname\" \"worldspawn\"\n";
    for (const auto& [k, v] : kv) text += "\"" + k + "\" \"" + v + "\"\n";
    text += "}\n";
    Id next = 1;
    ParseResult pr = parseMap(text, next);
    if (!pr.map || pr.map->entities.empty()) return Entity{};
    return pr.map->entities[0];
}

bool anyContains(const std::vector<std::string>& warns, const std::string& needle) {
    for (const std::string& w : warns)
        if (w.find(needle) != std::string::npos) return true;
    return false;
}

}  // namespace

TEST(sun_key_table_shape) {
    const auto& defs = sunKeyDefs();
    CHECK(defs.size() == 8);
    for (size_t i = 0; i < defs.size(); ++i)
        for (size_t j = i + 1; j < defs.size(); ++j) CHECK(defs[i].key != defs[j].key);
    for (const SunKeyDef& d : defs) {
        CHECK(!d.key.empty());
        CHECK(!d.help.empty());
    }
}

TEST(sun_key_pass_through_flags) {
    // Only the two keys the Sun and Sky dialog writes reach the built entity string (compile_iw8.cpp worldspawn wkv).
    for (const SunKeyDef& d : sunKeyDefs()) {
        const bool expected = d.key == "sundirection" || d.key == "suncolor";
        CHECK(d.passedThrough == expected);
    }
}

TEST(sun_key_stock_defaults_match_mp_shipment) {
    const auto& defs = sunKeyDefs();
    auto find = [&](const std::string& key) -> const SunKeyDef* {
        for (const SunKeyDef& d : defs)
            if (d.key == key) return &d;
        return nullptr;
    };
    const SunKeyDef* sd = find("sundirection");
    const SunKeyDef* sc = find("suncolor");
    const SunKeyDef* sl = find("sunlight");
    const SunKeyDef* en = find("csm_enable");
    const SunKeyDef* res = find("csm_max_resolution");
    const SunKeyDef* smp = find("csm_sample_size");
    CHECK(sd && sd->stockDefault == "-20.261 -15.4 0");
    CHECK(sc && sc->stockDefault == "1 0.9764 0.945");
    CHECK(sl && sl->stockDefault == "27.118");
    CHECK(en && en->stockDefault == "1");
    CHECK(res && res->stockDefault == "256");
    CHECK(smp && smp->stockDefault == "2");
}

TEST(sun_key_stock_values_pass_checks) {
    // Stock mp_shipment values: no range or parse warnings. The only warnings are for keys the build ignores.
    Entity ws = worldspawnWith({{"sundirection", "-20.261 -15.4 0"},
                                {"suncolor", "1 0.9764 0.945"},
                                {"sunlight", "27.118"},
                                {"csm_enable", "1"},
                                {"csm_max_resolution", "256"},
                                {"csm_sample_size", "2"}});
    const auto warns = checkSunKeys(ws);
    CHECK(!anyContains(warns, "out of range"));
    CHECK(!anyContains(warns, "not three numbers"));
    CHECK(!anyContains(warns, "below the horizon"));
    // Value warnings quote the key ("sundirection '...'"); the "not written" text also names both keys, so match the quote.
    CHECK(!anyContains(warns, "sundirection '"));
    CHECK(!anyContains(warns, "suncolor '"));
    CHECK(anyContains(warns, "sunlight is not written"));
    CHECK(anyContains(warns, "csm_enable is not written"));
    CHECK(anyContains(warns, "csm_max_resolution is not written"));
    CHECK(anyContains(warns, "csm_sample_size is not written"));
}

TEST(sun_key_absent_keys_no_warnings) {
    Entity ws = worldspawnWith({{"skyname", "sky_mp_test"}});
    CHECK(checkSunKeys(ws).empty());
    Entity empty;
    CHECK(checkSunKeys(empty).empty());
}

TEST(sun_key_sundirection_warnings) {
    CHECK(anyContains(checkSunKeys(worldspawnWith({{"sundirection", "30 0 0"}})), "positive pitch"));
    CHECK(anyContains(checkSunKeys(worldspawnWith({{"sundirection", "-95 0 0"}})), "out of range"));
    CHECK(checkSunKeys(worldspawnWith({{"sundirection", "-45 720 0"}})).empty());  // yaw is not bounded
    CHECK(anyContains(checkSunKeys(worldspawnWith({{"sundirection", "-45 0"}})), "not three numbers"));
    CHECK(anyContains(checkSunKeys(worldspawnWith({{"sundirection", "a b c"}})), "not three numbers"));
    CHECK(checkSunKeys(worldspawnWith({{"sundirection", "0 0 0"}})).empty());  // horizon is allowed
}

TEST(sun_key_suncolor_warnings) {
    CHECK(anyContains(checkSunKeys(worldspawnWith({{"suncolor", "300 1 1"}})), "is above 1"));
    CHECK(anyContains(checkSunKeys(worldspawnWith({{"suncolor", "-0.1 1 1"}})), "out of range"));
    CHECK(anyContains(checkSunKeys(worldspawnWith({{"suncolor", "1 1"}})), "not three numbers"));
    CHECK(checkSunKeys(worldspawnWith({{"suncolor", "0.2 0.4 0.6"}})).empty());
}

TEST(sun_key_csm_value_checks) {
    CHECK(anyContains(checkSunKeys(worldspawnWith({{"csm_enable", "2"}})), "is not 0, 1, true or false"));
    CHECK(anyContains(checkSunKeys(worldspawnWith({{"csm_enable", "true"}})), "is not written"));
    CHECK(anyContains(checkSunKeys(worldspawnWith({{"csm_max_resolution", "2.5"}})), "is not a whole number"));
    CHECK(anyContains(checkSunKeys(worldspawnWith({{"csm_sample_size", "x"}})), "is not a whole number"));
}

TEST(sun_key_sunlight_and_bounds_warnings) {
    CHECK(anyContains(checkSunKeys(worldspawnWith({{"sunlight", "-5"}})), "out of range"));
    CHECK(anyContains(checkSunKeys(worldspawnWith({{"sunlight", "bright"}})), "is not one number"));
    CHECK(anyContains(checkSunKeys(worldspawnWith({{"_csm_bound_halfsize", "-1 2 3"}})), "out of range"));
    CHECK(anyContains(checkSunKeys(worldspawnWith({{"_csm_bound_halfsize", "1 2"}})), "not three numbers"));
    // Centre has no verified bounds: a negative component is fine.
    CHECK(!anyContains(checkSunKeys(worldspawnWith({{"_csm_bound_center", "-605.6 44.5 -110.5"}})), "out of range"));
}
