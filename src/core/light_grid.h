// IW8 Radiant - light grid for builds (owner test 2026-11-10: the gun / viewmodel was black in custom maps because
// the editor wrote no light grid). docs/research/58_light_grid.md: the converter (x64-zt iw3_lightgrid.cpp) reads
// extracted/maps/mp/<id>.d3dbsp.lightgrid.json plus four binary arrays and bakes Replay's grid from them. Viewmodels,
// players, entities and static models sample it.
//
// The grid here is the converter's fixed lattice (32 x 32 x 64 units, origin -131072) over the map bounds, every point
// holding one ambient palette colour and a sun visibility (primaryLight 1 = sunlit, 0 = in shadow) from a ray toward the
// sun through the map's collision. Rows run along x, columns along y, z fastest.
#pragma once

#include <array>
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
    size_t colorCount = 1;  // palette entries
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
    // Per-point light (ROADMAP 5.20): the 56 samples (lightGridSampleDirections order) of the palette colour at a point,
    // in the grid's 8-bit units (255 = a surface lit by 1.0, the sun colour x 1 of the build). The converter projects them
    // to SH; models and viewmodels take that light (the sun comes on top through primaryLight). Called once per palette
    // cell (paletteCellXY x paletteCellXY x paletteCellZ lattice points; the cell's point nearest its centre among the
    // kept ones) from several threads. Unset: every point takes `ambient`.
    std::function<void(const Vec3& point, std::array<std::array<float, 3>, 56>& samples)> samplesAt;
    int paletteCellXY = 4, paletteCellZ = 1;
    int threads = 0;  // 0: all cores
};

// The 56 sample directions of a palette colour: the shell texels of the 4 x 4 x 4 model-lighting volume (z outer, then
// y, then x; the 8 interior texels left out), normalised (iw3_lightgrid.cpp LightGridProjection).
const std::array<Vec3, 56>& lightGridSampleDirections();

struct LightGridLocalLight {
    Vec3 origin;
    Vec3 color;   // RGB * intensity (linear scale)
    double radius = 400.0;
    bool spot = false;
    Vec3 dir{0, 0, -1};  // emission direction (unit)
    double cosOuter = 0.0;
    double cosInner = 0.5;
};

// The light of one point as those 56 samples (ROADMAP 5.20), in the grid's 8-bit units (255 = lit by 1.0): the sky
// where a ray escapes upward, one bounce of the sun and the sky off what a ray hits, plus direct & bounced
// contribution from local map lights (so indoor rooms lit by lamps illuminate viewmodels and props).
void lightGridSamplesAt(const class RayBvh& bvh, const std::vector<Vec3>& albedos, const Vec3& sunToward, const Vec3& sunColour, const Vec3& sky,
                        double reach, const Vec3& point, std::array<std::array<float, 3>, 56>& out,
                        const std::vector<LightGridLocalLight>* localLights = nullptr);

// The light a surface with normal `n` takes from a point's 56 samples, as the build's world shader reads a placed
// model's light (the converter's InstanceAmbient: the samples as the shell of a 4 x 4 x 4 volume of bytes, read
// trilinearly where the normal leaves the cube, times 2). In lightmap units (1 = lit by 1.0).
Vec3 modelLightFromSamples(const std::array<std::array<float, 3>, 56>& samples, const Vec3& n);

// Builds the files (validated: sizes and indices follow the converter's reader rules). False with *err when the bounds
// are invalid or the grid would exceed the converter's limits (entry_count <= 8,388,608, z extent <= 255 points).
bool buildLightGrid(const LightGridInput& in, LightGridFiles& out, std::string* err = nullptr);

// File names of the parts, relative to extracted/ (forward slashes, as the json's "file" values).
std::string lightGridPartPath(const std::string& mapId, const std::string& part);

}  // namespace iw8r
