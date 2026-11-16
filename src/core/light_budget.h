// IW8 Radiant - light budget (roadmap 5.15): which light entities the compiler turns into primary lights,
// how many of them reach each point of an area (heat map), and the Check Map warnings for the budget.
#pragma once

#include <string>
#include <vector>

#include "geometry.h"
#include "map_text.h"

namespace iw8r {

// Editor guidelines and converter limits. Only kMaxPrimaryLocalLights is a real limit; the rest are
// mapping guidelines that a mapper can tune here.
namespace lightbudget {
// Real limit. The converter pads the primary-light table to 256 entries (x64-zt
// src/convert/iw3_iw8/convert.cpp:1125-1132, `sortedLightLimit`). Slot 0 is empty and slot 1 is the sun, so 254
// light entities fit. The compiler drops the rest (compile_iw8.cpp localPrimaryLights, kFirstLocalLight).
inline constexpr size_t kMaxPrimaryLocalLights = 254;
// Guideline: more lights than this reaching one point makes the lit preview and Replay's per-pixel pass busy.
inline constexpr int kMaxOverlap = 8;
// Guideline: shadow casters reaching one point. Shadows are off in the compiler today (can_use_shadow_map false),
// so this only matters once shadows are written.
inline constexpr int kMaxShadowOverlap = 2;
// Guideline: radius above this (inches, the default is 400) is almost always a typo or a whole-room light.
inline constexpr double kHugeRadius = 4096.0;
// Guideline: a spot whose full outer cone is narrower than this (degrees) lights almost nothing. The compiler
// clamps fov_outer to at least 1 degree, so 1 degree is the narrowest cone a map can produce.
inline constexpr double kMinSpotConeDeg = 2.0;
}  // namespace lightbudget

// One light entity as the compiler sees it. Parsed by collectLights, which follows compile_iw8.cpp.
struct LightInfo {
    Id entity = kNoId;
    std::string classname;   // lower-cased: "light", "light_spot" or "light_omni"
    Vec3 origin;
    double radius = 400;     // compiler default 400; non-positive values are replaced by 400
    bool spot = false;       // light_spot, or a `light` with a resolved target or an fov key
    Vec3 dir;                // spot emission axis (unit length); zero for an omni
    double cosOuter = -1;    // cos(fov_outer / 2) for a spot; unused (-1) for an omni
    bool shadows = false;    // "shadows" key (1/true/yes). The compiler never reads it; see collectLights
};

// Parses every light entity in map order. Entities with primitives are skipped, as the compiler skips them.
std::vector<LightInfo> collectLights(const MapFile& map);

// True when `p` is lit by `light`: inside the radius, and for a spot also inside the outer cone.
bool lightReaches(const LightInfo& light, const Vec3& p);

// Lights reaching each XY cell centre at height z, row-major (index = row * cellsX + column, row = y).
// Cells split `bounds` evenly. Empty when the grid or the bounds are empty.
std::vector<int> lightOverlap(const std::vector<LightInfo>& lights, const AABB& bounds, int cellsX, int cellsY,
                              double z);

struct LightBudgetIssue {
    Id entity = kNoId;   // kNoId for a map-wide issue
    bool error = false;  // true: the compiler drops or breaks the light; false: warning
    std::string message;
};

// The Check Map rules for light budget. Overlap and shadow counts are sampled at each light's origin.
std::vector<LightBudgetIssue> checkLightBudget(const std::vector<LightInfo>& lights);

}  // namespace iw8r
