// IW8 Radiant Suite - generated starting spawns from a map's walkable geometry.
//
// A mapper (or a scripted agent) gets a reasonable spawn set in one step and then adjusts it. Floors come from the
// player-blocking solids of the collision world; each grid column of `area` contributes every floor below the sky
// trace start, so multi-storey maps get spawns on each level. A floor counts when its slope is walkable and the
// player box fits above it (playerHeight of headroom, playerRadius of side clearance).
//
// Output classnames: mp_dm_spawn, mp_tdm_spawn and mp_dom_spawn (the same free-for-all points), and the team starts
// mp_tdm_spawn_allies_start / _axis_start and mp_dom_spawn_allies_start / _axis_start.
#pragma once

#include <string>
#include <vector>

#include "collision_world.h"
#include "geometry.h"

namespace iw8r {

struct SpawnGenOptions {
    int ffaCount = 16;            // mp_dm_spawn, mp_tdm_spawn and mp_dom_spawn each (same points)
    int teamStartsPerSide = 4;    // mp_tdm_spawn_allies_start / _axis_start (and dom starts) per side
    double minSpacing = 256;      // between generated spawns (XY+Z distance)
    double cellSize = 64;         // candidate sampling grid
    double playerHeight = 72;     // clearance needed above the floor
    double playerRadius = 16;     // clearance around (a box trace)
    double maxFloorSlopeDeg = 40; // floor normal must be within this of +Z
};

struct GeneratedSpawn {
    std::string classname;  // "mp_dm_spawn", "mp_tdm_spawn", "mp_tdm_spawn_allies_start", "mp_tdm_spawn_axis_start",
                            // "mp_dom_spawn", "mp_dom_spawn_allies_start", "mp_dom_spawn_axis_start"
    Vec3 origin;            // on the floor (z = floor top)
    double yaw = 0;         // degrees in [0, 360), facing the centre of the candidates' bounds
};

struct SpawnGenResult {
    std::vector<GeneratedSpawn> spawns;
    size_t candidates = 0;              // walkable sample points found
    std::vector<std::string> warnings;  // e.g. "only 9 walkable points far enough apart (16 asked)"
};

// Samples a grid over `area` (XY). The sky trace starts at area.maxs.z + 64, so floors above that are not used.
// Deterministic: the same world and options always give the same output.
//  - FFA set: farthest-point sampling from the candidate nearest the area centre, keeping minSpacing, up to ffaCount.
//  - Team starts: the candidates are split along the longer XY side of their bounds; allies take the low half and axis
//    the high half. Per side, the teamStartsPerSide candidates nearest that side's far end that keep 128 spacing.
SpawnGenResult generateSpawns(const CollisionWorld& world, const AABB& area, const SpawnGenOptions& opt = {});

// The entity text block for one spawn ("{\n\"classname\" \"...\"\n\"origin\" \"x y z\"\n\"angles\" \"0 yaw 0\"\n}\n").
// Numbers have no trailing zeros.
std::string spawnEntityText(const GeneratedSpawn& s);

}  // namespace iw8r
