// IW8 Radiant Suite - software preview of one custom material (Material Editor live preview).
//
// Renders a material on a sphere, plane or cube, lit with the same terms as the editor camera's texture shader
// (viewport/render.cpp ps_tex): sRGB colour x tint, Lambert sun + ambient, a specular lobe whose reflectance comes from
// the specular RGB and whose gloss comes from its alpha, the normal map in the converter's IW3 slope layout
// (core/normal_pack.h), alpha test, alpha blend and the two shader blend modes.
//
// Not reproduced (the game draws these, the preview does not): shadows (sun shadow map, light entities), image based
// reflections (reflection probes), fog, the colour grade, the game's soft shoulder above 1 (the preview clips at white
// unless emission > 1, which uses the Reinhard curve from the brief), the game's exact sRGB and vertex lighting, the
// per-face vertex colour light, depth sorting between layers, and the exact Replay blend states.
#pragma once

#include <cstdint>
#include <vector>

namespace iw8r {

struct PreviewImage {
    int width = 0, height = 0;
    std::vector<uint8_t> rgba;  // RGBA8, row-major, top row first; empty = none
};

enum class PreviewShape { Sphere, Plane, Cube };

struct MaterialPreviewInput {
    PreviewImage color, normal, specular;
    bool alphaTest = false, twoSided = false, blend = false, unlit = false;
    int blendMode = 0;                  // 0 none, 1 additive, 2 multiply
    double emission = 0;                // > 1 glows
    double tint[3] = {1, 1, 1};
    PreviewShape shape = PreviewShape::Sphere;
    double yawDeg = 30, pitchDeg = 20;  // object rotation
    double sunYawDeg = 45, sunPitchDeg = 50;
    double sunColor[3] = {1, 0.96, 0.9}, ambient[3] = {0.25, 0.28, 0.33};  // linear light
    double exposure = 1;
    int background = 0;                 // 0 checker, 1 dark grey, 2 sky-to-ground gradient
    double uvRepeat = 2;                // texture repeats across the shape
};

// Renders size x size RGBA8 (alpha 255). Deterministic. Returns an empty image when size is 0 or less.
// The view is orthographic, looking down -Y with Z up. The object is turned by yaw (about Z) then pitch (about X).
// Each pixel uses 2 x 2 supersampling.
PreviewImage renderMaterialPreview(const MaterialPreviewInput& in, int size);

// Bilinear, wrapping sample of an image at (u, v) in 0..1 (any value wraps). out[] = RGBA in 0..1, sRGB encoded as
// stored. (1, 1, 1, 1) for an empty image.
void samplePreviewImage(const PreviewImage& img, double u, double v, double out[4]);

}  // namespace iw8r
