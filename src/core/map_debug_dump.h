// IW8 Radiant Suite - the in-game map debug overlay of the recompiled client (docs/research/78_map_debug_overlay.md).
//
// The client file DW_MapDebug.cpp adds the dvars iw8r_mapDebug (bit mask), iw8r_mapDebugDist, iw8r_mapDebugMax and
// iw8r_mapDebugFilter, and the command iw8r_mapDebugDump, which writes the player's position and one line per server
// entity to replay-dw-client.log. This module is the editor's side: the command texts, the parser of that dump, and
// the comparison of the game's entities with the map ("which of my entities survived the load, and where are they").
// Pure: no I/O.
#pragma once

#include <string>
#include <vector>

#include "geometry.h"
#include "map_text.h"

namespace iw8r {

// Bits of iw8r_mapDebug.
inline constexpr int kMapDebugLabels = 1, kMapDebugTriggers = 2, kMapDebugPanel = 4, kMapDebugCrosshair = 8,
                     kMapDebugAllBoxes = 16, kMapDebugActors = 32;

struct MapDebugPreset {
    const char* name;
    int mask;
    const char* description;
};
// Off, and four useful combinations, in menu order.
const std::vector<MapDebugPreset>& mapDebugPresets();

// A filter the client accepts and the command buffer can carry: letters, digits, '_', '-', single spaces, at most 96
// characters. Returns the cleaned text; `ok` false when a character had to be refused (the text is then empty).
std::string cleanMapDebugFilter(const std::string& text, bool* ok = nullptr);

// Command lines for the game's command buffer: the mask, and the filter ("" clears it).
std::vector<std::string> mapDebugCommands(int mask, const std::string& filter);
inline constexpr const char* kMapDebugDumpCommand = "iw8r_mapDebugDump";

struct GameEntity {
    int num = -1;
    std::string type, classname, codeClassname, targetname, target, noteworthy, linkname, linkto, model, modelType;
    Vec3 origin, angles, boxMid, boxHalf;
    bool linked = false;
    int parent = -1;
};

struct MapDebugDump {
    bool found = false;     // a "IW8R_MAPDEBUG begin" line exists
    bool complete = false;  // its "IW8R_MAPDEBUG end" line exists too
    bool hasView = false, hasFeet = false;
    Vec3 view, feet;
    double pitch = 0, yaw = 0;
    std::string audio;      // the rest of the IW8R_AUDIO line
    int declared = -1;      // entities=N of the end line
    std::vector<GameEntity> entities;
};
// Parses the LAST dump block of the log text. Lines may carry a time stamp or other text before the IW8R_ word.
MapDebugDump parseMapDebugDump(const std::string& logText);

struct DumpCompare {
    int gameEntities = 0;
    int checked = 0;   // named map entities looked for
    int matched = 0;   // found with the same number of copies
    int missing = 0;   // not in the game at all
    int moved = 0;     // found, but more than kDumpMoveTolerance from the map's origin
    std::vector<std::string> lines;  // the report, one finding per line (summary first)
};
inline constexpr double kDumpMoveTolerance = 2.0;
// Looks for the map's named run-time entities (script_model, script_brushmodel, script_origin, trigger_*, and any
// other point entity with a targetname) among the game's entities by targetname, and counts the spawn points.
DumpCompare compareDumpWithMap(const MapDebugDump& dump, const MapFile& map);

}  // namespace iw8r
