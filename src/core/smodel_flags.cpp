#include "smodel_flags.h"

#include <array>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <sstream>

namespace iw8r {
namespace {

// Replay's 16 per-placement LOD distance scales (doc 54; iw3_fastfile.cpp kReplayLodDistanceScales).
constexpr std::array<double, 16> kLodScales{0.25, 0.375, 0.5, 0.625, 0.75, 0.875, 1.0, 1.125,
                                            1.25, 1.5,   1.75, 2.0,   2.5,   3.0,   3.5, 4.0};

std::string trim(const std::string& text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

void warn(std::vector<std::string>* warnings, const std::string& text) {
    if (warnings) warnings->push_back(text);
}

// Accepts 1/0 and true/false. Anything else keeps `value` and warns.
void readBool(const std::map<std::string, std::string>& keys, const char* key, bool& value,
              std::vector<std::string>* warnings) {
    const auto it = keys.find(key);
    if (it == keys.end()) return;
    const std::string text = trim(it->second);
    if (text == "1" || text == "true") {
        value = true;
    } else if (text == "0" || text == "false") {
        value = false;
    } else {
        warn(warnings, std::string(key) + " '" + it->second + "' is not 0, 1, true or false; using " +
                           (value ? "1" : "0"));
    }
}

// Parses a whole finite number. Returns false for empty text, trailing junk, nan or inf.
bool parseWholeNumber(const std::string& text, double& out) {
    if (text.empty()) return false;
    errno = 0;
    char* end = nullptr;
    const double v = std::strtod(text.c_str(), &end);
    if (end != text.c_str() + text.size() || errno == ERANGE || !std::isfinite(v)) return false;
    out = v;
    return true;
}

std::string formatNumber(double value) {
    std::ostringstream out;
    out.precision(17);
    out << value;
    return out.str();
}

}  // namespace

SModelFlags readSModelFlags(const std::map<std::string, std::string>& keys, std::vector<std::string>* warnings) {
    SModelFlags flags;
    readBool(keys, kSModelKeyNoShadow, flags.noShadow, warnings);
    if (const auto it = keys.find(kSModelKeyGroundLighting); it != keys.end()) {
        std::uint32_t packed = 0;
        if (parseGroundColor(trim(it->second), packed)) {
            flags.groundLighting = true;
            flags.groundColor = packed;
        } else {
            readBool(keys, kSModelKeyGroundLighting, flags.groundLighting, warnings);
        }
    }
    readBool(keys, kSModelKeyNoCollision, flags.noCollision, warnings);

    if (const auto it = keys.find(kSModelKeyCullDist); it != keys.end()) {
        double v = 0;
        if (parseWholeNumber(trim(it->second), v) && v >= 0) {
            flags.cullDistance = v;
        } else {
            warn(warnings, std::string(kSModelKeyCullDist) + " '" + it->second +
                               "' is not a number of zero or more; using 0 (no distance cull)");
        }
    }

    if (const auto it = keys.find(kSModelKeyLodScale); it != keys.end()) {
        double v = 0;
        bool known = false;
        if (parseWholeNumber(trim(it->second), v))
            for (const double scale : kLodScales)
                known = known || std::fabs(scale - v) < 1.0e-9;
        if (known) {
            flags.lodScale = v;
        } else {
            warn(warnings, std::string(kSModelKeyLodScale) + " '" + it->second +
                               "' is not one of 0.25 .. 4 (the 16 Replay LOD scales); using 1");
        }
    }
    return flags;
}

void writeSModelFlags(const SModelFlags& flags, std::map<std::string, std::string>& keys) {
    const SModelFlags defaults;
    const auto put = [&](const char* key, bool on) {
        if (on)
            keys[key] = "1";
        else
            keys.erase(key);
    };
    put(kSModelKeyNoShadow, flags.noShadow);
    put(kSModelKeyGroundLighting, flags.groundLighting);
    if (flags.groundLighting && flags.groundColor != defaults.groundColor) {
        std::ostringstream c;
        c << (flags.groundColor & 0xFF) << ' ' << ((flags.groundColor >> 8) & 0xFF) << ' ' << ((flags.groundColor >> 16) & 0xFF);
        keys[kSModelKeyGroundLighting] = c.str();
    }
    put(kSModelKeyNoCollision, flags.noCollision);

    if (flags.cullDistance > 0 && std::isfinite(flags.cullDistance))
        keys[kSModelKeyCullDist] = formatNumber(flags.cullDistance);
    else
        keys.erase(kSModelKeyCullDist);

    if (flags.lodScale != defaults.lodScale)
        keys[kSModelKeyLodScale] = formatNumber(flags.lodScale);
    else
        keys.erase(kSModelKeyLodScale);
}

bool parseGroundColor(const std::string& text, std::uint32_t& packed) {
    std::istringstream in(text);
    double c[3];
    for (double& v : c)
        if (!(in >> v) || !std::isfinite(v) || v < 0) return false;
    std::string rest;
    if (in >> rest) return false;
    const bool bytes = c[0] > 1 || c[1] > 1 || c[2] > 1;
    std::uint32_t ch[3];
    for (int i = 0; i < 3; ++i) {
        const double v = bytes ? c[i] : c[i] * 255.0;
        if (v > 255.5) return false;
        ch[i] = static_cast<std::uint32_t>(std::lround(v));
    }
    packed = 0xFF000000u | ch[2] << 16 | ch[1] << 8 | ch[0];
    return true;
}

std::uint32_t smodelFlagBits(const SModelFlags& flags) {
    std::uint32_t bits = 0;
    if (flags.groundLighting) bits |= kSModelBitGroundLighting;
    if (flags.noShadow) bits |= kSModelBitNoShadow;
    if (flags.noCollision) bits |= kSModelBitNoCollision;
    return bits;
}

}  // namespace iw8r
