// Stock scripted object catalog (roadmap 6.13): TSV loading, wildcard matching, no-match and bad-row errors.
#include <filesystem>
#include <fstream>
#include <map>
#include <string>

#include "stock_scripted_catalog.h"
#include "test_harness.h"

using namespace iw8r;

namespace {

namespace fs = std::filesystem;

const char* kHeader = "# test catalog\n# kind\tclassname\tkey\tvalue_pattern\thandled_by\tworks\tmaps\tcount\n";

fs::path writeTsv(const std::string& name, const std::string& body) {
    const fs::path dir = fs::path(test::tmpDir()) / "stock_scripted_catalog";
    fs::create_directories(dir);
    const fs::path p = dir / name;
    std::ofstream out(p, std::ios::binary);
    out << body;
    return p;
}

std::string goodRows() {
    return std::string(kHeader) +
           "sd_site\ttrigger_use_touch\ttargetname\tbombzone\tsd.gsc:470\tunknown\tmp_shipment,mp_backlot2\t4\n"
           "sd_site\ttrigger_use_touch\tscript_bombmode_original\t1\tsd.gsc:439\tunknown\tmp_shipment\t6\n"
           "minimap_corner\tscript_origin\ttargetname\tminimap_corner\tcompass.gsc:12\tyes\tmp_shipment\t4\n"
           "battle_chatter_location\ttrigger_multiple_bcs_*\t-\t*\tbattlechatter_trigs.gsc\tunknown\t\t18\n"
           "rugby_endzone\tscript_struct_mp_rugby_endzone\ttargetname\trugby_endzone\trugby.gsc:174\tno\tmp_backlot2\t4\n"
           "killstreak_marker\tscript_origin\ttargetname\tairstrike*\tevents.gsc:2828\tunknown\tmp_shipment\t2\n";
}

ScriptedObjectCatalog loadGood(const std::string& name = "good.tsv") {
    ScriptedObjectCatalog cat;
    std::string err;
    const fs::path p = writeTsv(name, goodRows());
    CHECK(cat.load(p.string(), &err));
    CHECK(err.empty());
    return cat;
}

std::map<std::string, std::string> ent(std::map<std::string, std::string> keys) { return keys; }

}  // namespace

TEST(scripted_catalog_wildcard) {
    CHECK(scriptedObjectWildcardMatch("*", ""));
    CHECK(scriptedObjectWildcardMatch("*", "anything"));
    CHECK(scriptedObjectWildcardMatch("ctf_flag_*", "ctf_flag_allies"));
    CHECK(!scriptedObjectWildcardMatch("ctf_flag_*", "ctf_flag"));
    CHECK(scriptedObjectWildcardMatch("pf*_auto*", "pf1262_auto1"));
    CHECK(!scriptedObjectWildcardMatch("pf*_auto*", "pf1262_manual"));
    CHECK(scriptedObjectWildcardMatch("tactical_*_col", "tactical_ladder_col"));
    CHECK(!scriptedObjectWildcardMatch("tactical_*_col", "tactical_ladder_cols"));
    CHECK(scriptedObjectWildcardMatch("dom onslaught", "dom onslaught"));
    CHECK(!scriptedObjectWildcardMatch("dom onslaught", "dom"));
    CHECK(!scriptedObjectWildcardMatch("abc", "abd"));
    CHECK(scriptedObjectWildcardMatch("a*b*c", "aXXbYYc"));
    CHECK(!scriptedObjectWildcardMatch("a*b*c", "aXXcYYb"));
}

TEST(scripted_catalog_load_counts_rows) {
    ScriptedObjectCatalog cat = loadGood();
    CHECK(cat.size() == 6);
}

TEST(scripted_catalog_match_by_key_value) {
    ScriptedObjectCatalog cat = loadGood();
    const auto hits = cat.match(ent({{"classname", "trigger_use_touch"}, {"targetname", "bombzone"}}));
    CHECK(hits.size() == 1);
    if (hits.size() == 1) {
        CHECK(hits[0]->kind == "sd_site");
        CHECK(hits[0]->count == 4);
        CHECK(hits[0]->maps.size() == 2);
        CHECK(hits[0]->worksInCustom == "unknown");
    }
}

TEST(scripted_catalog_match_no_match) {
    ScriptedObjectCatalog cat = loadGood();
    // Right classname, wrong value.
    CHECK(cat.match(ent({{"classname", "trigger_use_touch"}, {"targetname", "bombzone_b"}})).empty());
    // Right key, wrong classname.
    CHECK(cat.match(ent({{"classname", "trigger_radius"}, {"targetname", "bombzone"}})).empty());
    // Key missing entirely.
    CHECK(cat.match(ent({{"classname", "trigger_use_touch"}})).empty());
    // No classname at all.
    CHECK(cat.match(ent({{"targetname", "bombzone"}})).empty());
}

TEST(scripted_catalog_match_wildcards_and_none_key) {
    ScriptedObjectCatalog cat = loadGood();
    // Classname wildcard with key '-' matches on classname alone.
    const auto bcs = cat.match(ent({{"classname", "trigger_multiple_bcs_containers"}}));
    CHECK(bcs.size() == 1);
    if (bcs.size() == 1) CHECK(bcs[0]->kind == "battle_chatter_location");
    CHECK(cat.match(ent({{"classname", "trigger_multiple_mp_rugby"}})).empty());
    // Value wildcard inside the value pattern.
    const auto airstrike = cat.match(ent({{"classname", "script_origin"}, {"targetname", "airstrikeheight"}}));
    CHECK(airstrike.size() == 1);
    // Exact value rule for minimap corners, and its works flag.
    const auto corner = cat.match(ent({{"classname", "script_origin"}, {"targetname", "minimap_corner"}}));
    CHECK(corner.size() == 1);
    if (corner.size() == 1) CHECK(corner[0]->worksInCustom == "yes");
}

TEST(scripted_catalog_match_returns_all_rows_in_file_order) {
    // Two rows match the same entity: the bombzone row and the bombmode row.
    ScriptedObjectCatalog cat = loadGood("order.tsv");
    const auto hits = cat.match(ent({{"classname", "trigger_use_touch"},
                                     {"targetname", "bombzone"},
                                     {"script_bombmode_original", "1"}}));
    CHECK(hits.size() == 2);
    if (hits.size() == 2) {
        CHECK(hits[0]->key == "targetname");
        CHECK(hits[1]->key == "script_bombmode_original");
    }
}

TEST(scripted_catalog_empty_maps_column_allowed) {
    ScriptedObjectCatalog cat = loadGood("empty_maps.tsv");
    const auto hits = cat.match(ent({{"classname", "trigger_multiple_bcs_center"}}));
    CHECK(hits.size() == 1);
    if (hits.size() == 1) CHECK(hits[0]->maps.empty());
}

TEST(scripted_catalog_reload_replaces_content) {
    ScriptedObjectCatalog cat = loadGood("reload_a.tsv");
    CHECK(cat.size() == 6);
    std::string err;
    const fs::path p = writeTsv("reload_b.tsv", std::string(kHeader) +
                                                     "dom_capture_trigger\ttrigger_radius\tscript_gameobjectname\t"
                                                     "dom onslaught\tdom.gsc:351\tyes\tmp_shipment\t6\n");
    CHECK(cat.load(p.string(), &err));
    CHECK(cat.size() == 1);
}

TEST(scripted_catalog_comments_and_crlf_skipped) {
    std::string body = std::string(kHeader) + "\r\n# another comment\r\n" +
                       "dom_capture_trigger\ttrigger_radius\tscript_gameobjectname\tdom onslaught\tdom.gsc:351\t"
                       "unknown\tmp_shipment\t6\r\n";
    ScriptedObjectCatalog cat;
    std::string err;
    CHECK(cat.load(writeTsv("crlf.tsv", body).string(), &err));
    CHECK(cat.size() == 1);
    CHECK(cat.match(ent({{"classname", "trigger_radius"}, {"script_gameobjectname", "dom onslaught"}})).size() == 1);
}

TEST(scripted_catalog_bad_rows_rejected) {
    struct Case {
        const char* name;
        std::string row;
        const char* expect;
    };
    const Case cases[] = {
        {"short_row", "sd_site\ttrigger_use_touch\ttargetname\tbombzone\tsd.gsc\tunknown\tmp_shipment\n", "8 tab"},
        {"long_row", "sd_site\ttrigger_use_touch\ttargetname\tbombzone\tsd.gsc\tunknown\tmp_shipment\t4\textra\n",
         "8 tab"},
        {"bad_works", "sd_site\ttrigger_use_touch\ttargetname\tbombzone\tsd.gsc\tmaybe\tmp_shipment\t4\n", "works"},
        {"bad_count", "sd_site\ttrigger_use_touch\ttargetname\tbombzone\tsd.gsc\tunknown\tmp_shipment\tfour\n",
         "count"},
        {"negative_count", "sd_site\ttrigger_use_touch\ttargetname\tbombzone\tsd.gsc\tunknown\tmp_shipment\t-1\n",
         "count"},
        {"empty_kind", "\ttrigger_use_touch\ttargetname\tbombzone\tsd.gsc\tunknown\tmp_shipment\t4\n", "kind"},
        {"empty_classname", "sd_site\t\ttargetname\tbombzone\tsd.gsc\tunknown\tmp_shipment\t4\n", "classname"},
        {"empty_key", "sd_site\ttrigger_use_touch\t\tbombzone\tsd.gsc\tunknown\tmp_shipment\t4\n", "key"},
        {"empty_value", "sd_site\ttrigger_use_touch\ttargetname\t\tsd.gsc\tunknown\tmp_shipment\t4\n", "value"},
        {"empty_map_name", "sd_site\ttrigger_use_touch\ttargetname\tbombzone\tsd.gsc\tunknown\ta,,b\t4\n", "map"},
    };
    for (const Case& c : cases) {
        ScriptedObjectCatalog cat;
        std::string err;
        // A good row first, then the bad row on line 4 (header on lines 1-2, a comment is not needed).
        const std::string body = std::string(kHeader) + c.row;
        const bool ok = cat.load(writeTsv(std::string("bad_") + c.name + ".tsv", body).string(), &err);
        CHECK(!ok);
        CHECK(cat.size() == 0);
        CHECK(err.find("line 3") != std::string::npos);
        CHECK(err.find(c.expect) != std::string::npos);
    }
}

TEST(scripted_catalog_missing_file_fails) {
    ScriptedObjectCatalog cat;
    std::string err;
    const fs::path p = fs::path(test::tmpDir()) / "stock_scripted_catalog" / "does_not_exist.tsv";
    CHECK(!cat.load(p.string(), &err));
    CHECK(!err.empty());
    CHECK(cat.size() == 0);
}
