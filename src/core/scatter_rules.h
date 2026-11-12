// IW8 Radiant Suite - procedural scatter rules (roadmap 8.12): place misc_model props on floors and
// walls by slope and material, with a density and a minimum spacing. Pure core: no UI.
//
// Pipeline: collectSurfaces() flattens the map's solid surfaces into triangles, scatter() samples points on
// them for one rule, placeScatter() writes the points as misc_model entities in one undo step.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "document.h"
#include "geometry.h"
#include "map_text.h"

namespace iw8r {

// One surface triangle. `n` is the unit normal facing the playable side (brush faces: outward plane normal;
// patch triangles: oriented toward the averaged vertex normal). `entity`/`item` identify the source primitive.
struct SurfaceTri {
    Vec3 a, b, c;
    Vec3 n;
    std::string material;
    Id entity = kNoId;
    Id item = kNoId;
};

// Triangles of worldspawn and func_group brushes and of every patch in them. Faces with tool materials
// (isIw8ToolMaterial) are skipped. With `region` set, only triangles whose bounds overlap it are kept.
std::vector<SurfaceTri> collectSurfaces(const MapFile& map, const AABB* region = nullptr);

struct ScatterRule {
    std::vector<std::string> models;  // misc_model "model" values to choose from
    std::vector<double> weights;      // per model; a size mismatch means uniform weights
    double density = 1.0;             // instances per 256 x 256 units of eligible surface
    double minSpacing = 0.0;          // minimum 3D distance between instance origins (0 = none)
    double minSlopeDeg = 0.0;         // slope = angle between the triangle normal and +Z
    double maxSlopeDeg = 30.0;
    std::vector<std::string> materialsAllow;  // case-insensitive substring match; empty = any material
    std::vector<std::string> materialsDeny;   // case-insensitive substring match; any hit rejects
    double scaleMin = 0.8;
    double scaleMax = 1.2;
    bool alignToNormal = false;       // pitch and roll follow the surface normal
    double sinkDepth = 0.0;           // origin is pushed this far below the surface (along -n)
    unsigned seed = 1;
};

struct ScatterPlacement {
    std::string model;
    Vec3 origin;
    Vec3 angles;  // pitch, yaw, roll (Radiant order, as generators.cpp)
    double scale = 1.0;
};

// Samples up to `maxCount` placements for `rule`: triangles are picked by area, points are uniform on them,
// rejected by slope, material and minimum spacing. Deterministic for a given seed and input.
std::vector<ScatterPlacement> scatter(const std::vector<SurfaceTri>& tris, const ScatterRule& rule,
                                      size_t maxCount = 5000);

// Adds one misc_model per placement as ONE undo step ("Scatter Rules") and selects them. Returns the count.
size_t placeScatter(Document& doc, const std::vector<ScatterPlacement>& placements);

// Preset I/O. The file form is {"format":"iw8r-scatter-rules","version":1,"rules":[...]}.
std::string scatterRulesToJson(const std::vector<ScatterRule>& rules);
// Missing members keep their defaults; wrong types or a bad document fail with `err` set.
bool scatterRulesFromJson(const std::string& text, std::vector<ScatterRule>& out, std::string* err = nullptr);

}  // namespace iw8r
