// Unlit baseline (roadmap 1.8): checkUnlitBaseline rules on small in-memory maps. Evidence: docs/research/55_unlit_baseline.md.
#include <string>
#include <utility>
#include <vector>

#include "map_text.h"
#include "test_harness.h"
#include "unlit_baseline.h"

using namespace iw8r;

namespace {

using KV = std::vector<std::pair<std::string, std::string>>;

std::string entityText(size_t index, const KV& kv) {
    std::string t = "// entity " + std::to_string(index) + "\n{\n";
    for (const auto& [k, v] : kv) t += "\"" + k + "\" \"" + v + "\"\n";
    return t + "}\n";
}

MapFile mapWith(const std::vector<KV>& entities) {
    std::string text = "iwmap 4\n";
    for (size_t i = 0; i < entities.size(); ++i) text += entityText(i, entities[i]);
    Id next = 1;
    ParseResult r = parseMap(text, next);
    CHECK(r.map.has_value());
    return r.map ? *r.map : MapFile{};
}

// A worldspawn with the given extra keys (classname is added).
KV worldspawn(const KV& extra = {}) {
    KV kv = {{"classname", "worldspawn"}};
    kv.insert(kv.end(), extra.begin(), extra.end());
    return kv;
}

KV lightEntity() { return {{"classname", "light"}, {"origin", "0 0 100"}}; }

int errorCount(const std::vector<UnlitBaselineIssue>& v) {
    int n = 0;
    for (const auto& i : v)
        if (i.error) ++n;
    return n;
}

int warningCount(const std::vector<UnlitBaselineIssue>& v) {
    int n = 0;
    for (const auto& i : v)
        if (!i.error) ++n;
    return n;
}

bool anyContains(const std::vector<UnlitBaselineIssue>& v, const std::string& needle, bool error) {
    for (const auto& i : v)
        if (i.error == error && i.message.find(needle) != std::string::npos) return true;
    return false;
}

}  // namespace

TEST(unlit_default_sun_is_clean) {
    const auto issues = checkUnlitBaseline(mapWith({worldspawn({{"sundirection", "-50 45 0"}, {"suncolor", "1 1 1"}})}));
    CHECK(errorCount(issues) == 0);
    CHECK(warningCount(issues) == 0);
}

TEST(unlit_stock_mp_shipment_values_have_no_error) {
    // Stock values from the mp_shipment worldspawn (doc 39): a lit sun above the horizon, no error.
    const auto issues = checkUnlitBaseline(mapWith(
        {worldspawn({{"sundirection", "-20.261 -15.4 0"}, {"suncolor", "1 0.9764 0.945"}, {"sunlight", "27.118"}})}));
    CHECK(errorCount(issues) == 0);
    CHECK(warningCount(issues) == 0);
}

TEST(unlit_no_worldspawn_warns_only) {
    const auto empty = checkUnlitBaseline(mapWith({}));
    CHECK(errorCount(empty) == 0);
    CHECK(anyContains(empty, "no worldspawn", false));

    const auto lightOnly = checkUnlitBaseline(mapWith({lightEntity()}));
    CHECK(errorCount(lightOnly) == 0);
}

TEST(unlit_missing_sun_keys_warn_only) {
    // The editor writes the default sun when the keys are absent, so the map is lit: warnings, no error.
    const auto issues = checkUnlitBaseline(mapWith({worldspawn()}));
    CHECK(errorCount(issues) == 0);
    CHECK(anyContains(issues, "no sundirection key", false));
    CHECK(anyContains(issues, "no suncolor key", false));
}

TEST(unlit_black_suncolor_is_error) {
    const auto noLights = checkUnlitBaseline(mapWith({worldspawn({{"suncolor", "0 0 0"}})}));
    CHECK(errorCount(noLights) == 2);  // black sun, and no light entity either
    CHECK(anyContains(noLights, "is black", true));
    CHECK(anyContains(noLights, "no light entities", true));
}

TEST(unlit_black_suncolor_with_light_is_single_error) {
    const auto issues = checkUnlitBaseline(mapWith({worldspawn({{"suncolor", "0 0 0"}}), lightEntity()}));
    CHECK(errorCount(issues) == 1);
    CHECK(anyContains(issues, "is black", true));
}

TEST(unlit_negative_suncolor_clamps_to_black) {
    // compile_iw8.cpp:1348 clamps negative components to 0, so the sun is black.
    const auto issues = checkUnlitBaseline(mapWith({worldspawn({{"suncolor", "-1 -0.5 -2"}}), lightEntity()}));
    CHECK(errorCount(issues) == 1);
    CHECK(anyContains(issues, "is black", true));
}

TEST(unlit_suncolor_on_255_scale_is_white) {
    // Values above 1 are read as 0-255 (compile_iw8.cpp:1345-1347): 255 255 255 is full white, no warning.
    const auto issues = checkUnlitBaseline(mapWith({worldspawn({{"sundirection", "-50 45 0"}, {"suncolor", "255 255 255"}})}));
    CHECK(errorCount(issues) == 0);
    CHECK(warningCount(issues) == 0);
}

TEST(unlit_dim_suncolor_warns) {
    const auto issues = checkUnlitBaseline(mapWith({worldspawn({{"suncolor", "0.1 0.1 0.1"}}), lightEntity()}));
    CHECK(errorCount(issues) == 0);
    CHECK(anyContains(issues, "is dim", false) || anyContains(issues, "below 0.25", false));
}

TEST(unlit_luminance_threshold_is_exclusive) {
    // 0.3 grey has luminance 0.3, above the 0.25 threshold: no dim warning.
    const auto issues = checkUnlitBaseline(mapWith({worldspawn({{"sundirection", "-50 45 0"}, {"suncolor", "0.3 0.3 0.3"}})}));
    CHECK(issues.empty());
}

TEST(unlit_sun_below_horizon_is_error) {
    const auto withLight = checkUnlitBaseline(mapWith({worldspawn({{"sundirection", "10 45 0"}, {"suncolor", "1 1 1"}}), lightEntity()}));
    CHECK(errorCount(withLight) == 1);
    CHECK(anyContains(withLight, "positive pitch", true));

    const auto noLight = checkUnlitBaseline(mapWith({worldspawn({{"sundirection", "10 45 0"}, {"suncolor", "1 1 1"}})}));
    CHECK(errorCount(noLight) == 2);  // below the horizon, and no light entity either
    CHECK(anyContains(noLight, "no light entities", true));
}

TEST(unlit_horizontal_sun_warns) {
    const auto zero = checkUnlitBaseline(mapWith({worldspawn({{"sundirection", "0 45 0"}, {"suncolor", "1 1 1"}})}));
    CHECK(errorCount(zero) == 0);
    CHECK(anyContains(zero, "nearly horizontal", false));

    // -10 is the inclusive edge of the warning band.
    const auto edge = checkUnlitBaseline(mapWith({worldspawn({{"sundirection", "-10 45 0"}, {"suncolor", "1 1 1"}})}));
    CHECK(errorCount(edge) == 0);
    CHECK(anyContains(edge, "nearly horizontal", false));

    const auto steeper = checkUnlitBaseline(mapWith({worldspawn({{"sundirection", "-10.5 45 0"}, {"suncolor", "1 1 1"}})}));
    CHECK(steeper.empty());
}

TEST(unlit_straight_down_sun_is_clean) {
    const auto issues = checkUnlitBaseline(mapWith({worldspawn({{"sundirection", "-90 0 0"}, {"suncolor", "1 1 1"}})}));
    CHECK(issues.empty());
}

TEST(unlit_unparsed_sundirection_warns) {
    const auto issues = checkUnlitBaseline(mapWith({worldspawn({{"sundirection", "abc"}, {"suncolor", "1 1 1"}})}));
    CHECK(errorCount(issues) == 0);
    CHECK(anyContains(issues, "not three numbers", false));
}

TEST(unlit_unparsed_suncolor_warns) {
    const auto issues = checkUnlitBaseline(mapWith({worldspawn({{"sundirection", "-50 45 0"}, {"suncolor", "red"}})}));
    CHECK(errorCount(issues) == 0);
    CHECK(anyContains(issues, "not three numbers", false));
}

TEST(unlit_ignored_keys_do_not_change_result) {
    // sunlight, csm_* and ambient-like keys are not read by the editor's extraction (doc 55), so they do not flag or fix
    // a dark sun.
    const auto issues = checkUnlitBaseline(mapWith({worldspawn({{"sundirection", "-50 45 0"},
                                                               {"suncolor", "1 1 1"},
                                                               {"sunlight", "0"},
                                                               {"csm_enable", "0"},
                                                               {"ambient", "0 0 0"}})}));
    CHECK(issues.empty());
}

TEST(unlit_black_sun_no_light_message_is_error_text) {
    // A black sun on a map with light entities is an error (the sun adds nothing) but not the no-light error.
    const auto issues = checkUnlitBaseline(mapWith({worldspawn({{"suncolor", "0 0 0"}}), lightEntity(), lightEntity()}));
    CHECK(!anyContains(issues, "no light entities", true));
}
