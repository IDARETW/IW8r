#include "foliage_sway.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <initializer_list>
#include <sstream>
#include <vector>

#include "material_blend.h"

namespace iw8r {

namespace {

constexpr double kPi = 3.14159265358979323846;

std::string lower(const std::string& s) {
    std::string out = s;
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

bool hasAny(const std::string& hay, std::initializer_list<const char*> needles) {
    for (const char* n : needles) {
        if (hay.find(n) != std::string::npos) return true;
    }
    return false;
}

}  // namespace

bool isFoliageMaterial(const std::string& materialName, const std::string& techset) {
    const std::string tech = lower(techset);
    if (tech.find("reactivemotion") != std::string::npos) return true;

    const std::string name = lower(materialName);
    const bool foliageName = hasAny(name, {"foliage", "grass", "weed", "bush", "leaf", "leaves", "tree", "canopy", "reed",
                                           "flower", "plant", "hedge", "shrub", "fern"});
    if (!foliageName) return false;
    if (techset.empty()) return true;
    if (tech.find("card_") != std::string::npos) return true;
    return materialBlendFromTechset(techset) == MaterialBlend::AlphaTest;
}

Vec3 swayOffset(const WindSettings& w, const Vec3& p, double heightAboveBase, double timeSec) {
    if (!std::isfinite(heightAboveBase) || !std::isfinite(timeSec) || !std::isfinite(w.strength) ||
        !std::isfinite(w.gustFreq) || !std::isfinite(w.dirYawDeg) || !std::isfinite(p.x) || !std::isfinite(p.y) ||
        !std::isfinite(p.z)) {
        return {};
    }
    if (heightAboveBase <= 0) return {};  // the base does not move

    const double h = std::min(heightAboveBase / kSwayFullHeight, 1.0);
    const double strength = std::clamp(w.strength, 0.0, kSwayMaxStrength);
    const double gust = std::clamp(w.gustFreq, 0.0, kSwayMaxGustHz);
    const double amplitude = kSwayAmplitude * strength * h * h;

    const double phase = 0.045 * p.x + 0.031 * p.y + 0.017 * p.z;
    const double wt = 2.0 * kPi * gust * timeSec;
    const double wave = 0.7 * std::sin(wt + phase) + 0.3 * std::sin(1.7 * wt + 1.3 * phase);

    const double yaw = w.dirYawDeg * kPi / 180.0;
    return {std::cos(yaw) * amplitude * wave, std::sin(yaw) * amplitude * wave, 0.0};
}

std::optional<WindSettings> parseWind(const std::string& value, std::string* err) {
    if (err) err->clear();
    std::istringstream in(value);
    std::vector<double> n;
    std::string tok;
    while (in >> tok) {
        char* end = nullptr;
        const double d = std::strtod(tok.c_str(), &end);
        if (!end || *end != '\0' || !std::isfinite(d)) {
            if (err) *err = "'" + tok + "' is not a finite number";
            return std::nullopt;
        }
        n.push_back(d);
    }
    if (n.size() != 3) {
        if (err) *err = "needs three numbers: yaw strength gust";
        return std::nullopt;
    }

    WindSettings w;
    w.dirYawDeg = n[0];
    w.strength = n[1];
    w.gustFreq = n[2];

    std::vector<std::string> notes;
    if (w.strength < 0 || w.strength > kSwayMaxStrength) {
        notes.push_back("strength " + formatNumber(w.strength) + " clamped to " +
                        formatNumber(std::clamp(w.strength, 0.0, kSwayMaxStrength)) + " (0.." +
                        formatNumber(kSwayMaxStrength) + ")");
        w.strength = std::clamp(w.strength, 0.0, kSwayMaxStrength);
    }
    if (w.gustFreq < 0 || w.gustFreq > kSwayMaxGustHz) {
        notes.push_back("gust " + formatNumber(w.gustFreq) + " clamped to " +
                        formatNumber(std::clamp(w.gustFreq, 0.0, kSwayMaxGustHz)) + " (0.." +
                        formatNumber(kSwayMaxGustHz) + " Hz)");
        w.gustFreq = std::clamp(w.gustFreq, 0.0, kSwayMaxGustHz);
    }
    if (err) {
        for (size_t i = 0; i < notes.size(); ++i) *err += (i ? "; " : "") + notes[i];
    }
    return w;
}

std::string formatWind(const WindSettings& w) {
    return formatNumber(w.dirYawDeg) + " " + formatNumber(w.strength) + " " + formatNumber(w.gustFreq);
}

std::optional<WindSettings> readWind(const Entity& worldspawn, std::string* warn) {
    const std::string* v = worldspawn.value(kWindKey);
    if (!v || v->empty()) return std::nullopt;

    std::string err;
    const std::optional<WindSettings> w = parseWind(*v, &err);
    if (warn && (!w || !err.empty())) {
        *warn += std::string(kWindKey) + " '" + *v + "': " + err + (w ? "" : "; wind ignored") + "\n";
    }
    return w;
}

}  // namespace iw8r
