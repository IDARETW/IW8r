// IW8 Radiant - IES photometric profiles (roadmap 5.13). See ies_profiles.h.
#include "ies_profiles.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <sstream>
#include <utility>

namespace iw8r {
namespace {

bool fail(std::string* err, const std::string& message) {
    if (err) *err = message;
    return false;
}

std::string trim(const std::string& s) {
    const auto first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
}

// Whole number in [1, max]. LM-63 counts are integers written as numbers.
bool wholeCount(double v, double max, size_t& out) {
    if (!(v >= 1.0) || v > max || v != std::floor(v)) return false;
    out = static_cast<size_t>(v);
    return true;
}

bool parseNumber(const std::string& token, double& out) {
    char* end = nullptr;
    out = std::strtod(token.c_str(), &end);
    return end != token.c_str() && *end == '\0' && std::isfinite(out);
}

double clamp01(double t) { return t < 0.0 ? 0.0 : (t > 1.0 ? 1.0 : t); }

// Raw candela of one plane at a vertical angle, bilinear over the vertical angles.
double planeValue(const IesProfile& p, size_t plane, double v) {
    const auto& angles = p.verticalAngles;
    const size_t n = angles.size();
    const size_t base = plane * n;
    if (n == 1) return p.candela[base];
    v = std::clamp(v, angles.front(), angles.back());
    // hi is the first angle above v; at the last angle it steps back so that lo and hi bracket v.
    size_t hi = static_cast<size_t>(std::upper_bound(angles.begin(), angles.end(), v) - angles.begin());
    if (hi >= n) hi = n - 1;
    const size_t lo = hi - 1;
    const double t = (v - angles[lo]) / (angles[hi] - angles[lo]);
    return (1.0 - t) * p.candela[base + lo] + t * p.candela[base + hi];
}

// Folds a horizontal angle into [0, 360) and then onto the stored planes. The last plane says what symmetry the
// table relies on: 90 mirrors at 0 and 90 (quadrant), 180 mirrors at 0 (half). Other ends need no fold; the
// caller wraps the gap past the last plane.
double foldHorizontal(double h, double last) {
    h = std::fmod(h, 360.0);
    if (h < 0.0) h += 360.0;
    if (last == 90.0 || last == 180.0) {
        if (h > 180.0) h = 360.0 - h;
    }
    if (last == 90.0 && h > 90.0) h = 180.0 - h;
    return h;
}

}  // namespace

bool parseIesFile(const std::string& text, IesProfile& out, std::string* err) {
    // Header line, keyword lines (ignored except TILT), then every number to the end of the text.
    std::istringstream in(text);
    std::string line, data;
    bool sawHeader = false, sawTilt = false;
    while (std::getline(in, line)) {
        const std::string t = trim(line);
        if (!sawHeader) {
            if (t.empty()) continue;
            if (t.rfind("IESNA", 0) != 0) return fail(err, "not an IES LM-63 file: first line is not IESNA");
            sawHeader = true;
            continue;
        }
        if (!sawTilt) {
            if (t.rfind("TILT=", 0) == 0) {
                // A TILT file or an INCLUDE table needs a second source; only the untilted table is read.
                if (trim(t.substr(5)) != "NONE") return fail(err, "unsupported " + t + ": only TILT=NONE is read");
                sawTilt = true;
            }
            continue;
        }
        data += line;
        data += ' ';
    }
    if (!sawHeader) return fail(err, "empty IES text");
    if (!sawTilt) return fail(err, "no TILT= line");

    std::vector<std::string> tokens;
    {
        std::istringstream ds(data);
        std::string tok;
        while (ds >> tok) tokens.push_back(tok);
    }
    size_t at = 0;
    auto next = [&](double& v) { return at < tokens.size() && parseNumber(tokens[at++], v); };

    // The 13 header numbers in file order: lamps, lumens, multiplier, V count, H count, photometric type, units
    // type, width, length, height, ballast factor, ballast-lamp factor, input watts. Only the multiplier, the
    // counts, the type and the units are used; the others are read to step past them.
    double lamps = 0, lumens = 0, multiplier = 0, vCountIn = 0, hCountIn = 0, photometricType = 0, unitsType = 0;
    double width = 0, length = 0, height = 0, ballast = 0, ballastLamp = 0, watts = 0;
    if (!next(lamps) || !next(lumens) || !next(multiplier) || !next(vCountIn) || !next(hCountIn) ||
        !next(photometricType) || !next(unitsType) || !next(width) || !next(length) || !next(height) ||
        !next(ballast) || !next(ballastLamp) || !next(watts))
        return fail(err, "truncated or non-numeric IES header");
    (void)(lamps + lumens + width + length + height + ballast + ballastLamp + watts);  // read, not used

    if (!(multiplier > 0.0)) return fail(err, "candela multiplier must be positive");
    if (photometricType != 1.0) return fail(err, "photometric type must be C (1); types A and B are not read");
    if (unitsType != 1.0 && unitsType != 2.0) return fail(err, "units type must be 1 (feet) or 2 (meters)");

    size_t vCount = 0, hCount = 0;
    if (!wholeCount(vCountIn, 10000, vCount) || !wholeCount(hCountIn, 10000, hCount))
        return fail(err, "vertical or horizontal angle count is not a whole number in 1..10000");
    if (vCount * hCount > 1000000) return fail(err, "candela table larger than 1,000,000 values");

    IesProfile profile;
    profile.multiplier = multiplier;
    profile.verticalAngles.resize(vCount);
    profile.horizontalAngles.resize(hCount);
    profile.candela.resize(vCount * hCount);
    for (auto& v : profile.verticalAngles)
        if (!next(v)) return fail(err, "truncated vertical angles");
    for (auto& h : profile.horizontalAngles)
        if (!next(h)) return fail(err, "truncated horizontal angles");
    for (auto& c : profile.candela)
        if (!next(c)) return fail(err, "truncated candela table");

    for (size_t i = 0; i < vCount; ++i) {
        if (profile.verticalAngles[i] < 0.0 || profile.verticalAngles[i] > 180.0)
            return fail(err, "vertical angle outside 0..180");
        if (i > 0 && !(profile.verticalAngles[i] > profile.verticalAngles[i - 1]))
            return fail(err, "vertical angles not strictly increasing");
    }
    for (size_t i = 0; i < hCount; ++i) {
        if (profile.horizontalAngles[i] < 0.0 || profile.horizontalAngles[i] > 360.0)
            return fail(err, "horizontal angle outside 0..360");
        if (i > 0 && !(profile.horizontalAngles[i] > profile.horizontalAngles[i - 1]))
            return fail(err, "horizontal angles not strictly increasing");
    }
    for (double c : profile.candela)
        if (c < 0.0) return fail(err, "negative candela value");

    out = std::move(profile);
    return true;
}

double iesIntensity(const IesProfile& p, double verticalDeg, double horizontalDeg) {
    const size_t planes = p.horizontalAngles.size();
    if (planes == 0 || p.verticalAngles.empty() || p.candela.size() != planes * p.verticalAngles.size())
        return 0.0;
    if (!std::isfinite(verticalDeg) || !std::isfinite(horizontalDeg)) return 0.0;
    if (planes == 1) return p.multiplier * planeValue(p, 0, verticalDeg);

    const auto& ha = p.horizontalAngles;
    const double a = foldHorizontal(horizontalDeg, ha.back());
    // Planes sit on a circle: the gap before the first plane and after the last one wraps through 360.
    const size_t hi = static_cast<size_t>(std::upper_bound(ha.begin(), ha.end(), a) - ha.begin());
    size_t lo = 0, up = 0;
    double loAng = 0.0, upAng = 0.0;
    if (hi == 0) {
        lo = planes - 1;
        loAng = ha.back() - 360.0;
        up = 0;
        upAng = ha.front();
    } else if (hi == planes) {
        lo = planes - 1;
        loAng = ha.back();
        up = 0;
        upAng = ha.front() + 360.0;
    } else {
        lo = hi - 1;
        up = hi;
        loAng = ha[lo];
        upAng = ha[hi];
    }
    const double t = upAng > loAng ? clamp01((a - loAng) / (upAng - loAng)) : 0.0;
    return p.multiplier * ((1.0 - t) * planeValue(p, lo, verticalDeg) + t * planeValue(p, up, verticalDeg));
}

std::vector<float> iesPolarCurve(const IesProfile& p, int samples) {
    constexpr int kMaxSamples = 1 << 20;
    if (samples < 2 || samples > kMaxSamples || p.verticalAngles.empty() || p.horizontalAngles.empty() ||
        p.candela.size() != p.horizontalAngles.size() * p.verticalAngles.size())
        return {};
    const double v0 = p.verticalAngles.front(), v1 = p.verticalAngles.back();
    std::vector<float> curve(static_cast<size_t>(samples));
    double peak = 0.0;
    for (int i = 0; i < samples; ++i) {
        const double v = v0 + (v1 - v0) * static_cast<double>(i) / static_cast<double>(samples - 1);
        const double value = iesIntensity(p, v, 0.0);
        curve[static_cast<size_t>(i)] = static_cast<float>(value);
        peak = std::max(peak, value);
    }
    if (peak > 0.0)
        for (float& c : curve) c = static_cast<float>(c / peak);
    return curve;
}

std::vector<int> iesReplayTexels(const IesProfile& profile) {
    std::vector<double> v(128);
    double peak = 0;
    for (int j = 0; j < 128; ++j) {
        const double t = std::asin(std::min(1.0, std::sqrt(2.0 * j / 255.0))) * 180.0 / 3.14159265358979323846;
        v[size_t(j)] = std::max(0.0, iesIntensity(profile, t, 0.0));
        peak = std::max(peak, v[size_t(j)]);
    }
    std::vector<int> out(128, 0);
    if (peak <= 0) return out;
    for (int j = 0; j < 128; ++j) out[size_t(j)] = std::clamp(int(std::lround(v[size_t(j)] / peak * 255.0)), 0, 255);
    return out;
}

}  // namespace iw8r
