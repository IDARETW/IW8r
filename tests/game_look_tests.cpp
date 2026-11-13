// Roadmap 5.11 / 5.21: the game's look for the viewport (core game_look, research 95).
#include "game_look.h"
#include "test_harness.h"

#include <cmath>

using namespace iw8r;

TEST(game_look_exposure_and_shaper) {
    // Research 95: EV 10.3319 gives exposure 1; the iw3 style's EV 10 with the +1.5 EV base.
    CHECK(std::fabs(gameManualExposure(10.3319) - 1.0) < 1e-3);
    CHECK(std::fabs(gameLutInputScale(10.0) - 180.0 * std::exp2(2.84 - 10.0) * std::exp2(kGameBaseExposureEv)) < 1e-9);
    // The 20-stop log shaper: 2^-10 .. 2^10 onto the LUT's texel centres.
    CHECK(std::fabs(gameLutCoordinate(0.0) - 0.5 / 64) < 1e-3);
    CHECK(std::fabs(gameLutCoordinate(1e9) - (63.0 / 64 + 0.5 / 64)) < 1e-9);
    CHECK(gameLutCoordinate(0.18) < gameLutCoordinate(1.0) && gameLutCoordinate(1.0) < gameLutCoordinate(10.0));
}

TEST(game_look_tonemap_curve) {
    double out[3];
    const double black[3] = {0, 0, 0};
    gameTonemap(black, out);
    CHECK(out[0] < 0.02 && out[1] < 0.02 && out[2] < 0.02);
    // Grey stays grey and rises with the input; a very bright input rolls off below 1.
    double last = -1;
    bool rising = true, grey = true;
    for (double x = 0.01; x < 2000; x *= 1.5) {
        const double in[3] = {x, x, x};
        gameTonemap(in, out);
        rising &= out[1] >= last - 1e-6;
        grey &= std::fabs(out[0] - out[1]) < 0.02 && std::fabs(out[2] - out[1]) < 0.02;
        last = out[1];
    }
    CHECK(rising && grey && last > 0.9 && last <= 1.0);
    // A sunlit mid grey (0.5 lit x the iw3 style's input scale) lands in the middle of the display range.
    const double lit = 0.5 * gameLutInputScale(10.0);
    const double in[3] = {lit, lit, lit};
    gameTonemap(in, out);
    CHECK(out[1] > 0.25 && out[1] < 0.75);
    // The LUT: 64^3 RGBA, black first, near white last.
    const std::vector<uint8_t> lut = buildGameTonemapLut(64);
    CHECK(lut.size() == size_t(64) * 64 * 64 * 4 && lut[0] < 6 && lut[lut.size() - 4] > 240 && lut[3] == 255);
}

TEST(game_look_auto_exposure) {
    const StockMapLook* ship = stockMapLook("mp_shipment");
    CHECK(ship && ship->exposure.autoExposure && std::fabs(ship->sunIntensity - 8.63193) < 1e-9 && !stockMapLook("mp_nowhere") && stockMapLook("mp_donetsk") == stockMapLook("mp_br_quarry"));
    if (!ship) return;
    CHECK(std::string(stockMapLookNearest(30.67234).map) == "mp_m_speedball" && std::string(stockMapLookNearest(8.5).map) == "mp_shipment");
    // At the mid EV's luminance the key is the mid key (0.18 x 2^midAdjust), times 2^exposureAdjust.
    const GameExposure& e = ship->exposure;
    const double Lmid = 0.001 * std::exp2(e.midEv - 2.84);
    const double expected = 0.18 * std::exp2(e.midAdjust) / Lmid * std::exp2(e.exposureAdjust);
    CHECK(std::fabs(gameExposureFor(e, Lmid) / expected - 1.0) < 1e-6);
    // Brighter scenes get less exposure; the product (the displayed key) still rises.
    const double dim = gameExposureFor(e, Lmid * 0.25), bright = gameExposureFor(e, Lmid * 4);
    CHECK(dim > bright && bright * Lmid * 4 > dim * Lmid * 0.25);
    // Manual exposure ignores the luminance; adaptation moves toward the target and never past it.
    GameExposure m;
    CHECK(std::fabs(gameExposureFor(m, 5.0) - gameManualExposure(10.0)) < 1e-12);
    // The grey inverse undoes the curve (display -> exposed value -> display).
    const std::vector<float> inv = gameTonemapInverseGrey(256);
    const double x = inv[128], in[3] = {x, x, x};
    double out[3];
    gameTonemap(in, out);
    CHECK(inv.size() == 256 && std::fabs(out[1] - 128.0 / 255.0) < 0.01 && inv[200] > inv[100] && inv[0] < 0.01);
    const double a = gameAdaptExposure(1.0, 4.0, 0.1, 1.0);
    CHECK(a > 1.0 && a < 4.0 && std::fabs(gameAdaptExposure(1.0, 4.0, 100.0, 1.0) - 4.0) < 1e-6);
}

TEST(game_look_fog_from_vision) {
    // mp_m_speedball's vision lines (tokens of the converter's replay_vision_names.inc).
    const std::string text = "SNNNRLTML \"2550\"\nMKKLQTSKSO \"1\"\nRPNOKKL \"1000\"\nTRPROKTLS \"1\"\n"
                             "LNOQTMMLSP \"19.3\"\nLNMRMNLNTT \"0.9176471 0.9568627 1\"\nPOKSTQPPQ \"5000\"\n"
                             "NPMPRRLNPO \"0.3\"\nMPRLMQNQO \"0.7\"\nMSTOLQOTKN \"10000\"\n";
    const GameFog f = gameFogFromVision(text);
    CHECK(f.on && f.halfPlane == 2550 && f.density == 10000 && f.start == 1000 && f.heightHalf == 5000 && std::fabs(f.rayleigh[0] - 0.9176471) < 1e-9);
    CHECK(std::fabs(gameFogInscatter(f) - 7.2) < 0.2);  // the haze of the owner's screenshot, about 7 scene units
    CHECK(!gameFogFromVision("RPNOKKL \"1000\"\n").on);
}

TEST(game_look_local_lights) {
    // convert.cpp's log of the lmproof map: IW3 radius 300, reference 150 -> intensity 11250.
    CHECK(std::fabs(gameLightIntensity(300, 150) - 11250.0) < 1e-6);
    CHECK(gameLightIntensity(300, 4) == gameLightIntensity(300, 16));  // the reference is at least 16
    // At the reference distance the game's light equals IW3's linear falloff value (1 - ref / r) times the window.
    const double I = gameLightIntensity(300, 150);
    const double a = gameLightAttenuation(I, 300, 150);
    const double w = 0.5;
    CHECK(std::fabs(a - 0.5 * w * w * (3 - 2 * w)) < 1e-9);
    CHECK(gameLightAttenuation(I, 300, 300) == 0.0 && gameLightAttenuation(I, 300, 50) > gameLightAttenuation(I, 300, 100));
    CHECK(gameLightAttenuation(I, 300, 100, 0.0) == 0.0);
    CHECK(std::fabs(srgbToLinear(1.0) - 1.0) < 1e-12 && std::fabs(srgbToLinear(0.5) - 0.214041) < 1e-5);
}
