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
// The base exposure step before the colour LUT. Research 95 inferred +1.5 EV (r_tonemapUniversalBaseExposureAdjust's
// game-test default). Calibrated 2026-11-12 against the owner's in-game screenshot of mp_m_speedball (same spot and
// FOV, Lit + Game Lighting): with the map's grading CLUT applied (GameGradingClut) the ground's luminance percentiles
// (25 / 50 / 75 %: 46 / 89 / 126 against the game's 44 / 86 / 126) match at +1.0 EV. (Without the grade 0 EV had
// looked right: the missing grade's contrast had been made up by exposure.)
constexpr double kGameBaseExposureEv = 1.0;
// The factor from the lit colour to the colour LUT's input: exposure x 2^kGameBaseExposureEv.
double gameLutInputScale(double ev = 10.0, double adjust = 0.0);

// The log shaper of cs_tonemap: LUT coordinate of a linear value x (per channel).
double gameLutCoordinate(double x, int size = 64);
// A vision's grading CLUT (vision field NONMKTKNNM = "<name>", image "gradingclut<name>": a size^3 3D texture, red
// fastest; mp_m_speedball's is 32^3 R16G16B16A16_UNORM). cs_universal_clut_generate (step 2, colorGradingFlags bit 0;
// disassembly lines 63-144) samples it at PQ(x / 1024) of the Rec.2020 linear colour, scaled to the texel centres
// (x 31/32 + 1/64), and decodes the PQ result back (x 1024): a grade in PQ space before the ACES RRT.
struct GameGradingClut {
    int size = 0;
    std::vector<float> rgb;  // size^3 x 3, PQ codes 0..1
    bool valid() const { return size >= 2 && rgb.size() == size_t(size) * size * size * 3; }
};
// Reads the DDS (DX10 header, 3D, R16G16B16A16_UNORM / R8G8B8A8_UNORM / R16G16B16A16_FLOAT).
bool gameGradingClutFromDds(const std::string& dds, GameGradingClut& out, std::string* err = nullptr);
// The ST.2084 PQ code of a linear value normalised to 10000 nits, and its inverse.
double pqEncode(double y);
double pqDecode(double code);
// The universal colour LUT's curve for one Rec.709 linear colour (already exposed): the display value 0..1 (encoded).
// grade: the map's grading CLUT (weight 1), or none.
void gameTonemap(const double in[3], double out[3], const GameGradingClut* grade = nullptr);
// The LUT itself: size^3 RGBA8 texels, red fastest, indexed by gameLutCoordinate per channel.
std::vector<uint8_t> buildGameTonemapLut(int size = 64, const GameGradingClut* grade = nullptr);
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

// A stock map's atmosphere from its vision (vision/<map>.vision; field tokens from the x64-zt converter's
// replay_vision_names.inc): distance fog whose transmittance halves every `halfPlane` units past start
// (fogHalfPlaneDistance: the atmospheric fog's, fogAtmosphericScattering 1; `density`, fogDensity, is the half distance
// of the plain fog only, x64-zt iw3_vision.cpp. Owner 2026-11-12: with fogDensity the haze was hardly visible), scaled by a height falloff
// (halves every heightHalf units above heightBase), up to maxOpacity; the inscattered light is
// 0.001 x 2^(inscatterEv - 2.84) / (4 pi) x fogColor (calibrated 2026-11-12 on mp_m_speedball: the distant haze in the
// owner's screenshot is about 7 scene units, this gives 7.2), tinted by the Rayleigh albedo and, toward the sun, by the
// Mie albedo with a Henyey-Greenstein lobe (mieBlend, mieAnisotropy). The sky takes the fog of skyDistance, times
// skyIntensity. Surfaces lose saturation by `desaturation` x the fog amount.
struct GameFog {
    bool on = false;
    double start = 0, density = 10000, halfPlane = 5000, maxOpacity = 1, maxDistance = 1e9, skyDistance = 40000, skyIntensity = 0.5;
    double heightBase = 0, heightHalf = 1e9, desaturation = 0, inscatterEv = 18;
    double color[3] = {1, 1, 1}, rayleigh[3] = {1, 1, 1}, mie[3] = {1, 1, 1};
    double mieAnisotropy = 0.4, mieBlend = 0;
    // The sky's fog: brightness (fogSkyIntensity scales the inscattered light seen on the sky; Hideout 10, Speedball
    // 0.5) and the band of zenith angles it covers (fogSkyMinAngle .. fogSkyMaxAngle, degrees from straight up: none at
    // the min, full at the max; Hideout 59 .. 100.8 keeps its haze to the horizon).
    double skyMinAngle = -90, skyMaxAngle = 90;
};
// Reads the vision text (`TOKEN "value"` lines). `on` only when the atmospheric fog fields are there.
GameFog gameFogFromVision(const std::string& visionText);
// The inscattered radiance scale, in scene light units.
double gameFogInscatter(const GameFog& f);

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
