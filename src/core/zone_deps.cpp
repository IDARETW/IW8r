#include "zone_deps.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <utility>

#include "build_preflight.h"
#include "compile_iw8.h"
#include "json_lite.h"
#include "map_text.h"

namespace iw8r {

namespace {

namespace fs = std::filesystem;

constexpr size_t kKinds = 5;
constexpr size_t kTopCandidates = 20;
constexpr char kStockPrefix[] = "iw8r_stock/";
constexpr char kStockModelPrefix[] = "iw8r_stock_";

std::string lowerStr(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool startsWith(const std::string& s, const std::string& p) { return s.compare(0, p.size(), p) == 0; }

bool endsWith(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// The part of a stock name after its last '/' (tm2co/me_curb_stone_01 -> me_curb_stone_01).
std::string lastSegment(const std::string& name) {
    const size_t slash = name.rfind('/');
    return slash == std::string::npos ? name : name.substr(slash + 1);
}

std::string fmtBytes(uint64_t bytes) {
    char b[32];
    if (bytes < 1024) std::snprintf(b, sizeof b, "%llu B", static_cast<unsigned long long>(bytes));
    else if (bytes < 1024ull * 1024) std::snprintf(b, sizeof b, "%.1f KB", double(bytes) / 1024.0);
    else std::snprintf(b, sizeof b, "%.1f MB", double(bytes) / (1024.0 * 1024.0));
    return b;
}

std::string padL(const std::string& s, size_t w) { return s.size() >= w ? s : std::string(w - s.size(), ' ') + s; }
std::string padR(const std::string& s, size_t w) { return s.size() >= w ? s : s + std::string(w - s.size(), ' '); }

// Zone names of a catalog zone column: comma separated, sorted and without repeats.
std::set<std::string> splitZones(const std::string& field) {
    std::set<std::string> out;
    std::string cur;
    const auto flush = [&] {
        size_t b = 0, e = cur.size();
        while (b < e && std::isspace(static_cast<unsigned char>(cur[b]))) ++b;
        while (e > b && std::isspace(static_cast<unsigned char>(cur[e - 1]))) --e;
        if (e > b) out.insert(cur.substr(b, e - b));
        cur.clear();
    };
    for (const char c : field) {
        if (c == ',') flush();
        else cur += c;
    }
    flush();
    return out;
}

// Name -> zones the catalog lists it in.
using ZoneMap = std::map<std::string, std::set<std::string>>;

std::vector<std::string> zonesOf(const ZoneMap& m, const std::string& name) {
    const auto it = m.find(name);
    if (it == m.end()) return {};
    return {it->second.begin(), it->second.end()};
}

// Reads a catalog of `name<TAB>zones...` rows (`#` lines are comments). The zones are column `zoneCol`. A grouped row
// ("a_c&a_s~123", the image catalog) lists several names; each takes the row's zones. Missing files are noted, not fatal.
void readCatalog(const fs::path& file, size_t zoneCol, bool lowerNames, bool grouped, ZoneMap& out, std::vector<std::string>& notes) {
    std::string text;
    if (!readFile(file.string(), text)) {
        notes.push_back("catalog not read: " + file.string());
        return;
    }
    size_t pos = 0;
    while (pos < text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        std::string row = text.substr(pos, end - pos);
        pos = end + 1;
        while (!row.empty() && row.back() == '\r') row.pop_back();
        if (row.empty() || row[0] == '#') continue;

        std::vector<std::string> cols;
        size_t start = 0;
        for (size_t i = 0; i <= row.size(); ++i) {
            if (i == row.size() || row[i] == '\t') {
                cols.push_back(row.substr(start, i - start));
                start = i + 1;
            }
        }
        if (cols.size() <= zoneCol) continue;
        const std::set<std::string> zones = splitZones(cols[zoneCol]);
        if (zones.empty()) continue;

        std::vector<std::string> names;
        if (grouped) {
            std::string key = cols[0].substr(0, cols[0].find('~'));
            size_t b = 0;
            for (size_t i = 0; i <= key.size(); ++i) {
                if (i == key.size() || key[i] == '&') {
                    if (i > b) names.push_back(key.substr(b, i - b));
                    b = i + 1;
                }
            }
        } else {
            names.push_back(cols[0]);
        }
        for (std::string name : names) {
            if (name.empty()) continue;
            if (lowerNames) name = lowerStr(name);
            out[name].insert(zones.begin(), zones.end());
        }
    }
}

// Model catalog names by the name the Compile panel gives their stock copies (stockModelBuildName).
struct ModelIndexEntry {
    std::string stockName;
    std::set<std::string> zones;
};
std::map<std::string, ModelIndexEntry> indexModels(const ZoneMap& models) {
    std::map<std::string, ModelIndexEntry> out;
    for (const auto& [name, zones] : models) {
        ModelIndexEntry& e = out[stockModelBuildName(name)];
        if (e.stockName.empty()) e.stockName = name;
        e.zones.insert(zones.begin(), zones.end());
    }
    return out;
}

// ---- Script and entity reading (the same names the preflight checks; the GSC tokenizer skips comments and keeps strings).

struct Tok {
    char kind;  // 'i' identifier, 's' string literal, 'p' punctuation
    std::string text;
};

std::vector<Tok> tokenize(const std::string& s) {
    std::vector<Tok> out;
    const size_t n = s.size();
    for (size_t i = 0; i < n;) {
        const char c = s[i];
        if (c == '/' && i + 1 < n && s[i + 1] == '/') {
            while (i < n && s[i] != '\n') ++i;
        } else if (c == '/' && i + 1 < n && s[i + 1] == '*') {
            const size_t e = s.find("*/", i + 2);
            i = e == std::string::npos ? n : e + 2;
        } else if (c == '"') {
            std::string v;
            size_t j = i + 1;
            while (j < n && s[j] != '"' && s[j] != '\n') {
                if (s[j] == '\\' && j + 1 < n && s[j + 1] != '\n') {
                    v += s[j + 1];
                    j += 2;
                } else {
                    v += s[j++];
                }
            }
            if (j < n && s[j] == '"') {
                out.push_back({'s', v});
                i = j + 1;
            } else {
                i = j;  // unterminated on its line: dropped
            }
        } else if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            size_t j = i;
            while (j < n && (std::isalnum(static_cast<unsigned char>(s[j])) || s[j] == '_')) ++j;
            out.push_back({'i', s.substr(i, j - i)});
            i = j;
        } else {
            if (!std::isspace(static_cast<unsigned char>(c))) out.push_back({'p', std::string(1, c)});
            ++i;
        }
    }
    return out;
}

// loadfx("name") gives an effect; playsound / playloopsound / playsoundatpos give sound aliases (every literal in the call).
void scanScript(const std::string& text, const std::string& where, std::map<std::string, std::string>& fx,
                std::map<std::string, std::string>& snd) {
    const std::vector<Tok> t = tokenize(text);
    for (size_t i = 0; i + 1 < t.size(); ++i) {
        if (t[i].kind != 'i' || t[i + 1].kind != 'p' || t[i + 1].text != "(") continue;
        const std::string name = lowerStr(t[i].text);
        if (name == "loadfx") {
            if (i + 2 < t.size() && t[i + 2].kind == 's' && !t[i + 2].text.empty()) fx.emplace(lowerStr(t[i + 2].text), where);
        } else if (name == "playsound" || name == "playloopsound" || name == "playsoundatpos") {
            int depth = 0;
            for (size_t j = i + 1; j < t.size(); ++j) {
                if (t[j].kind == 'p' && t[j].text == "(") {
                    ++depth;
                } else if (t[j].kind == 'p' && t[j].text == ")") {
                    if (--depth == 0) break;
                } else if (t[j].kind == 's' && !t[j].text.empty()) {
                    snd.emplace(lowerStr(t[j].text), where);
                }
            }
        }
    }
}

// Every string stored under `key` anywhere in the document.
void collectStrings(const json::Value& v, const std::string& key, std::vector<std::string>& into) {
    if (v.type == json::Value::Type::Object) {
        for (const auto& [k, child] : v.obj) {
            if (k == key)
                if (const std::string* s = child.string(); s && !s->empty()) into.push_back(*s);
            collectStrings(child, key, into);
        }
    } else if (v.type == json::Value::Type::Array) {
        for (const json::Value& child : v.arr) collectStrings(child, key, into);
    }
}

std::optional<json::Value> readJson(const fs::path& file) {
    std::string text;
    if (!readFile(file.string(), text)) return std::nullopt;
    return json::parse(text);
}

uint64_t fileBytes(const fs::path& p) {
    std::error_code ec;
    const uintmax_t n = fs::file_size(p, ec);
    return ec ? 0 : uint64_t(n);
}

struct MaterialFile {
    std::string name;                  // path under materials\ without .json, '/' separated
    uint64_t bytes = 0;
    std::vector<std::string> images;   // texture image names
};

}  // namespace

const char* depKindName(DepKind k) {
    switch (k) {
        case DepKind::Material: return "material";
        case DepKind::Image: return "image";
        case DepKind::Model: return "model";
        case DepKind::Effect: return "effect";
        case DepKind::Sound: return "sound";
    }
    return "?";
}

bool zoneIsAlwaysLoaded(const std::string& zone) {
    return startsWith(zone, "common_") || startsWith(zone, "techsets_common") || startsWith(zone, "code_") ||
           startsWith(zone, "mp_patch");
}

ZoneDepReport makeZoneDepReport(const std::string& extractedDir, const std::string& mapId, const std::string& catalogDir) {
    ZoneDepReport r;
    r.mapId = mapId;
    const fs::path root(extractedDir);
    const fs::path cat(catalogDir);
    std::error_code ec;
    if (!fs::is_directory(root, ec)) r.notes.push_back("extracted folder not found: " + extractedDir);

    // Catalogs: materials and models name their zones in column 1; the image rows are grouped; vfx is column 1 and sounds
    // column 3 (alias, loop, distMax, zones).
    ZoneMap materialsCat, imagesCat, modelsCat, vfxCat, soundsCat;
    readCatalog(cat / "iw8_materials.tsv", 1, false, false, materialsCat, r.notes);
    readCatalog(cat / "iw8_images.tsv", 1, false, true, imagesCat, r.notes);
    readCatalog(cat / "iw8_models.tsv", 1, false, false, modelsCat, r.notes);
    readCatalog(cat / "iw8_common_vfx.tsv", 1, true, false, vfxCat, r.notes);
    readCatalog(cat / "iw8_common_sounds.tsv", 3, true, false, soundsCat, r.notes);
    const std::map<std::string, ModelIndexEntry> modelsByBuild = indexModels(modelsCat);

    // ---- Carried files.
    std::vector<MaterialFile> materials;
    const fs::path matRoot = root / "materials";
    if (fs::is_directory(matRoot, ec)) {
        for (const auto& e : fs::recursive_directory_iterator(matRoot, ec)) {
            if (!e.is_regular_file(ec)) continue;
            std::string ext = lowerStr(e.path().extension().string());
            if (ext != ".json") continue;
            MaterialFile m;
            m.name = e.path().lexically_relative(matRoot).generic_string();
            m.name.resize(m.name.size() - 5);  // ".json"
            m.bytes = uint64_t(e.file_size(ec));
            if (const auto v = readJson(e.path()))
                if (const json::Value* tex = v->get("textures"))
                    for (const json::Value& t : tex->arr)
                        if (const json::Value* im = t.get("image"))
                            if (const std::string* s = im->string(); s && !s->empty()) m.images.push_back(*s);
            materials.push_back(std::move(m));
        }
    }
    std::sort(materials.begin(), materials.end(), [](const MaterialFile& a, const MaterialFile& b) { return a.name < b.name; });

    std::map<std::string, uint64_t> images;  // image stem -> dds bytes
    if (fs::is_directory(root / "images", ec)) {
        for (const auto& e : fs::directory_iterator(root / "images", ec)) {
            if (!e.is_regular_file(ec) || lowerStr(e.path().extension().string()) != ".dds") continue;
            images[e.path().stem().string()] = uint64_t(e.file_size(ec));
        }
    }

    // Models: xmodel\<name>.json with its geometry in <name>.replay.json; the bytes of both count.
    std::map<std::string, uint64_t> models;
    std::vector<std::string> modelMaterials;  // material names the replay geometry uses
    if (fs::is_directory(root / "xmodel", ec)) {
        for (const auto& e : fs::directory_iterator(root / "xmodel", ec)) {
            if (!e.is_regular_file(ec)) continue;
            const std::string name = e.path().filename().string();
            const std::string lower = lowerStr(name);
            if (!endsWith(lower, ".json") || endsWith(lower, ".replay.json")) continue;
            const std::string stem = name.substr(0, name.size() - 5);
            uint64_t bytes = uint64_t(e.file_size(ec));
            const fs::path replay = root / "xmodel" / (stem + ".replay.json");
            if (fs::exists(replay, ec)) {
                bytes += fileBytes(replay);
                if (const auto v = readJson(replay)) collectStrings(*v, "material", modelMaterials);
            }
            models[stem] = bytes;
        }
    }

    // Effects carried as fx\<name>.json (the build writes none yet, so this is usually empty) and sound files under sound\.
    std::map<std::string, uint64_t> fxCarried;
    if (fs::is_directory(root / "fx", ec)) {
        for (const auto& e : fs::directory_iterator(root / "fx", ec)) {
            if (!e.is_regular_file(ec) || lowerStr(e.path().extension().string()) != ".json") continue;
            fxCarried[lowerStr(e.path().stem().string())] = uint64_t(e.file_size(ec));
        }
    }
    uint64_t soundFiles = 0, soundBytes = 0;
    if (fs::is_directory(root / "sound", ec)) {
        for (const auto& e : fs::recursive_directory_iterator(root / "sound", ec)) {
            if (!e.is_regular_file(ec)) continue;
            if (endsWith(lowerStr(e.path().filename().string()), ".stock_sounds.txt")) continue;  // a list, not a sound
            ++soundFiles;
            soundBytes += uint64_t(e.file_size(ec));
        }
    }

    // ---- Stock copies. A material is a copy when its name is iw8r_stock/<stock>; an image is a copy when a stock material
    // names it as a texture (the image's stem is then <stock last segment>_<c|n|s>, as the catalogs name stock images); a
    // model is a copy when its name is iw8r_stock_<stock>.
    std::map<std::string, std::pair<std::string, std::string>> imageLinks;  // carried image -> (stock name, stock image name)
    for (const MaterialFile& m : materials) {
        if (!startsWith(m.name, kStockPrefix)) continue;
        const std::string stock = m.name.substr(sizeof kStockPrefix - 1);
        for (const std::string& img : m.images) {
            if (img.empty() || img[0] == '$' || !images.count(img) || imageLinks.count(img)) continue;
            const size_t us = img.rfind('_');
            if (us == std::string::npos) continue;
            imageLinks[img] = {stock, lastSegment(stock) + img.substr(us)};
        }
    }

    for (const MaterialFile& m : materials) {
        if (!startsWith(m.name, kStockPrefix)) continue;
        StockCopy c;
        c.kind = DepKind::Material;
        c.copyName = m.name;
        c.stockName = m.name.substr(sizeof kStockPrefix - 1);
        c.bytes = m.bytes;
        c.stockZones = zonesOf(materialsCat, c.stockName);
        r.stockCopies.push_back(c);
    }
    for (const auto& [img, link] : imageLinks) {
        StockCopy c;
        c.kind = DepKind::Image;
        c.copyName = img;
        c.stockName = link.second;
        c.bytes = images.at(img);
        c.stockZones = zonesOf(imagesCat, link.second);
        r.stockCopies.push_back(c);
    }
    for (const auto& [stem, bytes] : models) {
        if (!startsWith(stem, kStockModelPrefix)) continue;
        StockCopy c;
        c.kind = DepKind::Model;
        c.copyName = stem;
        c.bytes = bytes;
        if (const auto it = modelsByBuild.find(stem); it != modelsByBuild.end()) {
            c.stockName = it->second.stockName;
            c.stockZones.assign(it->second.zones.begin(), it->second.zones.end());
        }
        r.stockCopies.push_back(c);
    }

    // ---- Names used without a carried file.
    std::map<std::string, std::string> fxRefs, sndRefs, imgRefs, matRefs;  // name -> first place it is used
    const fs::path mapDir = root / "maps" / "mp";
    const std::string mapPrefix = "maps/mp/" + mapId;
    if (std::string text; readFile((mapDir / (mapId + ".gsc")).string(), text)) scanScript(text, mapPrefix + ".gsc", fxRefs, sndRefs);
    if (std::string text; readFile((mapDir / (mapId + ".d3dbsp.ents")).string(), text)) {
        std::vector<EntityKeyValues> blocks;
        if (parseEntityBlocks(text, blocks, nullptr)) {
            for (const EntityKeyValues& kv : blocks)
                for (const auto& [key, value] : kv) {
                    if (value.empty()) continue;
                    if (key == "fx") fxRefs.emplace(lowerStr(value), mapPrefix + ".d3dbsp.ents");
                    else if (key == "soundalias") sndRefs.emplace(lowerStr(value), mapPrefix + ".d3dbsp.ents");
                }
        }
    }
    if (std::string text; readFile((root / "sound" / (mapId + ".stock_sounds.txt")).string(), text)) {
        size_t pos = 0;
        while (pos < text.size()) {
            size_t end = text.find('\n', pos);
            if (end == std::string::npos) end = text.size();
            const std::string row = text.substr(pos, end - pos);
            pos = end + 1;
            const size_t sp = row.find_first_of(" \t\r");
            const std::string alias = row.substr(0, sp);
            if (!alias.empty()) sndRefs.emplace(lowerStr(alias), "sound/" + mapId + ".stock_sounds.txt");
        }
    }
    // Engine $ names: material textures, the world's sky and reflection probes, and the materials of the world and models.
    for (const MaterialFile& m : materials)
        for (const std::string& img : m.images)
            if (!img.empty() && img[0] == '$') imgRefs.emplace(img, "material " + m.name);
    if (auto world = readJson(mapDir / (mapId + ".d3dbsp.replay-world.json"))) {
        if (const json::Value* sky = world->get("sky"))
            if (const std::string* s = sky->string(); s && !s->empty() && (*s)[0] == '$') imgRefs.emplace(*s, "the world sky");
        if (const json::Value* probes = world->get("reflection_probes"))
            for (const json::Value& p : probes->arr)
                if (const json::Value* im = p.get("image"))
                    if (const std::string* s = im->string(); s && !s->empty() && (*s)[0] == '$') imgRefs.emplace(*s, "a reflection probe");
        std::vector<std::string> worldMaterials;
        collectStrings(*world, "material", worldMaterials);
        for (const std::string& m : worldMaterials)
            if (m[0] == '$') matRefs.emplace(m, "the map surfaces");
    }
    for (const std::string& m : modelMaterials)
        if (m[0] == '$') matRefs.emplace(m, "a model");

    const auto addByName = [&](DepKind kind, const std::map<std::string, std::string>& names, const ZoneMap& cat) {
        for (const auto& [name, where] : names) {
            ByNameRef b;
            b.kind = kind;
            b.name = name;
            b.where = where;
            b.zones = zonesOf(cat, name);
            r.byName.push_back(std::move(b));
        }
    };
    // An effect that is carried as fx\<name>.json is not a by-name reference.
    std::map<std::string, std::string> fxByName;
    for (const auto& [name, where] : fxRefs)
        if (!fxCarried.count(name)) fxByName.emplace(name, where);
    addByName(DepKind::Effect, fxByName, vfxCat);
    addByName(DepKind::Sound, sndRefs, soundsCat);
    addByName(DepKind::Image, imgRefs, imagesCat);
    addByName(DepKind::Material, matRefs, materialsCat);

    // ---- Totals per kind.
    for (size_t k = 0; k < kKinds; ++k) {
        KindTotals t;
        t.kind = DepKind(k);
        r.kinds.push_back(t);
    }
    const auto totals = [&](DepKind k) -> KindTotals& { return r.kinds[size_t(k)]; };
    for (const MaterialFile& m : materials) {
        ++totals(DepKind::Material).carried;
        totals(DepKind::Material).carriedBytes += m.bytes;
    }
    for (const auto& [img, bytes] : images) {
        ++totals(DepKind::Image).carried;
        totals(DepKind::Image).carriedBytes += bytes;
    }
    for (const auto& [stem, bytes] : models) {
        ++totals(DepKind::Model).carried;
        totals(DepKind::Model).carriedBytes += bytes;
    }
    for (const auto& [name, bytes] : fxCarried) {
        ++totals(DepKind::Effect).carried;
        totals(DepKind::Effect).carriedBytes += bytes;
    }
    totals(DepKind::Sound).carried = soundFiles;
    totals(DepKind::Sound).carriedBytes = soundBytes;
    for (const StockCopy& c : r.stockCopies) {
        KindTotals& t = totals(c.kind);
        ++t.stockCopies;
        t.stockCopyBytes += c.bytes;
        if (!c.stockZones.empty()) {
            ++t.referenceable;
            t.referenceableBytes += c.bytes;
        }
    }
    for (const ByNameRef& b : r.byName) {
        KindTotals& t = totals(b.kind);
        ++t.byName;
        if (!b.zones.empty()) ++t.byNameInZones;
    }

    // ---- Per-zone summary: each stock candidate and each by-name ref counts under every zone its catalog rows list; a
    // zone is the sole source when it is the only one listed.
    struct Usage {
        uint64_t assets = 0;
        uint64_t sole = 0;
    };
    std::map<std::string, Usage> usage;
    const auto take = [&](const std::vector<std::string>& zones) {
        for (const std::string& z : zones) ++usage[z].assets;
        if (zones.size() == 1) ++usage[zones[0]].sole;
    };
    for (const StockCopy& c : r.stockCopies)
        if (!c.stockZones.empty()) take(c.stockZones);
    for (const ByNameRef& b : r.byName)
        if (!b.zones.empty()) take(b.zones);
    for (const auto& [zone, u] : usage) r.zones.push_back({zone, zoneIsAlwaysLoaded(zone), u.assets, u.sole});
    std::sort(r.zones.begin(), r.zones.end(), [](const ZoneUsage& a, const ZoneUsage& b) {
        return a.assets != b.assets ? a.assets > b.assets : a.zone < b.zone;
    });
    return r;
}

std::string formatZoneDepReport(const ZoneDepReport& r) {
    std::string s = "zone dependencies: " + r.mapId + "\n";
    for (const std::string& n : r.notes) s += "  note: " + n + "\n";
    s += "zone classes: always loaded = common_*, techsets_common*, code_*, mp_patch*; every other zone is map-specific\n";

    s += "\nasset kinds (carried = files in the extraction; could ref = copies of stock assets a stock zone already holds)\n";
    s += "  " + padR("kind", 10) + padL("carried", 9) + padL("bytes", 11) + padL("copies", 8) + padL("copy bytes", 12) +
         padL("could ref", 10) + padL("ref saves", 12) + padL("by name", 9) + padL("in zones", 10) + "\n";
    for (const KindTotals& t : r.kinds) {
        s += "  " + padR(depKindName(t.kind), 10) + padL(std::to_string(t.carried), 9) + padL(fmtBytes(t.carriedBytes), 11) +
             padL(std::to_string(t.stockCopies), 8) + padL(fmtBytes(t.stockCopyBytes), 12) + padL(std::to_string(t.referenceable), 10) +
             padL(fmtBytes(t.referenceableBytes), 12) + padL(std::to_string(t.byName), 9) + padL(std::to_string(t.byNameInZones), 10) + "\n";
    }

    s += "\nzones the map takes assets from (sole = no other zone lists the asset)\n";
    s += "  " + padR("zone", 34) + padR("class", 14) + padL("assets", 8) + padL("sole", 8) + "\n";
    for (const ZoneUsage& z : r.zones)
        s += "  " + padR(z.zone, 34) + padR(z.alwaysLoaded ? "always" : "map-specific", 14) + padL(std::to_string(z.assets), 8) +
             padL(std::to_string(z.soleSource), 8) + "\n";
    if (r.zones.empty()) s += "  (no stock zone lists anything the map uses)\n";

    std::vector<const StockCopy*> cands;
    uint64_t total = 0;
    uint64_t perKind[kKinds] = {};
    for (const StockCopy& c : r.stockCopies) {
        if (c.stockZones.empty()) continue;
        cands.push_back(&c);
        total += c.bytes;
        perKind[size_t(c.kind)] += c.bytes;
    }
    std::sort(cands.begin(), cands.end(), [](const StockCopy* a, const StockCopy* b) {
        return a->bytes != b->bytes ? a->bytes > b->bytes : a->copyName < b->copyName;
    });
    size_t unresolved = 0;
    for (const StockCopy& c : r.stockCopies)
        if (c.stockZones.empty()) ++unresolved;
    s += "\nreference instead: " + std::to_string(cands.size()) + " carried copies of stock assets could be referenced, saving " +
         fmtBytes(total) + " (materials " + fmtBytes(perKind[size_t(DepKind::Material)]) + ", images " +
         fmtBytes(perKind[size_t(DepKind::Image)]) + ", models " + fmtBytes(perKind[size_t(DepKind::Model)]) + ")\n";
    s += "  " + std::to_string(unresolved) +
         " carried copies have no catalog row under their stock name; they are not counted (a lower bound)\n";

    const size_t shown = std::min(kTopCandidates, cands.size());
    size_t copyW = 4, stockW = 10;  // columns sized to the rows shown
    for (size_t i = 0; i < shown; ++i) {
        copyW = std::max(copyW, cands[i]->copyName.size());
        stockW = std::max(stockW, cands[i]->stockName.size());
    }
    s += "top " + std::to_string(shown) + " candidates by bytes\n";
    s += "  " + padL("bytes", 10) + "  " + padR("kind", 10) + padR("copy", copyW + 2) + padR("stock name", stockW + 2) + "zones\n";
    if (cands.empty()) s += "  (none)\n";
    for (size_t i = 0; i < shown; ++i) {
        const StockCopy& c = *cands[i];
        std::string zones;
        for (size_t z = 0; z < c.stockZones.size() && z < 3; ++z) zones += (z ? ", " : "") + c.stockZones[z];
        if (c.stockZones.size() > 3) zones += " +" + std::to_string(c.stockZones.size() - 3) + " more";
        s += "  " + padL(fmtBytes(c.bytes), 10) + "  " + padR(depKindName(c.kind), 10) + padR(c.copyName, copyW + 2) +
             padR(c.stockName, stockW + 2) + zones + "\n";
    }
    return s;
}

}  // namespace iw8r
