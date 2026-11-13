// IW8 Radiant Suite - the game's look for the viewport (roadmap 5.11 / 5.21; docs/research/95).
//
// What an editor-built map looks like in game, as numbers the viewport can use:
//   - the light units: the converter's world shader (research 95, "Editor maps in game") draws
//       albedo x (lightmap + sun visibility x N.L x suncolor) + albedo x local lights
//     with the local lights in the converter's units (convert.cpp: intensity = (1 - ref / radius) ref^2, the light
//     falling off as 1 / d^2 inside a smooth window);
//   - the post chain (research 95 section 3): manual exposure 180 x 2^(2.84 - EV), the +1.5 EV base, the 20-stop log
//     shaper and Replay's universal colour LUT (ACES 1.0 RRT, 333-nit SDR target, shoulder, display encode 1 / 2.4).
// Values marked INFERRED in research 95 are used as the defaults it names (EV 10 for the converter's iw3 style, RRT
// weight 1, red modifier 0, gamma 1).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace iw8r {

// The vision's manual exposure (tonemapAuto off): 180 x 2^(2.84 - ev + adjust). EV 10 is the converter's iw3 style.
double gameManualExposure(double ev = 10.0, double adjust = 0.0);
// The factor from the lit colour to the colour LUT's input: exposure x 2^1.5 (r_tonemapUniversalBaseExposureAdjust).
double gameLutInputScale(double ev = 10.0, double adjust = 0.0);

// The log shaper of cs_tonemap: LUT coordinate of a linear value x (per channel).
double gameLutCoordinate(double x, int size = 64);
// The universal colour LUT's curve for one Rec.709 linear colour (already exposed): the display value 0..1 (encoded).
void gameTonemap(const double in[3], double out[3]);
// The LUT itself: size^3 RGBA8 texels, red fastest, indexed by gameLutCoordinate per channel.
std::vector<uint8_t> buildGameTonemapLut(int size = 64);
// The curve's inverse for greys: n LUT inputs (exposed linear values) whose display grey is i / (n - 1); the top
// entries stop where the curve flattens (a clipped pixel reads as that brightest value).
std::vector<float> gameTonemapInverseGrey(int n = 256);

// A map's exposure (the vision's tonemap fields; research 95 section 3). Manual: 180 x 2^(2.84 - ev + exposureAdjust).
// Auto: metered from the frame's log-average luminance between the dark / mid / light EVs and their adjustments.
struct GameExposure {
    bool autoExposure = false;
    double ev = 10.0, exposureAdjust = 0.0, maxEv = -10.0;
    double darkEv = 6.0, midEv = 11.0, lightEv = 15.0;
    double darkAdjust = -2.0, midAdjust = 0.0, lightAdjust = 1.5;
    double minAdjust = -10.0, maxAdjust = 10.0;  // the key's clamp (defaults INFERRED)
    double adaptSpeed = 1.0;
};
// The exposure for a frame whose log-average luminance (Rec.709) is avgLuminance: the manual value, or the auto
// exposure's target (key / avg). Without the +1.5 EV base.
double gameExposureFor(const GameExposure& e, double avgLuminance);
// Moves the shown exposure toward the target over dt seconds at the vision's adapt speed (in log space; the rate is
// INFERRED).
double gameAdaptExposure(double current, double target, double dt, double adaptSpeed);

// A shipped map's lighting (ACTS dumps of each zone's ComWorld sun and <map>.vision tonemap fields 63-76, the x64-zt
// converter's kIw8LightingReferences, iw3_vision.cpp): the sun intensity and colour and the auto exposure.
struct StockMapLook {
    const char* map;
    double sunIntensity;
    double sunColor[3];
    GameExposure exposure;
};
// The known shipped maps; null for others.
const StockMapLook* stockMapLook(const std::string& map);
// The shipped map whose sun intensity is nearest (in log terms) to `sunIntensity`: the converter's own way of giving a
// map an exposure (iw3_vision.cpp MatchIw8Lighting). mp_br_tut's sun equals mp_m_speedball's, for example.
const StockMapLook& stockMapLookNearest(double sunIntensity);
const std::vector<StockMapLook>& stockMapLooks();

// sRGB texture value to linear (the game samples colour maps as sRGB).
double srgbToLinear(double c);

// A converted local light (convert.cpp): the converter's intensity from the IW3 radius and the reference distance
// (radius / 2 for an omni light, the distance along a spot's axis to the first surface, at least 16).
double gameLightIntensity(double radius, double reference);
// Its irradiance factor at distance d (map_surface_realtime.hlsl LocalLighting with a flat falloff profile and the
// window scale 1, both INFERRED): intensity / d^2 x w^2 (3 - 2w), w = saturate(1 - d / radius) x spotFactor.
// Times the light's colour (IW3 colour x intensity key) and N.L.
double gameLightAttenuation(double intensity, double radius, double dist, double spotFactor = 1.0);

}  // namespace iw8r
