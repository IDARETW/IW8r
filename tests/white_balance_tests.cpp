// The game's white balance matrix (core white_balance, research 120).
#include "white_balance.h"

#include <cmath>
#include <string>

#include "test_harness.h"

using namespace iw8r;

namespace {

bool isIdentity(const double m[9], double tol) {
    for (int i = 0; i < 9; ++i)
        if (std::fabs(m[i] - (i % 4 == 0 ? 1.0 : 0.0)) > tol) return false;
    return true;
}

void apply(const double m[9], const double in[3], double out[3]) {
    for (int r = 0; r < 3; ++r) out[r] = m[r * 3] * in[0] + m[r * 3 + 1] * in[1] + m[r * 3 + 2] * in[2];
}

double lum(const double c[3]) { return c[0] * 0.2126 + c[1] * 0.7152 + c[2] * 0.0722; }

}  // namespace

TEST(white_balance_defaults_are_identity) {
    double m[9];
    gameWhiteBalanceMatrix(GameWhiteBalance{}, m);
    CHECK(isIdentity(m, 1e-12));
    CHECK(!gameWhiteBalanceActive(GameWhiteBalance{}));
    // Method 6 is "none" whatever the illuminant.
    GameWhiteBalance none;
    none.method = 6, none.illuminant = 1, none.colorTempK = 9000;
    gameWhiteBalanceMatrix(none, m);
    CHECK(isIdentity(m, 1e-12));
    // Values out of range, a zero in the white point, a zero temperature: the identity, never a NaN.
    for (GameWhiteBalance bad : {GameWhiteBalance{7, 1}, GameWhiteBalance{4, 22}, GameWhiteBalance{-1, 0}, GameWhiteBalance{4, 1, 0.0}}) {
        gameWhiteBalanceMatrix(bad, m);
        CHECK(isIdentity(m, 1e-12));
    }
    GameWhiteBalance zeroWhite;
    zeroWhite.whitePoint[1] = 0;
    gameWhiteBalanceMatrix(zeroWhite, m);
    CHECK(isIdentity(m, 1e-12));
}

TEST(white_balance_locus_formulas) {
    double xy[2];
    dIlluminantChromaticity(6504, xy);  // D65
    CHECK(std::fabs(xy[0] - 0.3127) < 5e-4 && std::fabs(xy[1] - 0.3290) < 5e-4);
    dIlluminantChromaticity(5003, xy);  // D50
    CHECK(std::fabs(xy[0] - 0.3457) < 5e-4 && std::fabs(xy[1] - 0.3585) < 5e-4);
    dIlluminantChromaticity(7504, xy);  // D75, the formula's other branch
    CHECK(std::fabs(xy[0] - 0.2990) < 5e-4 && std::fabs(xy[1] - 0.3149) < 5e-4);
    // The black body approximation runs from warm to cool and stays near the D locus.
    double warm[2], cool[2];
    blackBodyChromaticity(3000, warm);
    blackBodyChromaticity(9000, cool);
    CHECK(warm[0] > cool[0] && warm[0] > 0.40 && cool[0] < 0.30);
    blackBodyChromaticity(5000, xy);
    CHECK(std::fabs(xy[0] - 0.3474) < 1e-3 && std::fabs(xy[1] - 0.3569) < 1e-3);
    // The tint moves across the locus: no change at 0, opposite ways for opposite signs, 1/3000 uv a unit.
    double base[2], plus[2], minus[2];
    dIlluminantChromaticity(5500, base);
    plus[0] = minus[0] = base[0], plus[1] = minus[1] = base[1];
    double same[2] = {base[0], base[1]};
    tintChromaticity(same, 5500, 0, true);
    CHECK(std::fabs(same[0] - base[0]) < 1e-9 && std::fabs(same[1] - base[1]) < 1e-9);
    tintChromaticity(plus, 5500, 3, true);
    tintChromaticity(minus, 5500, -3, true);
    CHECK(std::fabs((plus[0] - base[0]) + (minus[0] - base[0])) < 2e-5);
    CHECK(std::fabs((plus[1] - base[1]) + (minus[1] - base[1])) < 2e-5);
    const auto uv = [](const double p[2], double o[2]) {
        const double d = 1.0 / (1.5 - p[0] + 6.0 * p[1]);
        o[0] = 2.0 * p[0] * d, o[1] = 3.0 * p[1] * d;
    };
    double ub[2], up[2];
    uv(base, ub);
    uv(plus, up);
    CHECK(std::fabs(std::hypot(up[0] - ub[0], up[1] - ub[1]) - 0.001) < 1e-6);
}

TEST(white_balance_d50_is_neutral) {
    // sRGB in its D50 form, adapted from D50 to D65: the identity with Bradford (the form's own adaptation). The
    // other methods keep white white; their matrices differ from the identity in how they move saturated colours.
    double m[9];
    GameWhiteBalance wb;
    wb.illuminant = 6;  // D50
    wb.method = 3;
    gameWhiteBalanceMatrix(wb, m);
    CHECK(isIdentity(m, 2e-3));
    for (int method : {1, 2, 4, 5}) {
        wb.method = method;
        gameWhiteBalanceMatrix(wb, m);
        const double white[3] = {1, 1, 1};
        double out[3];
        apply(m, white, out);
        CHECK(std::fabs(out[0] - 1) < 5e-3 && std::fabs(out[1] - 1) < 5e-3 && std::fabs(out[2] - 1) < 5e-3);
    }
}

TEST(white_balance_stock_maps) {
    // Hideout: Von Kries, D illuminant 5,500 K, tint 2.2. Warmer than neutral: white gains red and loses blue.
    GameWhiteBalance hideout = gameWhiteBalanceFromVision(
        "LRROOOSOMM \"1 1 1\"\nMNMNSSLLOR \"4\"\nNLSRLSTPTN \"1\"\nTRQLLNNLS \"5500\"\nOPSMSQNKQ \"2.2\"\n");
    CHECK(hideout.method == 4 && hideout.illuminant == 1 && hideout.colorTempK == 5500 && hideout.tint == 2.2);
    CHECK(gameWhiteBalanceActive(hideout));
    double m[9], out[3];
    const double white[3] = {1, 1, 1};
    gameWhiteBalanceMatrix(hideout, m);
    apply(m, white, out);
    CHECK(out[0] > 1.02 && out[0] < 1.12);
    CHECK(out[2] < 0.93 && out[2] > 0.80);
    CHECK(std::fabs(lum(out) - 1.0) < 0.03);
    // The same numbers from a separate calculation of the published formula (research 120 section 3).
    CHECK(std::fabs(out[0] - 1.0779) < 2e-3 && std::fabs(out[1] - 0.9877) < 2e-3 && std::fabs(out[2] - 0.8908) < 2e-3);
    CHECK(std::fabs(m[0] - 1.0311) < 2e-3 && std::fabs(m[8] - 0.8983) < 2e-3 && std::fabs(m[2] - 0.0297) < 2e-3);
    // A cooler setting than 5,000 K turns the picture the other way; a higher temperature is warmer still.
    GameWhiteBalance cool = hideout, warmer = hideout;
    cool.colorTempK = 4500, cool.tint = 0;
    warmer.colorTempK = 6500, warmer.tint = 0;
    double c[3], w[3];
    gameWhiteBalanceMatrix(cool, m);
    apply(m, white, c);
    gameWhiteBalanceMatrix(warmer, m);
    apply(m, white, w);
    CHECK(c[2] > 1.0 && c[0] < 1.0);
    CHECK(w[0] > out[0] && w[2] < out[2]);
    // Aniyah Palace: method 0, a black body at 5,000 K: a small change only.
    GameWhiteBalance aniyah = gameWhiteBalanceFromVision("MNMNSSLLOR \"0\"\nNLSRLSTPTN \"2\"\nTRQLLNNLS \"5000\"\nOPSMSQNKQ \"0\"\n");
    CHECK(aniyah.method == 0 && aniyah.illuminant == 2);
    gameWhiteBalanceMatrix(aniyah, m);
    apply(m, white, out);
    CHECK(std::fabs(out[0] - 1) < 0.03 && std::fabs(out[1] - 1) < 0.03 && std::fabs(out[2] - 1) < 0.03);
    // A vision without the fields (Speedball, Shipment): nothing.
    CHECK(!gameWhiteBalanceActive(gameWhiteBalanceFromVision("LRROOOSOMM \"1 1 1\"\nMMQLTMKTPK \"0.08\"\n")));
    // A token that only starts like one of ours is not read.
    CHECK(gameWhiteBalanceFromVision("MNMNSSLLORX \"4\"\n").method == 0);
}

TEST(white_balance_own_map_overrides) {
    // worldspawn iw8r_vision: the fields by their tokens (any case), next to other overrides.
    const GameWhiteBalance wb = gameWhiteBalanceFromOverrides("tonemapExposure=0.5; mnmnssllor=4;NLSRLSTPTN=1;TRQLLNNLS=5500;OPSMSQNKQ=2.2");
    CHECK(wb.method == 4 && wb.illuminant == 1 && wb.colorTempK == 5500 && wb.tint == 2.2);
    CHECK(gameWhiteBalanceActive(wb));
    const GameWhiteBalance white = gameWhiteBalanceFromOverrides("toneWhitePoint=1.2 1 0.8");
    CHECK(white.whitePoint[0] == 1.2 && white.whitePoint[2] == 0.8 && gameWhiteBalanceActive(white));
    // Nothing set, or text that does not parse: none.
    CHECK(!gameWhiteBalanceActive(gameWhiteBalanceFromOverrides("")));
    CHECK(!gameWhiteBalanceActive(gameWhiteBalanceFromOverrides("tonemapExposure=0.5")));
    CHECK(!gameWhiteBalanceActive(gameWhiteBalanceFromOverrides("MNMNSSLLOR")));
}

TEST(white_balance_white_point) {
    // Illuminant 0, method 0: a scale in sRGB that makes the white point grey at its own luminance.
    GameWhiteBalance wb;
    wb.whitePoint[0] = 1.2, wb.whitePoint[1] = 1.0, wb.whitePoint[2] = 0.8;
    double m[9], out[3];
    gameWhiteBalanceMatrix(wb, m);
    apply(m, wb.whitePoint, out);
    const double l = lum(wb.whitePoint);
    CHECK(std::fabs(out[0] - l) < 1e-9 && std::fabs(out[1] - l) < 1e-9 && std::fabs(out[2] - l) < 1e-9);
    CHECK(m[1] == 0 && m[2] == 0 && m[3] == 0);
    // Method 1 (CAT02 on the white point): the white point becomes grey too, within the constants' rounding.
    wb.method = 1;
    gameWhiteBalanceMatrix(wb, m);
    apply(m, wb.whitePoint, out);
    CHECK(std::fabs(out[0] - out[1]) < 5e-3 && std::fabs(out[1] - out[2]) < 5e-3 && std::fabs(out[1] - l) < 5e-3);
    // A white white point: the identity for both.
    GameWhiteBalance plain;
    plain.method = 1;
    gameWhiteBalanceMatrix(plain, m);
    CHECK(isIdentity(m, 2e-3));
}
