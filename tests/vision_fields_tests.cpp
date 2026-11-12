#include "vision_fields.h"
#include "test_harness.h"

#include <set>
#include <string>

using namespace iw8r;

namespace {
using Pairs = std::vector<std::pair<std::string, std::string>>;
}

TEST(vision_fields_table_is_large) {
    CHECK(visionFields().size() > 150);
    CHECK(findVisionField("fogDensity") != nullptr);
}

TEST(vision_fields_names_unique_and_complete) {
    std::set<std::string> names;
    for (const VisionField& f : visionFields()) {
        CHECK(f.name && *f.name);
        CHECK(f.type && f.defaultText && f.group);
        names.insert(std::string(f.name));
    }
    CHECK(names.size() == visionFields().size());
}

TEST(vision_fields_lookup_is_case_insensitive) {
    const VisionField* f = findVisionField("FOGDENSITY");
    CHECK(f != nullptr);
    if (f) {
        CHECK(std::string(f->name) == "fogDensity");
        CHECK(std::string(f->group) == "fog");
        CHECK(std::string(f->type) == "scalar");
        CHECK(std::string(f->defaultText) == "25000");
    }
    CHECK(findVisionField("noSuchVisionField") == nullptr);
    CHECK(findVisionField("") == nullptr);
}

TEST(vision_fields_parse_format_round_trip) {
    Pairs p;
    std::string err;
    CHECK(parseVisionOverrides("fogDensity=25000;fogColor=1 0.5 0.25;tonemapExposure=10", p, &err));
    CHECK(p.size() == 3);
    if (p.size() == 3) {
        CHECK(p[1].first == "fogColor" && p[1].second == "1 0.5 0.25");
        CHECK(formatVisionOverrides(p) == "fogDensity=25000;fogColor=1 0.5 0.25;tonemapExposure=10");
        Pairs again;
        CHECK(parseVisionOverrides(formatVisionOverrides(p), again, nullptr));
        CHECK(again == p);
    }
}

TEST(vision_fields_parse_trims_and_skips_empty_items) {
    Pairs p;
    CHECK(parseVisionOverrides("  a = 1 ;; b=2 ;", p, nullptr));
    CHECK(p.size() == 2);
    if (p.size() == 2) {
        CHECK(p[0].first == "a" && p[0].second == "1");
        CHECK(p[1].first == "b" && p[1].second == "2");
    }
    CHECK(parseVisionOverrides("", p, nullptr));
    CHECK(p.empty());
}

TEST(vision_fields_parse_rejects_bad_syntax) {
    Pairs p;
    std::string err;
    CHECK(!parseVisionOverrides("fogDensity", p, &err));
    CHECK(!err.empty());
    CHECK(p.empty());
    err.clear();
    CHECK(!parseVisionOverrides("a=1;=3", p, &err));
    CHECK(!err.empty());
    err.clear();
    CHECK(!parseVisionOverrides("a=", p, &err));
    CHECK(!err.empty());
}

TEST(vision_fields_check_float_value) {
    const VisionField* f = findVisionField("tonemapExposure");
    CHECK(f != nullptr);
    if (!f) return;
    CHECK(checkVisionValue(*f, "1.5") == "");
    CHECK(checkVisionValue(*f, "-2e3") == "");
    CHECK(checkVisionValue(*f, "+.5") == "");
    CHECK(checkVisionValue(*f, "abc") != "");
    CHECK(checkVisionValue(*f, "1 2") != "");
    CHECK(checkVisionValue(*f, "1e999") != "");
    CHECK(checkVisionValue(*f, "") != "");
}

TEST(vision_fields_check_vec3_value) {
    const VisionField* f = findVisionField("fogColor");
    CHECK(f != nullptr);
    if (!f) return;
    CHECK(std::string(f->type) == "vector");
    CHECK(checkVisionValue(*f, "1 0.5 0") == "");
    CHECK(checkVisionValue(*f, "1 0.5") != "");
    CHECK(checkVisionValue(*f, "1 x 0") != "");
    CHECK(checkVisionValue(*f, "1") != "");
}

TEST(vision_fields_check_bool_value) {
    const VisionField flag{"synthetic_flag", "bool", "0", "misc"};
    CHECK(checkVisionValue(flag, "1") == "");
    CHECK(checkVisionValue(flag, "0") == "");
    CHECK(checkVisionValue(flag, "2") != "");
    CHECK(checkVisionValue(flag, "yes") != "");
}

TEST(vision_fields_check_non_numeric_kind) {
    const VisionField* f = findVisionField("thermalLutImage");
    CHECK(f != nullptr);
    if (f) {
        CHECK(std::string(f->type) == "image");
        CHECK(checkVisionValue(*f, "1") != "");
    }
}
