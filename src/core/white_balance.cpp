#include "white_balance.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <utility>
#include <vector>

#include "vision_fields.h"

namespace iw8r {

namespace {

// 3x3 matrices in the engine's layout: a vector is a row, out_j = sum_i in_i m[i][j]; mul(a, b) applies a, then b.
struct M3 {
    double m[3][3];
};

constexpr M3 kIdentity{{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}};

// s_XYZToCRD[method] (Replay 1.20 exe .rdata 0x1423E6950, 36 bytes each): XYZ to the cone response domain.
constexpr M3 kXyzToCrd[7] = {
    {{{3.240454, -0.969266, 0.0556434}, {-1.537138, 1.876011, -0.2040259}, {-0.4985314, 0.041556, 1.057225}}},  // 0 sRGB
    {{{0.7328, -0.7036, 0.003}, {0.4296, 1.6975, 0.0136}, {-0.1624, 0.0061, 0.9834}}},                          // 1 CAT02
    {{{1.2694, -0.8364, 0.0297}, {-0.0988, 1.8006, -0.0315}, {-0.1706, 0.0357, 1.0018}}},                       // 2 Sharp
    {{{0.8951, -0.7502, 0.0389}, {0.2664, 1.7135, -0.0685}, {-0.1614, 0.0367, 1.0296}}},                        // 3 Bradford
    {{{0.40024, -0.2263, 0}, {0.7076, 1.16532, 0}, {-0.08081, 0.0457, 0.91822}}},                               // 4 Von Kries
    kIdentity,                                                                                                  // 5 XYZ scaling
    kIdentity,                                                                                                  // 6 none
};
// s_sRGBD50ToXYZ (0x1423E6A50), s_sRGBD65ToXYZ (0x1423E6A78), s_XYZTosRGBD65 (0x1423E6AA0), s_sRGBToCAT02LMS (0x1423E6AC8).
constexpr M3 kSrgbD50ToXyz{{{0.4360747, 0.2225045, 0.0139322}, {0.3850649, 0.7168786, 0.0971045}, {0.1430804, 0.0606169, 0.7141733}}};
constexpr M3 kSrgbD65ToXyz{{{0.4124564, 0.2126729, 0.0193339}, {0.3575761, 0.7151522, 0.119192}, {0.1804375, 0.072175, 0.9503041}}};
constexpr M3 kXyzToSrgbD65{{{3.240454, -0.969266, 0.0556434}, {-1.537138, 1.876011, -0.2040259}, {-0.4985314, 0.041556, 1.057225}}};
constexpr M3 kSrgbToCat02{{{0.3904054, 0.07084159, 0.02310818}, {0.5499411, 0.9631718, 0.1280208}, {0.00892632, 0.00135775, 0.9362451}}};

// illuminants[] (Replay 1.20 exe .data 0x14411C190, 20 bytes each: x, y at 2 degrees, x, y at 10 degrees, cct). The
// game uses the 2 degree pair and the cct. 0 .. 2 are not table illuminants (white point, D custom, black body).
struct Illuminant {
    double x, y, cct;
};
constexpr Illuminant kIlluminants[22] = {
    {0.34567, 0.3585, 5003},  {0.34567, 0.3585, 5003},  {0.34567, 0.3585, 5003},  {0.44757, 0.40745, 2856},  // -, -, -, A
    {0.34842, 0.35161, 4874}, {0.31006, 0.31616, 6774}, {0.34567, 0.3585, 5003},  {0.33242, 0.34743, 5503},  // B, C, D50, D55
    {0.31271, 0.32902, 6504}, {0.29902, 0.31485, 7504}, {0.3131, 0.33727, 6430},  {0.37208, 0.37529, 4230},  // D65, D75, F1, F2
    {0.4091, 0.3943, 3450},   {0.44018, 0.40329, 2940}, {0.31379, 0.34531, 6350}, {0.3779, 0.38835, 4150},   // F3 .. F6
    {0.31292, 0.32933, 6500}, {0.34588, 0.35875, 5000}, {0.37417, 0.37281, 4150}, {0.34609, 0.35986, 5000},  // F7 .. F10
    {0.38052, 0.37713, 4000}, {0.43695, 0.40441, 3000},                                                      // F11, F12
};

M3 mul(const M3& a, const M3& b) {
    M3 r{};
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) r.m[i][j] = a.m[i][0] * b.m[0][j] + a.m[i][1] * b.m[1][j] + a.m[i][2] * b.m[2][j];
    return r;
}

void transform(const double in[3], const M3& a, double out[3]) {
    for (int j = 0; j < 3; ++j) out[j] = in[0] * a.m[0][j] + in[1] * a.m[1][j] + in[2] * a.m[2][j];
}

bool inverse(const M3& a, M3& out) {
    const double (*m)[3] = a.m;
    const double det = m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
                       m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
    if (!(std::fabs(det) > 1e-12)) return false;
    const double d = 1.0 / det;
    out.m[0][0] = (m[1][1] * m[2][2] - m[1][2] * m[2][1]) * d;
    out.m[0][1] = (m[0][2] * m[2][1] - m[0][1] * m[2][2]) * d;
    out.m[0][2] = (m[0][1] * m[1][2] - m[0][2] * m[1][1]) * d;
    out.m[1][0] = (m[1][2] * m[2][0] - m[1][0] * m[2][2]) * d;
    out.m[1][1] = (m[0][0] * m[2][2] - m[0][2] * m[2][0]) * d;
    out.m[1][2] = (m[0][2] * m[1][0] - m[0][0] * m[1][2]) * d;
    out.m[2][0] = (m[1][0] * m[2][1] - m[1][1] * m[2][0]) * d;
    out.m[2][1] = (m[0][1] * m[2][0] - m[0][0] * m[2][1]) * d;
    out.m[2][2] = (m[0][0] * m[1][1] - m[0][1] * m[1][0]) * d;
    return true;
}

M3 scaleOf(const double s[3]) {
    M3 r = kIdentity;
    for (int i = 0; i < 3; ++i) r.m[i][i] = s[i];
    return r;
}

// Row layout -> the public row-major "out = M in" form (the transpose).
void store(const M3& a, double m[9]) {
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) m[j * 3 + i] = a.m[i][j];
}

bool finite9(const double m[9]) {
    for (int i = 0; i < 9; ++i)
        if (!std::isfinite(m[i])) return false;
    return true;
}

void identity9(double m[9]) {
    for (int i = 0; i < 9; ++i) m[i] = i % 4 == 0 ? 1.0 : 0.0;
}

}  // namespace

void dIlluminantChromaticity(double colorTempK, double xy[2]) {
    const double t = 1.0 / colorTempK, t2 = t * t, t3 = t2 * t;
    const double x = colorTempK > 7000.0 ? t2 * 1901800.0 - t3 * 2006400000.0 + t * 247.48 + 0.23704
                                         : t2 * 2967800.0 - t3 * 4607000100.0 + t * 99.110001 + 0.244063;
    xy[0] = x;
    xy[1] = (2.87 - x * 3.0) * x - 0.275;
}

void blackBodyChromaticity(double colorTempK, double xy[2]) {
    const double t = 1.0 / colorTempK, t2 = t * t, t3 = t2 * t, t4 = t2 * t2, t5 = t3 * t2;
    const double x = 0.243185 - t5 * 1.8738099e15 + t4 * 5.8426602e12 - t3 * 6615550000.0 + t2 * 2888860.0 + t * 164.35201;
    xy[0] = x;
    xy[1] = x * 2.8780601 - 0.291251 -
            (x * 0.70877975 - x * t5 * 5.4613505e15 + x * t4 * 1.7028843e13 - x * t3 * 1.9281484e10 + x * t2 * 8419785.0 + x * t * 479.01544);
}

void tintChromaticity(double xy[2], double colorTempK, double tint, bool dIlluminant) {
    // The locus one kelvin further gives its direction; the tint moves across it in CIE 1960 uv.
    double next[2];
    if (dIlluminant) dIlluminantChromaticity(colorTempK + 1.0, next);
    else blackBodyChromaticity(colorTempK + 1.0, next);
    const double d0 = 1.0 / (1.5 - xy[0] + xy[1] * 6.0);
    const double u = xy[0] * 2.0 * d0, v = xy[1] * 3.0 * d0;
    const double d1 = 1.5 - next[0] + next[1] * 6.0;
    double du = next[0] * 2.0 / d1 - u, dv = next[1] * 3.0 / d1 - v;
    double len = std::sqrt(du * du + dv * dv);
    if (!(len > 0.0)) len = 1.0;
    du /= len, dv /= len;
    // Turned by -90 degrees: (du, dv) -> (dv, -du).
    const double k = tint * 0.00033333333;
    const double u2 = u + dv * k, v2 = v - du * k;
    const double den = u2 - v2 * 4.0 + 2.0;
    double x = u2 * 1.5 / den, y = v2 / den;
    x = std::clamp(x, 0.000001, 0.99999899);
    y = std::clamp(y, 0.000001, 0.99999899);
    if (x + y > 0.99999899) {
        const double s = 0.99999899 / (x + y);
        x *= s, y *= s;
    }
    xy[0] = x, xy[1] = y;
}

void gameWhiteBalanceMatrix(const GameWhiteBalance& wb, double m[9]) {
    identity9(m);
    if (wb.method < 0 || wb.method > 6 || wb.illuminant < 0 || wb.illuminant > 21) return;
    const double* w = wb.whitePoint;
    double xy[2] = {0, 0};
    if (wb.illuminant == 0) {
        if (!(w[0] > 0 && w[1] > 0 && w[2] > 0)) return;
        const double lum = w[0] * 0.2126 + w[1] * 0.7152 + w[2] * 0.0722;
        if (wb.method == 0) {  // a scale in sRGB that keeps the white point's luminance
            const double s[3] = {lum / w[0], lum / w[1], lum / w[2]};
            store(scaleOf(s), m);
            return;
        }
        if (wb.method == 1) {  // CAT02 on the white point
            M3 in2{};
            const double a = 0.94927287 / (w[0] * 0.39040545 + w[1] * 0.54994112 + w[2] * 0.0089263199) * lum;
            in2.m[0][0] = a * 2.8584676, in2.m[0][1] = a * -0.21018226, in2.m[0][2] = a * -0.041812003;
            const double b = 1.0353711 / (w[0] * 0.070841588 + w[1] * 0.96317178 + w[2] * 0.00135775) * lum;
            in2.m[1][0] = b * -1.6287878, in2.m[1][1] = b * 1.1582009, in2.m[1][2] = b * -0.11816935;
            const double c = 1.0873741 / (w[0] * 0.023108181 + w[1] * 0.12802079 + w[2] * 0.93624514) * lum;
            in2.m[2][0] = c * -0.024891036, in2.m[2][1] = c * 0.00032428149, in2.m[2][2] = c * 1.0686663;
            store(mul(kSrgbToCat02, in2), m);
            if (!finite9(m)) identity9(m);
            return;
        }
        double xyz[3];
        transform(w, kSrgbD65ToXyz, xyz);
        const double s = 1.0 / (xyz[0] + xyz[1] + xyz[2]);
        xy[0] = xyz[0] * s, xy[1] = xyz[1] * s;
    } else if (wb.illuminant == 1) {
        if (!(wb.colorTempK > 0)) return;
        dIlluminantChromaticity(wb.colorTempK, xy);
        tintChromaticity(xy, wb.colorTempK, wb.tint, true);
    } else if (wb.illuminant == 2) {
        if (!(wb.colorTempK > 0)) return;
        blackBodyChromaticity(wb.colorTempK, xy);
        tintChromaticity(xy, wb.colorTempK, wb.tint, false);
    } else {
        const Illuminant& il = kIlluminants[wb.illuminant];
        xy[0] = il.x, xy[1] = il.y;
        tintChromaticity(xy, il.cct, wb.tint, false);
    }
    if (wb.method == 6 || !(xy[1] > 0)) return;
    // From the illuminant to D65 in the method's cone space, between the D50 form of sRGB and XYZ to sRGB.
    const double d65[3] = {0.95046997, 1.0, 1.0888};
    const double src[3] = {xy[0] / xy[1], 1.0, (1.0 - xy[0] - xy[1]) / xy[1]};
    const M3& crd = kXyzToCrd[wb.method];
    double a[3], b[3];
    transform(d65, crd, a);
    transform(src, crd, b);
    if (!(std::fabs(b[0]) > 1e-12 && std::fabs(b[1]) > 1e-12 && std::fabs(b[2]) > 1e-12)) return;
    const double s[3] = {a[0] / b[0], a[1] / b[1], a[2] / b[2]};
    M3 inv;
    if (!inverse(crd, inv)) return;
    store(mul(mul(mul(mul(kSrgbD50ToXyz, crd), scaleOf(s)), inv), kXyzToSrgbD65), m);
    if (!finite9(m)) identity9(m);
}

bool gameWhiteBalanceActive(const GameWhiteBalance& wb) {
    double m[9];
    gameWhiteBalanceMatrix(wb, m);
    for (int i = 0; i < 9; ++i)
        if (std::fabs(m[i] - (i % 4 == 0 ? 1.0 : 0.0)) > 1e-6) return true;
    return false;
}

GameWhiteBalance gameWhiteBalanceFromVision(const std::string& text) {
    GameWhiteBalance wb;
    // `TOKEN "value"` lines.
    const auto value = [&](const char* token, std::string& out) {
        const std::string tok(token);
        size_t pos = 0;
        while (pos < text.size()) {
            size_t end = text.find('\n', pos);
            if (end == std::string::npos) end = text.size();
            size_t a = pos;
            while (a < end && (text[a] == ' ' || text[a] == '\t')) ++a;
            if (text.compare(a, tok.size(), tok) == 0) {
                const size_t after = a + tok.size();
                if (after < end && (text[after] == ' ' || text[after] == '\t' || text[after] == '"')) {
                    const size_t q1 = text.find('"', after);
                    const size_t q2 = q1 == std::string::npos || q1 >= end ? std::string::npos : text.find('"', q1 + 1);
                    if (q2 != std::string::npos && q2 <= end) {
                        out = text.substr(q1 + 1, q2 - q1 - 1);
                        return true;
                    }
                }
            }
            pos = end + 1;
        }
        return false;
    };
    const auto num = [&](const char* token, double& out) {
        std::string s;
        if (!value(token, s) || s.empty()) return;
        char* e = nullptr;
        const double x = std::strtod(s.c_str(), &e);
        if (e != s.c_str() && std::isfinite(x)) out = x;
    };
    double method = wb.method, illuminant = wb.illuminant;
    num("MNMNSSLLOR", method);
    num("NLSRLSTPTN", illuminant);
    num("TRQLLNNLS", wb.colorTempK);
    num("OPSMSQNKQ", wb.tint);
    // Out of range (or not a number) becomes -1, which the matrix answers with the identity.
    wb.method = method >= 0 && method <= 6 ? int(method) : -1;
    wb.illuminant = illuminant >= 0 && illuminant <= 21 ? int(illuminant) : -1;
    std::string s;
    if (value("LRROOOSOMM", s)) {
        double p[3];
        if (std::sscanf(s.c_str(), "%lf %lf %lf", &p[0], &p[1], &p[2]) == 3)
            for (int k = 0; k < 3; ++k) wb.whitePoint[k] = p[k];
    }
    return wb;
}

GameWhiteBalance gameWhiteBalanceFromOverrides(const std::string& overridesText) {
    std::vector<std::pair<std::string, std::string>> pairs;
    std::string err;
    if (!parseVisionOverrides(overridesText, pairs, &err)) return {};
    // As vision text lines, with toneWhitePoint under its token.
    std::string text;
    for (const auto& [name, value] : pairs) {
        std::string lower = name;
        for (char& c : lower) c = char(std::tolower(static_cast<unsigned char>(c)));
        std::string upper = name;
        for (char& c : upper) c = char(std::toupper(static_cast<unsigned char>(c)));
        text += (lower == "tonewhitepoint" ? std::string("LRROOOSOMM") : upper) + " " + char(34) + value + char(34) + char(10);
    }
    return gameWhiteBalanceFromVision(text);
}

}  // namespace iw8r
