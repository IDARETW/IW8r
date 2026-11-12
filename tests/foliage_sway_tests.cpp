// Foliage sway preview and iw8r_wind key tests (src/core/foliage_sway.cpp, doc 56).
#include <cmath>
#include <optional>
#include <string>

#include "foliage_sway.h"
#include "geometry.h"
#include "map_text.h"
#include "test_harness.h"

using namespace iw8r;

namespace {

double len2(const Vec3& v) { return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); }

bool sameVec(const Vec3& a, const Vec3& b) { return a.x == b.x && a.y == b.y && a.z == b.z; }

}  // namespace

TEST(foliage_sway_zero_at_base) {
    const WindSettings w{45, 1, 0.3};
    const Vec3 pts[] = {{0, 0, 0}, {120, -40, 8}, {-500, 300, 64}};
    const double heights[] = {0.0, -1.0, -500.0};
    for (const Vec3& p : pts) {
        for (double h : heights) {
            for (double t : {0.0, 0.7, 3.2, 99.5}) {
                CHECK(sameVec(swayOffset(w, p, h, t), Vec3{}));
            }
        }
    }
    // Zero strength is still rest at any height.
    CHECK(sameVec(swayOffset(WindSettings{45, 0, 0.3}, Vec3{10, 10, 0}, 128, 1.0), Vec3{}));
    // Non-finite input gives rest, never NaN.
    const Vec3 nanOut = swayOffset(w, Vec3{0, 0, 0}, std::nan(""), 1.0);
    CHECK(sameVec(nanOut, Vec3{}));
}

TEST(foliage_sway_grows_with_height) {
    const WindSettings w{0, 1, 0.3};
    const Vec3 p{37, -12, 0};
    // Same time and position, so the wave is identical: quadratic height growth gives a 4x ratio between 128 and 64.
    for (double t : {0.4, 1.1, 2.9}) {
        const double low = len2(swayOffset(w, p, 64, t));
        const double full = len2(swayOffset(w, p, 128, t));
        CHECK(full >= low);
        if (low > 1e-9) CHECK(std::abs(full / low - 4.0) < 1e-9);
        // Above the full height the amplitude stops growing.
        CHECK(std::abs(len2(swayOffset(w, p, 1000, t)) - full) < 1e-12);
    }
}

TEST(foliage_sway_bounded_amplitude) {
    const WindSettings extreme{123, 9.0, 40.0};  // clamped to strength 4 and 5 Hz inside swayOffset
    const WindSettings normal{-60, 1.0, 0.3};
    size_t sampled = 0;
    for (int ix = -3; ix <= 3; ++ix) {
        for (int iy = -3; iy <= 3; ++iy) {
            for (double h : {0.5, 16.0, 64.0, 128.0, 500.0}) {
                for (int k = 0; k < 40; ++k) {
                    const double t = 0.137 * k;
                    const Vec3 p{ix * 91.0, iy * 53.0, 0};
                    const Vec3 a = swayOffset(extreme, p, h, t);
                    const Vec3 b = swayOffset(normal, p, h, t);
                    CHECK(a.z == 0.0 && b.z == 0.0);
                    CHECK(len2(a) <= kSwayMaxOffset + 1e-9);
                    CHECK(len2(b) <= kSwayAmplitude * 1.0 + 1e-9);
                    ++sampled;
                }
            }
        }
    }
    CHECK(sampled == 7u * 7u * 5u * 40u);
    CHECK(kSwayMaxOffset == 24.0);
}

TEST(foliage_sway_deterministic) {
    const WindSettings w{30, 2.5, 0.8};
    const Vec3 p{250.5, -77.25, 32};
    for (double t : {0.0, 1.5, 12.25}) {
        const Vec3 a = swayOffset(w, p, 90, t);
        const Vec3 b = swayOffset(WindSettings{w}, p, 90, t);
        CHECK(sameVec(a, b));
        CHECK(sameVec(a, swayOffset(w, p, 90, t)));
    }
}

TEST(foliage_sway_phase_varies_by_position) {
    const WindSettings w{0, 1, 0.3};
    // Points far apart in x and y sit at different phases, so at the same time and height they lean differently.
    bool differs = false;
    for (double t : {0.5, 1.0, 2.0, 3.5}) {
        const Vec3 a = swayOffset(w, Vec3{0, 0, 0}, 128, t);
        const Vec3 b = swayOffset(w, Vec3{100, 0, 0}, 128, t);
        const Vec3 c = swayOffset(w, Vec3{0, 100, 0}, 128, t);
        if (std::abs(a.x - b.x) > 1e-3 || std::abs(a.x - c.x) > 1e-3) differs = true;
    }
    CHECK(differs);
    // Yaw 0 blows along +X: the y component is exactly zero, and yaw 90 blows along +Y.
    const Vec3 along = swayOffset(WindSettings{0, 1, 0.3}, Vec3{5, 5, 0}, 128, 1.0);
    CHECK(along.y == 0.0);
    const Vec3 across = swayOffset(WindSettings{90, 1, 0.3}, Vec3{5, 5, 0}, 128, 1.0);
    CHECK(std::abs(across.x) < 1e-12);
}

TEST(foliage_sway_material_heuristic) {
    CHECK(isFoliageMaterial("#tmo/foliage_grass_reeds_01",
                            "tmo/lit_3_lit_alt_ta1_808800_1002000201000011_0_1_1_0_0_138f0035b_0_0_1_0_0"));
    CHECK(isFoliageMaterial("#tmco/ee_foliage_tree_birch_canopy",
                            "#tmco/lit_3_lit_alt_ta1_808814_1002000201000011_0_1_1_0_0_138f0035b_0_0_1_0_0"));
    // Birch trunk and branches share the foliage name but draw opaque (rpl): no sway.
    CHECK(!isFoliageMaterial("#tmco/ee_foliage_tree_birch_trunk_branches",
                             "#tmco/lit_3_lit_rpl_ta1_800c00_1002000001000010_0_2_1_0_0_13810015b_0_0_1_0_0_0_0_3810015f"));
    // Name without a foliage word: no sway even when alpha tested.
    CHECK(!isFoliageMaterial("#tmo/uk_asphalt_pothole_01", "tmo/lit_3_lit_alt_ta1_809000_1002000001000011_0_1_1"));
    // Weed card proxies.
    CHECK(isFoliageMaterial("#tmo/li_mtl_uk_foliage_small_weeds_01_proxy",
                            "card_8_lit_card_rpl_ta1_c08000_1003000000000011_0_1_1_0_0_13"));
    // Custom material with no techset resolved yet: name decides.
    CHECK(isFoliageMaterial("my_bush_leaf", ""));
    CHECK(!isFoliageMaterial("brick_wall", ""));
    // A reactive-motion set name counts whatever the material is called.
    CHECK(isFoliageMaterial("anything", "tmo/lit_3_lit_reactivemotion_alt_ta1_1"));
}

TEST(iw8r_wind_format_round_trip) {
    const WindSettings w{-30.5, 2.25, 1.5};
    const std::string text = formatWind(w);
    std::string err = "unset";
    const std::optional<WindSettings> back = parseWind(text, &err);
    CHECK(back.has_value());
    CHECK(err.empty());
    if (back) {
        CHECK(back->dirYawDeg == w.dirYawDeg);
        CHECK(back->strength == w.strength);
        CHECK(back->gustFreq == w.gustFreq);
    }
    const WindSettings def;
    const std::optional<WindSettings> d = parseWind(formatWind(def));
    CHECK(d.has_value());
    if (d) {
        CHECK(d->dirYawDeg == 45 && d->strength == 1 && d->gustFreq == 0.3);
    }
}

TEST(iw8r_wind_bad_values) {
    const char* bad[] = {"", "   ", "1 2", "1 2 3 4", "a b c", "nan 1 0.3", "0 inf 0.3", "0 1 0.3 x", "0 1 -"};
    for (const char* text : bad) {
        std::string err;
        CHECK(!parseWind(text, &err).has_value());
        CHECK(!err.empty());
    }
}

TEST(iw8r_wind_clamped_values) {
    std::string err;
    const std::optional<WindSettings> high = parseWind("0 5 0.3", &err);
    CHECK(high.has_value());
    CHECK(!err.empty());
    if (high) CHECK(high->strength == kSwayMaxStrength && high->gustFreq == 0.3);

    err.clear();
    const std::optional<WindSettings> negGust = parseWind("0 1 -2", &err);
    CHECK(negGust.has_value());
    CHECK(!err.empty());
    if (negGust) CHECK(negGust->gustFreq == 0.0 && negGust->strength == 1.0);

    err.clear();
    const std::optional<WindSettings> hot = parseWind("-720 0.5 5.5", &err);
    CHECK(hot.has_value());
    CHECK(!err.empty());
    if (hot) CHECK(hot->dirYawDeg == -720 && hot->gustFreq == kSwayMaxGustHz);

    err.clear();
    const std::optional<WindSettings> clean = parseWind("90 1.5 0.5", &err);
    CHECK(clean.has_value());
    CHECK(err.empty());
}

TEST(iw8r_wind_read_from_worldspawn) {
    const std::string text =
        "{\n\"classname\" \"worldspawn\"\n\"iw8r_wind\" \"90 1.5 0.5\"\n}\n"
        "{\n\"classname\" \"worldspawn_other\"\n\"iw8r_wind\" \"1 2\"\n}\n";
    Id next = 1;
    const ParseResult r = parseMap(text, next);
    CHECK(r.map.has_value());
    if (!r.map) return;

    const Entity* good = nullptr;
    for (const Entity& e : r.map->entities) {
        if (e.classname() == "worldspawn") good = &e;
    }
    CHECK(good != nullptr);
    if (!good) return;

    std::string warn;
    const std::optional<WindSettings> w = readWind(*good, &warn);
    CHECK(w.has_value());
    CHECK(warn.empty());
    if (w) CHECK(w->dirYawDeg == 90 && w->strength == 1.5 && w->gustFreq == 0.5);

    // Absent key: nothing and no warning.
    std::string noWarn;
    const std::string plain = "{\n\"classname\" \"worldspawn\"\n}\n";
    Id next2 = 1;
    const ParseResult plainRes = parseMap(plain, next2);
    CHECK(plainRes.map.has_value());
    if (plainRes.map && !plainRes.map->entities.empty()) {
        CHECK(!readWind(plainRes.map->entities[0], &noWarn).has_value());
        CHECK(noWarn.empty());
    }

    // Malformed value: nothing, with a warning.
    std::string badWarn;
    const Entity* bad = nullptr;
    for (const Entity& e : r.map->entities) {
        if (e.classname() == "worldspawn_other") bad = &e;
    }
    CHECK(bad != nullptr);
    if (bad) {
        CHECK(!readWind(*bad, &badWarn).has_value());
        CHECK(!badWarn.empty());
    }
}
