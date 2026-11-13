// Research 111: the game's analytic fog (core game_fog). Expected numbers come from the research's Python reference
// (out/agent/fog111/fog_ref.py, double precision) for the stock visions of mp_rust, mp_m_speedball and mp_hideout.
#include "game_fog.h"
#include "test_harness.h"

#include <cmath>
#include <string>

using namespace iw8r;

namespace {

// The fog lines of the stock visions (world cache vision/<map>.vision.0), trimmed to what the fog reads.
const char* kRust =
    "MSTOLQOTKN \"45000\"\nRPNOKKL \"512\"\nNTPTLNNQQL \"3.5\"\nNMLMMNQLSQ \"0.9764706 0.9843137 1\"\nTRPROKTLS \"1\"\n"
    "MPNPSMMTTT \"45000\"\nMLORTTKOLN \"0\"\nMRTOMMTTQM \"1 0.6745098 0.3960784\"\nNKLTMMOOLP \"0\"\nNONQQTPNLT \"90\"\n"
    "RLNQONPKS \"0.8959909 -0.2832357 0.3420202\"\nNORQKLTPTR \"1.1\"\nLOROLOTNMP \"40.96\"\nSPQNRNKQR \"78.39999\"\n"
    "MKKLQTSKSO \"1\"\nSNNNRLTML \"4000\"\nNRTMTRQTRQ \"2000000\"\nNLQKSSPMKP \"1440572\"\nMKQSQRPPQR \"0.85\"\n"
    "LNOQTMMLSP \"21\"\nLNMRMNLNTT \"0.5176471 0.4784314 0.4313726\"\nLRKPNSNORL \"0.6431373 0.6117647 0.5568628\"\n"
    "MPRLMQNQO \"0.5\"\nNPMPRRLNPO \"0.25\"\nNOSTLRKPQ \"0\"\nPOKSTQPPQ \"170\"\nLOLKSRNSMR \"0\"\nNKOQRKQRMT \"0.032\"\n"
    "MLOSSOSRMM \"\"\n";
const char* kSpeedball =
    "MSTOLQOTKN \"10000\"\nRPNOKKL \"1000\"\nNTPTLNNQQL \"1.5\"\nNMLMMNQLSQ \"1 1 1\"\nTRPROKTLS \"1\"\n"
    "RLNQONPKS \"0.3778209 -0.4502697 -0.8090171\"\nNORQKLTPTR \"0.5\"\nLOROLOTNMP \"-77.75999\"\nSPQNRNKQR \"103.68\"\n"
    "MKKLQTSKSO \"1\"\nSNNNRLTML \"2550\"\nNRTMTRQTRQ \"20000\"\nNLQKSSPMKP \"50000\"\nMKQSQRPPQR \"0.5\"\n"
    "LNOQTMMLSP \"19.3\"\nLNMRMNLNTT \"0.9176471 0.9568627 1\"\nLRKPNSNORL \"1 0.972549 0.7960784\"\nMPRLMQNQO \"0.7\"\n"
    "NPMPRRLNPO \"0.3\"\nNOSTLRKPQ \"0\"\nPOKSTQPPQ \"5000\"\nLOLKSRNSMR \"0\"\nNKOQRKQRMT \"0\"\nMLOSSOSRMM \"\"\n";
const char* kHideout =
    "MSTOLQOTKN \"20000\"\nRPNOKKL \"1900\"\nNTPTLNNQQL \"17.76\"\nNMLMMNQLSQ \"0.7686275 0.8156863 0.8431373\"\n"
    "TRPROKTLS \"0.36\"\nRLNQONPKS \"0.4420073 -0.7643468 0.4694715\"\nNORQKLTPTR \"10\"\nLOROLOTNMP \"59.03999\"\n"
    "SPQNRNKQR \"100.8\"\nMKKLQTSKSO \"1\"\nSNNNRLTML \"12000\"\nNRTMTRQTRQ \"20000\"\nNLQKSSPMKP \"50000\"\n"
    "MKQSQRPPQR \"1\"\nLNOQTMMLSP \"20.5\"\nLNMRMNLNTT \"0.7490196 0.8745098 1\"\nLRKPNSNORL \"0.9215686 0.972549 1\"\n"
    "MPRLMQNQO \"0.7\"\nNPMPRRLNPO \"0.2\"\nNOSTLRKPQ \"-250\"\nPOKSTQPPQ \"3500\"\nLOLKSRNSMR \"63\"\nNKOQRKQRMT \"0.06900001\"\n"
    "MLOSSOSRMM \"\"\n";

bool near(double a, double b, double rel) { return std::fabs(a - b) <= rel * std::fmax(std::fabs(b), 1e-6); }
bool near3(const float a[3], double x, double y, double z, double rel) { return near(a[0], x, rel) && near(a[1], y, rel) && near(a[2], z, rel); }

GameFogConstants constantsFor(const char* vision, float eyeZ) { return gameFogConstants(gameFogRefdef(gameFogVisionFromText(vision)), eyeZ); }

// A level (z = 0) unit direction at 90 degrees to the fog sun direction's horizontal part, so cos to the sun is 0.
void levelPerpendicular(const GameFogConstants& k, float out[3]) {
    const float x = k.c[1][0], y = k.c[1][1], n = std::sqrt(x * x + y * y);
    out[0] = -y / n, out[1] = x / n, out[2] = 0;
}

}  // namespace

TEST(game_fog_vision_fields_and_defaults) {
    const GameFogVision r = gameFogVisionFromText(kRust);
    CHECK(r.atmospheric == 1 && r.startDist == 512 && r.halfPlane == 4000 && r.maxDistance == 2000000 && r.skyDistance == 1440572);
    CHECK(r.inscatteringEv == 21 && std::fabs(r.extinctionDesaturation - 0.85f) < 1e-6 && r.heightHalfPlane == 170);
    CHECK(std::fabs(r.heightAdditional - 0.032f) < 1e-7 && std::fabs(r.sunDir[2] - 0.3420202f) < 1e-7 && !r.hasSpline);
    // Absent fields keep the game's defaults (visionDefFields).
    const GameFogVision d = gameFogVisionFromText("");
    CHECK(d.atmospheric == 0 && std::fabs(d.startDist - 0.1f) < 1e-7 && d.halfPlane == 5000 && d.maxDistance == 50000);
    CHECK(d.inscatteringEv == 15 && d.mieAnisotropy == 0.5f && d.mieBlend == 0.5f && d.density == 70000 && d.sunEndFadeAngle == 360);
    CHECK(gameFogVisionFromText("MLOSSOSRMM \"fog_spline_x\"\n").hasSpline);
}

TEST(game_fog_rust_constants) {
    // cb7[131..137] for mp_rust from an eye at z = 64 (fog_ref.py).
    const GameFogRefdef f = gameFogRefdef(gameFogVisionFromText(kRust));
    CHECK(f.atmospheric && near(f.radianceScale, 292.9, 1e-3));
    const double dens = 0.69314718 / 4000;
    CHECK(near(f.extinction[0] / dens, 0.890, 3e-3) && near(f.extinction[1] / dens, 0.884, 3e-3) && near(f.extinction[2] / dens, 0.878, 3e-3));
    const GameFogConstants k = gameFogConstants(f, 64);
    CHECK(near(k.c[0][0], 1.44006e6, 1e-5) && near(k.c[0][1], -2.40385, 1e-4) && near(k.c[0][2], 1.30983, 1e-4) && near(k.c[0][3], 1440572, 1e-6));
    CHECK(near(k.c[1][3], -512, 1e-6) && near(k.c[1][0], 0.895991, 1e-5));
    CHECK(near3(k.c[2], 444.94, 442.181, 438.843, 1e-4) && near(k.c[2][3], 0.00407734, 1e-4));
    CHECK(near3(k.c[3], 30.5468, 27.526, 22.5668, 1e-4) && k.c[3][3] == 0.5f);
    CHECK(near3(k.c[4], 56.9584, 48.3414, 39.0254, 1e-4) && near(k.c[4][3], 0.77032, 1e-4));
    CHECK(k.c[5][2] == 1 && near(k.c[5][3], 0.032, 1e-6) && k.c[6][2] == 0 && near(k.c[6][3], 5e-7, 1e-5));
}

TEST(game_fog_limits) {
    const GameFogConstants k = constantsFor(kRust, 64);
    float dir[3];
    levelPerpendicular(k, dir);
    // Zero distance and anything before fogStartDist: no fog.
    for (float d : {0.0f, 100.0f, 512.0f}) {
        const GameFogSample s = gameFogEvaluate(k, nullptr, dir, d, false);
        CHECK(s.transmittance[0] == 1 && s.transmittance[2] == 1 && s.inscatter[0] == 0 && s.inscatter[1] == 0);
    }
    // Farther = thicker and brighter; transmittance stays in (0, 1].
    float lastT = 1, lastL = 0;
    bool mono = true;
    for (float d = 600; d < 60000; d *= 1.7f) {  // (beyond, T underflows float and L stops changing)
        const GameFogSample s = gameFogEvaluate(k, nullptr, dir, d, false);
        mono &= s.transmittance[1] < lastT && s.inscatter[1] > lastL && s.transmittance[1] > 0;
        lastT = s.transmittance[1], lastL = s.inscatter[1];
    }
    CHECK(mono);
    // Far away the inscatter reaches the single-scatter limit (mie x phaseM + rayleigh x phaseR) at cos 0.
    const GameFogSample far = gameFogEvaluate(k, nullptr, dir, 1e7f, false);
    const double pr = 0.059683, pm = (1 - 0.25) * 0.079577;
    CHECK(far.transmittance[0] < 1e-6 && near(far.inscatter[0], k.c[3][0] * pm + k.c[4][0] * pr, 1e-4));
    // The sky straight up is outside rust's band (zenith angle < 40.96): untouched.
    const float up[3] = {0, 0, 1};
    const GameFogSample zen = gameFogEvaluate(k, nullptr, up, 123, true);
    CHECK(zen.transmittance[0] == 1 && zen.inscatter[2] == 0);
}

TEST(game_fog_rust_values) {
    const GameFogConstants k = constantsFor(kRust, 64);
    float dir[3];
    levelPerpendicular(k, dir);
    // Level ray from z = 64, cos 0 to the fog sun (fog_ref.py table).
    GameFogSample s = gameFogEvaluate(k, nullptr, dir, 4000, false);
    CHECK(near3(s.transmittance, 0.650, 0.651, 0.653, 3e-3) && near3(s.inscatter, 1.830, 1.579, 1.274, 3e-3));
    s = gameFogEvaluate(k, nullptr, dir, 16000, false);
    CHECK(near3(s.transmittance, 0.147, 0.149, 0.151, 1e-2) && near3(s.inscatter, 4.454, 3.854, 3.121, 3e-3));
    // The sky at the horizon: fully fogged (the additional fog has no height falloff), L = the far limit.
    s = gameFogEvaluate(k, nullptr, dir, 0, true);
    CHECK(s.transmittance[0] < 1e-3 && near3(s.inscatter, 5.223, 4.528, 3.676, 3e-3));
    // Height: from z = 1000 the same level ray is much clearer.
    const GameFogConstants hi = constantsFor(kRust, 1000);
    s = gameFogEvaluate(hi, nullptr, dir, 4000, false);
    CHECK(near(s.transmittance[0], 0.974, 2e-3) && near3(s.inscatter, 0.136, 0.117, 0.094, 1e-2));
    // Looking down from z = 1000 thickens it again (denser air below); looking up thins it.
    const float down[3] = {dir[0] * 0.8f, dir[1] * 0.8f, -0.6f}, upward[3] = {dir[0] * 0.8f, dir[1] * 0.8f, 0.6f};
    const float tDown = gameFogEvaluate(hi, nullptr, down, 1500, false).transmittance[0];
    const float tUp = gameFogEvaluate(hi, nullptr, upward, 1500, false).transmittance[0];
    const float tLevel = gameFogEvaluate(hi, nullptr, dir, 1500, false).transmittance[0];
    CHECK(tDown < tLevel && tLevel < tUp);
    // The Mie lobe points along fogSunDir: level toward it (cos 0.94) is brighter than across it (fog_ref.py 4.517).
    const float hn = std::sqrt(k.c[1][0] * k.c[1][0] + k.c[1][1] * k.c[1][1]);
    const float toward[3] = {k.c[1][0] / hn, k.c[1][1] / hn, 0};
    CHECK(near(gameFogEvaluate(k, nullptr, toward, 4000, false).inscatter[0], 4.517, 3e-3));
}

TEST(game_fog_speedball_and_hideout_values) {
    const GameFogConstants sb = constantsFor(kSpeedball, 64);
    float dir[3];
    levelPerpendicular(sb, dir);
    GameFogSample s = gameFogEvaluate(sb, nullptr, dir, 4000, false);
    CHECK(near3(s.transmittance, 0.469, 0.461, 0.468, 3e-3) && near3(s.inscatter, 2.378, 2.497, 2.504, 3e-3));
    s = gameFogEvaluate(sb, nullptr, dir, 0, true);
    CHECK(near(s.transmittance[0], 0.081, 1e-2) && near3(s.inscatter, 4.110, 4.261, 4.324, 3e-3));

    const GameFogConstants ho = constantsFor(kHideout, 64);
    levelPerpendicular(ho, dir);
    // fogMaxOpacity 0.36 does not cap the atmospheric fog.
    s = gameFogEvaluate(ho, nullptr, dir, 16000, false);
    CHECK(near(s.transmittance[0], 0.440, 3e-3) && near3(s.inscatter, 3.669, 4.972, 6.482, 3e-3));
    // fogSkyIntensity 10 does not brighten the atmospheric sky haze.
    s = gameFogEvaluate(ho, nullptr, dir, 0, true);
    CHECK(near(s.transmittance[0], 0.490, 3e-3) && near3(s.inscatter, 3.341, 4.528, 5.903, 3e-3));
    // fogExtinctionDesaturation 1: grey extinction.
    CHECK(s.transmittance[0] == s.transmittance[1] && s.transmittance[1] == s.transmittance[2]);
}

TEST(game_fog_plain_branch) {
    // fogAtmosphericScattering 0: exp fog of half distance fogDensity, floored at 1 - maxOpacity, coloured by fogColor
    // x intensity, toward the sun by fogSunColor; the sky is 1 - skyIntensity x maxOpacity x band.
    const char* plain = "MKKLQTSKSO \"0\"\nMSTOLQOTKN \"1000\"\nRPNOKKL \"0\"\nTRPROKTLS \"0.8\"\nNTPTLNNQQL \"2\"\n"
                        "NMLMMNQLSQ \"1 1 1\"\nMLORTTKOLN \"0\"\nNORQKLTPTR \"0.5\"\nLOROLOTNMP \"0\"\nSPQNRNKQR \"90\"\n";
    const GameFogConstants k = constantsFor(plain, 0);
    CHECK(k.c[6][2] == -1 && near(k.c[3][3], 0.2, 1e-6));
    const float dir[3] = {0, 1, 0};  // 90 degrees from the default sun direction (1, 0, 0)
    GameFogSample s = gameFogEvaluate(k, nullptr, dir, 1000, false);
    CHECK(near(s.transmittance[0], 0.5, 2e-2) && near(s.inscatter[0], 2 * (1 - s.transmittance[0]), 1e-5));
    s = gameFogEvaluate(k, nullptr, dir, 1e6f, false);
    CHECK(near(s.transmittance[0], 0.2, 1e-5));
    s = gameFogEvaluate(k, nullptr, dir, 0, true);  // horizon: full band
    CHECK(near(s.transmittance[0], 1 - 0.5 * 0.8, 1e-5));
}

TEST(game_fog_spline_lut) {
    // No asset: the zero image, no change.
    float lut[kGameFogLutSize];
    GameFogSplineEntry none{nullptr, 1.0f, 512, 4000, 20000};
    gameFogSplineBlend(&none, 1, 512, 20000, lut);
    bool zero = true;
    for (float v : lut) zero &= std::fabs(v) < 1e-4f;
    CHECK(zero);
    // One spline reproduces its own texels (texel 0 is forced to 0); two equal halves give the same.
    float curve[kGameFogLutSize];
    for (int i = 0; i < kGameFogLutSize; ++i) curve[i] = 0.3f * float(i) / 127.0f;
    GameFogSplineEntry one{curve, 1.0f, 512, 4000, 20000};
    gameFogSplineBlend(&one, 1, 512, 20000, lut);
    bool same = lut[0] == 0;
    for (int i = 1; i < kGameFogLutSize; ++i) same &= std::fabs(lut[i] - curve[i]) < 2e-3f;
    CHECK(same);
    GameFogSplineEntry two[2] = {{curve, 0.5f, 512, 4000, 20000}, {curve, 0.5f, 512, 4000, 20000}};
    float lut2[kGameFogLutSize];
    gameFogSplineBlend(two, 2, 512, 20000, lut2);
    bool same2 = true;
    for (int i = 0; i < kGameFogLutSize; ++i) same2 &= std::fabs(lut2[i] - lut[i]) < 2e-3f;
    CHECK(same2);
    // The fetch: x(2 - x) mapping onto the texel centres.
    CHECK(std::fabs(gameFogLutSample(curve, 1.0f) - 0.3f) < 1e-4f && std::fabs(gameFogLutSample(curve, 0.0f)) < 1e-4f);
    CHECK(std::fabs(gameFogLutSample(curve, 0.5f) - 0.3f * 0.75f) < 1e-3f && gameFogLutSample(nullptr, 0.7f) == 0);
    // A LUT thins the fog (it is subtracted from the normalised distance).
    const GameFogConstants k = constantsFor(kSpeedball, 64);
    float dir[3];
    levelPerpendicular(k, dir);
    CHECK(gameFogEvaluate(k, curve, dir, 8000, false).transmittance[0] > gameFogEvaluate(k, nullptr, dir, 8000, false).transmittance[0]);
}
