// Regression tests for three robustness fixes found by the fuzz pass:
//   1. json_lite: a lone UTF-16 surrogate escape becomes U+FFFD; a valid pair still makes one 4-byte code point.
//   2. map_text: a canonical key id above UINT32_MAX (more than 20 digits wrap) stays an opaque line, byte-exact.
//   3. iw8_mapents: JSON numbers used as indices or counts are range-checked (no negative, huge or fractional cast).
#include <cstdio>
#include <filesystem>
#include <string>
#include <variant>

#include "iw8_mapents.h"
#include "json_lite.h"
#include "map_text.h"
#include "test_harness.h"

using namespace iw8r;
namespace fs = std::filesystem;

namespace {

const std::string kRepl = "\xEF\xBF\xBD";  // U+FFFD in UTF-8 (kept as a variable so no hex escape runs into a letter)

// Parses a JSON document that is a single string and returns its decoded value ("<fail>" when it is not one).
std::string parseString(const std::string& text) {
    auto v = json::parse(text, nullptr);
    if (!v || !v->string()) return "<fail>";
    return *v->string();
}

std::string hexEncode(const std::string& s) {
    static const char* const d = "0123456789abcdef";
    std::string o;
    for (unsigned char c : s) {
        o += d[c >> 4];
        o += d[c & 15];
    }
    return o;
}

}  // namespace

// ------------------------------------------------------------------ 1. lone surrogates in json_lite

TEST(robust_json_lone_high_surrogate_is_replacement_char) {
    CHECK(parseString("\"\\uD800\"") == kRepl);
    CHECK(parseString("\"a\\uD800b\"") == "a" + kRepl + "b");
    CHECK(parseString("\"\\uDBFF\"") == kRepl);
}

TEST(robust_json_lone_low_surrogate_is_replacement_char) {
    CHECK(parseString("\"\\uDC00\"") == kRepl);
    CHECK(parseString("\"\\uDFFF\"") == kRepl);
    CHECK(parseString("\"x\\uDC00y\"") == "x" + kRepl + "y");
}

TEST(robust_json_high_surrogate_before_non_low_escape) {
    // The high surrogate is replaced; the escape after it is decoded on its own, not consumed as a pair.
    CHECK(parseString("\"\\uD800\\u0041\"") == kRepl + "A");
    // A second high surrogate followed by a low one still forms a pair (U+10000 = F0 90 80 80).
    CHECK(parseString("\"\\uD800\\uD800\\uDC00\"") == kRepl + "\xF0\x90\x80\x80");
}

TEST(robust_json_valid_pair_unchanged) {
    CHECK(parseString("\"\\uD83D\\uDE00\"") == "\xF0\x9F\x98\x80");  // U+1F600
    CHECK(parseString("\"\\uDBFF\\uDFFF\"") == "\xF4\x8F\xBF\xBF");  // U+10FFFF, the top of the range
}

// ------------------------------------------------------------------ 2. canonical key ids out of range

TEST(robust_canonical_key_id_above_uint32_stays_opaque) {
    // 2^64 used to wrap to 0 and 2^32 to 0 as well; both must stay opaque lines. 4294967295 is the largest valid id.
    const std::string text =
        "{\n212 \"worldspawn\"\n18446744073709551616 \"x\"\n4294967296 \"y\"\n"
        "99999999999999999999999 \"z\"\n4294967295 \"ok\"\n}\n";
    Id nextId = 1;
    const ParseResult r = parseMap(text, nextId);
    CHECK(r.map.has_value());
    if (!r.map) return;
    CHECK(r.map->text() == text);  // byte-exact round trip
    const Entity& ent = r.map->entities.front();
    CHECK(*ent.value("classname") == "worldspawn");
    int opaque = 0, canonicalZero = 0, canonicalMax = 0;
    for (const auto& it : ent.items) {
        if (std::holds_alternative<OpaqueLine>(it.v)) ++opaque;
        const KeyValueLine* kv = it.kv();
        if (kv && kv->form == KeyForm::Canonical && kv->keyId == 0) ++canonicalZero;
        if (kv && kv->form == KeyForm::Canonical && kv->keyId == 0xFFFFFFFFu) ++canonicalMax;
    }
    CHECK(opaque == 3);
    CHECK(canonicalZero == 0);  // no line may silently become id 0
    CHECK(canonicalMax == 1);
}

// ------------------------------------------------------------------ 3. JSON numbers as indices and counts

TEST(robust_mapents_bad_numbers_are_treated_as_missing) {
    // Negative, fractional and huge (>= 2^64, > UINT32_MAX) numbers in index, name, count and length fields.
    // Before the fix each one was cast straight to an unsigned type (undefined behaviour). NaN and infinity cannot
    // come out of json_lite (it refuses 1e400), so they are covered by the helper's own finite check.
    const std::string entityString = std::string("{\n212 \"worldspawn\"\n}\n") + std::string(1, '\0');
    const std::string json =
        "{\n  \"format\": \"mw19-asset-json\",\n  \"profile\": \"replay-1.20\",\n  \"pool\": \"map_ents\",\n"
        "  \"asset\": { \"fields\": {\n"
        "    \"name\": { \"string\": \"maps/mp/robust.d3dbsp\" },\n"
        "    \"entityString\": { \"bytes\": \"" + hexEncode(entityString) + "\" },\n"
        "    \"numEntityChars\": { \"value\": " + std::to_string(entityString.size()) + " },\n"
        "    \"spawnList\": {\n"
        "      \"spawnsCount\": { \"value\": -1 },\n"
        "      \"spawns\": { \"values\": [\n"
        "        { \"index\": { \"value\": -1 }, \"name\": { \"value\": 1 }, \"target\": { \"value\": 1.5 },\n"
        "          \"origin\": { \"v\": [1, 2, 3] }, \"angles\": [0, 0, 0] },\n"
        "        { \"index\": { \"value\": 1e30 }, \"name\": { \"value\": -2 }, \"target\": { \"value\": 1e30 },\n"
        "          \"script_noteworthy\": { \"value\": 1e30 } },\n"
        "        { \"index\": { \"value\": 7 }, \"name\": { \"value\": 1 } }\n"
        "      ] }\n"
        "    },\n"
        "    \"trigger\": { \"count\": { \"value\": -3 } },\n"
        "    \"clientTrigger\": { \"trigger\": { \"count\": { \"value\": 2.5 } } },\n"
        "    \"numSubModels\": { \"value\": 1e30 },\n"
        "    \"dynEntCountTotal\": { \"value\": 4 }\n"
        "  } }\n}\n";

    const fs::path dir = fs::path(test::tmpDir()) / "robustness_fixes";
    fs::create_directories(dir);
    const fs::path asset = dir / "robust_asset.json";
    CHECK(writeFileAtomic(asset.string(), json));

    Iw8MapEntsDump out;
    std::string err;
    // No script-string table: a valid index then shows as "#n", which makes the name checks easy to read.
    CHECK(loadActsMapEnts(asset.string(), std::string(), out, &err));
    CHECK(err.empty());
    CHECK(out.entityString == entityString);
    CHECK(out.numEntityChars == entityString.size());

    // Three records are kept. A bad index falls back to the default 0, a bad name or target to "" (missing).
    CHECK(out.spawns.size() == 3);
    if (out.spawns.size() == 3) {
        CHECK(out.spawns[0].index == 0);
        CHECK(out.spawns[0].name == "#1");
        CHECK(out.spawns[0].target.empty());
        CHECK(out.spawns[0].origin.x == 1.0 && out.spawns[0].origin.z == 3.0);
        CHECK(out.spawns[1].index == 0);
        CHECK(out.spawns[1].name.empty());
        CHECK(out.spawns[1].target.empty());
        CHECK(out.spawns[1].noteworthy.empty());
        CHECK(out.spawns[2].index == 7);
        CHECK(out.spawns[2].name == "#1");
    }

    // Invalid counts are not recorded; a valid one still is. A bad spawnsCount skips the consistency check.
    CHECK(out.sections.count("trigger.models") == 0);
    CHECK(out.sections.count("clientTrigger.models") == 0);
    CHECK(out.sections.count("cmodels") == 0);
    CHECK(out.sections.count("spawnList.spawns") == 0);
    CHECK(out.sections.count("dynEnts") == 1 && out.sections.at("dynEnts") == 4);
}
