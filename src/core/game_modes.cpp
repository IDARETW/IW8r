#include "game_modes.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "json_lite.h"

namespace iw8r {

namespace {

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

std::string quote(const std::string& s) {
    std::string out = "\"";
    for (const char c : s) {
        if (c == '"' || c == '\\') out += '\\';
        if (static_cast<unsigned char>(c) < 0x20) continue;
        out += c;
    }
    return out + "\"";
}

bool validGameTypeName(const std::string& g) {
    if (g.empty() || g.size() > 11) return false;
    return std::all_of(g.begin(), g.end(), [](unsigned char c) { return std::islower(c) || std::isdigit(c) || c == '_'; });
}

GameModePreset make(const char* id, const char* title, const char* gametype, std::vector<std::pair<std::string, std::string>> dvars,
                    const char* description, bool noCircle = false, bool noGulag = false, bool skipDrop = false) {
    GameModePreset p;
    p.id = id, p.title = title, p.gametype = gametype, p.dvars = std::move(dvars), p.description = description;
    p.brNoCircle = noCircle, p.brNoGulag = noGulag, p.brSkipDrop = skipDrop;
    p.builtin = true;
    return p;
}

}  // namespace

bool validModeDvarName(const std::string& name) {
    if (name.empty() || name.size() > 64) return false;
    return std::all_of(name.begin(), name.end(), [](unsigned char c) { return std::isalnum(c) || c == '_'; });
}

bool validModeDvarValue(const std::string& value) {
    if (value.empty() || value.size() > 96) return false;
    return std::all_of(value.begin(), value.end(), [](unsigned char c) {
        return std::isalnum(c) || c == '_' || c == '.' || c == '/' || c == ':' || c == '-';
    });
}

bool validGameModeId(const std::string& id) {
    if (id.empty() || id.size() > 24) return false;
    return std::all_of(id.begin(), id.end(), [](unsigned char c) { return std::islower(c) || std::isdigit(c) || c == '_'; });
}

const std::vector<GameModePreset>& builtinGameModes() {
    static const std::vector<GameModePreset> modes = {
        make("war", "Team Deathmatch", "war", {}, "The core team game type."),
        make("dm", "Free-for-all", "dm", {}, "Every player for themselves."),
        make("dom", "Domination", "dom", {}, "Needs the three flags of the Domination template."),
        make("sd", "Search and Destroy", "sd", {}, "Needs the bomb sites of the Search and Destroy template."),
        make("ctf", "Capture the Flag", "ctf", {}, "Needs the two flag bases."),
        make("koth", "Hardpoint", "koth", {}, "Needs hardpoint zones; hardpoint_chevron scriptables mark them."),
        // Research 87, 4.3: the dvars of the private server's playlist catalogue (data/playlists/5800/catalog.json of
        // the owner's server project), entry for entry. All four are the script "br". RKMMNSQKO is the name the br
        // scripts read the loot table from; loot_table_name is the engine's.
        make("br_trios", "Warzone: Battle Royale Trios", "br",
             {{"OKTSLRSQMS", "-1"}, {"RKMMNSQKO", "mp/loot_item_defs_s3.csv"},
              {"br_mission_reward_groups_filename", "mp/brmission_reward_groups.csv"}, {"loot_table_name", "mp/loot_item_defs_s3.csv"},
              {"scr_br_teamsize", "3"}, {"scriptable_force_large_mode", "1"}, {"sv_forceNetfieldLoD", "-1"}},
             "The Warzone battle royale in teams of three, as the Trios playlist sets it. Meant for the stock Warzone maps."),
        make("br_quads", "Warzone: Battle Royale Quads", "br",
             {{"OKTSLRSQMS", "-1"}, {"RKMMNSQKO", "mp/loot_item_defs_s3.csv"},
              {"br_mission_reward_groups_filename", "mp/brmission_reward_groups.csv"}, {"loot_table_name", "mp/loot_item_defs_s3.csv"},
              {"scr_br_teamsize", "4"}, {"scriptable_force_large_mode", "1"}, {"sv_forceNetfieldLoD", "-1"}},
             "Teams of four."),
        make("br_solos_stim", "Warzone: Stimulus Solos", "br",
             {{"OKTSLRSQMS", "-1"}, {"RKMMNSQKO", "mp/loot_item_defs_cash.csv"},
              {"br_mission_reward_groups_filename", "mp/brmission_reward_groups.csv"}, {"loot_table_name", "mp/loot_item_defs_cash.csv"},
              {"scr_br_teamsize", "1"}, {"scriptable_force_large_mode", "1"}, {"sv_forceNetfieldLoD", "-1"}},
             "Solo battle royale with the cash loot table (buy back with money)."),
        make("blood_money", "Warzone: Blood Money (Plunder)", "br",
             {{"OKTSLRSQMS", "-1"}, {"br_kiosk_items_filename", "mp/brKioskPurchases_DMZ.csv"},
              {"br_mission_reward_filename", "mp/brmission_rewards_dmz.csv"},
              {"br_mission_reward_groups_filename", "mp/brmission_reward_groups_dmz.csv"}, {"scr_br_gametype", "dmz"},
              {"scr_br_teamsize", "3"}, {"scr_br_timelimit", "1800"}, {"scr_dmz_lc_active", "1"}, {"sv_forceNetfieldLoD", "-1"},
              {"ui_blood_money", "1"}},
             "The cash-collecting variant: the br script with its dmz sub-mode."),
        // For a small custom map (research 87, section 6): what the stock tutorial map switches off.
        make("br_small_map", "Warzone on a small map (no circle, no gulag, no plane)", "br",
             {{"scr_br_teamsize", "1"}, {"scr_br_circle_disable", "1"}, {"scr_br_gulag", "0"}},
             "The br game type on a map without circle data, a gulag arena or a plane path, as the stock Warzone tutorial map "
             "runs it. Players start at the map's spawn points.",
             true, true, true),
        make("br_small_map_teams", "Warzone on a small map, teams of three", "br",
             {{"scr_br_teamsize", "3"}, {"scr_br_circle_disable", "1"}, {"scr_br_gulag", "0"}},
             "The same with teams of three.", true, true, true),
    };
    return modes;
}

std::string modeDvarsText(const std::vector<std::pair<std::string, std::string>>& dvars) {
    std::string out;
    for (const auto& [name, value] : dvars) out += (out.empty() ? "" : "; ") + name + "=" + value;
    return out;
}

std::vector<std::pair<std::string, std::string>> parseModeDvars(const std::string& text, std::vector<std::string>* notes) {
    std::vector<std::pair<std::string, std::string>> out;
    for (size_t at = 0; at <= text.size();) {
        const size_t semi = text.find(';', at);
        const std::string pair = trim(text.substr(at, semi == std::string::npos ? std::string::npos : semi - at));
        at = semi == std::string::npos ? text.size() + 1 : semi + 1;
        if (pair.empty()) continue;
        const size_t eq = pair.find('=');
        const std::string name = trim(eq == std::string::npos ? pair : pair.substr(0, eq));
        const std::string value = eq == std::string::npos ? std::string() : trim(pair.substr(eq + 1));
        if (!validModeDvarName(name) || !validModeDvarValue(value)) {
            if (notes) notes->push_back("mode dvar '" + pair + "' is not name=value with a plain value; left out");
            continue;
        }
        bool replaced = false;
        for (auto& have : out)
            if (have.first == name) have.second = value, replaced = true;
        if (!replaced && out.size() < 64) out.push_back({name, value});
    }
    return out;
}

std::string modeBrText(const GameModePreset& p) {
    std::string out;
    const auto add = [&](bool on, const char* word) {
        if (on) out += std::string(out.empty() ? "" : " ") + word;
    };
    add(p.brNoCircle, "nocircle");
    add(p.brNoGulag, "nogulag");
    add(p.brSkipDrop, "skipdrop");
    return out;
}

void parseModeBr(const std::string& text, bool& noCircle, bool& noGulag, bool& skipDrop) {
    noCircle = noGulag = skipDrop = false;
    std::istringstream in(text);
    for (std::string word; in >> word;) {
        std::transform(word.begin(), word.end(), word.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        if (word == "nocircle") noCircle = true;
        else if (word == "nogulag") noGulag = true;
        else if (word == "skipdrop") skipDrop = true;
    }
}

std::string modeRulesText(const ModeRules& r) {
    std::string out;
    const auto add = [&](const std::string& part) { out += (out.empty() ? "" : "; ") + part; };
    if (r.health > 0) add("health=" + std::to_string(r.health));
    if (r.speed > 0) {
        std::ostringstream ss;
        ss << r.speed;
        add("speed=" + ss.str());
    }
    if (!r.weapon.empty()) add("weapon=" + r.weapon);
    if (r.infiniteAmmo) add("ammo=infinite");
    return out;
}

ModeRules parseModeRules(const std::string& text, std::vector<std::string>* notes) {
    ModeRules r;
    std::string part;
    std::istringstream in(text);
    const auto trim = [](std::string s) {
        const auto a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
        return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
    };
    while (std::getline(in, part, ';')) {
        part = trim(part);
        if (part.empty()) continue;
        const size_t eq = part.find('=');
        std::string name = trim(part.substr(0, eq)), value = eq == std::string::npos ? std::string() : trim(part.substr(eq + 1));
        for (char& c : name) c = char(std::tolower(static_cast<unsigned char>(c)));
        for (char& c : value) c = char(std::tolower(static_cast<unsigned char>(c)));
        char* end = nullptr;
        if (name == "health") {
            const long v = std::strtol(value.c_str(), &end, 10);
            if (!value.empty() && *end == 0 && v >= 1 && v <= 1000) r.health = int(v);
            else if (notes) notes->push_back("rule health=" + value + ": a number 1 to 1000");
        } else if (name == "speed") {
            const double v = std::strtod(value.c_str(), &end);
            if (!value.empty() && *end == 0 && v >= 0.2 && v <= 3.0) r.speed = v;
            else if (notes) notes->push_back("rule speed=" + value + ": a scale 0.2 to 3");
        } else if (name == "weapon") {
            const bool ok = !value.empty() && value.size() <= 64 &&
                            std::all_of(value.begin(), value.end(), [](unsigned char c) { return std::islower(c) || std::isdigit(c) || c == '_'; });
            if (ok) r.weapon = value;
            else if (notes) notes->push_back("rule weapon=" + value + ": a weapon name of a-z, 0-9 and _");
        } else if (name == "ammo") {
            if (value == "infinite") r.infiniteAmmo = true;
            else if (notes) notes->push_back("rule ammo=" + value + ": only ammo=infinite");
        } else if (notes) {
            notes->push_back("unknown rule '" + name + "' (health, speed, weapon, ammo)");
        }
    }
    return r;
}

std::string gameModePresetJson(const GameModePreset& p) {
    std::string json = "{\n  \"id\": " + quote(p.id) + ",\n  \"title\": " + quote(p.title) + ",\n  \"gametype\": " + quote(p.gametype) +
                       ",\n  \"description\": " + quote(p.description) + ",\n  \"dvars\": {";
    for (size_t i = 0; i < p.dvars.size(); ++i)
        json += std::string(i ? "," : "") + "\n    " + quote(p.dvars[i].first) + ": " + quote(p.dvars[i].second);
    json += std::string(p.dvars.empty() ? "" : "\n  ") + "},\n  \"br\": {\"no_circle\": " + (p.brNoCircle ? "true" : "false") +
            ", \"no_gulag\": " + (p.brNoGulag ? "true" : "false") + ", \"skip_drop\": " + (p.brSkipDrop ? "true" : "false") + "}";
    if (!p.rules.empty()) json += ",\n  \"rules\": " + quote(modeRulesText(p.rules));
    return json + "\n}\n";
}

bool parseGameModePreset(const std::string& text, GameModePreset& out, std::string* err) {
    const auto fail = [&](const std::string& what) {
        if (err) *err = what;
        return false;
    };
    std::string perr;
    const std::optional<json::Value> root = json::parse(text, &perr);
    if (!root || root->type != json::Value::Type::Object) return fail("not a JSON object" + (perr.empty() ? std::string() : ": " + perr));
    GameModePreset p;
    const auto str = [&](const char* key) {
        const json::Value* v = root->get(key);
        const std::string* s = v ? v->string() : nullptr;
        return s ? *s : std::string();
    };
    p.id = str("id"), p.title = str("title"), p.gametype = str("gametype"), p.description = str("description");
    if (!validGameModeId(p.id)) return fail("id '" + p.id + "' must be 1 to 24 characters of a-z, 0-9 and _");
    if (!validGameTypeName(p.gametype)) return fail("gametype '" + p.gametype + "' is not a game type name (a-z, 0-9, _; at most 11)");
    if (p.title.empty()) p.title = p.id;
    if (const json::Value* dv = root->get("dvars")) {
        if (dv->type != json::Value::Type::Object) return fail("dvars must be an object of name: value");
        for (const auto& [name, value] : dv->obj) {
            const std::string* s = value.string();
            std::string v = s ? *s : std::string();
            if (!s && value.type == json::Value::Type::Number) {
                std::ostringstream ss;
                ss << value.num;
                v = ss.str();
            }
            if (!validModeDvarName(name)) return fail("dvar name '" + name + "' has a character other than a letter, a digit or _");
            if (!validModeDvarValue(v)) return fail("the value of dvar " + name + " must be 1 to 96 characters of letters, digits and _ . / : -");
            p.dvars.push_back({name, v});
        }
    }
    if (const json::Value* br = root->get("br"); br && br->type == json::Value::Type::Object) {
        const auto flag = [&](const char* key) {
            const json::Value* v = br->get(key);
            return v && v->type == json::Value::Type::Bool && v->b;
        };
        p.brNoCircle = flag("no_circle"), p.brNoGulag = flag("no_gulag"), p.brSkipDrop = flag("skip_drop");
    }
    if (const std::string rules = str("rules"); !rules.empty()) {
        std::vector<std::string> notes;
        p.rules = parseModeRules(rules, &notes);
        if (!notes.empty()) return fail(notes.front());
    }
    out = std::move(p);
    return true;
}

std::vector<GameModePreset> loadGameModes(const std::string& userDir, std::vector<std::string>* notes) {
    std::vector<GameModePreset> modes = builtinGameModes();
    std::error_code ec;
    std::vector<std::filesystem::path> files;
    for (std::filesystem::directory_iterator it(std::filesystem::path(userDir), ec), end; !ec && it != end; it.increment(ec)) {
        const std::string name = it->path().filename().string();
        const std::string suffix = ".iw8mode.json";
        if (it->is_regular_file(ec) && name.size() > suffix.size() && name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0)
            files.push_back(it->path());
    }
    std::sort(files.begin(), files.end());
    for (const auto& file : files) {
        std::ifstream f(file, std::ios::binary);
        std::ostringstream ss;
        ss << f.rdbuf();
        GameModePreset p;
        std::string err;
        if (!parseGameModePreset(ss.str(), p, &err)) {
            if (notes) notes->push_back(file.filename().string() + ": " + err);
            continue;
        }
        bool replaced = false;
        for (auto& have : modes)
            if (have.id == p.id) have = p, replaced = true;
        if (!replaced) modes.push_back(std::move(p));
    }
    return modes;
}

const GameModePreset* findGameMode(const std::vector<GameModePreset>& modes, const std::string& id) {
    for (const auto& m : modes)
        if (m.id == id) return &m;
    return nullptr;
}

}  // namespace iw8r
