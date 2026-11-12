// Parser fuzz and robustness tests (roadmap 9.14).
//
// Deterministic: every generator is a std::mt19937 with a constant seed (no clocks, no random_device). Mutation
// operators draw with `rng() % n` rather than std::uniform_int_distribution, so the mutant sequence is the same on
// every standard library. Covers:
//   1. byte-exact parse -> write round trip of the .map fixtures (tests/data and the tutorial map);
//   2. 2000 mutants per seed map through the iwmap 4 parser: no crash, no hang, an accepted mutant writes back
//      byte for byte, reloads to the same text, and a Document edit on it undoes/redoes exactly;
//   3. 500 mutants of an ACTS MapEnts JSON dump through json_lite, and 300 through the IW8 entity-string reader
//      (loadActsMapEnts): never throws; rejections carry a message; accepted results are self-consistent;
//   4. huge inputs: a 50 MB map line, 100k generated entities, deep brace and bracket nesting.
//
// Debugging aids: IW8R_FUZZ_TRACE=1 writes each case to <tmp>/fuzz/trace/last_<kind>.bin before it runs, so a crash
// that kills the process leaves the offending bytes behind. Any failing case is written to <tmp>/fuzz/failures/.
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <string>
#include <vector>

#include "document.h"
#include "iw8_mapents.h"
#include "json_lite.h"
#include "map_text.h"
#include "test_harness.h"

using namespace iw8r;
namespace fs = std::filesystem;

namespace {

using Rng = std::mt19937;
using Clock = std::chrono::steady_clock;

size_t below(Rng& rng, size_t n) { return n ? size_t(rng() % n) : 0; }

double secondsSince(Clock::time_point t0) { return std::chrono::duration<double>(Clock::now() - t0).count(); }

// Repository root: the folder above tests/ that holds this file (falls back to a walk up from the working directory).
fs::path repoRoot() {
    std::error_code ec;
    fs::path p = fs::absolute(fs::path(__FILE__), ec).parent_path().parent_path();
    if (fs::exists(p / "tests" / "data", ec)) return p;
    for (fs::path q = fs::current_path(ec); !q.empty(); q = q.parent_path()) {
        if (fs::exists(q / "tests" / "data", ec)) return q;
        if (q == q.root_path()) break;
    }
    return {};
}

fs::path scratch(const std::string& sub) {
    const fs::path p = fs::path(test::tmpDir()) / "fuzz" / sub;
    fs::create_directories(p);
    return p;
}

void writeBytes(const fs::path& p, const std::string& data) {
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f.write(data.data(), std::streamsize(data.size()));
}

bool traceOn() {
    const char* v = std::getenv("IW8R_FUZZ_TRACE");
    return v && *v && *v != '0';
}

void trace(const char* kind, const std::string& input) {
    if (traceOn()) writeBytes(scratch("trace") / (std::string("last_") + kind + ".bin"), input);
}

// Failing cases are kept as exact bytes so they can be reduced and reported.
void dumpFailure(const char* kind, size_t index, const std::string& input) {
    const fs::path p = scratch("failures") / (std::string(kind) + "_" + std::to_string(index) + ".bin");
    writeBytes(p, input);
    std::printf("    failing input (%zu bytes) written to %s\n", input.size(), p.string().c_str());
}

// Escapes bytes for a readable message: printable ASCII as is, everything else as \xHH.
std::string escapeBytes(const std::string& s, size_t max = 300) {
    std::string o;
    for (size_t i = 0; i < s.size() && o.size() < max; ++i) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (c == '\n') o += "\\n";
        else if (c == '\r') o += "\\r";
        else if (c == '\t') o += "\\t";
        else if (c == '\\') o += "\\\\";
        else if (c >= 0x20 && c < 0x7f) o += char(c);
        else {
            char b[8];
            std::snprintf(b, sizeof b, "\\x%02x", unsigned(c));
            o += b;
        }
    }
    if (s.size() > max) o += "...";
    return o;
}

// One mutation per call. Operators: byte flip, range delete, line duplicate, quote/brace/paren insert, truncate,
// CRLF <-> LF change, NUL insert, non-ASCII insert, insert of a map-like fragment.
std::string mutate(Rng& rng, const std::string& src) {
    static const char kPunct[] = "\"\"{}(){}\"\\";
    static const char* const kUtf8[] = {"\xC3\xA9", "\xEF\xBB\xBF", "\xF0\x9F\x98\x80", "\xE2\x80\xA8", "\xC0\xAF", "\xFF\xFE"};
    static const char* const kDict[] = {
        "{", "}", "{}", "\"classname\" \"x\"", "\"origin\" \"0 0 0\"", "212 \"worldspawn\"", "0 \"name\" \"v",
        "( 0 0 0 ) ( 0 0 1 ) ( 0 1 0 ) caulk 64 64 0 0 0 0", "v 1 2 3 t 0 0 0 0", "(", ")", "// brush 0", "iwmap 4",
        "\"unterminated", "  ", "\t"};
    std::string s = src;
    switch (below(rng, 9)) {
        case 0: {  // byte flip: flip one bit, or overwrite with a random byte
            if (s.empty()) {
                s.push_back(char(rng()));
                break;
            }
            const size_t p = below(rng, s.size());
            if (rng() & 1) s[p] = char(s[p] ^ char(1u << (rng() % 8)));
            else s[p] = char(rng() & 0xFF);
            break;
        }
        case 1: {  // delete a range of up to 256 bytes
            if (s.empty()) break;
            const size_t a = below(rng, s.size());
            const size_t len = 1 + below(rng, std::min<size_t>(256, s.size() - a));
            s.erase(a, len);
            break;
        }
        case 2: {  // duplicate the line under a random position (a last line without LF gets one first)
            if (s.empty()) break;
            const size_t p = below(rng, s.size());
            size_t a = 0;
            if (p > 0) {
                const size_t q = s.rfind('\n', p - 1);
                if (q != std::string::npos) a = q + 1;
            }
            size_t b = s.find('\n', p);
            b = (b == std::string::npos) ? s.size() : b + 1;
            if (s[b - 1] != '\n') {
                s += '\n';
                b = s.size();
            }
            const std::string line = s.substr(a, b - a);
            s.insert(b, line);
            break;
        }
        case 3: {  // insert 1-4 quotes, braces, parens or backslashes
            const size_t n = 1 + below(rng, 4);
            std::string ins;
            for (size_t k = 0; k < n; ++k) ins += kPunct[below(rng, sizeof(kPunct) - 1)];
            s.insert(below(rng, s.size() + 1), ins);
            break;
        }
        case 4:  // truncate (possibly to nothing)
            s.resize(below(rng, s.size() + 1));
            break;
        case 5: {  // line endings: all CRLF -> LF, all LF -> CRLF, or flip one terminator
            const size_t mode = below(rng, 3);
            if (mode == 2) {
                size_t q = s.find('\n', below(rng, s.size() + 1));
                if (q == std::string::npos) q = s.find('\n');
                if (q != std::string::npos) {
                    if (q > 0 && s[q - 1] == '\r') s.erase(q - 1, 1);
                    else s.insert(q, "\r");
                }
                break;
            }
            std::string o;
            o.reserve(s.size() + s.size() / 16);
            for (size_t k = 0; k < s.size(); ++k) {
                if (mode == 0 && s[k] == '\r' && k + 1 < s.size() && s[k + 1] == '\n') continue;
                if (mode == 1 && s[k] == '\n' && (k == 0 || s[k - 1] != '\r')) o += '\r';
                o += s[k];
            }
            s = o;
            break;
        }
        case 6: {  // NUL bytes
            const size_t n = 1 + below(rng, 2);
            s.insert(below(rng, s.size() + 1), n, '\0');
            break;
        }
        case 7: {  // non-ASCII: a UTF-8 sequence (including a BOM and invalid lead bytes) or random high bytes
            std::string ins;
            if (rng() & 1) {
                ins = kUtf8[below(rng, sizeof(kUtf8) / sizeof(kUtf8[0]))];
            } else {
                const size_t n = 1 + below(rng, 4);
                for (size_t k = 0; k < n; ++k) ins += char(0x80 + below(rng, 128));
            }
            s.insert(below(rng, s.size() + 1), ins);
            break;
        }
        default: {  // map-like fragment (brace lines, KV lines, face lines, patch vertex, bad headers)
            std::string ins = kDict[below(rng, sizeof(kDict) / sizeof(kDict[0]))];
            ins += (rng() & 1) ? "\n" : "\r\n";
            s.insert(below(rng, s.size() + 1), ins);
            break;
        }
    }
    return s;
}

// ---- seeds

std::string readFixture(const fs::path& p, bool& ok) {
    std::string text;
    ok = readFile(p.string(), text);
    return text;
}

std::string hexEncode(const std::string& s) {
    static const char* const d = "0123456789abcdef";
    std::string o;
    o.reserve(s.size() * 2);
    for (unsigned char c : s) {
        o += d[c >> 4];
        o += d[c & 15];
    }
    return o;
}

// Entity string as the MapEnts asset stores it: canonical and non-canonical keys, NUL terminator counted.
std::string entityStringSeed() {
    std::string es =
        "{\n212 \"worldspawn\"\n0 \"fuzz_key\" \"abc\"\n}\n"
        "{\n\"origin\" \"1 2 3\"\n\"classname\" \"info_player_start\"\n}\n";
    es.push_back('\0');
    return es;
}

// A minimal mw19-asset-json (replay-1.20 map_ents) dump. The hex payload is spliced in at @HEX@.
std::string jsonSeed() {
    std::string j = R"({
  "format": "mw19-asset-json",
  "profile": "replay-1.20",
  "pool": "map_ents",
  "asset": { "fields": {
    "name": { "string": "maps/mp/fuzz.d3dbsp" },
    "entityString": { "bytes": "@HEX@" },
    "numEntityChars": { "value": @N@ },
    "spawnList": {
      "spawnsCount": { "value": 2 },
      "spawns": { "values": [
        { "index": { "value": 0 }, "name": { "value": 1 }, "target": { "value": 2 },
          "script_noteworthy": { "value": 0 }, "origin": { "v": [1.5, -2, 3] }, "angles": [0, 90, 0] },
        { "index": { "value": 1 }, "name": { "value": 3 }, "target": { "value": 9 },
          "script_noteworthy": { "value": 0 }, "origin": { "v": [4, 5, 6] }, "angles": [0, 0, 0] }
      ] }
    },
    "trigger": { "count": { "value": 3 } },
    "numSubModels": { "value": 4 }
  } }
})";
    const std::string es = entityStringSeed();
    j.replace(j.find("@HEX@"), 5, hexEncode(es));
    j.replace(j.find("@N@"), 3, std::to_string(es.size()));
    return j;
}

const char* kScriptStrings = R"(["", "mp_spawn_a", "mp_target", "mp_spawn_b"])";

// ---- map fuzzing

// A Document edit on a loaded mutant: move its first primitive (regenerates the primitive's lines) or, when there is
// none, set a key on the first entity. Undo must restore `base`, redo must reproduce the edited text.
bool checkEditUndo(Document& d, const std::string& base) {
    Id ent = kNoId, item = kNoId;
    bool havePrim = false;
    for (const auto& e : d.map().entities) {
        for (const auto& it : e.items)
            if (it.isPrimitive()) {
                ent = e.id;
                item = it.id;
                havePrim = true;
                break;
            }
        if (havePrim) break;
    }
    bool changed = false;
    if (havePrim) {
        d.select({ent, item}, false);
        changed = d.translateSelection({8, -8, 0});
    }
    if (!changed && !d.map().entities.empty()) changed = d.setKeyValue(d.map().entities.front().id, "fuzz_key", "1 2 3");
    if (!changed) return false;
    const std::string edited = d.text();
    // Not asserted: edited != base. Document::moveRefs records a transaction for any selected primitive even when
    // translate changes nothing (a primitive the parser could not classify, or a patch with no vertices), so a
    // successful move can leave the text as it was. Undo and redo must still hold either way. See the report note
    // on "return false when nothing changed" in document.h.
    CHECK(d.undo());
    CHECK(d.text() == base);
    CHECK(d.redo());
    CHECK(d.text() == edited);
    CHECK(d.undo());
    CHECK(d.text() == base);
    return true;
}

struct MapFuzzCounts {
    size_t loaded = 0, rejected = 0, edits = 0;
};

// Runs one input through every map invariant: rejection carries a located message; acceptance is byte-exact,
// idempotent and undoable. Failures are reported through CHECK (the caller dumps the input).
void runMapCase(const std::string& in, MapFuzzCounts& n) {
    try {
        Document d;
        std::string err;
        if (d.loadFromText(in, "fuzz.map", &err)) {
            ++n.loaded;
            const std::string out = d.text();
            CHECK(out == in);  // an accepted map is written back byte for byte
            Document again;
            CHECK(again.loadFromText(out, "fuzz.map", &err));  // idempotence: write then reload
            CHECK(again.text() == out);
            n.edits += checkEditUndo(d, in);
        } else {
            ++n.rejected;
            CHECK(err.rfind("fuzz.map:", 0) == 0);  // rejected with a located message
            CHECK(err.find("internal error") == std::string::npos);  // the lossless self-check never fires
        }
    } catch (const std::exception& e) {
        std::printf("    exception: %s\n", e.what());
        CHECK(false);
    } catch (...) {
        CHECK(false);
    }
}

MapFuzzCounts fuzzMap(const char* kind, const std::string& seed, size_t count, uint32_t rngSeed) {
    Rng rng(rngSeed);
    MapFuzzCounts n;
    for (size_t i = 0; i < count; ++i) {
        const std::string in = mutate(rng, seed);
        trace(kind, in);
        const int before = test::failures();
        runMapCase(in, n);
        if (test::failures() != before) dumpFailure(kind, i, in);
    }
    return n;
}

// ---- JSON and MapEnts fuzzing

void writeMutantAndLoad(const fs::path& assetPath, const fs::path& strings, const std::string& in, bool useStrings, bool& ok,
                        std::string& err, Iw8MapEntsDump& out) {
    writeBytes(assetPath, in);
    err.clear();
    ok = loadActsMapEnts(assetPath.string(), useStrings ? strings.string() : std::string(), out, &err);
}

}  // namespace

// ------------------------------------------------------------------ 1. round trip

TEST(fuzz_roundtrip_map_fixtures) {
    const fs::path root = repoRoot();
    CHECK(!root.empty());
    std::vector<fs::path> files;
    if (!root.empty()) {
        std::error_code ec;
        for (const auto& e : fs::directory_iterator(root / "tests" / "data", ec))
            if (e.is_regular_file() && e.path().extension() == ".map") files.push_back(e.path());
        files.push_back(root / "samples" / "tutorial" / "iw8r_tutorial.map");
    }
    std::sort(files.begin(), files.end());
    CHECK(files.size() >= 2);
    for (const auto& p : files) {
        bool ok = false;
        const std::string text = readFixture(p, ok);
        CHECK(ok);
        if (!ok) continue;
        Id next = 1;
        const ParseResult r = parseMap(text, next);
        CHECK(r.map.has_value());
        if (!r.map) {
            std::printf("  parse error in %s: line %zu %s\n", p.string().c_str(), r.error->line, r.error->message.c_str());
            continue;
        }
        CHECK(r.map->text() == text);
        // The Document-level round trip of tests/data/iw8_sample_room.map is already asserted by
        // iw8_sample_room_edit_save_reopen (core_tests.cpp); the tutorial map has no other coverage, so it goes through
        // Document as well.
        if (p.filename().string() == "iw8r_tutorial.map") {
            Document d;
            std::string err;
            CHECK(d.loadFromText(text, p.string(), &err));
            CHECK(d.text() == text);
        }
        std::printf("  ok   %-28s %8zu bytes  %4zu entities\n", p.filename().string().c_str(), text.size(), r.map->entities.size());
    }
}

// ------------------------------------------------------------------ 2. map mutation fuzz

TEST(fuzz_mutation_map_iw8_sample_room) {
    const fs::path root = repoRoot();
    bool ok = false;
    const std::string seed = readFixture(root / "tests" / "data" / "iw8_sample_room.map", ok);
    CHECK(ok);
    if (!ok) return;
    const auto t0 = Clock::now();
    const MapFuzzCounts n = fuzzMap("map_sample", seed, 2000, 0x9E01A001u);
    std::printf("  iw8_sample_room.map: 2000 mutants, %zu accepted (%zu edit+undo checked), %zu rejected, %.2f s\n", n.loaded,
                n.edits, n.rejected, secondsSince(t0));
    CHECK(n.loaded > 0);
    CHECK(n.rejected > 0);
    CHECK(n.edits > 0);
}

TEST(fuzz_mutation_map_tutorial) {
    const fs::path root = repoRoot();
    bool ok = false;
    const std::string seed = readFixture(root / "samples" / "tutorial" / "iw8r_tutorial.map", ok);
    CHECK(ok);
    if (!ok) return;
    const auto t0 = Clock::now();
    const MapFuzzCounts n = fuzzMap("map_tutorial", seed, 2000, 0x9E01A002u);
    std::printf("  iw8r_tutorial.map: 2000 mutants, %zu accepted (%zu edit+undo checked), %zu rejected, %.2f s\n", n.loaded,
                n.edits, n.rejected, secondsSince(t0));
    CHECK(n.loaded > 0);
    CHECK(n.rejected > 0);
    CHECK(n.edits > 0);
}

// Hand-written edge cases that random mutation rarely produces. Same invariants as the mutants.
TEST(fuzz_map_targeted_edge_inputs) {
    const std::string cases[] = {
        "",
        "\n",
        std::string("\0", 1),
        "\xEF\xBB\xBFiwmap 4\n{\n\"classname\" \"worldspawn\"\n}\n",                 // UTF-8 BOM before the header
        "iwmap 4\r{\r\"classname\" \"worldspawn\"\r}\r",                              // lone CRs, no LF at all
        "iwmap 4\n{\n\"classname\" \"worldspawn\"\n}{\n}\n",                          // "}{" on one line
        "iwmap 4\n{\n\"classname\" \"worldspawn\"\n\"x\n}\n",                         // unterminated key quote
        "iwmap 4\n{\n\"classname\" \"worldspawn\"\n\"_c\" \"{}{\"\n}\n",              // braces inside a quoted value
        "iwmap 4\n{\n\"classname\" \"worldspawn\"\n18446744073709551616 \"x\" \"y\"\n}\n",  // canonical id 2^64 (wraps)
        "iwmap 4\n{\n\"classname\" \"worldspawn\"\n4294967295 \"x\" \"y\"\n4294967296 \"x\" \"y\"\n}\n",
        "iwmap 4\n{\n\"classname\" \"worldspawn\"\n0 \"\" \"v\"\n0 \"n\" \"v\"\n}\n",     // empty and bare script-string keys
        "iwmap 4\n{\n\"classname\" \"worldspawn\"\n{\n( nan 0 0 ) ( 0 inf 0 ) ( 0 0 -inf ) caulk\n}\n}\n",  // non-finite
        "iwmap 4\n{\n\"classname\" \"worldspawn\"\n{\n( 1e400 0 0 ) ( 0 1 0 ) ( 1 1 0 ) caulk\n}\n}\n",      // overflow
        "iwmap 4\n{\n\"classname\" \"worldspawn\"\n{\n( 1e308 0 0 ) ( 0 1e308 0 ) ( 0 0 1e308 ) caulk 0 0 0 0 0 0\n}\n}\n",
        "iwmap 4\n{\n\"classname\" \"worldspawn\"\n{\n( 0 0 0 ) ( 0 0 1 ) ( 0 1 0 ) caulk\n}}\n}\n",             // "}}" closes twice
        "iwmap 4\n{\n\"classname\" \"worldspawn\"\n{\n( 0 0 0 ) ( 0 0 1 ) ( 0 1 0 ) caulk\n}\n} // trailing\n",
        "iwmap 4\n{\n\"classname\" \"worldspawn\"\n{\n\tmesh\n{\n( ( 0 0 0 ) )\n}\n}\n}\n",
        "iwmap 4\n{\n\"classname\" \"worldspawn\"\n{\n\tcurve\n{\n\t(\n\tv 1e400 2 3 t 0 0\n\tv 1 2 nan t 0 0\n\t)\n}\n}\n}\n",
        "iwmap 4\n{\n\"classname\" \"worldspawn\"\n// unterminated\n{\n",
        "iwmap 4\n{\n\"classname\" \"worldspawn\"\n}\n\n\n\n// tail only\n",
        "// header comment only\n",
    };
    MapFuzzCounts n;
    for (const auto& c : cases) {
        const int before = test::failures();
        runMapCase(c, n);
        if (test::failures() != before) dumpFailure("targeted", size_t(&c - cases), c);
    }
    std::printf("  %zu targeted inputs: %zu accepted, %zu rejected, %zu edit+undo checked\n", std::size(cases), n.loaded,
                n.rejected, n.edits);
}

// ------------------------------------------------------------------ 3. json_lite and entity-string reader fuzz

TEST(fuzz_json_lite_mutations) {
    const std::string seed = jsonSeed();
    std::string err;
    CHECK(json::parse(seed, &err).has_value());  // the seed itself is valid JSON
    Rng rng(0x9E01A003u);
    size_t loaded = 0, rejected = 0;
    const auto t0 = Clock::now();
    for (size_t i = 0; i < 500; ++i) {
        const std::string in = mutate(rng, seed);
        trace("json", in);
        const int before = test::failures();
        try {
            std::string perr;
            const auto v = json::parse(in, &perr);
            if (v) {
                ++loaded;
            } else {
                ++rejected;
                CHECK(!perr.empty());  // nullopt always comes with a byte-offset message
            }
        } catch (const std::exception& e) {
            std::printf("    exception: %s\n", e.what());
            CHECK(false);
        } catch (...) {
            CHECK(false);
        }
        if (test::failures() != before) dumpFailure("json", i, in);
    }
    std::printf("  json_lite: 500 mutants, %zu accepted, %zu rejected, %.2f s\n", loaded, rejected, secondsSince(t0));
    CHECK(rejected > 0);
}

TEST(fuzz_json_lite_garbage_is_rejected) {
    // Fixed garbage: each must come back as nullopt with a message, never as a value or an exception.
    const std::string garbage[] = {
        "", " ", "[", "]", "{", "{\"a\"", "{\"a\":}", "[1,]", "[1 2]", "\"abc", "1e999", "-", "+1", "tru", "nul",
        "\xff\xfe", "{\"a\":1} x", std::string("[\0]", 3), "\"\\x\"", "\"\\u12\"", "{\"a\":1,}",
    };
    for (const auto& g : garbage) {
        std::string err;
        bool threw = false;
        std::optional<json::Value> v;
        try {
            v = json::parse(g, &err);
        } catch (...) {
            threw = true;
        }
        CHECK(!threw);
        CHECK(!v.has_value());
        CHECK(!err.empty());
    }
    // Nesting is bounded (512 levels): 500 levels parse, 100000 levels are refused without recursing that deep.
    std::string ok500(500, '['), bad(100000, '['), badObj;
    ok500 += std::string(500, ']');
    CHECK(json::parse(ok500, nullptr).has_value());
    std::string err;
    CHECK(!json::parse(bad, &err).has_value());
    CHECK(err.find("nesting") != std::string::npos);
    for (int i = 0; i < 100000; ++i) badObj += "{\"a\":";
    err.clear();
    CHECK(!json::parse(badObj, &err).has_value());
    CHECK(err.find("nesting") != std::string::npos);
}

TEST(fuzz_iw8_mapents_mutations) {
    const fs::path dir = scratch("iw8");
    const fs::path seedPath = dir / "seed_asset.json";
    const fs::path strings = dir / "script_strings.json";
    const fs::path mutantPath = dir / "mutant_asset.json";
    writeBytes(strings, kScriptStrings);

    // The seed must load, so the mutations start from a real dump.
    writeBytes(seedPath, jsonSeed());
    Iw8MapEntsDump seedOut;
    std::string err;
    CHECK(loadActsMapEnts(seedPath.string(), strings.string(), seedOut, &err));
    CHECK(seedOut.entityString == entityStringSeed());
    CHECK(seedOut.numEntityChars == seedOut.entityString.size());
    CHECK(seedOut.spawns.size() == 2);
    if (seedOut.spawns.size() == 2) {
        CHECK(seedOut.spawns[0].name == "mp_spawn_a");
        CHECK(seedOut.spawns[1].target == "#9");  // index beyond the script string table stays numeric
    }

    const std::string seed = jsonSeed();
    Rng rng(0x9E01A004u);
    size_t accepted = 0, rejected = 0;
    const auto t0 = Clock::now();
    for (size_t i = 0; i < 300; ++i) {
        const std::string in = mutate(rng, seed);
        trace("iw8", in);
        const int before = test::failures();
        try {
            const bool useStrings = (i % 4) != 0;
            bool ok = false;
            Iw8MapEntsDump out;
            writeMutantAndLoad(mutantPath, strings, in, useStrings, ok, err, out);
            if (ok) {
                ++accepted;
                CHECK(out.numEntityChars == out.entityString.size());  // the reader's own consistency check
                CHECK(out.profile == "replay-1.20");
            } else {
                ++rejected;
                CHECK(!err.empty());
                CHECK(err.rfind(mutantPath.string(), 0) == 0);  // messages name the file they came from
            }
        } catch (const std::exception& e) {
            std::printf("    exception: %s\n", e.what());
            CHECK(false);
        } catch (...) {
            CHECK(false);
        }
        if (test::failures() != before) dumpFailure("iw8", i, in);
    }
    std::printf("  iw8 MapEnts reader: 300 mutants, %zu accepted, %zu rejected, %.2f s\n", accepted, rejected, secondsSince(t0));
    CHECK(rejected > 0);
}

TEST(fuzz_iw8_mapents_garbage_is_rejected) {
    const fs::path dir = scratch("iw8");
    const fs::path strings = dir / "script_strings.json";
    writeBytes(strings, kScriptStrings);
    const fs::path p = dir / "garbage_asset.json";
    const std::string garbage[] = {"", "null", "[]", "{}", "[1,2,3]", std::string("\x00\x01\x02", 3), "{\"format\":\"x\"",
                                   "{\"format\":\"mw19-asset-json\",\"profile\":\"replay-1.19\",\"pool\":\"map_ents\"}"};
    for (const auto& g : garbage) {
        writeBytes(p, g);
        Iw8MapEntsDump out;
        std::string err;
        bool threw = false, ok = true;
        try {
            ok = loadActsMapEnts(p.string(), strings.string(), out, &err);
        } catch (...) {
            threw = true;
        }
        CHECK(!threw);
        CHECK(!ok);
        CHECK(!err.empty());
    }
    Iw8MapEntsDump out;
    std::string err;
    CHECK(!loadActsMapEnts((dir / "does_not_exist.json").string(), strings.string(), out, &err));
    CHECK(!err.empty());
}

// ------------------------------------------------------------------ 4. huge and deep inputs

TEST(fuzz_huge_50mb_map_line) {
    const auto t0 = Clock::now();
    const std::string blob(50u << 20, 'q');
    const std::string in = "iwmap 4\r\n{\r\n\"classname\" \"worldspawn\"\r\n\"_blob\" \"" + blob + "\"\r\n}\r\n";
    Document d;
    std::string err;
    CHECK(d.loadFromText(in, "huge_line.map", &err));
    // worldspawn: the classname line and the blob line.
    CHECK(d.map().entities.size() == 1);
    if (d.map().entities.size() == 1) CHECK(d.map().entities[0].items.size() == 2);
    CHECK(d.text() == in);
    const double s = secondsSince(t0);
    std::printf("  50 MB line: load + write in %.2f s\n", s);
    CHECK(s < 30.0);
}

TEST(fuzz_huge_100k_entities) {
    const auto t0 = Clock::now();
    std::string in = "iwmap 4\n";
    in.reserve(100000 * 96);
    for (int i = 0; i < 100000; ++i)
        in += "{\n\"classname\" \"info_player_start\"\n\"origin\" \"" + std::to_string(i) + " 0 0\"\n}\n";
    Id next = 1;
    const ParseResult r = parseMap(in, next);
    CHECK(r.map.has_value());
    if (r.map) {
        CHECK(r.map->entities.size() == 100000);
        CHECK(r.map->text() == in);
    }
    const double s = secondsSince(t0);
    std::printf("  100k entities: parse + write in %.2f s\n", s);
    CHECK(s < 30.0);
}

TEST(fuzz_deep_brace_nesting) {
    // A primitive whose body nests 200000 levels deep on separate lines: the scan is a depth counter, not recursion.
    const auto t0 = Clock::now();
    const size_t depth = 200000;
    std::string in = "iwmap 4\n{\n\"classname\" \"worldspawn\"\n{\n";
    in.reserve(in.size() + depth * 4 + (depth + 1) * 2 + 8);
    for (size_t i = 0; i < depth; ++i) in += "{\n";
    for (size_t i = 0; i < depth + 1; ++i) in += "}\n";  // closes the nesting, the primitive, then the entity is closed below
    in += "}\n";
    Id next = 1;
    const ParseResult r = parseMap(in, next);
    CHECK(r.map.has_value());
    if (r.map) CHECK(r.map->text() == in);

    // The same depth on one line at top level is refused (an entity must open on a line of its own).
    const std::string oneLine(100000, '{');
    Id n2 = 1;
    const ParseResult bad = parseMap(oneLine, n2);
    CHECK(!bad.map.has_value());
    CHECK(bad.error.has_value());

    std::printf("  %zu-level brace nesting: %.2f s\n", depth, secondsSince(t0));
    CHECK(secondsSince(t0) < 30.0);
}
