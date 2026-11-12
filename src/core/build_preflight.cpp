#include "build_preflight.h"

#include <cstdlib>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <map>
#include <optional>
#include <set>

#include "json_lite.h"
#include "map_text.h"
#include "script_check.h"

namespace iw8r {

namespace {

namespace fs = std::filesystem;

std::string lowerStr(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool identStart(char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_'; }
bool identChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }
bool isWs(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }
// Characters of a script path such as scripts\mp\utility\game (the part before `::`).
bool pathChar(char c) { return identChar(c) || c == '\\' || c == '/'; }

// Language keywords of the GSC engine. A keyword followed by `(` is not a call of a builtin, and the name after `thread`
// or `childthread` is a call of a function, so keywords also tell the scan whether a name is a method or a function.
const std::set<std::string>& keywords() {
    static const std::set<std::string> k = {"if",     "else",   "for",   "foreach",  "while",   "do",     "switch",
                                            "case",   "default", "break", "continue", "return", "wait",   "waittill",
                                            "waittillmatch", "waittillframeend", "notify", "endon", "thread", "childthread",
                                            "isdefined", "undefined", "true", "false", "new", "self", "level", "game", "in"};
    return k;
}

// A name is a plain relative path inside the build: no traversal, no drive, no backslash.
bool safeName(const std::string& s) {
    if (s.empty() || s.size() > 260) return false;
    if (s.find("..") != std::string::npos || s[0] == '/' || s.find('\\') != std::string::npos || s.find(':') != std::string::npos)
        return false;
    return true;
}

struct Diag {
    int line;
    std::string message;
};

// The script with comments and string contents blanked (newlines and the string quotes' positions kept, so offsets in the
// result match the text). Unterminated comments and strings are reported. Same approach as script_check.cpp.
std::string blankCode(const std::string& text, std::vector<Diag>& diags) {
    std::string out = text;
    int line = 1;
    const size_t n = text.size();
    for (size_t i = 0; i < n;) {
        const char c = text[i];
        if (c == '\n') {
            ++line;
            ++i;
        } else if (c == '/' && i + 1 < n && text[i + 1] == '/') {
            while (i < n && text[i] != '\n') out[i++] = ' ';
        } else if (c == '/' && i + 1 < n && text[i + 1] == '*') {
            const int start = line;
            bool closed = false;
            while (i < n) {
                if (text[i] == '*' && i + 1 < n && text[i + 1] == '/') {
                    out[i] = out[i + 1] = ' ';
                    i += 2;
                    closed = true;
                    break;
                }
                if (text[i] == '\n') ++line;
                else out[i] = ' ';
                ++i;
            }
            if (!closed) diags.push_back({start, "comment opened with /* is never closed"});
        } else if (c == '"') {
            const int start = line;
            out[i++] = ' ';
            bool closed = false;
            while (i < n && text[i] != '\n') {
                if (text[i] == '\\' && i + 1 < n && text[i + 1] != '\n') {
                    out[i] = out[i + 1] = ' ';
                    i += 2;
                    continue;
                }
                if (text[i] == '"') {
                    out[i++] = ' ';
                    closed = true;
                    break;
                }
                out[i++] = ' ';
            }
            if (!closed) diags.push_back({start, "string is not closed on this line"});
        } else {
            ++i;
        }
    }
    return out;
}

size_t skipWs(const std::string& s, size_t i) {
    while (i < s.size() && isWs(s[i])) ++i;
    return i;
}

// The index of the `)` that closes the `(` at `open` in `code`, or npos.
size_t matchClose(const std::string& code, size_t open) {
    int depth = 0;
    for (size_t i = open; i < code.size(); ++i) {
        if (code[i] == '(') ++depth;
        else if (code[i] == ')' && --depth == 0) return i;
    }
    return std::string::npos;
}

// The string literal whose opening quote is at `q` in the original text (empty when it is not closed on its line).
std::string literalAt(const std::string& text, size_t q, size_t* endOut) {
    const size_t e = text.find_first_of("\"\n", q + 1);
    if (e == std::string::npos || text[e] != '"') {
        if (endOut) *endOut = e == std::string::npos ? text.size() : e;
        return "";
    }
    if (endOut) *endOut = e + 1;
    return text.substr(q + 1, e - q - 1);
}

// Effects and sounds the scripts refer to: lower-case name -> first place it is used (for the warning text).
struct Refs {
    std::map<std::string, std::string> effects;
    std::map<std::string, std::string> sounds;
};

// Checks the map script (generated part plus the appended user script). Calls are checked against the definitions in the
// file, the builtins and the converter names. Names inside comments and strings are not calls.
void checkScript(const std::string& text, const std::string& file, const std::string& mapId, const PreflightCatalogs& cat,
                 std::vector<PreflightIssue>& out, Refs& refs) {
    std::vector<Diag> diags;
    const std::string code = blankCode(text, diags);
    for (const Diag& d : diags) out.push_back({true, "script", file + ":" + std::to_string(d.line) + ": " + d.message});

    // Brackets: same rules as checkUserScript, on the whole file.
    struct Open {
        char ch;
        int line;
    };
    std::vector<Open> stack;
    int line = 1;
    for (size_t i = 0; i < code.size(); ++i) {
        const char c = code[i];
        if (c == '\n') {
            ++line;
        } else if (c == '(' || c == '{' || c == '[') {
            stack.push_back({c, line});
        } else if (c == ')' || c == '}' || c == ']') {
            const char want = c == ')' ? '(' : c == '}' ? '{' : '[';
            if (stack.empty()) {
                out.push_back({true, "script", file + ":" + std::to_string(line) + ": '" + c + "' has nothing to close"});
            } else {
                if (stack.back().ch != want)
                    out.push_back({true, "script", file + ":" + std::to_string(line) + ": '" + c + "' closes the '" + stack.back().ch +
                                                      "' opened on line " + std::to_string(stack.back().line)});
                stack.pop_back();
            }
        }
    }
    for (const Open& o : stack)
        out.push_back({true, "script", file + ":" + std::to_string(o.line) + ": '" + o.ch + "' is never closed"});

    // The user script (worldspawn iw8r_script) is appended after this marker line (map_script.cpp). checkUserScript adds
    // the #include / #using rule, which the generated part never breaks; its bracket and name messages are covered above
    // and by the whole-file duplicate check below, so only that rule is kept.
    const std::string marker = "// ---- map script (worldspawn iw8r_script) ----\n";
    const size_t mpos = text.find(marker);
    if (mpos != std::string::npos) {
        const size_t start = mpos + marker.size();
        const int offset = int(std::count(text.begin(), text.begin() + long(start), '\n'));
        for (const ScriptIssue& si : checkUserScript(text.substr(start)))
            if (si.message.find("cannot be used") != std::string::npos)
                out.push_back({true, "script", file + ":" + std::to_string(offset + si.line) + ": " + si.message});
    }

    // Definitions. Each one is a name followed by `(...)` and `{` at the top level.
    std::set<std::string> defined;
    std::set<std::string> seenDefs;
    for (const std::string& name : scriptFunctionNames(text)) {
        const std::string l = lowerStr(name);
        if (!seenDefs.insert(l).second) out.push_back({true, "script", file + ": " + name + "() is defined twice"});
        defined.insert(l);
    }
    if (!defined.count("main")) out.push_back({true, "script", file + ": main() is missing; the game starts the map with it"});

    std::vector<size_t> newlines;
    for (size_t i = 0; i < text.size(); ++i)
        if (text[i] == '\n') newlines.push_back(i);
    const auto lineOf = [&](size_t pos) {
        return size_t(std::lower_bound(newlines.begin(), newlines.end(), pos) - newlines.begin()) + 1;
    };
    const std::string ownPath = lowerStr("maps\\mp\\" + mapId);

    const size_t n = code.size();
    int depth = 0;
    for (size_t i = 0; i < n;) {
        const char c = code[i];
        if (c == '{') {
            ++depth;
            ++i;
            continue;
        }
        if (c == '}') {
            if (depth > 0) --depth;
            ++i;
            continue;
        }
        if (!identStart(c) || (i > 0 && identChar(code[i - 1]))) {
            ++i;
            continue;
        }
        size_t e = i;
        while (e < n && identChar(code[e])) ++e;
        const size_t open = skipWs(code, e);
        if (open >= n || code[open] != '(') {
            i = e;
            continue;
        }
        const size_t close = matchClose(code, open);
        if (close == std::string::npos) {  // unbalanced: already reported
            i = e;
            continue;
        }
        const std::string name = code.substr(i, e - i);
        const std::string lname = lowerStr(name);
        const size_t after = skipWs(code, close + 1);
        if (depth == 0 && after < n && code[after] == '{') {  // a definition header
            i = e;
            continue;
        }
        if (keywords().count(lname)) {
            i = e;
            continue;
        }
        const std::string where = file + ":" + std::to_string(lineOf(i));

        // path\to\file::fn(  (a qualified call into another script)
        if (i >= 2 && code[i - 1] == ':' && code[i - 2] == ':') {
            const size_t p = i - 2;
            size_t b = p;
            while (b > 0 && pathChar(code[b - 1])) --b;
            const std::string path = code.substr(b, p - b);
            if (lowerStr(path) == ownPath) {
                if (!defined.count(lname))
                    out.push_back({true, "script", where + ": " + path + "::" + name + "() is not defined in this map script"});
            } else if (path.empty() || path.find('/') != std::string::npos || path.find(".gsc") != std::string::npos) {
                out.push_back({false, "script", where + ": '" + path + "::" + name + "()': GSC paths use backslashes and no extension"});
            }
            i = e;
            continue;
        }

        // The identifier before the name decides method (`ent name()`) or function (`name()`, `thread name()`).
        std::string prev;
        size_t pe = i;
        while (pe > 0 && isWs(code[pe - 1])) --pe;
        if (pe > 0 && identChar(code[pe - 1])) {
            size_t pb = pe;
            while (pb > 0 && identChar(code[pb - 1])) --pb;
            if (identStart(code[pb])) prev = lowerStr(code.substr(pb, pe - pb));
        }
        // self / level / game are entities: `self takeallweapons()` is a method call.
        if (!prev.empty() && (!keywords().count(prev) || prev == "self" || prev == "level" || prev == "game")) {
            if (!cat.methods.count(lname))
                out.push_back({true, "script", where + ": unknown method '" + name + "()' called on '" + prev + "'"});
        } else if (!defined.count(lname) && !cat.functions.count(lname) && !cat.converterFunctions.count(lname)) {
            out.push_back({true, "script", where + ": unknown function '" + name +
                                              "()': not a builtin, not defined in this script, not a converter name"});
        }

        // Literal names: loadfx("effect") and the sound aliases of the play calls.
        if (lname == "loadfx") {
            const size_t a = skipWs(text, open + 1);
            if (a < text.size() && text[a] == '"') {
                const std::string v = literalAt(text, a, nullptr);
                if (!v.empty()) refs.effects.emplace(lowerStr(v), where);
            }
        } else if (lname == "playsound" || lname == "playloopsound" || lname == "playsoundatpos") {
            for (size_t q = open + 1; q < close; ++q) {
                if (text[q] != '"') continue;
                size_t end = q + 1;
                const std::string v = literalAt(text, q, &end);
                if (!v.empty()) refs.sounds.emplace(lowerStr(v), where);
                q = end - 1;
            }
        }
        i = e;
    }
}

// The entity blocks of a .ents file: each block is a list of key / value pairs in file order.
using EntityKeys = std::vector<std::pair<std::string, std::string>>;
bool parseEnts(const std::string& text, std::vector<EntityKeys>& out, std::string* err) {
    bool open = false;
    std::vector<std::string> strings;
    for (size_t i = 0; i < text.size();) {
        const char c = text[i];
        if (c == '{') {
            if (open) {
                if (err) *err = "entity block opened inside another at offset " + std::to_string(i);
                return false;
            }
            open = true;
            strings.clear();
            ++i;
        } else if (c == '}') {
            if (!open) {
                if (err) *err = "'}' outside an entity block at offset " + std::to_string(i);
                return false;
            }
            EntityKeys kv;
            for (size_t k = 0; k + 1 < strings.size(); k += 2) kv.push_back({strings[k], strings[k + 1]});
            out.push_back(std::move(kv));
            open = false;
            ++i;
        } else if (c == '"') {
            const size_t e = text.find('"', i + 1);
            if (e == std::string::npos) {
                if (err) *err = "unterminated string at offset " + std::to_string(i);
                return false;
            }
            if (open) strings.push_back(text.substr(i + 1, e - i - 1));
            i = e + 1;
        } else {
            ++i;
        }
    }
    if (open) {
        if (err) *err = "entity block is never closed";
        return false;
    }
    return true;
}

// Every string stored under `key` anywhere in the document (surface materials, world model names).
void collectStrings(const json::Value& v, const std::string& key, const std::string& where, std::map<std::string, std::string>& into) {
    if (v.type == json::Value::Type::Object) {
        for (const auto& [k, child] : v.obj) {
            if (k == key)
                if (const std::string* s = child.string(); s && !s->empty()) into.emplace(*s, where);
            collectStrings(child, key, where, into);
        }
    } else if (v.type == json::Value::Type::Array) {
        for (const json::Value& child : v.arr) collectStrings(child, key, where, into);
    }
}

// Names of a catalog file: the first tab-separated column of each row, `#` lines skipped.
bool readCatalogNames(const fs::path& file, std::set<std::string>& out, std::string* err) {
    std::string text;
    if (!readFile(file.string(), text)) {
        if (err) *err = "cannot read " + file.string();
        return false;
    }
    size_t pos = 0;
    while (pos < text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        std::string row = text.substr(pos, end - pos);
        pos = end + 1;
        if (row.empty() || row[0] == '#') continue;
        if (const size_t tab = row.find('\t'); tab != std::string::npos) row.resize(tab);
        while (!row.empty() && (row.back() == '\r' || row.back() == ' ')) row.pop_back();
        if (!row.empty()) out.insert(lowerStr(row));
    }
    return true;
}

}  // namespace

bool parseEntityBlocks(const std::string& text, std::vector<EntityKeyValues>& out, std::string* err) {
    return parseEnts(text, out, err);
}

bool loadPreflightCatalogs(const std::string& dataDir, PreflightCatalogs& cat, std::string* err) {
    const fs::path data(dataDir);
    // Builtins: `f<TAB>name` (functions) and `m<TAB>name` (methods); other lines are comments.
    const fs::path builtins = data / "generated" / "iw8_gsc_builtins.txt";
    std::string text;
    if (!readFile(builtins.string(), text)) {
        if (err) *err = "cannot read " + builtins.string() + " (use --data <dataDir>)";
        return false;
    }
    size_t pos = 0;
    while (pos < text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        const std::string row = text.substr(pos, end - pos);
        pos = end + 1;
        if (row.size() < 3 || row[0] == '#' || row[1] != '\t') continue;
        std::string name = row.substr(2);
        while (!name.empty() && (name.back() == '\r' || name.back() == ' ')) name.pop_back();
        if (row[0] == 'f') cat.functions.insert(lowerStr(name));
        else if (row[0] == 'm') cat.methods.insert(lowerStr(name));
    }
    if (cat.functions.empty() || cat.methods.empty()) {
        if (err) *err = "no builtins read from " + builtins.string();
        return false;
    }
    if (!readCatalogNames(data / "asset_catalog" / "iw8_common_vfx.tsv", cat.effects, err)) return false;
    if (!readCatalogNames(data / "asset_catalog" / "iw8_common_sounds.tsv", cat.sounds, err)) return false;
    return true;
}

std::vector<PreflightIssue> preflightExtraction(const std::string& extractedDir, const std::string& mapId,
                                                const PreflightCatalogs& cat) {
    std::vector<PreflightIssue> out;
    const auto error = [&](const char* category, const std::string& msg) { out.push_back({true, category, msg}); };
    const auto warning = [&](const char* category, const std::string& msg) { out.push_back({false, category, msg}); };

    if (!safeName(mapId) || mapId.find('/') != std::string::npos) {
        error("build", "map id '" + mapId + "' is not a plain name");
        return out;
    }
    const fs::path root(extractedDir);
    if (!fs::is_directory(root)) {
        error("build", "extracted folder not found: " + extractedDir);
        return out;
    }

    // Files of the map (compile_iw8.cpp writes them under maps\mp\<id>).
    const fs::path mapDir = root / "maps" / "mp";
    const std::string mapPrefix = "maps/mp/" + mapId;
    std::string gsc, entsText, worldText;
    const bool haveGsc = readFile((mapDir / (mapId + ".gsc")).string(), gsc);
    const bool haveEnts = readFile((mapDir / (mapId + ".d3dbsp.ents")).string(), entsText);
    const bool haveWorld = readFile((mapDir / (mapId + ".d3dbsp.replay-world.json")).string(), worldText);
    if (!haveGsc) error("build", "missing " + mapPrefix + ".gsc: the map has no script");
    if (!haveEnts) error("build", "missing " + mapPrefix + ".d3dbsp.ents: the map has no entities");
    if (!haveWorld) error("build", "missing " + mapPrefix + ".d3dbsp.replay-world.json: the map has no world");

    // What the build refers to. First place of each name is kept for the messages.
    Refs refs;
    std::map<std::string, std::string> materialWhere;  // material name -> first user
    std::map<std::string, std::string> imageWhere;     // image name -> first user
    std::map<std::string, std::string> modelWhere;     // xmodel name -> first user

    if (haveGsc) {
        const size_t firstScriptIssue = out.size();
        checkScript(gsc, mapPrefix + ".gsc", mapId, cat, out, refs);
        // The mapper's own script (worldspawn iw8r_script) is appended after a marker line, so a line number in the
        // combined script means nothing to them (owner playtest 2026-11-11). Messages about lines after the marker say
        // the line in the mapper's file first.
        const std::string marker = "// ---- map script (worldspawn iw8r_script) ----\n";
        if (const size_t mpos = gsc.find(marker); mpos != std::string::npos) {
            const int offset = int(std::count(gsc.begin(), gsc.begin() + long(mpos + marker.size()), '\n'));
            const std::string prefix = mapPrefix + ".gsc:";
            for (size_t i = firstScriptIssue; i < out.size(); ++i) {
                std::string& m = out[i].message;
                if (m.compare(0, prefix.size(), prefix) != 0) continue;
                size_t end = prefix.size();
                while (end < m.size() && std::isdigit(static_cast<unsigned char>(m[end]))) ++end;
                if (end == prefix.size()) continue;
                const int line = std::atoi(m.substr(prefix.size(), end - prefix.size()).c_str());
                if (line <= offset) continue;
                m = "your map script, line " + std::to_string(line - offset) + " (line " + std::to_string(line) +
                    " of the combined " + mapPrefix + ".gsc)" + m.substr(end);
            }
        }
    }

    if (haveEnts) {
        std::vector<EntityKeys> entities;
        std::string perr;
        if (!parseEnts(entsText, entities, &perr)) {
            error("build", mapPrefix + ".d3dbsp.ents: " + perr);
        } else {
            for (size_t i = 0; i < entities.size(); ++i) {
                const std::string where = "entity " + std::to_string(i) + " of " + mapPrefix + ".d3dbsp.ents";
                for (const auto& [key, value] : entities[i]) {
                    if (value.empty()) continue;
                    if (key == "model" && value[0] != '*') modelWhere.emplace(value, where);  // *N is a brush model
                    else if (key == "fx") refs.effects.emplace(lowerStr(value), where);
                    else if (key == "soundalias") refs.sounds.emplace(lowerStr(value), where);
                }
            }
        }
    }

    if (haveWorld) {
        std::string jerr;
        const std::optional<json::Value> world = json::parse(worldText, &jerr);
        if (!world) {
            error("build", mapPrefix + ".d3dbsp.replay-world.json does not parse: " + jerr);
        } else {
            const std::string where = "the map surfaces";
            // Decal volumes may name a stock decal material the game already has loaded (research 80), so their
            // materials are not required to be in the build; a decal material that IS in the build is checked like any
            // other through its own file.
            for (const auto& [member, value] : world->obj)
                if (member != "decal_volumes") collectStrings(value, "material", where, materialWhere);
            if (const json::Value* sky = world->get("sky"))
                if (const std::string* s = sky->string(); s && !s->empty()) imageWhere.emplace(*s, "the world sky");
            if (const json::Value* probes = world->get("reflection_probes"))
                for (const json::Value& p : probes->arr)
                    if (const json::Value* im = p.get("image"))
                        if (const std::string* s = im->string(); s && !s->empty()) imageWhere.emplace(*s, "a reflection probe");
            if (const json::Value* models = world->get("models"))
                for (const json::Value& m : models->arr)
                    if (const json::Value* mv = m.get("model"))
                        if (const std::string* s = mv->string(); s && !s->empty()) modelWhere.emplace(*s, "the world models");
        }
    }

    // Models the converter carries from a stock zone (collision json carry_assets, e.g. the model of a vehicle spawn)
    // are not files of the extraction: the converter adds them when it writes the zone.
    {
        std::string colText, cerr;
        if (readFile((mapDir / (mapId + ".d3dbsp.replay-collision.json")).string(), colText))
            if (const std::optional<json::Value> col = json::parse(colText, &cerr))
                if (const json::Value* carry = col->get("carry_assets"); carry && carry->type == json::Value::Type::Array)
                    for (const json::Value& entry : carry->arr) {
                        const json::Value* type = entry.get("type");
                        const json::Value* name = entry.get("name");
                        if (type && type->string() && *type->string() == "xmodel" && name && name->string())
                            for (auto it = modelWhere.begin(); it != modelWhere.end();)
                                it = it->first == *name->string() ? modelWhere.erase(it) : std::next(it);
                    }
    }
    // Models: xmodel\<name>.json (the table) and .replay.json (the geometry, with the surface materials).
    for (const auto& [model, where] : modelWhere) {
        if (!safeName(model) || model.find('/') != std::string::npos) {
            error("model", "model '" + model + "' (" + where + ") is not a plain xmodel name");
            continue;
        }
        const fs::path table = root / "xmodel" / (model + ".json");
        const fs::path geometry = root / "xmodel" / (model + ".replay.json");
        if (!fs::exists(table)) error("model", "model '" + model + "' used by " + where + " is not in the build (xmodel/" + model + ".json)");
        if (!fs::exists(geometry)) {
            error("model", "model '" + model + "' used by " + where + " has no geometry (xmodel/" + model + ".replay.json)");
            continue;
        }
        std::string text, jerr;
        if (!readFile(geometry.string(), text)) {
            error("build", "cannot read xmodel/" + model + ".replay.json");
            continue;
        }
        const std::optional<json::Value> v = json::parse(text, &jerr);
        if (!v) {
            error("build", "xmodel/" + model + ".replay.json does not parse: " + jerr);
            continue;
        }
        collectStrings(*v, "material", "model '" + model + "'", materialWhere);
    }

    // Materials: materials\<name>.json, unless $-named (engine built-ins). Their textures name images.
    for (const auto& [mat, where] : materialWhere) {
        if (mat[0] == '$') continue;
        if (!safeName(mat)) {
            error("material", "material '" + mat + "' used by " + where + " is not a plain material name");
            continue;
        }
        const fs::path file = root / "materials" / (mat + ".json");
        std::string text, jerr;
        if (!readFile(file.string(), text)) {
            error("material", "material '" + mat + "' used by " + where + " is not in the build (materials/" + mat + ".json)");
            continue;
        }
        const std::optional<json::Value> v = json::parse(text, &jerr);
        if (!v) {
            error("material", "materials/" + mat + ".json does not parse: " + jerr);
            continue;
        }
        if (const json::Value* tex = v->get("textures"))
            for (const json::Value& t : tex->arr)
                if (const json::Value* im = t.get("image"))
                    if (const std::string* s = im->string(); s && !s->empty()) imageWhere.emplace(*s, "material '" + mat + "'");
    }

    // Images: images\<name>.dds. Built-in $ images always exist.
    for (const auto& [img, where] : imageWhere) {
        if (img[0] == '$') continue;
        if (!safeName(img) || img.find('/') != std::string::npos) {
            error("image", "image '" + img + "' used by " + where + " is not a plain image name");
            continue;
        }
        if (!fs::exists(root / "images" / (img + ".dds")))
            error("image", "image '" + img + "' used by " + where + " is not in the build (images/" + img + ".dds)");
    }

    // Sound aliases the generated script plays through variables: listed in sound\<id>.stock_sounds.txt.
    std::string stock;
    if (readFile((root / "sound" / (mapId + ".stock_sounds.txt")).string(), stock)) {
        size_t pos = 0;
        while (pos < stock.size()) {
            size_t end = stock.find('\n', pos);
            if (end == std::string::npos) end = stock.size();
            std::string row = stock.substr(pos, end - pos);
            pos = end + 1;
            const size_t sp = row.find_first_of(" \t\r");
            const std::string alias = row.substr(0, sp);
            if (!alias.empty()) refs.sounds.emplace(lowerStr(alias), "sound/" + mapId + ".stock_sounds.txt");
        }
    }

    // Effects and sounds: warnings only. A name in the always-loaded catalogs needs no file; anything else is looked for in
    // the build: fx\<name>.iw3.json (8.2 authored effects, the converter's IW3 effect route) or a hand-placed fx\<name>.json.
    for (const auto& [fx, where] : refs.effects) {
        if (cat.effects.count(fx)) continue;
        if (safeName(fx) && (fs::exists(root / "fx" / (fx + ".json")) || fs::exists(root / "fx" / (fx + ".iw3.json")))) continue;
        warning("effect", "effect '" + fx + "' used by " + where + " is not in the always-loaded effects and not in the build; it may not play");
    }
    for (const auto& [snd, where] : refs.sounds) {
        if (cat.sounds.count(snd)) continue;
        warning("sound", "sound alias '" + snd + "' used by " + where + " is not in the always-loaded sound catalog; it may be silent");
    }
    return out;
}

}  // namespace iw8r
