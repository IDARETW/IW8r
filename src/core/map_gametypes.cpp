// IW8 Radiant - game types of a map (see map_gametypes.h).
#include "map_gametypes.h"

#include "gametype_script.h"

#include <cctype>
#include <map>
#include <set>

namespace iw8r {
namespace {

struct Census {
    std::map<std::string, int> classes;      // classname -> count
    std::map<std::string, int> targetnames;  // targetname -> count
    std::set<std::string> domLabels;         // script_label of flag_primary triggers
    int bombzoneTriggers = 0;                // trigger_use_touch named bombzone
    int hardpointZoneTriggers = 0;           // trigger_multiple named hardpoint_zone
};

Census census(const MapFile& map) {
    Census c;
    for (const Entity& e : map.entities) {
        const std::string cls = e.classname();
        ++c.classes[cls];
        const std::string* tn = e.value("targetname");
        if (!tn) continue;
        ++c.targetnames[*tn];
        if (*tn == "flag_primary")
            if (const std::string* label = e.value("script_label")) c.domLabels.insert(*label);
        if (*tn == "bombzone" && cls == "trigger_use_touch") ++c.bombzoneTriggers;
        if (*tn == "hardpoint_zone" && cls == "trigger_multiple") ++c.hardpointZoneTriggers;
    }
    return c;
}

int count(const std::map<std::string, int>& m, const std::string& key) {
    const auto it = m.find(key);
    return it == m.end() ? 0 : it->second;
}

void need(GameTypeCheck& g, bool ok, const std::string& what) {
    if (!ok) g.missing.push_back(what);
}

void needClass(GameTypeCheck& g, const Census& c, const std::string& cls, int atLeast) {
    const int have = count(c.classes, cls);
    need(g, have >= atLeast, std::to_string(atLeast) + " x " + cls + " (has " + std::to_string(have) + ")");
}

void needName(GameTypeCheck& g, const Census& c, const std::string& name) {
    need(g, count(c.targetnames, name) >= 1, "an entity with targetname " + name);
}

}  // namespace

std::vector<GameTypeCheck> gameTypeChecks(const MapFile& map) {
    const Census c = census(map);
    std::vector<GameTypeCheck> out;
    auto add = [&](const char* gametype, const char* title) -> GameTypeCheck& {
        out.push_back({gametype, title, false, {}});
        return out.back();
    };
    {
        GameTypeCheck& g = add("war", "Team Deathmatch");
        needClass(g, c, "mp_tdm_spawn", 2);
        needClass(g, c, "mp_tdm_spawn_allies_start", 1);
        needClass(g, c, "mp_tdm_spawn_axis_start", 1);
    }
    {
        GameTypeCheck& g = add("dm", "Free-for-all");
        needClass(g, c, "mp_dm_spawn", 2);
    }
    {
        GameTypeCheck& g = add("dom", "Domination");
        needClass(g, c, "mp_dom_spawn", 2);
        needClass(g, c, "mp_dom_spawn_allies_start", 1);
        needClass(g, c, "mp_dom_spawn_axis_start", 1);
        for (const char* label : {"_a", "_b", "_c"})
            need(g, c.domLabels.count(label) > 0, std::string("a flag trigger (targetname flag_primary) with script_label ") + label);
    }
    {
        GameTypeCheck& g = add("sd", "Search and Destroy");
        needClass(g, c, "mp_sd_spawn_attacker", 1);
        needClass(g, c, "mp_sd_spawn_defender", 1);
        need(g, c.bombzoneTriggers >= 2, "2 bomb site triggers (trigger_use_touch, targetname bombzone; has " + std::to_string(c.bombzoneTriggers) + ")");
        needName(g, c, "sd_bomb");
        needName(g, c, "sd_bomb_pickup_trig");
    }
    {
        GameTypeCheck& g = add("ctf", "Capture the Flag");
        needClass(g, c, "mp_ctf_spawn", 2);
        needClass(g, c, "mp_ctf_spawn_allies_start", 1);
        needClass(g, c, "mp_ctf_spawn_axis_start", 1);
        for (const char* name : {"ctf_flag_allies", "ctf_flag_axis", "ctf_trig_allies", "ctf_trig_axis", "ctf_zone_allies", "ctf_zone_axis"})
            needName(g, c, name);
    }
    {
        // Warzone (research 87, section 6): the br script takes its starting spawns from mp_dm_spawn_start and its
        // prematch areas from script_struct entities named br_prematch_insertion_point. Circle, gulag and plane data
        // are switched off by the map (worldspawn iw8r_br) or come from a game mode preset.
        GameTypeCheck& g = add("br", "Warzone (battle royale)");
        needClass(g, c, "mp_dm_spawn_start", 1);
        needName(g, c, "br_prematch_insertion_point");
    }
    {
        GameTypeCheck& g = add("koth", "Hardpoint");
        needClass(g, c, "mp_koth_spawn", 2);
        needClass(g, c, "mp_koth_spawn_allies_start", 1);
        needClass(g, c, "mp_koth_spawn_axis_start", 1);
        need(g, c.hardpointZoneTriggers >= 1,
             "at least 1 Hardpoint zone trigger (trigger_multiple, targetname hardpoint_zone)");
    }
    for (GameTypeCheck& g : out) g.supported = g.missing.empty();
    return out;
}

bool normalizeGameTypeList(const std::string& text, std::string& out) {
    out.clear();
    std::vector<std::string> names;
    std::string word;
    auto flush = [&]() {
        if (word.empty()) return true;
        if (word.size() > 11) return false;
        bool seen = false;
        for (const std::string& n : names) seen = seen || n == word;
        if (!seen) names.push_back(word);
        word.clear();
        return true;
    };
    for (char raw : text) {
        const char ch = char(std::tolower(static_cast<unsigned char>(raw)));
        if (ch == ' ' || ch == '\t' || ch == ',' || ch == ';') {
            if (!flush()) return false;
        } else if ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '_') {
            word += ch;
        } else {
            return false;
        }
    }
    if (!flush() || names.empty() || names.size() > 16) return false;
    for (const std::string& n : names) out += (out.empty() ? "" : " ") + n;
    return true;
}

std::string detectedGameTypes(const MapFile& map) {
    std::string list;
    for (const GameTypeCheck& g : gameTypeChecks(map))
        if (g.supported) list += (list.empty() ? "" : " ") + g.gametype;
    return list.empty() ? "war" : list;
}

namespace {
// 13.6: the game type of the map's own game type script (worldspawn iw8r_gametype_script) joins the list.
void addOwnGameType(const Entity* world, MapGameTypes& r) {
    const std::string* gp = world ? world->value(kGameTypeScriptKey) : nullptr;
    const std::string name = gp ? gameTypeNameOfScript(*gp) : std::string();
    if (name.empty()) return;
    if ((" " + r.list + " ").find(" " + name + " ") != std::string::npos) return;
    r.list += (r.list.empty() ? "" : " ") + name;
    r.notes.push_back("game type '" + name + "' of the map's own game type script is listed");
}
}  // namespace

MapGameTypes mapGameTypes(const MapFile& map) {
    MapGameTypes r;
    const std::vector<GameTypeCheck> checks = gameTypeChecks(map);
    const Entity* world = nullptr;
    for (const Entity& e : map.entities)
        if (e.isWorldspawn()) {
            world = &e;
            break;
        }
    const std::string* key = world ? world->value("iw8r_gametypes") : nullptr;
    if (key && !key->empty()) {
        std::string list;
        if (normalizeGameTypeList(*key, list)) {
            r.list = list;
            r.fromKey = true;
            r.notes.push_back("game types from worldspawn iw8r_gametypes: " + list);
            // A listed game type the editor can check and finds incomplete is worth one line: the match would start
            // without its spawns or objects.
            const std::string padded = " " + list + " ";
            for (const GameTypeCheck& g : checks)
                if (!g.supported && padded.find(" " + g.gametype + " ") != std::string::npos)
                    r.notes.push_back(g.gametype + " (" + g.title + ") is listed but the map lacks: " + g.missing.front() +
                                      (g.missing.size() > 1 ? " and " + std::to_string(g.missing.size() - 1) + " more" : ""));
            addOwnGameType(world, r);
            return r;
        }
        r.notes.push_back("worldspawn iw8r_gametypes '" + *key + "' is not a list of game type names (lower-case names of up to 11 "
                          "characters, spaces between them); the detected list is used");
    }
    for (const GameTypeCheck& g : checks)
        if (g.supported) r.list += (r.list.empty() ? "" : " ") + g.gametype;
    if (r.list.empty()) {
        r.list = "war";
        r.notes.push_back("no game type is complete (Team Deathmatch lacks: " + (checks.empty() || checks.front().missing.empty() ? std::string("?") : checks.front().missing.front()) +
                          "); the map is listed with war");
    } else {
        r.notes.push_back("game types detected: " + r.list);
    }
    const bool hasBrKey = world && world->value("iw8r_br") && !world->value("iw8r_br")->empty();
    for (const GameTypeCheck& g : checks) {
        // Only game types the mapper has started on are worth a line (some of their entities exist).
        if (g.supported || g.gametype == "war") continue;
        size_t total = 0;
        if (g.gametype == "dm") total = 1;
        else if (g.gametype == "dom") total = 6;
        else if (g.gametype == "sd") total = 5;
        else if (g.gametype == "ctf") total = 9;
        else if (g.gametype == "br") total = 2;
        else if (g.gametype == "koth") total = 4;
        if (g.missing.size() < total || (g.gametype == "br" && hasBrKey))
            r.notes.push_back(g.gametype + " (" + g.title + ") is not offered; the map lacks: " + g.missing.front() +
                              (g.missing.size() > 1 ? " and " + std::to_string(g.missing.size() - 1) + " more" : ""));
    }
    addOwnGameType(world, r);
    return r;
}

std::string withGameTypes(const std::string& mapJson, const std::string& list) {
    static const std::string key = "\"gameTypes\"";
    const std::string value = "\"" + list + "\"";
    // Replace an existing value.
    size_t at = mapJson.find(key);
    if (at != std::string::npos) {
        size_t p = at + key.size();
        while (p < mapJson.size() && (mapJson[p] == ' ' || mapJson[p] == '\t' || mapJson[p] == ':' || mapJson[p] == '\r' || mapJson[p] == '\n')) ++p;
        if (p < mapJson.size() && mapJson[p] == '"') {
            const size_t end = mapJson.find('"', p + 1);
            if (end != std::string::npos) return mapJson.substr(0, p) + value + mapJson.substr(end + 1);
        }
        return mapJson;
    }
    // Add it as the last key of the top-level object.
    const size_t close = mapJson.find_last_of('}');
    const size_t open = mapJson.find('{');
    if (open == std::string::npos || close == std::string::npos || close < open) return mapJson;
    size_t last = close;
    while (last > open && (mapJson[last - 1] == ' ' || mapJson[last - 1] == '\t' || mapJson[last - 1] == '\r' || mapJson[last - 1] == '\n')) --last;
    const bool empty = last == open + 1;
    const std::string eol = mapJson.find("\r\n") != std::string::npos ? "\r\n" : "\n";
    return mapJson.substr(0, last) + (empty ? "" : ",") + eol + "  " + key + ": " + value + eol + mapJson.substr(close);
}

}  // namespace iw8r
