// IW8 Radiant Suite - IES photometric profiles (roadmap 5.13): LM-63 text parsing, candela lookup and a polar
// preview curve. Core only: no UI, no compiler output. The compiler does not write IES profiles yet; see
// docs/research/38_ies_profiles.md for the converter route that would carry one.
#pragma once

#include <string>
#include <vector>

namespace iw8r {

// A photometric distribution in the file's own convention (IES LM-63, TILT=NONE, type C). Vertical angles are
// measured from the nadir: 0 points straight down, 180 straight up. Horizontal angles are C planes in degrees.
struct IesProfile {
    std::vector<double> verticalAngles;    // degrees, strictly increasing, within [0, 180]
    std::vector<double> horizontalAngles;  // degrees, strictly increasing, within [0, 360]
    // Raw candela table, plane-major as the file lists it: plane p, vertical index v is candela[p * V + v],
    // where V = verticalAngles.size().
    std::vector<double> candela;
    double multiplier = 1.0;  // candela multiplier from the header; iesIntensity applies it, the table does not
};

// Parses LM-63 text (first line "IESNA...", a TILT= line, then the numbers). Accepts TILT=NONE only, photometric
// type C only, and tables up to 1,000,000 candela values. Extra numbers after the table are ignored. On failure
// returns false, sets *err when given, and leaves `out` unchanged.
bool parseIesFile(const std::string& text, IesProfile& out, std::string* err);

// Candela (multiplier applied) at a vertical and horizontal angle, bilinear between the stored angles. Vertical
// angles outside the table clamp to its ends. Horizontal angles fold by the symmetry the last plane implies: a
// table ending at 90 is quadrant symmetric, 180 mirrors about the 0 plane, and any other end wraps around the
// full turn. A table with one plane is the same at every horizontal angle. Returns 0 for an empty profile.
double iesIntensity(const IesProfile& profile, double verticalDeg, double horizontalDeg);

// `samples` values of the C0 plane across the table's vertical range, normalized so the peak is 1 (all zeros
// when the plane is dark). Empty when samples is below 2 or above 1,048,576, or the profile is empty.
std::vector<float> iesPolarCurve(const IesProfile& profile, int samples);

// Replay's IES segment for a spot light (x64-zt convert.cpp `ies_profiles`, as the IW7 exporter fills it,
// export_entities.cpp:1170-1213): 128 R8 texels, texel j at coordinate j / 255, the coordinate of a direction at angle
// t off the spot axis being (1 - cos^2 t) / 2 (front hemisphere), so t_j = asin(sqrt(2 j / 255)). Each texel is the
// profile's intensity at t_j in the C0 plane, normalised to the profile's front-hemisphere peak (255 = peak).
// Mapping taken from the converter's comment; Replay's shader use of it is NOT VERIFIED in game.
std::vector<int> iesReplayTexels(const IesProfile& profile);

}  // namespace iw8r
