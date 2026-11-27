#include "custom_sounds.h"

#include <cctype>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>

namespace iw8r {
namespace fs = std::filesystem;

namespace {
std::string num(double v) {
    if (!std::isfinite(v)) v = 0;
    char buf[48];
    std::snprintf(buf, sizeof buf, "%.9g", v);
    return buf;
}
}  // namespace

bool isValidCustomSoundName(const std::string& name) {
    if (name.empty() || name.size() > 60) return false;
    for (char c : name)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return false;
    return true;
}

std::string findCustomSound(const std::vector<std::string>& roots, const std::string& name) {
    if (!isValidCustomSoundName(name)) return {};
    for (const std::string& r : roots)
        for (const char* ext : {".wav", ".WAV", ".mp3", ".MP3"}) {
            std::error_code ec;
            const fs::path p = fs::path(r) / (name + ext);
            if (fs::is_regular_file(p, ec)) return p.string();
        }
    return {};
}

double converterRoleGain(const std::string& aliasName, bool looping) {
    if (looping) return 0.18;
    std::string n = aliasName;
    for (char& c : n) c = char(std::tolower(static_cast<unsigned char>(c)));
    const auto has = [&](const char* w) { return n.find(w) != std::string::npos; };
    if (has("glass") || has("break") || has("shatter") || has("tv_") || has("door")) return 0.45;
    if (has("fire") || has("flare") || has("flame") || has("burn")) return 0.35;
    return 1.0;
}

std::string customSoundAliasJson(const CustomSoundAlias& a) {
    // IW3 alias flags: bit 0 looping, bits 8-13 the entity channel in channels.def order (1 auto, 24 ambient).
    const unsigned channel = a.channel ? std::min(a.channel, 32u) : (a.ambient2d ? 24u : 1u);
    const unsigned flags = (a.looping ? 1u : 0u) | (channel << 8);
    const double gain = converterRoleGain(a.name, a.looping);
    const double dmin = std::max(0.0, a.distMin), dmax = std::max(dmin + 1, a.distMax);

    std::vector<CustomSoundVariant> list = a.variants;
    if (list.empty()) {
        CustomSoundVariant v;
        v.fileName = a.fileName;
        v.volumeMin = a.volumeMin >= 0.0 ? a.volumeMin : a.volume;
        v.volumeMax = a.volumeMax >= 0.0 ? a.volumeMax : a.volume;
        v.pitchMin = a.pitchMin;
        v.pitchMax = a.pitchMax;
        v.lfePercentage = a.lfePercentage;
        v.centerPercentage = a.centerPercentage;
        v.startDelayMs = a.startDelayMs;
        v.secondary = a.secondary;
        list.push_back(std::move(v));
    }

    std::string out = "{\"name\":\"" + a.name + "\",\"aliases\":[";
    for (size_t i = 0; i < list.size(); ++i) {
        const CustomSoundVariant& v = list[i];
        const double vmin = std::clamp(std::min(v.volumeMin, v.volumeMax), 0.0, 1.0) / gain;
        const double vmax = std::clamp(std::max(v.volumeMin, v.volumeMax), 0.0, 1.0) / gain;
        const double pmin = std::clamp(std::min(v.pitchMin, v.pitchMax), 0.1, 4.0);
        const double pmax = std::clamp(std::max(v.pitchMin, v.pitchMax), 0.1, 4.0);
        const double prob = std::max(0.0, v.probability);
        const double lfe = std::clamp(v.lfePercentage, 0.0, 1.0);
        const double ctr = std::clamp(v.centerPercentage, 0.0, 1.0);
        const int delay = std::max(0, v.startDelayMs);
        if (i) out += ",";
        out += "{\"file\":{\"name\":\"" + v.fileName + "\",\"directory\":\"iw8r\"},\"volume\":{\"min\":" + num(vmin) +
               ",\"max\":" + num(vmax) + "},\"pitch\":{\"min\":" + num(pmin) + ",\"max\":" + num(pmax) +
               "},\"distance\":{\"min\":" + num(dmin) + ",\"max\":" + num(dmax) + "},\"probability\":" + num(prob) +
               ",\"lfe_percentage\":" + num(lfe) + ",\"start_delay\":" + std::to_string(delay) +
               ",\"flags\":" + std::to_string(flags) + ",\"center_percentage\":" + num(ctr);
        if (!v.secondary.empty()) out += ",\"secondary\":\"" + v.secondary + "\"";
        out += "}";
    }
    out += "]}";
    return out;
}

}  // namespace iw8r
