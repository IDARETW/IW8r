// IW8 Radiant - spawn analysis: sight lines between spawns, spacing warnings and an XY clustering heat map.
//
// Spawns are the mp_*spawn* entities (classnames in docs/research/10_iw8_entity_inventory.md) plus info_player_start.
//   - mode: the text between "mp_" and "_spawn" (mp_tdm_spawn_axis_start -> "tdm"); "" for info_player_start.
//   - team: 1 allies (classname contains "allies"), 2 axis ("axis"), 0 otherwise (secondary, attacker/defender, ...).
//   - start: the classname ends in "_start".
// Spawns only ever relate to spawns of the same mode. An enemy is a start spawn of the other team (both teams nonzero).
// Friendly is any other same-mode spawn that is not an enemy.
// Distances are measured between eye points (origin + kSpawnEyeHeight); the offset cancels, so they equal origin distances.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "collision_world.h"
#include "geometry.h"
#include "map_text.h"

namespace iw8r {

// Standing eye height above the spawn origin (the feet), in units.
inline constexpr double kSpawnEyeHeight = 60.0;

// Editor guidelines used by spawnWarnings(). They are advice, not hard rules of the IW8 build.
// A start spawn that sees at least this many enemy start spawns is warned (spawn camping).
inline constexpr int kEnemySightWarnCount = 2;
// A spawn with another same-mode spawn closer than this (3D) is a spawn trap risk.
inline constexpr double kMinSpawnSpacing = 128.0;
// The allies and axis start spawns of one mode should be, on average, at least this far apart.
inline constexpr double kMinTeamStartSpacing = 1024.0;

struct SpawnPoint {
    Id entity = kNoId;      // Entity::id of the spawn entity
    std::string classname;
    std::string mode;       // "tdm", "dm", "dom", "koth", ...; "" for info_player_start
    int team = 0;           // 0 any, 1 allies, 2 axis
    bool start = false;     // classname ends in "_start"
    Vec3 eye;               // origin + (0, 0, kSpawnEyeHeight)
    double yaw = 0;         // second value of "angles" (or the "angle" key), degrees
};

// Every spawn entity with an origin, in map entity order. Entities without an origin are skipped.
std::vector<SpawnPoint> collectSpawns(const MapFile& map);

struct SpawnExposure {
    Id entity = kNoId;
    int visibleEnemies = 0;        // enemy start spawns with an unblocked eye-to-eye ray within maxDistance
    int visibleAny = 0;            // any other same-mode spawn with an unblocked ray within maxDistance
    double nearestEnemy = 0;       // geometric distance to the nearest enemy start; +infinity when there is none
    double nearestFriendly = 0;    // geometric distance to the nearest friendly spawn; +infinity when there is none
};

// One result per input spawn, same order. Sight uses the collision world's ray trace (traceRay fraction 1 = clear).
// Distances ignore visibility and maxDistance.
std::vector<SpawnExposure> analyseSpawns(const std::vector<SpawnPoint>& spawns, const CollisionWorld& world,
                                         double maxDistance = 4000);

// Human-readable warnings. `exposures` must come from analyseSpawns() on the same `spawns` (matched by index).
// Covers: start spawns that see kEnemySightWarnCount or more enemy starts; spawns closer than kMinSpawnSpacing to another
// same-mode spawn; and per mode, allies and axis starts whose average pairwise distance is below kMinTeamStartSpacing.
std::vector<std::string> spawnWarnings(const std::vector<SpawnPoint>& spawns, const std::vector<SpawnExposure>& exposures);

// Row-major (cellsY rows of cellsX) XY heat map over `bounds`. Each cell value is the sum over spawns of
// max(0, 1 - d / radius), where d is the XY distance from the cell centre to the spawn. Empty when cellsX or cellsY < 1.
std::vector<double> spawnDensity(const std::vector<SpawnPoint>& spawns, const AABB& bounds, int cellsX, int cellsY,
                                 double radius);

}  // namespace iw8r
