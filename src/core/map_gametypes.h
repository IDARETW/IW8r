// IW8 Radiant Suite - which game types a map can be played in (roadmap F.11, 6.2, 6.18).
//
// The recompiled Replay client lists a custom map with Team Deathmatch only unless the package's map.json names more
// ("gameTypes": "war dm dom"; client DW_MapAvailability.cpp, research 79). The editor writes that list: a game type is
// offered when the map has the spawn classes and the objects that game type's stock maps carry (research 32). The
// mapper can name the list directly with worldspawn "iw8r_gametypes".
//
// Names are ui_gametype values: war (Team Deathmatch), dm (Free-for-all), dom (Domination), sd (Search and Destroy),
// ctf (Capture the Flag). Other game types are not detected (their needs are not researched); list them by hand.
#pragma once

#include <string>
#include <vector>

#include "map_text.h"

namespace iw8r {

struct GameTypeCheck {
    std::string gametype;              // "war"
    std::string title;                 // "Team Deathmatch"
    bool supported = false;
    std::vector<std::string> missing;  // what the map lacks for it (empty when supported)
};

// One entry per detected game type, in menu order (war, dm, dom, sd, ctf, br).
std::vector<GameTypeCheck> gameTypeChecks(const MapFile& map);

// Normalises a hand-written list: lower-case names of [a-z0-9_], 1 to 11 characters, single spaces, no repeats, at
// most 16 names. False when the text has none or breaks a rule.
bool normalizeGameTypeList(const std::string& text, std::string& out);

// The detected list alone ("war dm"), whatever worldspawn iw8r_gametypes says; "war" when no game type is complete.
std::string detectedGameTypes(const MapFile& map);

struct MapGameTypes {
    std::string list;                // for map.json "gameTypes"; never empty ("war" at least)
    bool fromKey = false;            // taken from worldspawn iw8r_gametypes
    std::vector<std::string> notes;  // one line each, for the build log and Map Info
};
MapGameTypes mapGameTypes(const MapFile& map);

// map.json text with "gameTypes" set to `list` (added, or replaced). The other keys and their order are kept; the text
// is returned unchanged when it is not a JSON object.
std::string withGameTypes(const std::string& mapJson, const std::string& list);

}  // namespace iw8r
