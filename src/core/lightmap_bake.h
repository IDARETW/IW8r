// IW8 Radiant - lightmap baking for the editor's own maps (roadmap 5.11 / 5.21; docs/research/94 and 95).
//
// The game lights a world texel from three planes (research 94): an irradiance colour C, an ambient fraction f and a
// tangent-space direction D, as  (1 - f) C / max(D.z, .001) sat(N.D) + f C  (the stock world pixel shader, as the
// converter reads it). The converter already writes those planes from an extraction's lightmap pair in the IW7 layout
// (an ambient layer A and a directional layer B with the direction's slopes, log encoded; iw3_render_assets.cpp
// EncodeNativeLightmapPixel with constant_ambient: C = A + B D.z, f = lum(A) / lum(C)). This file bakes A, B and D:
//   - charts: the triangles of each surface grown into charts of near-equal normal, projected on the chart plane,
//     packed into one square atlas (the converter's native path takes one lightmap);
//   - texels: the position, normal and tangent frame of the triangle under each texel centre; border texels are filled
//     from their neighbours so bilinear filtering does not bleed;
//   - light: the sun and point / spot lights with shadow rays (B and D: the luminance-weighted direction of the direct
//     light), the sky and one bounce of the sun from cosine-weighted hemisphere rays (A).
// Light values are in the build's light units (the sun gives sunColour * sunIntensity on a surface facing it).
// Pure core code; multithreaded.
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "geometry.h"
#include "ray_bvh.h"

namespace iw8r {

struct BakeVertex {
    Vec3 pos, normal, tangent;
};
struct BakeSurface {
    std::vector<BakeVertex> verts;
    std::vector<uint32_t> idx;  // triangles
    bool lit = true;            // false: no lightmap (sky, tool surfaces); still casts shadows
    // What the surface reflects of the light that bounces off it (linear, 0..1 per channel): the average of its
    // material's colour image. A negative x: not known, BakeSettings::bounceAlbedo grey is used. triAlbedo, when it has
    // one entry per triangle, gives it per triangle instead (the viewport bakes every polygon in one surface).
    Vec3 albedo{-1, -1, -1};
    std::vector<Vec3> triAlbedo;
    // The lightmap density of each triangle against BakeSettings::texelSize (2 = texels half as wide; a face's
    // lightmap scale, core lightmap_fields lightmapDensityOfSuffix). Empty or not one per triangle: 1 everywhere.
    // Triangles of different densities are never in one chart.
    std::vector<float> triDensity;
};

// The bounce reflectance of a material from its colour image: the average of the sRGB bytes in linear light, kept
// inside 0.02 .. 0.9 (nothing reflects all light, and nothing none). `rgba` holds `texels` RGBA8 pixels; `step` reads
// every step-th pixel (1 = all).
Vec3 bakeAlbedoFromRgba(const uint8_t* rgba, size_t texels, size_t step = 1);

// A light entity in the game's units (core game_look): colour x intensity is the IW3 colour (the _color and
// intensity keys); the converter turns the radius and a reference distance into the game light's intensity, which
// falls off as 1 / d^2 inside a smooth window to the radius.
struct BakeLight {
    Vec3 pos;
    Vec3 color{1, 1, 1};
    double intensity = 1, radius = 256;
    double reference = 0;  // the converter's reference distance; 0: radius / 2 (omni) or the spot axis's first hit
    bool spot = false;
    Vec3 dir{0, 0, -1};         // spot direction
    double cosInner = 1, cosOuter = 0.7;
    // false: the game draws this light in real time (a converted primary light), so the lightmap keeps only its
    // bounce; true: its direct light is baked too.
    bool direct = true;
};

struct BakeSettings {
    double texelSize = 16;      // world units per texel (grows when the atlas does not fit)
    int maxSize = 2048;         // the atlas side limit
    int padding = 2;            // texels around each chart
    int skyRays = 32;           // hemisphere rays per texel
    double bounceAlbedo = 0.45; // the bounce surface reflectance where a surface does not give its own
    // Bounces after the first, estimated: the first bounce times 1 / (1 - albedo x enclosure), where enclosure is the
    // share of a texel's rays that hit geometry (0 under the open sky, 1 in a closed room) and albedo the mean of what
    // they hit. False: one bounce only.
    bool laterBounces = true;
    int threads = 0;            // 0: all cores
    double chartNormalDot = 0.95;
};

struct BakeScene {
    std::vector<BakeSurface> surfaces;          // the surfaces to light (also occluders)
    std::vector<RayBvh::Triangle> extraOccluders;  // models and other geometry that only casts shadows
    Vec3 sunDir{0, 0, 1};                       // unit vector toward the sun
    Vec3 sunColor{1, 1, 1};
    double sunIntensity = 0;                    // 0: no sun
    // false: the game draws the sun in real time (the converter's world shader adds visibility x N.L x sun on top of
    // the lightmap), so the lightmap keeps only its bounce and the sun visibility.
    bool sunDirect = true;
    Vec3 skyColor{0.55, 0.65, 0.8};
    double skyIntensity = 0.35;                 // irradiance of an open sky on an upward surface, in sun units
    std::vector<BakeLight> lights;
    // When set and it returns true for a texel, its result is the texel's ambient light and no sky or bounce ray is
    // traced there (brushes on a stock map: the map's own light grid, plan 1.9). Called from the bake's threads.
    std::function<bool(const Vec3& pos, const Vec3& normal, Vec3& ambient)> ambientAt;
};

struct BakeResult {
    bool ok = false;
    std::string error;
    int width = 0, height = 0;
    double texelSize = 0;  // the size used
    size_t charts = 0, texelsLit = 0;
    // Per surface, per vertex (after the split below): the lightmap UV in [0, 1].
    std::vector<std::vector<std::array<float, 2>>> uv;
    // Surfaces with vertices split where one vertex belonged to two charts (indices remapped).
    std::vector<BakeSurface> surfaces;
    // Per surface, per vertex: the vertex of the input surface it copies (identity for vertices that were not split).
    std::vector<std::vector<uint32_t>> sourceVertex;
    // Per texel (row-major, width * height): ambient and directional light (rgb) and the direction of the directional
    // light in the texel's tangent frame (unit, z >= 0); covered: 1 where a triangle lies under the texel.
    std::vector<std::array<float, 3>> ambient, directional, direction;
    std::vector<uint8_t> covered;
    // Per texel: the sun's visibility (1 lit, 0 shadowed; the IW3 primary-light mask the converter's shader falls back
    // on where its baked sun depth does not reach).
    std::vector<float> sunVisibility;
    // Per texel: the direction of `direction` in world space (the viewport shades in world space).
    std::vector<std::array<float, 3>> directionWorld;
};

// Bakes the scene. `progress` (optional) is called with 0..1.
BakeResult bakeLightmap(const BakeScene& scene, const BakeSettings& settings, const std::function<void(double)>& progress = {});

// The extraction's lightmap pair for the converter (IW7 layout, log encoding: byte = 255 log2(1 + s v) / log2(1 + s M)):
//   primary: width * height bytes, the sun visibility (255 lit; all 255 when the result has none);
//   secondary: width * (2 * height) BGRA bytes, the ambient layer (BGR + direction slope X in alpha) above the
//   directional layer (BGR + direction slope Y), slopes D.x / D.z and D.y / D.z encoded as the converter decodes them.
// `logScale` and `maximum` are the world's "lightmap_encoding".
struct EncodedLightmap {
    std::vector<uint8_t> primary, secondary;
    double logScale = 16, maximum = 1;
};
EncodedLightmap encodeLightmapPair(const BakeResult& r, double logScale = 16);

// Decodes one texel of an encoded pair back to the game's plane values (C, f, D) as the converter computes them
// (tests and the viewport preview).
void decodeLightmapTexel(const EncodedLightmap& e, int width, int height, int x, int y, float c[3], float& f, float d[3]);

}  // namespace iw8r
