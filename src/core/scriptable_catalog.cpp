#include "scriptable_catalog.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace iw8r {

namespace {

std::string lowerOf(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return s;
}

int intOf(const std::string& s, int fallback = 0) {
    if (s.empty()) return fallback;
    char* end = nullptr;
    const long v = std::strtol(s.c_str(), &end, 0);
    return end && *end == '\0' ? int(v) : fallback;
}

std::string jsonText(const std::string& s) {
    std::string out = "\"";
    for (const char c : s) {
        if (c == '"' || c == '\\') out += '\\';
        if (static_cast<unsigned char>(c) < 0x20) continue;
        out += c;
    }
    return out + "\"";
}

std::string numberText(double v) {
    if (!std::isfinite(v)) v = 0;
    char buf[40];
    std::snprintf(buf, sizeof buf, "%.6g", v);
    return buf;
}

}  // namespace

bool ScriptableCatalog::parse(const std::string& text) {
    defs_.clear();
    std::istringstream in(text);
    bool header = false;
    for (std::string line; std::getline(in, line);) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        std::vector<std::string> f;
        for (size_t at = 0;;) {
            const size_t tab = line.find('\t', at);
            f.push_back(line.substr(at, tab == std::string::npos ? std::string::npos : tab - at));
            if (tab == std::string::npos) break;
            at = tab + 1;
        }
        if (!header) {  // the column-name line
            header = true;
            if (!f.empty() && f[0] == "name") continue;
        }
        if (f.size() < 15 || f[0].empty()) continue;
        ScriptableDefinition d;
        d.name = f[0];
        d.zones = f[1];
        d.alwaysLoaded = intOf(f[2]) != 0;
        d.parts = intOf(f[3]);
        d.serverInstancedParts = std::clamp(intOf(f[4]), 0, 255);
        d.usableParts = std::clamp(intOf(f[5]), 0, 255);
        d.maxDynentParts = std::clamp(intOf(f[6], 1), 1, 255);
        d.physicsDynents = std::clamp(intOf(f[7]), 0, 16);
        d.instanceDynent = intOf(f[8]) != 0;
        d.sparseOccluder = intOf(f[9]) != 0;
        d.collisionFlags = std::clamp(intOf(f[10]), 0, 255);
        d.physicsLimit = intOf(f[11]) != 0;
        d.model = f[13];
        d.observed = f[14] == "observed";
        d.verified = f.size() > 15 && intOf(f[15]) != 0;
        d.physicsLimitDetail = f.size() > 16 ? intOf(f[16]) != 0 : d.physicsLimit;
        defs_.push_back(std::move(d));
    }
    std::sort(defs_.begin(), defs_.end(), [](const ScriptableDefinition& a, const ScriptableDefinition& b) { return lowerOf(a.name) < lowerOf(b.name); });
    return true;
}

bool ScriptableCatalog::load(const std::string& tsvPath, std::string* err) {
    std::ifstream f(tsvPath, std::ios::binary);
    if (!f) {
        if (err) *err = "cannot read " + tsvPath;
        return false;
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    return parse(ss.str());
}

const ScriptableDefinition* ScriptableCatalog::find(const std::string& name) const {
    const std::string key = lowerOf(name);
    const auto it = std::lower_bound(defs_.begin(), defs_.end(), key,
                                     [](const ScriptableDefinition& d, const std::string& k) { return lowerOf(d.name) < k; });
    return it != defs_.end() && lowerOf(it->name) == key ? &*it : nullptr;
}

std::string ScriptableDefinition::carryZone() const {
    if (alwaysLoaded) return {};
    for (size_t at = 0; at < zones.size();) {
        const size_t comma = zones.find(',', at);
        const std::string zone = zones.substr(at, comma == std::string::npos ? std::string::npos : comma - at);
        if (zone.rfind("mp_", 0) == 0) return zone;
        if (comma == std::string::npos) break;
        at = comma + 1;
    }
    return {};
}

bool ScriptableDefinition::brZone() const {
    for (size_t at = 0; at < zones.size();) {
        const size_t comma = zones.find(',', at);
        if (zones.substr(at, comma == std::string::npos ? std::string::npos : comma - at) == "common_br_mp") return true;
        if (comma == std::string::npos) break;
        at = comma + 1;
    }
    return false;
}

const ScriptableCatalog& ScriptableCatalog::shared(const std::string& startDir) {
    static const ScriptableCatalog catalog = [&] {
        ScriptableCatalog c;
        std::error_code ec;
        std::filesystem::path d = std::filesystem::absolute(startDir, ec);
        for (int up = 0; up < 5 && !d.empty(); ++up, d = d.parent_path()) {
            const auto f = d / "data" / "asset_catalog" / "iw8_scriptables.tsv";
            if (std::filesystem::is_regular_file(f, ec)) {
                c.load(f.string());
                break;
            }
            if (d == d.parent_path()) break;
        }
        return c;
    }();
    return catalog;
}

std::string scriptableInstanceJson(const ScriptablePlacement& p) {
    if (!p.def) return "{}";
    const ScriptableDefinition& d = *p.def;
    std::string json = "{\"definition\":" + jsonText(d.name) + ",\"origin\":[" + numberText(p.origin[0]) + "," + numberText(p.origin[1]) + "," +
                       numberText(p.origin[2]) + "],\"angles\":[" + numberText(p.angles[0]) + "," + numberText(p.angles[1]) + "," +
                       numberText(p.angles[2]) + "]";
    const auto text = [&](const char* key, const std::string& value) {
        if (!value.empty()) json += std::string(",\"") + key + "\":" + jsonText(value);
    };
    text("targetname", p.targetname);
    text("target", p.target);
    text("script_linkname", p.linkname);
    text("script_linkto", p.linkto);
    text("script_noteworthy", p.noteworthy);
    text("carry_zone", p.carryZone);
    json += ",\"server_instanced_parts\":" + std::to_string(d.serverInstancedParts) + ",\"usable_parts\":" + std::to_string(d.usableParts) +
            ",\"physics_limit\":" + (d.physicsLimit ? "true" : "false") + ",\"max_dynent_parts\":" + std::to_string(d.maxDynentParts) +
            ",\"instance_dynent\":" + (d.instanceDynent ? "true" : "false") + ",\"sparse_occluder\":" + (d.sparseOccluder ? "true" : "false") +
            ",\"collision_flags\":" + std::to_string(d.collisionFlags) + ",\"physics_dynents\":" + std::to_string(d.physicsDynents);
    // Written only when it differs: a converter without the key takes physics_limit for both.
    if (d.physicsLimitDetail != d.physicsLimit) json += std::string(",\"physics_limit_detail\":") + (d.physicsLimitDetail ? "true" : "false");
    return json + "}";
}

}  // namespace iw8r
