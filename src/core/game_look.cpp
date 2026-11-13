#include "game_look.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <map>

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
double gameLutInputScale(double ev, double adjust) { return gameManualExposure(ev, adjust) * std::exp2(kGameBaseExposureEv); }

double gameLutCoordinate(double x, int size) {
    const double u = std::clamp((std::log2(std::max(x, 0.0) + 1.0 / 1024.0) + 10.0) / 20.0, 0.0, 1.0);
    return u * (size - 1.0) / size + 0.5 / size;
}

double pqEncode(double y) {
    const double m1 = 0.1593017578125, m2 = 78.84375, c1 = 0.8359375, c2 = 18.8515625, c3 = 18.6875;
    const double p = std::pow(std::clamp(y, 0.0, 1.0), m1);
    return std::pow((c1 + c2 * p) / (1.0 + c3 * p), m2);
}

double pqDecode(double code) {
    const double m1 = 0.1593017578125, m2 = 78.84375, c1 = 0.8359375, c2 = 18.8515625, c3 = 18.6875;
    const double p = std::pow(std::clamp(code, 0.0, 1.0), 1.0 / m2);
    return std::pow(std::max(p - c1, 0.0) / (c2 - c3 * p), 1.0 / m1);
}

namespace {
uint32_t rd32(const std::string& s, size_t o) {
    uint32_t v = 0;
    std::memcpy(&v, s.data() + o, 4);
    return v;
}
float halfToFloat(uint16_t h) {
    const uint32_t s = (h >> 15) & 1, e = (h >> 10) & 31, m = h & 1023;
    float v = e == 0 ? std::ldexp(float(m), -24) : e == 31 ? (m ? NAN : INFINITY) : std::ldexp(float(m | 1024), int(e) - 25);
    return s ? -v : v;
}
// Trilinear sample of the CLUT at texture coordinates (0..1, texel centres at (i + 0.5) / size), clamped.
void sampleClut(const GameGradingClut& g, const double uvw[3], double out[3]) {
    const int n = g.size;
    int i0[3], i1[3];
    double f[3];
    for (int k = 0; k < 3; ++k) {
        const double p = std::clamp(uvw[k] * n - 0.5, 0.0, double(n - 1));
        i0[k] = int(std::floor(p));
        i1[k] = std::min(i0[k] + 1, n - 1);
        f[k] = p - i0[k];
    }
    for (int c = 0; c < 3; ++c) out[c] = 0;
    for (int corner = 0; corner < 8; ++corner) {
        const int r = corner & 1 ? i1[0] : i0[0], gg = corner & 2 ? i1[1] : i0[1], b = corner & 4 ? i1[2] : i0[2];
        const double w = (corner & 1 ? f[0] : 1 - f[0]) * (corner & 2 ? f[1] : 1 - f[1]) * (corner & 4 ? f[2] : 1 - f[2]);
        const size_t o = ((size_t(b) * n + size_t(gg)) * n + size_t(r)) * 3;
        for (int c = 0; c < 3; ++c) out[c] += w * g.rgb[o + size_t(c)];
    }
}
}  // namespace

bool gameGradingClutFromDds(const std::string& d, GameGradingClut& out, std::string* err) {
    const auto fail = [&](const char* why) {
        if (err) *err = why;
        return false;
    };
    if (d.size() < 148 || d.compare(0, 4, "DDS ") != 0) return fail("not a DDS");
    const uint32_t h = rd32(d, 12), w = rd32(d, 16), depth = rd32(d, 24);
    if (d.compare(84, 4, "DX10") != 0) return fail("no DX10 header");
    const uint32_t fmt = rd32(d, 128), dim = rd32(d, 132);
    if (dim != 4 || w != h || w != depth || w < 2 || w > 128) return fail("not a cube-sized 3D texture");
    const size_t n = w, count = n * n * n;
    const size_t bpp = fmt == 11 || fmt == 10 ? 8 : fmt == 28 ? 4 : 0;
    if (!bpp) return fail("unsupported pixel format");
    if (d.size() < 148 + count * bpp) return fail("truncated");
    out.size = int(n);
    out.rgb.resize(count * 3);
    for (size_t i = 0; i < count; ++i)
        for (int c = 0; c < 3; ++c) {
            const size_t o = 148 + i * bpp;
            float v = 0;
            if (fmt == 11) {
                uint16_t u;
                std::memcpy(&u, d.data() + o + size_t(c) * 2, 2);
                v = float(u) / 65535.0f;
            } else if (fmt == 10) {
                uint16_t u;
                std::memcpy(&u, d.data() + o + size_t(c) * 2, 2);
                v = halfToFloat(u);
            } else {
                v = float(uint8_t(d[o + size_t(c)])) / 255.0f;
            }
            out.rgb[i * 3 + size_t(c)] = v;
        }
    return true;
}

void gameTonemap(const double in[3], double out[3], const GameGradingClut* grade) {
    // 1. Rec.709 -> Rec.2020; 2. the vision's grading CLUT in PQ space (when given); 3. RRT via AP0 (Rec.2020 -> XYZ ->
    // AP0), then AP1 -> XYZ -> Rec.2020.
    double c[3], xyz[3], ap0[3], ap1[3];
    mul(kRec709To2020, in, c);
    for (double& v : c) v = std::max(0.0, v);
    if (grade && grade->valid()) {
        double uvw[3], q[3];
        for (int k = 0; k < 3; ++k) uvw[k] = pqEncode(std::min(c[k] / 1024.0, 1.0)) * 0.96875 + 0.015625;
        sampleClut(*grade, uvw, q);
        for (int k = 0; k < 3; ++k) c[k] = pqDecode(q[k]) * 1024.0;
    }
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

std::vector<uint8_t> buildGameTonemapLut(int size, const GameGradingClut* grade) {
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
                gameTonemap(in, out, grade);
                for (int k = 0; k < 3; ++k) lut[o++] = uint8_t(std::lround(std::clamp(out[k], 0.0, 1.0) * 255.0));
                lut[o++] = 255;
            }
    return lut;
}

double gameExposureFor(const GameExposure& e, double avgLuminance) {
    const double manualCap = 180.0 * std::exp2(2.84 - e.maxEv);
    if (!e.autoExposure) return std::min(gameManualExposure(e.ev, e.exposureAdjust), manualCap);
    const double avg = std::max(avgLuminance, 1e-6);
    const auto L = [](double ev) { return 0.001 * std::exp2(ev - 2.84); };
    const auto k = [](double adjust) { return std::pow(0.18 * std::exp2(adjust), 1.0 / 2.2); };
    const double Ld = L(e.darkEv), Ll = L(std::max(e.lightEv, e.darkEv + 0.01)), Lm = L(e.midEv);
    const double kd = k(e.darkAdjust), kl = k(e.lightAdjust), km = k(e.midAdjust);
    const double span = std::log(Ll / Ld);
    const double t = std::max(std::log(avg / Ld) / span, 0.0);
    const double tm = std::clamp(std::log(Lm / Ld) / span, 1e-4, 1.0 - 1e-4);
    const double ratio = std::clamp((km - kd) / (kl - kd), 1e-4, 1.0 - 1e-4);
    const double p = std::log(ratio) / std::log(tm);
    double key = std::pow(std::pow(t, p) * (kl - kd) + kd, 2.2);
    key = std::clamp(key, 0.18 * std::exp2(e.minAdjust), 0.18 * std::exp2(e.maxAdjust));
    return std::min(key / avg * std::exp2(e.exposureAdjust), manualCap);
}

double gameAdaptExposure(double current, double target, double dt, double adaptSpeed) {
    if (!(current > 0) || !(target > 0)) return target;
    const double a = 1.0 - std::exp(-std::max(dt, 0.0) * std::max(adaptSpeed, 0.01) * 4.0);
    return std::exp(std::log(current) + (std::log(target) - std::log(current)) * a);
}

namespace {
GameExposure autoExposure(double ev, double adjust, double adapt, double dark, double mid, double light, double dAdj, double mAdj,
                          double lAdj) {
    GameExposure e;
    e.autoExposure = true;
    e.ev = ev, e.exposureAdjust = adjust, e.adaptSpeed = adapt;
    e.darkEv = dark, e.midEv = mid, e.lightEv = light;
    e.darkAdjust = dAdj, e.midAdjust = mAdj, e.lightAdjust = lAdj;
    return e;
}
}  // namespace

const std::vector<StockMapLook>& stockMapLooks() {
    // Fields 63-76 in order: mode 3, auto 1, curve 1, exposure, auto adjust 0, max EV -10, exposure adjust, adapt speed,
    // dark / mid / light EV, dark / mid / light adjust (the converter's table, same order).
    static const std::vector<StockMapLook> looks = {
        {"mp_m_king", 1.55685, {1.2781, 0.952621, 0.650445}, autoExposure(6, 0, 0.075, 4.56, 6.605912, 8.57, -1.6, -0.279, 1.28)},
        {"mp_emporium", 3.39796, {1.4395, 0.9178, 0.520111}, autoExposure(8.5, 0, 0.075, 5.6, 8.229471, 11.9, -2, -0.6758758, 1.5)},
        {"mp_shipment", 8.63193, {1.04877, 0.993345, 0.922306}, autoExposure(10.5, 0.6, 0.75, 6.1, 12.14071, 15, -2, -0.3869346, 1.5)},
        {"mp_village2", 11.0536, {0.983046, 1.00121, 1.0379}, autoExposure(10, 0, 0.075, 8, 11, 14, -1.5, 0, 1.5)},
        {"mp_br_quarry", 11.512, {1.14193, 0.976265, 0.817178}, autoExposure(12, 0, 0.15, 9, 11.75, 16, -1, 0.15, 2)},
        {"mp_rust", 11.6969, {1.11411, 0.989194, 0.771041}, autoExposure(10, 0, 0.06, 7.53, 10.3, 14, -3, -1, 1.3)},
        {"mp_backlot2", 20.3368, {0.98634, 1.00452, 0.995408}, autoExposure(12, -0.1, 0.075, 6.2, 11.23978, 16, -5, -0.9157199, 3.2)},
        {"mp_m_fork", 23.7478, {1.02338, 0.996126, 0.969523}, autoExposure(11.5, 0, 0.075, 5.6, 10.31878, 14.5, -2.38, -1.024797, 1.5)},
        {"mp_m_speed", 23.9449, {0.97691, 1.00372, 1.03118}, autoExposure(14, 0, 0.075, 7, 11.34573, 15.6, -1, 0.05577891, 1.6)},
        {"mp_aniyah_tac", 24.6499, {0.994142, 1.00325, 0.985086}, autoExposure(12, 0, 0.075, 8.6, 12.49304, 16.2, -3.2, -0.7722935, 2.2)},
        {"mp_t_gun_course", 25.8181, {1.13566, 0.979416, 0.804427}, autoExposure(14, 0, 0.1, 11.5, 13.25, 15, -0.25, 0.4, 0.75)},
        {"mp_hideout", 26.9233, {0.984644, 1.00247, 1.02072}, autoExposure(12, 0, 0.075, 9, 10.7015, 15, -3, -0.9118453, 2)},
        {"mp_m_speedball", 30.6723, {1.00331, 1.00331, 0.95747}, autoExposure(13, 0.65, 0.075, 10.8, 13.21207, 14.6, -0.5, 0.04669083, 1.5)},
    };
    return looks;
}

const StockMapLook* stockMapLook(const std::string& map) {
    // Verdansk matches Quarry (the converter's table leaves it out for that reason).
    const std::string name = map == "mp_donetsk" ? std::string("mp_br_quarry") : map;
    for (const StockMapLook& l : stockMapLooks())
        if (name == l.map) return &l;
    return nullptr;
}

std::vector<float> gameTonemapInverseGrey(int n) {
    // The grey curve sampled on a log grid, then inverted by search (it rises monotonically).
    std::vector<double> xs, ds;
    for (double lx = -10; lx <= 10; lx += 0.01) {
        const double x = std::exp2(lx), in[3] = {x, x, x};
        double out[3];
        gameTonemap(in, out);
        xs.push_back(x), ds.push_back(out[1]);
    }
    std::vector<float> inv(size_t(std::max(n, 2)));
    for (size_t i = 0; i < inv.size(); ++i) {
        const double d = std::min(double(i) / double(inv.size() - 1), 0.995);
        size_t k = 0;
        while (k + 1 < ds.size() && ds[k + 1] < d) ++k;
        const double t = k + 1 < ds.size() && ds[k + 1] > ds[k] ? std::clamp((d - ds[k]) / (ds[k + 1] - ds[k]), 0.0, 1.0) : 0.0;
        inv[i] = float(k + 1 < xs.size() ? xs[k] + (xs[k + 1] - xs[k]) * t : xs.back());
    }
    return inv;
}

const StockMapLook& stockMapLookNearest(double sunIntensity) {
    const auto& all = stockMapLooks();
    size_t best = 0;
    double bestD = 1e300;
    for (size_t k = 0; k < all.size(); ++k) {
        const double d = std::fabs(std::log(std::max(sunIntensity, 1e-3) / all[k].sunIntensity));
        if (d < bestD) bestD = d, best = k;
    }
    return all[best];
}

GameFog gameFogFromVision(const std::string& text) {
    GameFog f;
    std::map<std::string, std::string> v;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        const std::string line = text.substr(pos, end - pos);
        pos = end + 1;
        const size_t q1 = line.find('"'), q2 = q1 == std::string::npos ? q1 : line.find('"', q1 + 1);
        if (q2 == std::string::npos) continue;
        std::string key = line.substr(0, q1);
        while (!key.empty() && (key.back() == ' ' || key.back() == '\t')) key.pop_back();
        while (!key.empty() && (key.front() == ' ' || key.front() == '\t')) key.erase(key.begin());
        v[key] = line.substr(q1 + 1, q2 - q1 - 1);
    }
    const auto num = [&](const char* tok, double& out) {
        const auto it = v.find(tok);
        if (it == v.end() || it->second.empty()) return false;
        try {
            out = std::stod(it->second);
        } catch (...) {
            return false;
        }
        return true;
    };
    const auto vec = [&](const char* tok, double out[3]) {
        const auto it = v.find(tok);
        if (it == v.end()) return false;
        double a[3];
        if (std::sscanf(it->second.c_str(), "%lf %lf %lf", &a[0], &a[1], &a[2]) != 3) return false;
        for (int k = 0; k < 3; ++k) out[k] = a[k];
        return true;
    };
    double scatter = 0;
    const bool haveHalf = num("SNNNRLTML", f.halfPlane);           // fogHalfPlaneDistance
    num("MKKLQTSKSO", scatter);                                    // fogAtmosphericScattering
    num("RPNOKKL", f.start);                                       // fogStartDist
    num("MSTOLQOTKN", f.density);                                  // fogDensity (the half distance)
    num("TRPROKTLS", f.maxOpacity);                                // fogMaxOpacity
    num("NRTMTRQTRQ", f.maxDistance);                              // fogMaxDistance
    num("NLQKSSPMKP", f.skyDistance);                              // fogSkyDistance
    num("NORQKLTPTR", f.skyIntensity);                             // fogSkyIntensity
    num("NOSTLRKPQ", f.heightBase);                                // fogHeightFogBaseHeight
    num("POKSTQPPQ", f.heightHalf);                                // fogHeightFogHalfPlaneDistance
    num("MKQSQRPPQR", f.desaturation);                             // fogExtinctionDesaturation
    num("LNOQTMMLSP", f.inscatterEv);                              // fogInscatteringEv
    vec("NMLMMNQLSQ", f.color);                                    // fogColor
    vec("LNMRMNLNTT", f.rayleigh);                                 // fogRayleighAlbedo
    vec("LRKPNSNORL", f.mie);                                      // fogMieAlbedo
    num("MPRLMQNQO", f.mieAnisotropy);                             // fogMieAnisotropy
    num("NPMPRRLNPO", f.mieBlend);                                 // fogMieBlendStrength
    f.on = haveHalf && scatter > 0 && f.halfPlane > 0 && f.maxOpacity > 0;
    if (f.heightHalf <= 0) f.heightHalf = 1e9;
    return f;
}

double gameFogInscatter(const GameFog& f) { return 0.001 * std::exp2(f.inscatterEv - 2.84) / (4.0 * 3.14159265358979); }

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
