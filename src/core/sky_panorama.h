// Equirectangular and Radiance HDR panoramas as custom sky images (roadmap 5.17). The output is the legacy DDS cube that
// sky_image.h produces for stock skies, so the same converter path (x64-zt DecodeDds) reads both.
//
// Conventions (world axes, Z up, as in the rest of the editor):
//  - Panorama column: the centre column is +X and azimuth grows to the LEFT (+Y a quarter of the width left of the
//    centre), the seam is at -X: the usual layout of panoramas viewed from inside (Blender, HDRI sites).
//  - Panorama row: row 0 is the zenith, the bottom row the nadir (elevation = 90 - 180 * v/h degrees).
//  - yawDeg rotates the sky about +Z: content at panorama azimuth a appears in the world at azimuth a + yaw.
//  - Sun: worldspawn "sundirection" "pitch yaw 0" is the toward-sun vector (cos p cos y, cos p sin y, -sin p)
//    (compile_iw8.cpp sunDirection, scene.cpp sunToward). Negative pitch = sun above the horizon.
#pragma once

#include <string>
#include <vector>

#include "geometry.h"

namespace iw8r {

// Linear RGB floats (rgb.size() == w * h * 3), row 0 at the top.
struct FloatImage {
    int w = 0, h = 0;
    std::vector<float> rgb;
};

// Reads PNG/JPG/TGA/BMP (8-bit sRGB, converted to linear) or a Radiance .hdr (already linear). The image is expected to be
// 2:1 (360 x 180 degrees); other shapes are sampled as if they were. False with *err on an unreadable file.
bool readPanorama(const std::string& path, FloatImage& out, std::string* err);

// Six faces of faceSize texels (4..2048) as a legacy B8G8R8A8 cube DDS, one mip, the layout sky_image.cpp writes.
// Each texel is bilinear-sampled from the panorama (linear space, azimuth seam wrapped, poles clamped), multiplied by
// 2^exposureEv (exposure in EV stops), clamped to [0, 1] and sRGB-encoded. The clamp is a hard tone map: highlights
// above 1 saturate. Returns an empty string with *err on bad input.
std::string cubeDdsFromPanorama(const FloatImage& pano, int faceSize, double yawDeg, double exposureEv, std::string* err);

// World direction (Z up, unit length) of the brightest region after the yaw rotation. Luminance is averaged over cells of
// at most 256 columns weighted by cos(elevation) (solid angle), then a 3x3 cell box blur picks the peak, so one hot pixel is
// diluted rather than chosen. A tie or an all-black image returns the first cell.
Vec3 brightestDirection(const FloatImage& pano, double yawDeg);

// worldspawn "sundirection" value ("pitch yaw 0") whose toward-sun vector is dir: the inverse of sunDirection in
// compile_iw8.cpp. Pitch is in [-90, 90] (negative above the horizon), yaw in [0, 360), both rounded to 0.01.
// A zero vector gives the sun straight up ("-90 0 0").
std::string sunDirectionKeyFor(const Vec3& dir);

}  // namespace iw8r
