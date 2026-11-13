// IW8 Radiant Suite - which entities a game mode keeps (View > Game Mode).
//
// The game deletes, when a match starts, every entity whose script_gameobjectname does not name one of the current
// gametype's objects (research 40 section 3.1: scripts\mp\gameobjects::main(list); tokens split on spaces and compared
// for equality, a leading '!' inverts the test). Script structs are filtered by substring instead (section 3.2:
// load.gsc filterstructs). Spawn points are per mode by class name (mp_<mode>_spawn*). Entities without a
// script_gameobjectname stay in every mode. The lists per gametype are section 7's table.
#pragma once

#include <string>
#include <vector>

namespace iw8r {

struct GameModeView {
    std::string id;                  // the gametype (getgametype): war, dm, dom, sd, ...
    std::string label;               // menu text
    std::vector<std::string> keeps;  // gameobject names its gameobjects::main list holds
    std::string spawnToken;          // mp_<token>_spawn* are its spawn points
};

// The stock MP gametypes the editor can show (research 40 section 7, plus team deathmatch and free-for-all).
const std::vector<GameModeView>& gameModeViews();
// By id; null for "" / "all" or an unknown id.
const GameModeView* findGameModeView(const std::string& id);
// Whether the mode keeps an entity of this class with this script_gameobjectname (null = the key is absent).
bool gameModeKeepsEntity(const GameModeView& mode, const std::string& classname, const std::string* gameobjectName);
// Whether a spawn class name (mp_tdm_spawn_allies_start, ...) belongs to the mode; names that are not mp_*_spawn*
// are kept.
bool gameModeKeepsSpawn(const GameModeView& mode, const std::string& spawnClass);

}  // namespace iw8r
