#include "game_mode_view.h"

#include <algorithm>
#include <sstream>

namespace iw8r {

const std::vector<GameModeView>& gameModeViews() {
    // Research 40 section 7 (gate lists of the stock gametype scripts). Team deathmatch and free-for-all pass only the
    // gametype's own name, so every gameobject entity is removed there.
    static const std::vector<GameModeView> modes = {
        {"war", "Team Deathmatch", {"war"}, "tdm"},
        {"dm", "Free-for-all", {"dm"}, "dm"},
        {"dom", "Domination", {"dom"}, "dom"},
        {"sd", "Search and Destroy", {"sd", "bombzone"}, "sd"},
        {"koth", "Hardpoint", {"koth", "hardpoint"}, "koth"},
        {"hq", "Headquarters", {"hq", "hqloc", "hardpoint"}, "hq"},
        {"ctf", "Capture the Flag", {"ctf"}, "ctf"},
        {"dd", "Demolition", {"dd", "dd_bombzone"}, "dd"},
        {"cyber", "Cyber Attack", {"cyber"}, "cyber"},
        {"grind", "Grind", {"grind"}, "grind"},
        {"grnd", "Drop Zone", {"grnd"}, "grnd"},
    };
    return modes;
}

const GameModeView* findGameModeView(const std::string& id) {
    for (const GameModeView& m : gameModeViews())
        if (m.id == id) return &m;
    return nullptr;
}

bool gameModeKeepsSpawn(const GameModeView& mode, const std::string& spawnClass) {
    if (spawnClass.rfind("mp_", 0) != 0) return true;
    const size_t at = spawnClass.find("_spawn", 3);
    if (at == std::string::npos) return true;
    return spawnClass.substr(3, at - 3) == mode.spawnToken;
}

bool gameModeKeepsEntity(const GameModeView& mode, const std::string& classname, const std::string* gameobjectName) {
    if (classname.rfind("mp_", 0) == 0 && classname.find("_spawn") != std::string::npos && !gameModeKeepsSpawn(mode, classname))
        return false;
    if (!gameobjectName || gameobjectName->empty()) return true;
    std::string value = *gameobjectName;
    const bool negated = value[0] == '!';
    if (negated) value.erase(0, 1);
    bool match = false;
    if (classname == "script_struct") {
        match = value.find(mode.id) != std::string::npos;  // load.gsc: issubstr against the gametype
    } else {
        std::istringstream in(value);
        std::string token;
        while (in >> token)
            if (std::find(mode.keeps.begin(), mode.keeps.end(), token) != mode.keeps.end()) match = true;
    }
    return negated ? !match : match;
}

}  // namespace iw8r
