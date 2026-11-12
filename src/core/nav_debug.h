// Reader and overlay helpers for the x64-zt navigation debug dump.
//
// The converter (games/iw8/common/replay_navigation.cpp, Build, near the "debug dump" block) writes this
// JSON when the environment variable IW8_ZONETOOL_NAV_DEBUG is set to a non-empty file path. Nothing else
// in the converter reads that variable. The dump is diagnostic: it has no schema version and is only
// written when the navigation build runs (at least one spawn or node_pathnode, plus blocking collision).
//
// Units: IW world units (inches), Z up, the same frame as the map's own coordinates.
#pragma once

#include <string>
#include <utility>
#include <vector>

#include "geometry.h"

namespace iw8r {

struct NavDebug {
    // Walkable height grid. Column (ix, iy) is stored at index iy * nx + ix, and its centre is at
    // (minX + (ix + 0.5) * cell, minY + (iy + 0.5) * cell). A column with no kept floor holds -99999.
    double minX = 0, minY = 0, cell = 0;
    int nx = 0, ny = 0;
    std::vector<double> top;

    // One polygon per navmesh area, corners in CCW order seen from above. areaAdjacent[i][k] is the
    // area across the edge from corner k to corner k+1 (wrapping), or -1 when that edge is a boundary.
    std::vector<std::vector<Vec3>> areaPolys;
    std::vector<std::vector<int>> areaAdjacent;

    // Pathnodes. nodeZones[i] is the zone id of nodes[i]. Each link is a segment from the node that
    // lists it to the node it points at; the dump lists links per source node, so a pair may appear twice.
    std::vector<Vec3> nodes;
    std::vector<unsigned> nodeZones;
    std::vector<std::pair<Vec3, Vec3>> links;

    std::vector<Vec3> spawns;
    unsigned fixedCount = 0;  // "fixed" in the dump: node_pathnode records that became fixed nodes
    unsigned zoneCount = 0;   // "zones" in the dump
};

// Parses the dump. On failure returns false, leaves `out` empty and sets *err (when non-null).
// Rejects anything the converter would not write: non-finite or non-numeric values, a top array whose
// length is not nx * ny, areas with fewer than 3 corners, out-of-range adjacency or link indices, and
// missing top-level keys.
bool readNavDebug(const std::string& jsonText, NavDebug& out, std::string* err);

// Line segments for a 3D overlay: every area edge, a small cross and vertical tick at each node, and
// every link. All points are raised by zLift on Z (world units).
std::vector<std::pair<Vec3, Vec3>> navDebugLines(const NavDebug& dump, double zLift);

// One line of counts and bounds, for the log or a status label.
std::string navDebugSummary(const NavDebug& dump);

}  // namespace iw8r
