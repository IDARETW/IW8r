#pragma once
// Sun and shadow worldspawn keys (roadmap 5.16). The key table lists the keys seen on stock worldspawns and what the
// editor and the x64-zt fork do with each one; see docs/research/39_sun_shadows.md for the evidence (file:line).
//
// Only sundirection and suncolor are written into the built entity string (compile_iw8.cpp, worldspawn wkv). The other
// keys are NOT passed through: the editor drops them and the fork never reads csm_*. checkSunKeys warns about values
// out of range and about keys the build ignores, so a mapper who copies a stock worldspawn sees why nothing changes.
#include <optional>
#include <string>
#include <vector>

#include "map_text.h"

namespace iw8r {

enum class SunValueType { Vec3, Float, Bool, Int };

struct SunKeyDef {
    std::string key;                 // resolved key name as written on worldspawn
    SunValueType type;
    std::string stockDefault;        // value seen on the stock worldspawn of mp_shipment ("" = not on that map)
    // Bounds. They apply to every number in the value, except sundirection where they apply to the pitch (the first
    // number) only; yaw and roll are not bounded. nullopt = no bound verified.
    std::optional<double> min;
    std::optional<double> max;
    std::string help;
    bool passedThrough;              // true when the editor writes the key into the built entity string
};

// The table, in a fixed order. Stock defaults are copied from the cached mp_shipment worldspawn (see the research doc).
const std::vector<SunKeyDef>& sunKeyDefs();

// Warnings for worldspawn sun/shadow keys: values that do not parse, values out of range, sun below the horizon, and
// keys the build does not write (passedThrough false). Empty when the keys are absent or valid. Never throws.
std::vector<std::string> checkSunKeys(const Entity& worldspawn);

}  // namespace iw8r
