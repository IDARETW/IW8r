// misc_model lighting, shadow and cull flags (src/core/smodel_flags.*, roadmap 5.22, doc 54).
#include <map>
#include <string>
#include <vector>

#include "smodel_flags.h"
#include "test_harness.h"

using namespace iw8r;

namespace {

using Keys = std::map<std::string, std::string>;

}  // namespace

TEST(smodel_flags_defaults_from_no_keys) {
    std::vector<std::string> warnings;
    const SModelFlags f = readSModelFlags(Keys{}, &warnings);
    CHECK(!f.noShadow);
    CHECK(!f.groundLighting);
    CHECK(!f.noCollision);
    CHECK(f.cullDistance == 0.0);
    CHECK(f.lodScale == 1.0);
    CHECK(warnings.empty());
    CHECK(smodelFlagBits(f) == 0u);
}

TEST(smodel_flags_round_trip) {
    SModelFlags in;
    in.noShadow = true;
    in.groundLighting = true;
    in.noCollision = true;
    in.cullDistance = 4000.0;
    in.lodScale = 2.5;

    Keys keys;
    writeSModelFlags(in, keys);
    CHECK(keys.at("no_shadow") == "1");
    CHECK(keys.at("ground_lighting") == "1");
    CHECK(keys.at("no_collision") == "1");
    CHECK(keys.at("cull_dist") == "4000");
    CHECK(keys.at("lod_scale") == "2.5");

    std::vector<std::string> warnings;
    const SModelFlags out = readSModelFlags(keys, &warnings);
    CHECK(warnings.empty());
    CHECK(out.noShadow == in.noShadow);
    CHECK(out.groundLighting == in.groundLighting);
    CHECK(out.noCollision == in.noCollision);
    CHECK(out.cullDistance == in.cullDistance);
    CHECK(out.lodScale == in.lodScale);
}

TEST(smodel_flags_round_trip_is_exact_for_odd_cull_distance) {
    SModelFlags in;
    in.cullDistance = 1234.5678901234;
    Keys keys;
    writeSModelFlags(in, keys);
    const SModelFlags out = readSModelFlags(keys, nullptr);
    CHECK(out.cullDistance == in.cullDistance);
}

TEST(smodel_flags_default_write_erases_stale_keys) {
    Keys keys{{"no_shadow", "1"}, {"ground_lighting", "1"}, {"cull_dist", "900"}, {"lod_scale", "4"},
              {"no_collision", "1"}, {"model", "tm/foo"}};
    writeSModelFlags(SModelFlags{}, keys);
    CHECK(keys.count("no_shadow") == 0);
    CHECK(keys.count("ground_lighting") == 0);
    CHECK(keys.count("cull_dist") == 0);
    CHECK(keys.count("lod_scale") == 0);
    CHECK(keys.count("no_collision") == 0);
    CHECK(keys.count("model") == 1);  // unrelated keys are left alone
}

TEST(smodel_flags_bool_spellings) {
    std::vector<std::string> warnings;
    CHECK(readSModelFlags(Keys{{"no_shadow", "true"}}, &warnings).noShadow);
    CHECK(readSModelFlags(Keys{{"no_shadow", " 1 "}}, &warnings).noShadow);
    CHECK(!readSModelFlags(Keys{{"no_shadow", "false"}}, &warnings).noShadow);
    CHECK(!readSModelFlags(Keys{{"no_shadow", "0"}}, &warnings).noShadow);
    CHECK(warnings.empty());
}

TEST(smodel_flags_bad_bool_warns_and_keeps_default) {
    std::vector<std::string> warnings;
    const SModelFlags f = readSModelFlags(Keys{{"no_shadow", "yes"}, {"ground_lighting", "2"}}, &warnings);
    CHECK(!f.noShadow);
    CHECK(!f.groundLighting);
    CHECK(warnings.size() == 2);
}

TEST(smodel_flags_bad_cull_distance_warns) {
    for (const char* bad : {"-5", "abc", "", "nan", "inf", "12x", "1e400"}) {
        std::vector<std::string> warnings;
        const SModelFlags f = readSModelFlags(Keys{{"cull_dist", bad}}, &warnings);
        CHECK(f.cullDistance == 0.0);
        CHECK(warnings.size() == 1);
    }
}

TEST(smodel_flags_zero_cull_distance_is_no_cull_without_warning) {
    std::vector<std::string> warnings;
    const SModelFlags f = readSModelFlags(Keys{{"cull_dist", "0"}}, &warnings);
    CHECK(f.cullDistance == 0.0);
    CHECK(warnings.empty());
}

TEST(smodel_flags_lod_scale_table) {
    std::vector<std::string> warnings;
    CHECK(readSModelFlags(Keys{{"lod_scale", "0.25"}}, &warnings).lodScale == 0.25);
    CHECK(readSModelFlags(Keys{{"lod_scale", "0.375"}}, &warnings).lodScale == 0.375);
    CHECK(readSModelFlags(Keys{{"lod_scale", "4"}}, &warnings).lodScale == 4.0);
    CHECK(warnings.empty());
}

TEST(smodel_flags_bad_lod_scale_warns) {
    for (const char* bad : {"3.3", "0.2", "8", "fast", "5"}) {
        std::vector<std::string> warnings;
        const SModelFlags f = readSModelFlags(Keys{{"lod_scale", bad}}, &warnings);
        CHECK(f.lodScale == 1.0);
        CHECK(warnings.size() == 1);
    }
}

TEST(smodel_flags_null_warnings_pointer_is_allowed) {
    const SModelFlags f = readSModelFlags(Keys{{"no_shadow", "maybe"}, {"cull_dist", "-1"}, {"lod_scale", "9"}}, nullptr);
    CHECK(!f.noShadow);
    CHECK(f.cullDistance == 0.0);
    CHECK(f.lodScale == 1.0);
}

TEST(smodel_flags_bit_pattern_per_flag) {
    SModelFlags f;
    CHECK(smodelFlagBits(f) == 0u);

    f = SModelFlags{};
    f.groundLighting = true;
    CHECK(smodelFlagBits(f) == 1u);
    CHECK(smodelFlagBits(f) == kSModelBitGroundLighting);

    f = SModelFlags{};
    f.noShadow = true;
    CHECK(smodelFlagBits(f) == 2u);
    CHECK(smodelFlagBits(f) == kSModelBitNoShadow);

    f = SModelFlags{};
    f.noCollision = true;
    CHECK(smodelFlagBits(f) == 4u);
    CHECK(smodelFlagBits(f) == kSModelBitNoCollision);

    // Cull distance and LOD scale are numbers, not bits.
    f = SModelFlags{};
    f.cullDistance = 900.0;
    f.lodScale = 4.0;
    CHECK(smodelFlagBits(f) == 0u);

    f.groundLighting = f.noShadow = f.noCollision = true;
    CHECK(smodelFlagBits(f) == 7u);
}

TEST(smodel_flags_key_names_are_stable) {
    CHECK(std::string(kSModelKeyNoShadow) == "no_shadow");
    CHECK(std::string(kSModelKeyGroundLighting) == "ground_lighting");
    CHECK(std::string(kSModelKeyCullDist) == "cull_dist");
    CHECK(std::string(kSModelKeyNoCollision) == "no_collision");
    CHECK(std::string(kSModelKeyLodScale) == "lod_scale");
}

// 4.8: ground lighting colour.
TEST(smodel_flags_ground_colour) {
    using Keys = std::map<std::string, std::string>;
    std::uint32_t c = 0;
    CHECK(parseGroundColor("1 0.5 0", c) && c == 0xFF0080FFu);
    CHECK(parseGroundColor("90 80 70", c) && (c & 0xFF) == 90 && ((c >> 8) & 0xFF) == 80 &&
          ((c >> 16) & 0xFF) == 70);
    CHECK(!parseGroundColor("1", c) && !parseGroundColor("1 2", c) && !parseGroundColor("300 0 0", c) && !parseGroundColor("1 1 1 x", c));
    std::vector<std::string> w;
    SModelFlags f = readSModelFlags(Keys{{"ground_lighting", "1"}}, &w);
    CHECK(f.groundLighting && f.groundColor == 0xFF282828u && w.empty());
    f = readSModelFlags(Keys{{"ground_lighting", "0.2 0.4 0.6"}}, &w);
    CHECK(f.groundLighting && (f.groundColor & 0xFF) == 51 && ((f.groundColor >> 8) & 0xFF) == 102 && ((f.groundColor >> 16) & 0xFF) == 153);
    Keys out;
    writeSModelFlags(f, out);
    CHECK(out.at("ground_lighting") == "51 102 153");
    CHECK(readSModelFlags(out, nullptr).groundColor == f.groundColor);
}
