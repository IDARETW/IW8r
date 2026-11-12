// IW8 Radiant Suite - unlit baseline check (roadmap 1.8). See unlit_baseline.h and docs/research/55_unlit_baseline.md.
#include "unlit_baseline.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "light_budget.h"

namespace iw8r {
namespace {

// Below this luminance the sun is dimmer than the dimmest CoD4 day sun (x64-zt iw3_vision.cpp, kIw3DaySuns[0] = 0.3817).
// Under the default --style iw3 the converter uses the suncolor luminance as the sun intensity (doc 55 section 2).
constexpr double kDimSunLuminance = 0.25;
// Pitch from this value up to zero (degrees, negative = above the horizon) leaves floors with at most sin(10 deg) of the
// sun. Pitch above zero puts the sun below the horizon.
constexpr double kNearlyHorizontalPitchDeg = -10.0;

// Three whitespace-separated numbers, as compile_iw8.cpp parseVec3Text reads them (extra tokens are ignored).
bool parseVec3(const std::string& text, double out[3]) {
    const auto tok = splitTokens(text);
    if (tok.size() < 3) return false;
    for (size_t k = 0; k < 3; ++k) {
        char* end = nullptr;
        const double v = std::strtod(tok[k].c_str(), &end);
        if (end == tok[k].c_str() || *end != '\0' || !std::isfinite(v)) return false;
        out[k] = v;
    }
    return true;
}

// The editor's reading of suncolor (compile_iw8.cpp:1340-1349): when any component is above 1 all three are read as
// 0-255 and divided by 255, and a negative component is clamped to 0.
bool sunColorOf(const std::string& text, double out[3]) {
    if (!parseVec3(text, out)) return false;
    if (std::max({out[0], out[1], out[2]}) > 1.0)
        for (size_t k = 0; k < 3; ++k) out[k] /= 255.0;
    for (size_t k = 0; k < 3; ++k) out[k] = std::max(0.0, out[k]);
    return true;
}

}  // namespace

std::vector<UnlitBaselineIssue> checkUnlitBaseline(const MapFile& map) {
    std::vector<UnlitBaselineIssue> out;

    const Entity* worldspawn = nullptr;
    for (const Entity& e : map.entities)
        if (e.isWorldspawn()) {
            worldspawn = &e;
            break;
        }
    if (!worldspawn)
        out.push_back({false, "no worldspawn: the editor writes the default sun (pitch -50, yaw 45, white)"});

    // True when the sun adds no light at all: a black suncolor, or a sun below the horizon.
    bool sunAddsNothing = false;

    if (worldspawn) {
        if (const std::string* sd = worldspawn->value("sundirection")) {
            double v[3];
            if (!parseVec3(*sd, v)) {
                out.push_back({false, "sundirection '" + *sd + "' is not three numbers: the editor uses -50 45 0"});
            } else if (v[0] > 0.0) {
                sunAddsNothing = true;
                out.push_back({true, "sundirection '" + *sd +
                                         "' has positive pitch: the sun is below the horizon, so upward faces get no sun"});
            } else if (v[0] >= kNearlyHorizontalPitchDeg) {
                out.push_back({false, "sundirection '" + *sd +
                                          "' is nearly horizontal: floors get at most sin(|pitch|) of the sun"});
            }
        } else {
            out.push_back({false, "no sundirection key: the editor uses the default -50 45 0"});
        }

        if (const std::string* sc = worldspawn->value("suncolor")) {
            double c[3];
            if (!sunColorOf(*sc, c)) {
                out.push_back({false, "suncolor '" + *sc + "' is not three numbers: the editor uses 1 1 1"});
            } else {
                const double luminance = 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2];
                if (luminance <= 0.0) {
                    sunAddsNothing = true;
                    out.push_back({true, "suncolor '" + *sc + "' is black: the sun adds no light to the map"});
                } else if (luminance < kDimSunLuminance) {
                    out.push_back({false, "suncolor '" + *sc + "' has luminance below 0.25: the sun is dim (the "
                                          "default --style iw3 uses this luminance as the sun intensity)"});
                }
            }
        } else {
            out.push_back({false, "no suncolor key: the editor uses 1 1 1"});
        }
    }

    // With no sun light, only the light entities light an unbaked map. The converter reads no ambient key (doc 55).
    if (sunAddsNothing && collectLights(map).empty())
        out.push_back({true, "no light entities either: nothing lights an unbaked map"});

    return out;
}

}  // namespace iw8r
