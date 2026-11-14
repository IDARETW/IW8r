#include "volume_mask.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <sstream>

namespace iw8r {

namespace {

std::string lowered(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return s;
}

bool number(const std::string& text, double& out) {
    if (text.empty()) return false;
    char* end = nullptr;
    out = std::strtod(text.c_str(), &end);
    return end && *end == 0 && std::isfinite(out);
}

std::string shortNumber(double v) {
    char buf[40];
    std::snprintf(buf, sizeof buf, "%.6g", v);
    return buf;
}

// A picture name as a file stem: no folder, no ending, letters, digits, '_' and '-' only.
std::string cleanImage(const std::string& text) {
    std::string name = lowered(text);
    const size_t slash = name.find_last_of("/\\");
    if (slash != std::string::npos) name.erase(0, slash + 1);
    const size_t dot = name.find_last_of('.');
    if (dot != std::string::npos) name.erase(dot);
    std::string out;
    for (const char c : name)
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-') out += c;
    return out;
}

}  // namespace

bool parseVolumeMask(const std::string& text, VolumeMask& out, std::string* err) {
    const auto fail = [&](const char* why) {
        if (err) *err = why;
        return false;
    };
    std::istringstream in(text);
    std::vector<std::string> words;
    for (std::string w; in >> w;) words.push_back(w);
    if (words.empty()) return fail("no picture named");
    VolumeMask m;
    m.image = cleanImage(words[0]);
    if (m.image.empty()) return fail("the picture name has no usable letters");
    size_t at = 1;
    if (at < words.size()) {
        const std::string a = lowered(words[at]);
        if (a == "x" || a == "y" || a == "z") m.axis = a[0] - 'x', ++at;
    }
    std::vector<double> n;
    for (; at < words.size(); ++at) {
        double v = 0;
        if (!number(words[at], v)) return fail("a size, scroll or offset is no number");
        n.push_back(v);
    }
    if (n.size() != 0 && n.size() != 1 && n.size() != 2 && n.size() != 4 && n.size() != 6)
        return fail("give a size, two sizes, two sizes and a scroll, or two sizes, a scroll and an offset");
    if (n.size() >= 1) m.size[0] = m.size[1] = n[0];
    if (n.size() >= 2) m.size[1] = n[1];
    if (n.size() >= 4) m.scroll[0] = n[2], m.scroll[1] = n[3];
    if (n.size() >= 6) m.offset[0] = n[4], m.offset[1] = n[5];
    if (!(std::fabs(m.size[0]) >= 1e-3) || !(std::fabs(m.size[1]) >= 1e-3)) return fail("a size of 0");
    out = m;
    return true;
}

std::string formatVolumeMask(const VolumeMask& m) {
    std::string s = m.image;
    if (m.axis != 2) s += m.axis == 0 ? " x" : " y";
    const bool offset = m.offset[0] != 0 || m.offset[1] != 0;
    const bool scroll = offset || m.scroll[0] != 0 || m.scroll[1] != 0;
    s += " " + shortNumber(m.size[0]);
    if (scroll || m.size[1] != m.size[0]) s += " " + shortNumber(m.size[1]);
    if (scroll) s += " " + shortNumber(m.scroll[0]) + " " + shortNumber(m.scroll[1]);
    if (offset) s += " " + shortNumber(m.offset[0]) + " " + shortNumber(m.offset[1]);
    return s;
}

std::vector<VolumeMask> volumeMasksOf(const Entity& entity, std::vector<std::string>* warnings) {
    std::vector<VolumeMask> out;
    for (const char* key : kVolumeMaskKeys) {
        const std::string* v = entity.value(key);
        if (!v || v->empty()) continue;
        VolumeMask m;
        std::string err;
        if (parseVolumeMask(*v, m, &err)) out.push_back(m);
        else if (warnings) warnings->push_back(std::string(key) + " '" + *v + "' not used: " + err);
    }
    return out;
}

void volumeMaskTerms(const VolumeMask& m, double time, float out[4]) {
    out[0] = float(1.0 / m.size[0]);
    out[1] = float(1.0 / m.size[1]);
    for (int k = 0; k < 2; ++k) {
        // Only the part within one repeat matters; keeping it small keeps a float exact after hours of scrolling.
        double term = (m.offset[k] + time * m.scroll[k]) / m.size[k] + 0.5;
        term -= std::floor(term);
        out[2 + k] = float(term);
    }
}

void volumeMaskUv(const VolumeMask& m, const double local[3], double time, double uv[2]) {
    const double a = m.axis == 0 ? local[1] : local[0];
    const double b = m.axis == 2 ? local[1] : local[2];
    float t[4];
    volumeMaskTerms(m, time, t);
    uv[0] = a * t[0] + t[2];
    uv[1] = -b * t[1] + t[3];
}

std::string volumeMaskImageName(const std::string& image) { return "iw8r_vmask_" + cleanImage(image); }

std::string findVolumeMaskFile(const std::string& image, const std::vector<std::string>& folders) {
    namespace fs = std::filesystem;
    const std::string stem = cleanImage(image);
    if (stem.empty()) return {};
    for (const std::string& folder : folders)
        for (const char* ext : {".png", ".tga", ".jpg", ".bmp"}) {
            std::error_code ec;
            const fs::path p = fs::path(folder) / (stem + ext);
            if (fs::is_regular_file(p, ec)) return p.string();
        }
    return {};
}

}  // namespace iw8r
