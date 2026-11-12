// IW8 Radiant Suite - light grid for builds (owner test 2026-11-10: the gun / viewmodel was black in custom maps because
// the editor wrote no light grid). docs/research/58_light_grid.md: the converter (x64-zt iw3_lightgrid.cpp) reads
// extracted/maps/mp/<id>.d3dbsp.lightgrid.json plus four binary arrays and bakes Replay's grid from them. Viewmodels,
// players, entities and static models sample it.
//
// The grid here is the converter's fixed lattice (32 x 32 x 64 units, origin -131072) over the map bounds, every point
// holding one ambient palette colour and a sun visibility (primaryLight 1 = sunlit, 0 = in shadow) from a ray toward the
// sun through the map's collision. Rows run along x, columns along y, z fastest.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "geometry.h"

namespace iw8r {

struct LightGridFiles {
    std::string json;  // <id>.d3dbsp.lightgrid.json
    std::string rowStarts, rowData, entries, colors;  // the four binary arrays
    size_t entryCount = 0, sunlit = 0;
    int dims[3] = {0, 0, 0};  // lattice points along x, y, z
};

struct LightGridInput {
    std::string mapId;       // "mp_name" (the json "name" is maps/mp/<id>.d3dbsp)
    AABB bounds;             // world bounds of the map
    Vec3 sunToward{0, 0, 1};  // unit vector toward the sun
    double ambient[3] = {0.7, 0.7, 0.7};  // palette colour 0..1 (times 255 in the file)
    // Sun visibility test for a lattice point (true = the sun reaches it); empty = every point sunlit.
    std::function<bool(const Vec3& point)> sunVisible;
    // Floor heights of the column at (x, y) (walkable surfaces, top to bottom); empty = the column is outside the map.
    // When set, each column keeps only the points from below its lowest floor to `bandAbove` units over its highest
    // (where players and viewmodels are), which keeps the grid small. Unset: the full box.
    std::function<std::vector<double>(double x, double y)> floorsAt;
    double bandBelow = 64, bandAbove = 320;
    size_t maxEntries = 4'000'000;  // larger grids fall back to every point sunlit (no traces)
};

// Builds the files (validated: sizes and indices follow the converter's reader rules). False with *err when the bounds
// are invalid or the grid would exceed the converter's limits (entry_count <= 8,388,608, z extent <= 255 points).
bool buildLightGrid(const LightGridInput& in, LightGridFiles& out, std::string* err = nullptr);

// File names of the parts, relative to extracted/ (forward slashes, as the json's "file" values).
std::string lightGridPartPath(const std::string& mapId, const std::string& part);

}  // namespace iw8r
