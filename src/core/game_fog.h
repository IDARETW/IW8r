// The game's analytic (distance + height) fog, decoded from Replay 1.20 (research 111): a CPU reference of
// cs_post_opaque_processes PO:34-345 with the exe's constant fill, written so it ports 1:1 to HLSL.
//
// Pipeline (all VERIFIED in the Replay exe or the shader, see docs/research/111_analytic_fog.md):
//   vision text -> GameFogVision (raw fields, game defaults)
//   -> gameFogRefdef (CG_VisionSetApplyToRefdef's fog part, exe 0x1723430)
//   -> gameFogConstants (R_SetFrameFog, exe 0x19a9940: cb7[131..138])
//   -> gameFogEvaluate per view ray (the shader) -> transmittance T (rgb) and inscatter L (rgb)
//   -> composite: out = scene * T + L   (radiance before the radiometric scale; the volumetric fog multiplies both
//      by its own transmittance and adds its scatter afterwards, research 109 section 2.4).
// The fog spline LUT (texture T3) is zero on every stock map whose vision leaves fogSpline empty; gameFogSplineBlend
// rebuilds it (cs_blend_fog_splines) for visions that blend several spline assets.
#pragma once

#include <string>

namespace iw8r {

// The vision's fog fields (names from Dvars.txt; defaults from the exe's visionDefFields table).
struct GameFogVision {
    // atmospheric fog (fogAtmosphericScattering > 0)
    float atmospheric = 0;                    // fogAtmosphericScattering (MKKLQTSKSO)
    float startDist = 0.1f;                   // fogStartDist (RPNOKKL)
    float halfPlane = 5000;                   // fogHalfPlaneDistance (SNNNRLTML)
    float maxDistance = 50000;                // fogMaxDistance (NRTMTRQTRQ)
    float skyDistance = 50000;                // fogSkyDistance (NLQKSSPMKP)
    float extinctionDesaturation = 0;         // fogExtinctionDesaturation (MKQSQRPPQR)
    float inscatteringEv = 15;                // fogInscatteringEv (LNOQTMMLSP)
    float rayleighAlbedo[3] = {1, 1, 1};      // fogRayleighAlbedo (LNMRMNLNTT), sRGB
    float mieAlbedo[3] = {1, 1, 1};           // fogMieAlbedo (LRKPNSNORL), sRGB
    float mieAnisotropy = 0.5f;               // fogMieAnisotropy (MPRLMQNQO)
    float mieBlend = 0.5f;                    // fogMieBlendStrength (NPMPRRLNPO)
    float sunDir[3] = {1, 0, 0};              // fogSunDir (RLNQONPKS): the phase functions' direction
    float skyMinAngle = 0, skyMaxAngle = 90;  // fogSkyMinAngle (LOROLOTNMP), fogSkyMaxAngle (SPQNRNKQR), degrees
    float heightBase = 0;                     // fogHeightFogBaseHeight (NOSTLRKPQ)
    float heightHalfPlane = 5000;             // fogHeightFogHalfPlaneDistance (POKSTQPPQ)
    float heightMaxOffset = 0;                // fogHeightFogMaximumOffset (LOLKSRNSMR)
    float heightAdditional = 0;               // fogHeightFogAdditionalFog (NKOQRKQRMT)
    // plain fog (fogAtmosphericScattering 0)
    float density = 70000;                    // fogDensity (MSTOLQOTKN): the plain fog's half distance
    float color[3] = {1, 1, 1};               // fogColor (NMLMMNQLSQ), sRGB
    float colorIntensity = 1;                 // fogColorIntensity (NTPTLNNQQL)
    float maxOpacity = 1;                     // fogMaxOpacity (TRPROKTLS)
    float sunDensity = 70000;                 // fogSunDensity (MPNPSMMTTT)
    float sunColor[3] = {1, 1, 1};            // fogSunColor (MRTOMMTTQM), sRGB
    float sunColorIntensity = 1;              // fogSunColorIntensity (MLORTTKOLN)
    float sunBeginFadeAngle = 0;              // fogSunBeginFadeAngle (NKLTMMOOLP), degrees
    float sunEndFadeAngle = 360;              // fogSunEndFadeAngle (NONQQTPNLT), degrees
    float skyIntensity = 0;                   // fogSkyIntensity (NORQKLTPTR): plain fog only
    bool hasSpline = false;                   // fogSpline (MLOSSOSRMM) names an asset (the editor has no curve for it)
};
// Reads `TOKEN "value"` lines; absent fields keep the defaults above.
GameFogVision gameFogVisionFromText(const std::string& visionText);

// The fog the client hands the renderer (refdef_t::fog), as the exe derives it from the vision.
struct GameFogRefdef {
    float maxOpticalDepth[3];  // log2(e) x maxDistance x extinction (atmospheric); plain: log2(e) x ln2 / density
    float startDist;
    float rayleigh[3];         // atmospheric: Rayleigh scattering x radiance scale / extinction; plain: fog colour
    float maxDistance;         // plain: fogMaxOpacity
    float mie[3];              // atmospheric: Mie scattering x radiance scale / extinction; plain: sun colour
    float mieAnisotropy;       // plain: 1 - fogMaxOpacity (the lowest transmittance)
    float sunDir[3];
    float skyDistance;         // plain: fogSkyIntensity x fogMaxOpacity
    float skyFalloffScale, skyFalloffBias;
    float heightBase, heightDensity, heightMaxOffset, heightAdditional;
    bool atmospheric;
    float sunFalloffScale, sunFalloffBias;  // plain fog only
    float extinction[3];       // not in the refdef: the extinction per unit, kept for tests and docs
    float radianceScale;       // not in the refdef: 0.001 x 2^(fogInscatteringEv - 2.84)
};
GameFogRefdef gameFogRefdef(const GameFogVision& v);

// cb7[131..138] of the post-opaque pass (and of every shader that fogs). eyeZ: the camera's world z.
// skyDomeDistance: cb7[138].x, sqrt(2) x min(world half size x, y); the analytic fog does not use it.
struct GameFogConstants {
    float c[8][4];
};
GameFogConstants gameFogConstants(const GameFogRefdef& f, float eyeZ, float skyDomeDistance = 0);

// The fog spline LUT (T3): 128 texels over x = saturate((distance - start) / maxDistance), texel i at
// x(2 - x) = i / 127. The fog subtracts the LUT value from x before it becomes optical depth.
constexpr int kGameFogLutSize = 128;
struct GameFogSplineEntry {
    const float* curve;  // the spline asset's 128 texels; null = the zero image (no asset)
    float weight;        // blend weight byte / 255
    float startDist, halfPlane, maxDistance;
};
// cs_blend_fog_splines (run by the game only when 2+ entries blend; with one entry T3 is that entry's own image).
// startDist / maxDistance: the refdef's weighted sums. Writes out[128].
void gameFogSplineBlend(const GameFogSplineEntry* entries, int count, float startDist, float maxDistance, float out[kGameFogLutSize]);
// The shader's T3 fetch at x in [0, 1] (bilinear over the texel centres). lut may be null (zero image).
float gameFogLutSample(const float* lut, float x);

struct GameFogSample {
    float transmittance[3];  // multiplies the scene radiance
    float inscatter[3];      // added, in scene radiance units (before cb7[42].x, the radiometric scale)
};
// One view ray: dir = unit world direction from the eye, distance = eye-to-surface length (ignored for the sky),
// sky = the pixel is sky (far plane). lut: T3 (null = zero).
GameFogSample gameFogEvaluate(const GameFogConstants& k, const float* lut, const float dir[3], float distance, bool sky);

}  // namespace iw8r
