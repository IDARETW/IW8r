// IW8 Radiant Suite - editor side of the in-game map debug overlay (see map_debug_dump.h).
#include "map_debug_dump.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>

namespace iw8r {
namespace {

std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> out;
    size_t p = 0;
    while (true) {
        const size_t q = s.find(sep, p);
        out.push_back(s.substr(p, q == std::string::npos ? std::string::npos : q - p));
        if (q == std::string::npos) break;
        p = q + 1;
    }
    return out;
}

std::string trim(std::string s) {
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.pop_back();
    size_t b = 0;
    while (b < s.size() && (s[b] == ' ' || s[b] == '\t')) ++b;
    return s.substr(b);
}

bool parseVec3(const std::string& text, Vec3& out) {
    double x = 0, y = 0, z = 0;
    if (std::sscanf(text.c_str(), "%lf %lf %lf", &x, &y, &z) != 3) return false;
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) return false;
    out = Vec3{x, y, z};
    return true;
}

std::string fmt(const Vec3& v) {
    char buf[96];
    std::snprintf(buf, sizeof(buf), "%.0f %.0f %.0f", v.x, v.y, v.z);
    return buf;
}

bool startsWith(const std::string& s, const char* p) { return s.rfind(p, 0) == 0; }

}  // namespace

const std::vector<MapDebugPreset>& mapDebugPresets() {
    static const std::vector<MapDebugPreset> presets = {
        {"Off", 0, "No overlay"},
        {"Where am I", kMapDebugPanel, "A panel with your position in map units, the map, entity counts and the audio zone"},
        {"Entities and triggers", kMapDebugLabels | kMapDebugTriggers | kMapDebugPanel,
         "Labels on the map's entities, wire boxes on triggers, and the panel"},
        {"Entities and crosshair", kMapDebugLabels | kMapDebugTriggers | kMapDebugPanel | kMapDebugCrosshair,
         "The same, plus what the crosshair is on: surface type, flags, normal and the entity hit"},
        {"Everything", kMapDebugLabels | kMapDebugTriggers | kMapDebugPanel | kMapDebugCrosshair | kMapDebugAllBoxes | kMapDebugActors,
         "Every entity with bounds as a box, including players, AI and items"},
    };
    return presets;
}

std::string cleanMapDebugFilter(const std::string& text, bool* ok) {
    if (ok) *ok = true;
    std::string out;
    bool space = false;
    for (char ch : text) {
        if (ch == ' ' || ch == '\t') {
            space = !out.empty();
            continue;
        }
        const bool good = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '_' || ch == '-';
        if (!good) {
            if (ok) *ok = false;
            return {};
        }
        if (space) out += ' ';
        space = false;
        out += ch;
    }
    if (out.size() > 96) {
        if (ok) *ok = false;
        return {};
    }
    return out;
}

std::vector<std::string> mapDebugCommands(int mask, const std::string& filter) {
    if (mask < 0) mask = 0;
    if (mask > 255) mask = 255;
    const std::string clean = cleanMapDebugFilter(filter);
    return {"set iw8r_mapDebug " + std::to_string(mask), clean.empty() ? std::string("set iw8r_mapDebugFilter \"\"") : "set iw8r_mapDebugFilter " + clean};
}

MapDebugDump parseMapDebugDump(const std::string& logText) {
    MapDebugDump d;
    const size_t begin = logText.rfind("IW8R_MAPDEBUG begin");
    if (begin == std::string::npos) return d;
    d.found = true;
    size_t p = begin;
    while (p < logText.size()) {
        size_t end = logText.find('\n', p);
        if (end == std::string::npos) end = logText.size();
        const std::string line = logText.substr(p, end - p);
        p = end + 1;
        const size_t at = line.find("IW8R_");
        if (at == std::string::npos) continue;
        const std::string body = trim(line.substr(at));
        if (startsWith(body, "IW8R_MAPDEBUG end")) {
            d.complete = true;
            const size_t n = body.find("entities=");
            if (n != std::string::npos) d.declared = std::atoi(body.c_str() + n + 9);
            break;
        }
        if (startsWith(body, "IW8R_VIEW ")) {
            double x, y, z, pitch, yaw;
            if (std::sscanf(body.c_str() + 10, "%lf %lf %lf %lf %lf", &x, &y, &z, &pitch, &yaw) == 5) {
                d.view = Vec3{x, y, z};
                d.pitch = pitch;
                d.yaw = yaw;
                d.hasView = true;
            }
        } else if (startsWith(body, "IW8R_FEET ")) {
            d.hasFeet = parseVec3(body.substr(10), d.feet);
        } else if (startsWith(body, "IW8R_AUDIO")) {
            d.audio = trim(body.substr(10));
        } else if (startsWith(body, "IW8R_ENT ") || startsWith(body, "IW8R_ENT|")) {
            // num|type|classname|code_classname|targetname|target|script_noteworthy|script_linkname|script_linkto|
            // model|origin|angles|box_mid|box_half|model_type|linked|parent
            const std::vector<std::string> c = split(trim(body.substr(9)), '|');
            if (c.size() < 17) continue;
            GameEntity e;
            e.num = std::atoi(c[0].c_str());
            e.type = trim(c[1]);
            e.classname = trim(c[2]);
            e.codeClassname = trim(c[3]);
            e.targetname = trim(c[4]);
            e.target = trim(c[5]);
            e.noteworthy = trim(c[6]);
            e.linkname = trim(c[7]);
            e.linkto = trim(c[8]);
            e.model = trim(c[9]);
            if (!parseVec3(c[10], e.origin)) continue;
            parseVec3(c[11], e.angles);
            parseVec3(c[12], e.boxMid);
            parseVec3(c[13], e.boxHalf);
            e.modelType = trim(c[14]);
            e.linked = std::atoi(c[15].c_str()) != 0;
            e.parent = std::atoi(c[16].c_str());
            d.entities.push_back(std::move(e));
        }
    }
    return d;
}

DumpCompare compareDumpWithMap(const MapDebugDump& dump, const MapFile& map) {
    DumpCompare r;
    r.gameEntities = int(dump.entities.size());
    std::map<std::string, std::vector<const GameEntity*>> byName;
    int gameSpawns = 0;
    for (const GameEntity& e : dump.entities) {
        if (!e.targetname.empty()) byName[e.targetname].push_back(&e);
        if (e.classname.find("spawn") != std::string::npos) ++gameSpawns;
    }

    struct Wanted {
        int copies = 0;
        std::string classname;
        std::vector<Vec3> origins;  // of the copies that have an origin key
    };
    std::map<std::string, Wanted> wanted;
    int mapSpawns = 0;
    for (const Entity& e : map.entities) {
        const std::string cls = e.classname();
        if (cls.rfind("mp_", 0) == 0 && cls.find("spawn") != std::string::npos) ++mapSpawns;
        if (e.isWorldspawn()) continue;
        const std::string* tn = e.value("targetname");
        if (!tn || tn->empty()) continue;
        // Entities that exist only for the build (lights, effects and sounds become other things; structs live in
        // the script) are not expected as server entities.
        const bool runtime = cls == "script_model" || cls == "script_brushmodel" || cls == "script_origin" || cls.rfind("trigger_", 0) == 0;
        if (!runtime) continue;
        Wanted& w = wanted[*tn];
        ++w.copies;
        if (w.classname.empty()) w.classname = cls;
        if (const std::string* o = e.value("origin")) {
            Vec3 v;
            if (parseVec3(*o, v)) w.origins.push_back(v);
        }
    }

    std::vector<std::string> findings;
    for (const auto& [name, w] : wanted) {
        ++r.checked;
        const auto it = byName.find(name);
        if (it == byName.end()) {
            ++r.missing;
            findings.push_back("MISSING  " + name + " (" + w.classname + (w.copies > 1 ? " x " + std::to_string(w.copies) : "") + "): no entity with this targetname is in the game");
            continue;
        }
        const std::vector<const GameEntity*>& got = it->second;
        if (int(got.size()) != w.copies) {
            findings.push_back("COUNT    " + name + ": " + std::to_string(w.copies) + " in the map, " + std::to_string(got.size()) + " in the game");
            continue;
        }
        // One named entity with an origin key: its place can be compared.
        if (w.copies == 1 && w.origins.size() == 1) {
            const Vec3 d = got.front()->origin - w.origins.front();
            const double dist = length(d);
            if (dist > kDumpMoveTolerance) {
                ++r.moved;
                char buf[64];
                std::snprintf(buf, sizeof(buf), "%.1f", dist);
                findings.push_back("MOVED    " + name + " (" + w.classname + "): map " + fmt(w.origins.front()) + ", game " + fmt(got.front()->origin) + " (" + buf +
                                   " units; a scripted mover is expected to differ)");
                continue;
            }
        }
        ++r.matched;
    }

    r.lines.push_back(std::to_string(r.gameEntities) + " entities in the game (limit 2046)" +
                      (dump.declared >= 0 && dump.declared != r.gameEntities ? "; the dump said " + std::to_string(dump.declared) + ", some lines were not readable" : ""));
    r.lines.push_back(std::to_string(r.checked) + " named map entities looked for: " + std::to_string(r.matched) + " found in place, " + std::to_string(r.moved) +
                      " at another place, " + std::to_string(r.missing) + " missing");
    r.lines.push_back("spawn points: " + std::to_string(mapSpawns) + " in the map, " + std::to_string(gameSpawns) +
                      " entities with 'spawn' in their class in the game" + (mapSpawns > 0 && gameSpawns == 0 ? " (spawn points are kept in a list of their own, not as entities)" : ""));
    if (dump.hasFeet) r.lines.push_back("player feet at " + fmt(dump.feet) + (dump.audio.empty() ? "" : "; audio: " + dump.audio));
    for (std::string& f : findings) r.lines.push_back(std::move(f));
    return r;
}

}  // namespace iw8r
