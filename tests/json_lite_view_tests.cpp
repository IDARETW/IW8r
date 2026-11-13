// json_lite targeted reading (memory pass 2026-11-13): validate(), valueEnd(), View, FileText. The stock readers rely on
// these giving exactly what the whole-text DOM gives, so most checks compare against json::parse.
#include "json_lite.h"
#include "test_harness.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace iw8r;
namespace fs = std::filesystem;

namespace {

// Structural equality of a View and the DOM value of the same text: member order and keys, element order, scalars
// parsed the same way.
bool sameAs(const json::View& v, const json::Value& d) {
    if (!v.valid() || v.type() != d.type) return false;
    switch (d.type) {
        case json::Value::Type::Object: {
            size_t i = 0;
            bool ok = true;
            const bool walked = v.forEachMember([&](const std::string& k, json::View m) {
                if (i >= d.obj.size() || d.obj[i].first != k || !sameAs(m, d.obj[i].second)) ok = false;
                ++i;
                return ok;
            });
            return walked && ok && i == d.obj.size();
        }
        case json::Value::Type::Array: {
            size_t i = 0;
            bool ok = true;
            const bool walked = v.forEachElement([&](json::View e) {
                if (i >= d.arr.size() || !sameAs(e, d.arr[i])) ok = false;
                ++i;
                return ok;
            });
            return walked && ok && i == d.arr.size();
        }
        case json::Value::Type::String: {
            std::string s;
            return v.string(s) && s == d.str;
        }
        case json::Value::Type::Number: {
            const auto n = v.number();
            return n && *n == d.num;
        }
        case json::Value::Type::Bool: {
            const auto p = v.parse();
            return p && p->type == json::Value::Type::Bool && p->b == d.b;
        }
        case json::Value::Type::Null: return v.isNull();
    }
    return false;
}

// Exact equality of two DOM values.
bool sameDom(const json::Value& a, const json::Value& b) {
    if (a.type != b.type || a.b != b.b || a.str != b.str || a.arr.size() != b.arr.size() || a.obj.size() != b.obj.size())
        return false;
    if (a.type == json::Value::Type::Number && !(a.num == b.num)) return false;
    for (size_t i = 0; i < a.arr.size(); ++i)
        if (!sameDom(a.arr[i], b.arr[i])) return false;
    for (size_t i = 0; i < a.obj.size(); ++i)
        if (a.obj[i].first != b.obj[i].first || !sameDom(a.obj[i].second, b.obj[i].second)) return false;
    return true;
}

// Strings with braces, brackets, quotes, escaped quotes and backslashes, keys with escapes, a key repeated, nesting.
const char* kTricky = R"( {
  "a\"}{": "x { [ \" ] } \\",
  "skip": {"surfaces": {"values": "not this one"}, "s": "\"surfaces\":{\"values\":[1,2]}"},
  "surfaces" : { "values" : [ {"n": 1, "t": "]}"}, [ ], { }, "\\\"", -2.5e3, true, false, null, [[["deep"]]] ] },
  "dup": 1,
  "dup": 2,
  "k\u0065y": "escaped key",
  "uni": "\u00e9\ud83d\ude00\ud800x",
  "empty": "",
  "num": 1e5,
  "arr": [1,2 , 3]
} )";

}  // namespace

TEST(json_lite_validate_matches_parse) {
    const std::vector<std::string> inputs = {
        kTricky, "{}", "[]", "\"s\"", "1", "-0.5e-2", "true", "null", " { \"a\" : [ 1 , 2 ] } ",
        // malformed: every error kind of the parser
        "", "   ", "{", "[", "{\"a\"", "{\"a\":", "{\"a\":1", "{\"a\":1,", "{\"a\" 1}", "{a:1}", "[1 2]", "[1,]", "{\"a\":1,}",
        "\"abc", "\"\\x\"", "\"\\u12\"", "\"\\u12G4\"", "+1", "1e999", "-", "tru", "nul", "{} x", "[1]]", "\"\\", "@",
        std::string(600, '[') + std::string(600, ']'),
    };
    for (const std::string& in : inputs) {
        std::string perr, verr;
        const bool parsed = json::parse(in, &perr).has_value();
        const bool valid = json::validate(in, &verr);
        CHECK(parsed == valid);
        CHECK(perr == verr);  // the same message and byte offset
    }
    // Byte mutants of the tricky text: validate and parse agree on every one, with the same message.
    uint32_t rng = 12345;
    const std::string seed = kTricky;
    size_t rejected = 0;
    for (int i = 0; i < 2000; ++i) {
        std::string m = seed;
        for (int k = 0; k < 1 + i % 3; ++k) {
            rng = rng * 1664525u + 1013904223u;
            const size_t at = (rng >> 8) % m.size();
            rng = rng * 1664525u + 1013904223u;
            const char pool[] = "{}[]\",:\\ ue0-.a";
            m[at] = pool[(rng >> 8) % (sizeof(pool) - 1)];
        }
        std::string perr, verr;
        const bool parsed = json::parse(m, &perr).has_value();
        const bool valid = json::validate(m, &verr);
        CHECK(parsed == valid);
        CHECK(perr == verr);
        if (!valid) ++rejected;
    }
    CHECK(rejected > 100);
}

TEST(json_lite_view_matches_dom) {
    const auto dom = json::parse(kTricky);
    CHECK(dom.has_value());
    if (!dom) return;
    const json::View root(kTricky);
    CHECK(root.valid());
    CHECK(root.type() == json::Value::Type::Object);
    CHECK(sameAs(root, *dom));  // every member and element, recursively

    // get() is Value::get: the first member of that name, keys compared decoded, not a match inside a string.
    const json::View vals = root.get("surfaces").get("values");
    CHECK(vals.type() == json::Value::Type::Array);
    CHECK(sameAs(vals, *dom->get("surfaces")->get("values")));
    CHECK(root.get("dup").number() == 1.0);
    CHECK(root.get("key").valid());
    std::string s;
    CHECK(root.get("key").string(s) && s == "escaped key");
    CHECK(root.get("a\"}{").string(s) && s == "x { [ \" ] } \\");
    CHECK(root.get("skip").get("s").string(s) && s == "\"surfaces\":{\"values\":[1,2]}");
    CHECK(root.get("uni").string(s) && s == *dom->get("uni")->string());
    // Missing members, lookups on the wrong type and on invalid views give invalid views.
    CHECK(!root.get("nope").valid());
    CHECK(!root.get("num").get("x").valid());
    CHECK(!root.get("arr").get("0").valid());
    CHECK(!json::View().get("a").valid());
    CHECK(!json::View("").valid());
    CHECK(!json::View("   ").valid());
    CHECK(json::View().type() == json::Value::Type::Null);
    // Elements parsed one at a time equal the elements of the whole-text DOM.
    size_t i = 0;
    vals.forEachElement([&](json::View e) {
        const auto one = e.parse();
        CHECK(one.has_value() && i < dom->get("surfaces")->get("values")->arr.size() &&
              sameDom(*one, dom->get("surfaces")->get("values")->arr[i]));
        ++i;
        return true;
    });
    CHECK(i == 9);
    // Stopping early.
    size_t seen = 0;
    CHECK(root.get("arr").forEachElement([&](json::View) { return ++seen < 2; }));
    CHECK(seen == 2);
    // forEach on the wrong type.
    CHECK(!root.get("arr").forEachMember([](const std::string&, json::View) { return true; }));
    CHECK(!root.get("num").forEachElement([](json::View) { return true; }));
    CHECK(json::View("[]").forEachElement([](json::View) { return true; }));
    CHECK(json::View(" { } ").forEachMember([](const std::string&, json::View) { return true; }));
}

TEST(json_lite_view_strings_and_numbers) {
    std::string_view raw;
    CHECK(json::View(R"("00ff")").rawString(raw) && raw == "00ff");
    CHECK(json::View(R"("")").rawString(raw) && raw.empty());
    CHECK(!json::View(R"("a\"b")").rawString(raw));  // escapes are not raw
    std::string s;
    CHECK(json::View(R"("a\"b\u0041")").string(s) && s == "a\"bA");
    CHECK(!json::View("12").rawString(raw));
    CHECK(!json::View("12").string(s));
    CHECK(!json::View("null").string(s));
    CHECK(json::View("12").number() == 12.0);
    CHECK(json::View(" -2.5e3 ").number() == -2500.0);
    CHECK(!json::View("\"12\"").number().has_value());
    CHECK(!json::View("true").number().has_value());
    CHECK(!json::View("null").number().has_value());
    CHECK(json::View("null").isNull());
    CHECK(json::View("false").type() == json::Value::Type::Bool);
    // The value ends where the parser says it does, even with brackets and quotes inside strings.
    const std::string t = R"( {"a":"}]\"[{","b":[1,{"c":"]"}]} , 7)";
    const size_t e = json::valueEnd(t, 0);
    CHECK(e != std::string::npos && t.substr(0, e).back() == '}');
    CHECK(json::parse(t.substr(0, e)).has_value());
    CHECK(json::valueEnd(R"({"a":"unterminated})", 0) == std::string::npos);
    CHECK(json::valueEnd("[1,[2,3]", 0) == std::string::npos);
    CHECK(json::valueEnd("  42 ,", 0) == 4);
    CHECK(json::valueEnd("", 0) == std::string::npos);
    // parseView parses a piece of a larger text without copying it.
    const std::string big = "xx[1,2]yy";
    const auto pv = json::parseView(std::string_view(big).substr(2, 5));
    CHECK(pv && pv->arr.size() == 2);
}

TEST(json_lite_file_text) {
    const fs::path dir = fs::path(iw8r::test::tmpDir()) / "json_file_text";
    fs::create_directories(dir);
    const std::string body = std::string(kTricky) + std::string(100000, ' ');
    {
        std::ofstream(dir / "a.json", std::ios::binary) << body;
        std::ofstream(dir / "empty.json", std::ios::binary);
    }
    {
        json::FileText f;
        CHECK(f.open(dir / "a.json"));
        CHECK(f.view() == body);
        CHECK(json::validate(f.view()));
        std::string s;
        CHECK(json::View(f.view()).get("key").string(s) && s == "escaped key");
        f.close();
        CHECK(f.view().empty());
    }
    {
        json::FileText f;
        CHECK(f.open(dir / "empty.json"));  // an empty file cannot be mapped: read instead
        CHECK(f.view().empty());
        std::string err;
        CHECK(!json::validate(f.view(), &err));
        CHECK(err == "unexpected end at byte 0");
    }
    {
        json::FileText f;
        CHECK(!f.open(dir / "missing.json"));
        CHECK(f.view().empty());
    }
    // Reopening replaces the previous file; the file can be read by others while open.
    {
        json::FileText f;
        CHECK(f.open(dir / "a.json"));
        std::ifstream other(dir / "a.json", std::ios::binary);
        CHECK(other.good());
        CHECK(f.open(dir / "empty.json"));
        CHECK(f.view().empty());
    }
    std::error_code ec;
    fs::remove_all(dir, ec);
}
