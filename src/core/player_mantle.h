// IW8 Radiant - play preview mantling: ledge detection over the CollisionWorld, the Replay 1.20 mantle
// curve (origin -> mid-rise -> ledge top, as a Bezier path) and its timing. Geometry follows the Replay walk
// simulator (E:\mw124\mw120rproxy\iw8-zonetool\src\zonetool\sim\walk_mantle.cpp), with the editor's brush world
// standing in for the engine's mantle edge list.
#pragma once

#include <vector>

#include "collision_world.h"

namespace iw8r {

// Bezier path through authored points (walk_mantle.cpp BuildMantlePath, 64 arc-length samples per segment).
struct MantleBezier {
    Vec3 p0, p1, p2, p3;
    double arcLength = 0;
};
struct MantlePath {
    std::vector<MantleBezier> segments;
    double arcLength = 0;
};
MantlePath buildMantlePath(const std::vector<Vec3>& points);
// Position at a normalized arc length in [0, 1] (walk_mantle.cpp EvaluateMantlePath).
Vec3 evaluateMantlePath(const MantlePath& path, double normalizedArc);

// Ordinary (non-over) mantle waypoints: origin, a mid-rise point and the ledge top (walk_mantle.cpp
// BuildMantleWaypoints, the table branch for firstOffset.z in [24, 72]). `firstOffset` is ledge - origin.
std::vector<Vec3> buildMantleWaypoints(Vec3 origin, Vec3 firstOffset);

// Duration of an ordinary mantle in seconds (walk_mantle.cpp MantleDurationMilliseconds, height branches with
// the stock mode scale {1.0, 0.75}); the height is quantized the way the engine packs it (8 bits over 190 units).
double mantleDurationSeconds(double ledgeHeight, double arcLength, double horizontalSpeed);

// Velocity given to the player when the mantle completes (walk_mantle.cpp Replay120MantleExitVelocity).
Vec3 mantleExitVelocity(Vec3 origin, Vec3 landing, double durationSeconds);

struct MantleCandidate {
    bool valid = false;
    Vec3 landing;              // feet position on the ledge top
    double ledgeHeight = 0;    // ledge top above the feet
    bool forceCrouch = false;  // the standing body does not fit on the top; the mantle ends crouched
};

// Looks for a mantlable ledge in front of `origin` along `dir` (flat, unit). Rules:
//  - a vertical face blocks the chest-height forward probe (within reach);
//  - the top is the first downward hit beyond that face between 16 and 72 units above the feet, with a walkable normal;
//  - a ledge taller than the body needs head clearance over the feet (point trace);
//  - the box of the body must pass over the top and reach 4 units beyond the edge;
//  - the feet must land on the top (too-thin tops fail), and the crouched body must fit on it (low ceilings fail).
MantleCandidate findMantleCandidate(const CollisionWorld& world, Vec3 origin, Vec3 dir, double radius,
                                    double standHeight, double crouchHeight, double currentHeight);

// Active mantle, owned by PlayerState.
struct MantleControl {
    MantlePath path;
    double elapsed = 0, duration = 0;
    Vec3 start, end;
};

}  // namespace iw8r
