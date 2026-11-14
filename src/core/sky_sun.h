// Sun direction from a sky image (owner request 2026-11-10): the brightest part of the sky above the horizon, as the
// worldspawn sundirection the build and the lit preview read.
//
// Input: a converter cube map (legacy DDS, six B8G8R8A8 faces, sky_image.h skyCubemapFromDds), the same image the
// camera's sky box draws. Face texel (x, y) of face f maps to world (Z-up) through the camera's sky box mapping
// (camera_view.cpp appendSkyBox, inverting x64-zt SampleCubemapLinear): u = 2 (x + 0.5) / size - 1, v likewise, and
//   f0 (1, -v, -u)  f1 (-1, -v, u)  f2 (u, 1, v)  f3 (u, -1, -v)  f4 (u, -v, 1)  f5 (-u, -v, -1).
// The sun is the luminance^16-weighted centroid of the texels within 2 % of the brightest one above the horizon,
// weighted by texel solid angle, so a saturated sun disc (LDR skies clip it) gives its centre and a lone hot pixel
// in a cloud does not win.
#pragma once

#include <optional>
#include <string>

#include "geometry.h"

namespace iw8r {

struct SkySun {
    Vec3 dir;              // unit vector toward the sun, z > 0
    double peak = 0;       // brightest luminance found, 0..1
    double contrast = 0;   // peak / mean luminance above the horizon (a clear sun disc is well above 2)
};

// nullopt when the bytes are not a six-face B8G8R8A8 cube map or nothing above the horizon is lit.
std::optional<SkySun> sunFromSkyCube(const std::string& legacyCubeDds);

// Convenience: any sky DDS the editor can draw (converted with skyCubemapFromDds first).
std::optional<SkySun> sunFromSkyDds(const std::string& ddsBytes, std::string* err = nullptr);

// The skyrotation yaw (degrees, -180..180) of a map that sets none: the turn that puts the sky image's sun on the map
// sun's yaw (worldspawn sundirection yaw), as stock maps do with their skyrotation. A sky without a clear sun (contrast
// below 1.6, or none found) is taken to hold it at yaw 0, the stock convention. *imageSunYaw gets the yaw found.
double autoSkyRotationYaw(const std::string& skyDdsBytes, double sunYaw, double* imageSunYaw = nullptr);
// The same from the sun already found in the sky (nullopt: none).
double autoSkyRotationYaw(const std::optional<SkySun>& skySun, double sunYaw, double* imageSunYaw = nullptr);

}  // namespace iw8r
