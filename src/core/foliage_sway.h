// IW8 Radiant - foliage sway preview and the wind worldspawn key (roadmap 5.23, part of 5.12; research doc 56).
//
// PREVIEW ONLY. swayOffset is an editor-camera approximation: it shows how foliage would lean under a chosen wind. It is
// not Replay's reactive-motion simulation (doc 56 section 3), and its amplitudes are editor choices, not measured values.
// Nothing here reaches the build: x64-zt does not read iw8r_wind yet (a fork is needed, doc 56 section 5).
//
//   iw8r_wind "yaw strength gust"   worldspawn key, three numbers:
//     yaw       degrees; 0 = +X, counter-clockwise seen from above; any finite value (360 is the same as 0)
//     strength  0..4 (1 = the amplitude in kSwayAmplitude); out of range is clamped with a warning
//     gust      cycles per second, 0..5; out of range is clamped with a warning
//   Malformed text (not exactly three finite numbers) is ignored with a warning, as atmosphere.h does for its keys.
#pragma once

#include <optional>
#include <string>

#include "geometry.h"
#include "map_text.h"

namespace iw8r {

struct WindSettings {
    double dirYawDeg = 45;  // direction the wind blows towards, degrees (0 = +X, CCW from above)
    double strength = 1;    // 0..kSwayMaxStrength; 1 = the stock amplitude below
    double gustFreq = 0.3;  // cycles per second of the main wave, 0..kSwayMaxGustHz
};

inline constexpr const char* kWindKey = "iw8r_wind";

// Limits and amplitude of the preview model (map units; 1 unit = 1 IW8 world unit). See doc 56 section 6.
inline constexpr double kSwayMaxStrength = 4.0;
inline constexpr double kSwayMaxGustHz = 5.0;
inline constexpr double kSwayFullHeight = 128.0;  // height above the base at which the sway reaches full amplitude
inline constexpr double kSwayAmplitude = 6.0;     // horizontal sway at strength 1 and full height
// Largest |offset| swayOffset can return for any input: kSwayAmplitude * kSwayMaxStrength.
inline constexpr double kSwayMaxOffset = kSwayAmplitude * kSwayMaxStrength;

// True when a material should sway in the preview. Two rules, both heuristics (doc 56 section 2.3):
//  1. The techset name contains "reactivemotion" (IW7-style reactive set name seen in x64-zt's h1 converter; UNVERIFIED
//     for IW8 names): true.
//  2. Otherwise the material name must contain a foliage word (foliage, grass, weed, bush, leaf, leaves, tree, canopy,
//     reed, flower, plant, hedge, shrub, fern). Then: an empty techset counts as foliage (custom or unresolved material);
//     a "card_" techset (weed card proxies) counts; any other techset must be alpha tested ("alt" blend token, see
//     material_blend.h). This keeps bark and branch sets (blend "rpl") out.
bool isFoliageMaterial(const std::string& materialName, const std::string& techset);

// Horizontal displacement of a point at `heightAboveBase` above the foliage base, at time `timeSec`, in map units.
// z is always 0. Zero at or below the base (heightAboveBase <= 0) and for non-finite inputs. Otherwise:
//   amplitude = kSwayAmplitude * strength * h^2,  h = min(heightAboveBase / kSwayFullHeight, 1)
//   wave      = 0.7 sin(w t + phase) + 0.3 sin(1.7 w t + 1.3 phase),  w = 2 pi gust,  |wave| <= 1
//   phase     = 0.045 x + 0.031 y + 0.017 z (radians): neighbouring points move out of step
//   offset    = amplitude * wave * (cos yaw, sin yaw, 0)
// So |offset| <= kSwayMaxOffset, growth is quadratic in height, and the result is a pure function of the inputs.
Vec3 swayOffset(const WindSettings& w, const Vec3& worldPos, double heightAboveBase, double timeSec);

// Parses the iw8r_wind value. Returns nullopt for malformed text. Out-of-range numbers are clamped and reported in `err`
// together with the clamped result; `err` is left empty for a clean value.
std::optional<WindSettings> parseWind(const std::string& value, std::string* err = nullptr);

// Writes the value text in the form parseWind reads (formatNumber, so it round-trips exactly).
std::string formatWind(const WindSettings& w);

// Reads iw8r_wind from the worldspawn. Absent key: nullopt, no warning. Malformed or clamped values add one line per
// problem to `warn` (when given).
std::optional<WindSettings> readWind(const Entity& worldspawn, std::string* warn = nullptr);

}  // namespace iw8r
