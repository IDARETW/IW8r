// Time-of-day presets (5.19): valid values, one undo step, keep-sky option.
#include <cstdio>
#include <string>

#include "atmosphere.h"
#include "document.h"
#include "test_harness.h"
#include "tod_presets.h"

using namespace iw8r;

namespace {
const char* kMap =
    "iwmap 4\n"
    "// entity 0\n"
    "{\n"
    "\"classname\" \"worldspawn\"\n"
    "\"skyname\" \"sky_custom\"\n"
    "\"iw8r_fog\" \"10 20 0.1 0.2 0.3\"\n"
    "}\n";
}  // namespace

TEST(tod_presets_values_parse) {
    CHECK(todPresets().size() >= 5);
    for (const TodPreset& p : todPresets()) {
        bool sun = false, sky = false;
        for (const auto& [k, v] : p.keys) {
            if (k == kFogKey && !v.empty()) CHECK(parseFog(v).has_value());
            if (k == kFilmKey && !v.empty()) CHECK(parseFilm(v).has_value());
            if (k == "sundirection") {
                double a = 0, b = 0, c = 0;
                sun = std::sscanf(v.c_str(), "%lf %lf %lf", &a, &b, &c) == 3 && a < 0;  // sun above the horizon
            }
            if (k == "skyname") sky = v.rfind("sky_mp_", 0) == 0;
        }
        CHECK(sun);
        CHECK(sky);
    }
    CHECK(findTodPreset("night") != nullptr && findTodPreset("nope") == nullptr);
}

TEST(tod_preset_is_one_undo_step) {
    Document doc;
    std::string err;
    CHECK(doc.loadFromText(kMap, "", &err));
    const size_t before = doc.undoPosition();
    CHECK(applyTodPreset(doc, "interior"));
    CHECK(doc.undoPosition() == before + 1);
    const Entity* w = doc.worldspawn();
    CHECK(w->value(kFogKey) == nullptr);  // interior removes the fog
    CHECK(w->value("skyname") && *w->value("skyname") == "sky_mp_shipment");
    CHECK(w->value("iw8r_exposure_adjust") && *w->value("iw8r_exposure_adjust") == "0.5");
    CHECK(!applyTodPreset(doc, "interior"));  // nothing left to change
    CHECK(doc.undo());
    CHECK(doc.worldspawn()->value(kFogKey) && *doc.worldspawn()->value(kFogKey) == "10 20 0.1 0.2 0.3");
    CHECK(*doc.worldspawn()->value("skyname") == "sky_custom");
}

TEST(tod_preset_keep_sky) {
    Document doc;
    std::string err;
    CHECK(doc.loadFromText(kMap, "", &err));
    CHECK(applyTodPreset(doc, "dusk", true));
    CHECK(*doc.worldspawn()->value("skyname") == "sky_custom");
    CHECK(*doc.worldspawn()->value("suncolor") == "1 0.58 0.32");
    CHECK(!applyTodPreset(doc, "unknown"));
}
