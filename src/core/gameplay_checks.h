// IW8 Radiant - gameplay sanity checks on spawns and mode entities.
//
// Rules (doc 15 §3.4 for the required classes):
//   - spawn (info_player_start, or mp_*spawn*) whose player box (32 x 32 x 72, origin at the feet) overlaps a
//     colliding brush of worldspawn or func_group. Detail brushes count: they block players.
//   - two spawns closer than 32 units (3D distance); reported on the later entity.
//   - spawn with no brush top within 128 units below its origin.
//   - pitch (first angles value) or roll (third) of a spawn not 0.
//   - missing mp_global_intermission.
//   - TDM: at least 2 mp_tdm_spawn, and at least one mp_tdm_spawn_allies_start and mp_tdm_spawn_axis_start
//     (always required: the IW8 converter needs these classes, doc 15 §3.4).
//   - DM: when any mp_dm_spawn* exists, at least 2 mp_dm_spawn.
//   - DOM: when mp_dom_spawn exists, at least one mp_dom_spawn_allies_start and mp_dom_spawn_axis_start.
// Mode-level issues use entity 0 and its position is the origin of the first spawn (or zero).
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "geometry.h"
#include "map_text.h"

namespace iw8r {

struct GameplayIssue {
    size_t entity = 0;     // index into MapFile::entities
    std::string classname;
    std::string message;
    Vec3 at;               // world position of the issue
};

std::vector<GameplayIssue> gameplayIssues(const MapFile& map);

// Killstreak readiness (6.21, research 42): what stock maps carry for helicopter, airstrike and UAV killstreaks that
// this map lacks. Notes, not errors: how the gametype scripts use these is UNVERIFIED in game. care_package_col is
// added by the converter itself, so it is not listed.
std::vector<std::string> killstreakNotes(const MapFile& map);

}  // namespace iw8r
