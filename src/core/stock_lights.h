// Stock map local lights (ComWorld primary lights of type 2 spot and 3 omni) as Replay 1.20 renders them at runtime
// (docs/research/106). Every spot and omni is evaluated per pixel, diffuse and specular, by the world and the prop
// shaders; the stock lightmap holds none of their direct light.
//   - The lights: the cached ComWorld export (fields.primaryLights.values; MainWindow::loadStockSun caches it).
//   - IES: the map's lightdef assets (name, iesProfile, firstPixel, coordOffset, coordScale) and its `*ieslookup`
//     image (R8, width x 1), both exported by ACTS from the map zone. Without them every light samples white.
//   - packStockLight writes Replay's 96-byte packed record (research 106 section 3.1); the viewport uploads it as a
//     structured buffer and runs the shader of section 3.2.
//   - evalStockLightDiffuse is a CPU transcription of the shader's diffuse (tests, offline checks).
// Read-only. Nothing here writes a file.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "stock_lightmap.h"

namespace iw8r {

// One ComWorld primary light (the fields the runtime path reads; research 106 section 3.1).
struct StockPrimaryLight {
    int index = 0;  // its row in primaryLights (0 is the empty light, the sun is type 1)
    int type = 0;   // 1 sun, 2 spot, 3 omni
    bool canUseShadowMap = false, needsDynamicShadows = false, isVolumetric = false;
    float intensity = 0;
    float color[3] = {0, 0, 0};  // colorLinearSrgb (luminance 1 on every stock light)
    float dir[3] = {0, 0, 0};    // points back toward the light (research 106 section 3.1)
    float up[3] = {0, 0, 1};
    float origin[3] = {0, 0, 0};
    float radius = 0, bulbRadius = 0;
    float bulbLength[3] = {0, 0, 0};
    float cosHalfFovOuter = 0, cosHalfFovInner = 0, cosHalfFovCollimation = 0;
    float shadowNearPlaneBias = 0, shadowSoftness = 0, shadowBias = 0;
    float distanceFalloff = 0, rotationLimit = 1;
    std::string defName;
};

// From the ComWorld export text: every primary light (all types; the caller keeps 2 and 3). Fails on a missing
// primaryLights array.
bool parseStockPrimaryLights(const std::string& comWorldJson, std::vector<StockPrimaryLight>& out, std::string* err = nullptr);

// A lightdef asset export (one file): its IES slot in the map's *ieslookup strip.
struct StockLightDef {
    std::string name, iesProfile;
    int firstPixel = 0;
    float coordOffset = 0, coordScale = 0;  // 0, 0: no profile (texel 0, white)
};
bool parseStockLightDef(const std::string& json, StockLightDef& out, std::string* err = nullptr);

// Replay's packed light record (t49, research 106 section 3.1), with a world-space position (the game's is
// camera-relative). 96 bytes; the viewport uploads it as is.
struct StockGameLight {
    float diffuse[3] = {0, 0, 0};
    float collimationOffset = 0;
    float dir[3] = {0, 0, 0};
    uint32_t packed = 0xFFu;  // bits 0-7 shadow slot (0xFF = none), 8-9 type, 30 volumetric
    float bulbDir[3] = {0, 0, 0};
    float bulbRadius = 1;
    float pos[3] = {0, 0, 0};
    float invRadius = 0;
    float spot[4] = {0, 0, 0, 1};
    float uvIrHeat[3] = {0, 0, 0};
    uint32_t iesLookup = 0;  // half(coordOffset) | half(coordScale) << 16
};
static_assert(sizeof(StockGameLight) == 96, "the GPU record is 96 bytes");

// The game's filters and packing (research 106 section 5.2): false for other types, a radius under 1.01, a black or
// zero-intensity light, and a spot with cosHalfFovOuter 1. def: the light's lightdef (by defName), null for none.
bool packStockLight(const StockPrimaryLight& light, const StockLightDef* def, StockGameLight& out);

// The type in the packed bits (2 spot, 3 omni).
inline int stockGameLightType(const StockGameLight& g) { return int((g.packed >> 8) & 3u); }

// IEEE half conversion: floatToHalf / halfToFloat of stock_lightmap.h (the shader's f16tof32 reads the result).

// The shader's diffuse for one light at point P with unit normal N (research 106 section 3.2, shadow 1): E (rgb, to be
// multiplied by the diffuse albedo, before the (1 - max F) factor), the attenuation, the cone cosine and the IES
// coordinate u. ies(coord) returns the strip's value at a strip coordinate (null: 1, a white strip).
struct StockLightEval {
    double E[3] = {0, 0, 0};
    double att = 0, cosA = 0, u = 0;
};
StockLightEval evalStockLightDiffuse(const StockGameLight& g, const double P[3], const double N[3],
                                     const std::function<float(float)>& ies = nullptr);

// The shadow camera of a canUseShadowMap light (research 106 section 4.2): pushed back along dir so the frustum holds
// the bulb, looking along -dir with roll from up, square, FOV 2 acos(cosHalfFovOuter); near = offset + the bulb's
// length along the axis + shadowNearPlaneBias, far = offset + radius. Lookup: shadow = saturate((exp(k (zr - zs)) - b) /
// (1 - b)) with z = (far - depth) / (far - near) for receiver and occluder, k = (1 - softness) 88 + softness,
// b = shadowBias. cast = false for lights without a shadow map (or no usable cone).
struct StockLightShadow {
    bool cast = false;
    float eye[3] = {0, 0, 0}, forward[3] = {0, 0, -1}, up[3] = {0, 1, 0};
    float fov = 1.0f;  // radians, both axes
    float zNear = 1, zFar = 2;
    float k = 40, bias = 0.75f;
};
StockLightShadow stockLightShadowCamera(const StockPrimaryLight& light, const StockGameLight& packed);
// Its view-projection, row-vector convention (v * M), D3D depth 0..1 from zNear to zFar, w = the depth along forward.
void stockLightShadowMatrix(const StockLightShadow& s, float out[16]);

}  // namespace iw8r
