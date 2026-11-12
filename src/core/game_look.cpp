#include "game_look.h"

#include <algorithm>
#include <cmath>

namespace iw8r {

namespace {

using M3 = double[3][3];

void mul(const M3 m, const double v[3], double out[3]) {
    double r[3];
    for (int i = 0; i < 3; ++i) r[i] = m[i][0] * v[0] + m[i][1] * v[1] + m[i][2] * v[2];
    out[0] = r[0], out[1] = r[1], out[2] = r[2];
}

// research 95 step 1 (VERIFIED constants).
const M3 kRec709To2020 = {{0.6274, 0.3293, 0.0433}, {0.0691, 0.9195, 0.0114}, {0.0164, 0.0880, 0.8956}};
const M3 kRec2020To709 = {{1.6605, -0.5876, -0.0728}, {-0.1246, 1.1329, -0.0083}, {-0.0182, -0.1006, 1.1187}};
// Rec.2020 <-> XYZ (D65) and the ACES matrices (ACES 1.0 reference, no chromatic adaptation).
const M3 kRec2020ToXyz = {{0.6369580483, 0.1446169036, 0.1688809752},
                          {0.2627002120, 0.6779980715, 0.0593017165},
                          {0.0, 0.0280726930, 1.0609850577}};
const M3 kXyzToRec2020 = {{1.7166511880, -0.3556707838, -0.2533662814},
                          {-0.6666843518, 1.6164812366, 0.0157685458},
                          {0.0176398574, -0.0427706133, 0.9421031212}};
// Bradford white point adaptation between Rec.2020 (D65) and ACES (D60), as the ACES output transforms do.
const M3 kD65ToD60 = {{1.01303, 0.00610531, -0.014971}, {0.00769823, 0.998165, -0.00503203}, {-0.00284131, 0.00468516, 0.924507}};
const M3 kD60ToD65 = {{0.987224, -0.00611327, 0.0159533}, {-0.00759836, 1.00186, 0.00533002}, {0.00307257, -0.00509595, 1.08168}};
const M3 kXyzToAp0 = {{1.0498110175, 0.0, -0.0000974845}, {-0.4959030231, 1.3733130458, 0.0982400361}, {0.0, 0.0, 0.9912520182}};
const M3 kAp0ToAp1 = {{1.4514393161, -0.2365107469, -0.2149285693},
                      {-0.0765537734, 1.1762296998, -0.0996759264},
                      {0.0083161484, -0.0060324498, 0.9977163014}};
const M3 kAp1ToXyz = {{0.6624541811, 0.1340042065, 0.1561876870},
                      {0.2722287168, 0.6740817658, 0.0536895174},
                      {-0.0055746495, 0.0040607335, 1.0103391003}};
constexpr double kAp1Y[3] = {0.2722287168, 0.6740817658, 0.0536895174};

double rgbToSaturation(const double c[3]) {
    const double mx = std::max({c[0], c[1], c[2], 1e-10}), mn = std::max(std::min({c[0], c[1], c[2]}), 1e-10);
    return (mx - mn) / std::max(mx, 1e-2);
}
double rgbToYc(const double c[3]) {
    const double r = c[0], g = c[1], b = c[2];
    const double chroma = std::sqrt(std::max(0.0, b * (b - g) + g * (g - r) + r * (r - b)));
    return (b + g + r + 1.75 * chroma) / 3.0;
}
double sigmoidShaper(double x) {
    const double t = std::max(1.0 - std::fabs(x / 2.0), 0.0);
    const double y = 1.0 + (x < 0 ? -1.0 : 1.0) * (1.0 - t * t);
    return y / 2.0;
}
double glowFwd(double ycIn, double glowGainIn, double glowMid) {
    if (ycIn <= 2.0 / 3.0 * glowMid) return glowGainIn;
    if (ycIn >= 2.0 * glowMid) return 0.0;
    return glowGainIn * (glowMid / ycIn - 0.5);
}

// ACES 1.0 segmented_spline_c5_fwd with the RRT parameters (research 95: the stock coefficients).
double splineC5(double x) {
    static const double low[6] = {-4.0, -4.0, -3.1573765773, -0.4852499958, 1.8477324706, 1.8477324706};
    static const double high[6] = {-0.7185482425, 2.0810307172, 3.6681241237, 4.0, 4.0, 4.0};
    const double minX = 0.18 * std::exp2(-15.0), minY = 0.0001, midX = 0.18, maxX = 0.18 * std::exp2(18.0), maxY = 10000.0;
    const double logx = std::log10(std::max(x, 6.10352e-5));
    const auto segment = [&](const double* c, double t0, double t1) {
        const double k = 3.0 * (logx - t0) / (t1 - t0);
        const int j = std::clamp(int(k), 0, 2);
        const double t = k - j;
        const double c0 = c[j], c1 = c[j + 1], c2 = c[j + 2];
        // monomials (t^2, t, 1) x M x cf, M = {{.5, -1, .5}, {-1, 1, .5}, {.5, 0, 0}} (columns as in the CTL)
        const double a = 0.5 * c0 - 1.0 * c1 + 0.5 * c2;
        const double b = -1.0 * c0 + 1.0 * c1;
        const double d = 0.5 * c0 + 0.5 * c1;
        return a * t * t + b * t + d;
    };
    double logy;
    if (logx <= std::log10(minX)) logy = std::log10(minY);
    else if (logx < std::log10(midX)) logy = segment(low, std::log10(minX), std::log10(midX));
    else if (logx < std::log10(maxX)) logy = segment(high, std::log10(midX), std::log10(maxX));
    else logy = std::log10(maxY);
    return std::pow(10.0, logy);
}

// ACES 1.0 RRT (red modifier weight 0): AP0 in, AP1 out (OCES, nits).
void rrt(const double ap0[3], double out[3]) {
    double c[3] = {ap0[0], ap0[1], ap0[2]};
    const double s = sigmoidShaper((rgbToSaturation(c) - 0.4) / 0.2);
    const double glow = 1.0 + glowFwd(rgbToYc(c), 0.05 * s, 0.08);
    for (double& v : c) v = std::max(0.0, v * glow);
    double ap1[3];
    mul(kAp0ToAp1, c, ap1);
    for (double& v : ap1) v = std::max(0.0, v);
    // RRT_SAT_MAT (0.96 of the colour kept around the AP1 luminance).
    const double y = kAp1Y[0] * ap1[0] + kAp1Y[1] * ap1[1] + kAp1Y[2] * ap1[2];
    for (double& v : ap1) v = 0.04 * y + 0.96 * v;
    for (int k = 0; k < 3; ++k) out[k] = splineC5(ap1[k]);
}

double luma2020(const double c[3]) { return 0.2627 * c[0] + 0.6780 * c[1] + 0.0593 * c[2]; }

}  // namespace

double gameManualExposure(double ev, double adjust) { return 180.0 * std::exp2(2.84 - ev + adjust); }
double gameLutInputScale(double ev, double adjust) { return gameManualExposure(ev, adjust) * std::exp2(1.5); }

double gameLutCoordinate(double x, int size) {
    const double u = std::clamp((std::log2(std::max(x, 0.0) + 1.0 / 1024.0) + 10.0) / 20.0, 0.0, 1.0);
    return u * (size - 1.0) / size + 0.5 / size;
}

void gameTonemap(const double in[3], double out[3]) {
    // 1. Rec.709 -> Rec.2020; 2. no grading; 3. RRT via AP0 (Rec.2020 -> XYZ -> AP0), then AP1 -> XYZ -> Rec.2020.
    double c[3], xyz[3], ap0[3], ap1[3];
    mul(kRec709To2020, in, c);
    for (double& v : c) v = std::max(0.0, v);
    mul(kRec2020ToXyz, c, xyz);
    mul(kD65ToD60, xyz, xyz);
    mul(kXyzToAp0, xyz, ap0);
    rrt(ap0, ap1);
    mul(kAp1ToXyz, ap1, xyz);
    mul(kD60ToD65, xyz, xyz);
    mul(kXyzToRec2020, xyz, c);
    // 4. Display mapping in nits: gamma 1, then the 333-nit SDR target.
    for (double& v : c) v = std::max(0.0, v) / 333.0;
    // 5. Shoulder per channel, hue preserved by luma, then a little desaturation of bright values (INFERRED reading:
    // amount max(0.8 (1.1 h(L) - 0.1), gamut excess / 2), h(L) = 0.2 L below 2.5).
    double sh[3];
    for (int k = 0; k < 3; ++k) sh[k] = c[k] < 0.15 ? c[k] : 0.15 + 0.85 * (1.0 - std::exp(-(c[k] - 0.15) / 0.85));
    const double l0 = luma2020(c), l1 = luma2020(sh);
    if (l0 > 1e-12)
        for (double& v : c) v *= l1 / l0;
    const double L = luma2020(c);
    const double h = L < 2.5 ? 0.2 * L : 0.5 + 0.5 * (1.0 - std::exp(-2.0 * (0.2 * L - 0.5)));
    const double excess = std::max(0.0, std::max({c[0], c[1], c[2]}) - 1.0);
    const double amount = std::clamp(std::max(0.8 * (1.1 * h - 0.1), excess * 0.5), 0.0, 1.0);
    for (double& v : c) v = v + (L - v) * amount;
    // 6. Rec.2020 -> Rec.709, saturate, encode y^(1/2.4); 7. black 0, white 1.
    mul(kRec2020To709, c, out);
    for (int k = 0; k < 3; ++k) out[k] = std::pow(std::clamp(out[k], 0.0, 1.0), 1.0 / 2.4);
}

std::vector<uint8_t> buildGameTonemapLut(int size) {
    std::vector<uint8_t> lut(size_t(size) * size * size * 4);
    std::vector<double> axis(static_cast<size_t>(size));
    for (int i = 0; i < size; ++i) {
        // The texel centre's u back to x: u = i / (size - 1) over the shaper's range.
        const double u = double(i) / double(size - 1);
        axis[size_t(i)] = std::max(0.0, std::exp2(20.0 * u - 10.0) - 1.0 / 1024.0);
    }
    size_t o = 0;
    for (int b = 0; b < size; ++b)
        for (int g = 0; g < size; ++g)
            for (int r = 0; r < size; ++r) {
                const double in[3] = {axis[size_t(r)], axis[size_t(g)], axis[size_t(b)]};
                double out[3];
                gameTonemap(in, out);
                for (int k = 0; k < 3; ++k) lut[o++] = uint8_t(std::lround(std::clamp(out[k], 0.0, 1.0) * 255.0));
                lut[o++] = 255;
            }
    return lut;
}

double srgbToLinear(double c) { return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4); }

double gameLightIntensity(double radius, double reference) {
    if (radius <= 0) return 0;
    const double ref = std::max(reference, 16.0);
    return std::max(0.0, 1.0 - ref / radius) * ref * ref;
}

double gameLightAttenuation(double intensity, double radius, double dist, double spotFactor) {
    if (radius <= 0 || dist >= radius) return 0;
    const double w = std::clamp(1.0 - dist / radius, 0.0, 1.0) * std::clamp(spotFactor, 0.0, 1.0);
    return intensity / std::max(dist * dist, 1.0) * w * w * (3.0 - 2.0 * w);
}

}  // namespace iw8r
