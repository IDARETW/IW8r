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
    const unsigned channel = a.ambient2d ? 24u : 1u;
    const unsigned flags = (a.looping ? 1u : 0u) | (channel << 8);
    // The converter clamps volume x role gain to 0..1, so the role gain is divided out here.
    const double vol = std::clamp(a.volume, 0.0, 1.0) / converterRoleGain(a.name, a.looping);
    const double dmin = std::max(0.0, a.distMin), dmax = std::max(dmin + 1, a.distMax);
    return "{\"name\":\"" + a.name + "\",\"aliases\":[{\"file\":{\"name\":\"" + a.fileName +
           "\",\"directory\":\"iw8r\"},\"volume\":{\"min\":" + num(vol) + ",\"max\":" + num(vol) +
           "},\"pitch\":{\"min\":1,\"max\":1},\"distance\":{\"min\":" + num(dmin) + ",\"max\":" + num(dmax) +
           "},\"probability\":1,\"lfe_percentage\":0,\"start_delay\":0,\"flags\":" + std::to_string(flags) +
           ",\"center_percentage\":0}]}";
}

}  // namespace iw8r
