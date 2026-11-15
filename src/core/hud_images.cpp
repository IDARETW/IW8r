#include "hud_images.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <set>
#include <system_error>
#include <utility>

#include "custom_materials.h"
#include "minimap.h"

namespace iw8r {

namespace fs = std::filesystem;

std::string hudImageName(const std::string& fileName) {
    // Without the folder and the extension.
    std::string stem = fileName;
    if (const size_t slash = stem.find_last_of("/\\"); slash != std::string::npos) stem.erase(0, slash + 1);
    if (const size_t dot = stem.find_last_of('.'); dot != std::string::npos && dot > 0) stem.erase(dot);
    std::string out;
    for (const char ch : stem) {
        const unsigned char c = static_cast<unsigned char>(ch);
        if (std::isalnum(c) && c < 128) out.push_back(char(std::tolower(c)));
        else if (!out.empty() && out.back() != '_') out.push_back('_');  // anything else: one underscore
    }
    while (!out.empty() && out.back() == '_') out.pop_back();
    if (out == "hud") out.clear();  // "hud_.png": only the prefix
    if (out.rfind("hud_", 0) == 0) out.erase(0, 4);
    if (out.empty()) return {};
    if (out.size() > 48) out.resize(48);  // asset names stay short
    return "hud_" + out;
}

std::vector<HudImage> listHudImages(const std::string& folder, std::vector<std::string>* warnings) {
    std::vector<HudImage> out;
    std::error_code ec;
    if (folder.empty() || !fs::is_directory(fs::path(folder), ec)) return out;
    const auto warn = [&](const std::string& line) {
        if (warnings) warnings->push_back(line);
    };
    // File name and full path, as text. A name the system's character set cannot hold makes the conversion throw: such a
    // file is left out (the image decoder could not open it either).
    std::vector<std::pair<std::string, std::string>> files;
    size_t unusable = 0;
    for (fs::directory_iterator it(fs::path(folder), ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        try {
            std::string ext = it->path().extension().string();
            for (char& c : ext) c = char(std::tolower(static_cast<unsigned char>(c)));
            if (ext == ".png" || ext == ".tga" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".dds")
                files.emplace_back(it->path().filename().string(), it->path().string());
        } catch (...) {
            ++unusable;
        }
    }
    if (unusable)
        warn(std::to_string(unusable) + " file(s) in the HUD images folder left out: their names use characters outside the system's character set");
    std::sort(files.begin(), files.end());
    std::set<std::string> seen;
    for (const auto& [file, path] : files) {
        const std::string name = hudImageName(file);
        if (name.empty()) {
            warn("HUD image " + file + " left out: its name has no letter or digit");
            continue;
        }
        if (!seen.insert(name).second) {
            warn("HUD image " + file + " left out: another file is already " + name);
            continue;
        }
        if (out.size() >= kMaxHudImages) {
            warn("HUD image " + file + " left out: more than " + std::to_string(kMaxHudImages) + " HUD images");
            continue;
        }
        out.push_back({name, path});
    }
    return out;
}

bool encodeHudImageDds(const std::string& picturePath, std::vector<uint8_t>& dds, std::string* err) {
    int w = 0, h = 0;
    std::vector<uint8_t> rgba;
    if (!decodeImageFile(picturePath, w, h, rgba, err)) return false;
    return encodeHudRgbaDds(w, h, rgba, dds, err);
}

bool encodeHudRgbaDds(int w, int h, const std::vector<uint8_t>& rgba, std::vector<uint8_t>& dds, std::string* err) {
    if (w <= 0 || h <= 0 || rgba.size() != size_t(w) * size_t(h) * 4) {
        if (err) *err = "the picture is empty";
        return false;
    }
    int side = 4;
    while (side < std::max(w, h) && side < 1024) side *= 2;
    MinimapImage img;
    img.width = img.height = side;
    img.rgba.resize(size_t(side) * size_t(side) * 4);
    // Bilinear, colour weighted by alpha so a transparent border does not darken the rim.
    for (int y = 0; y < side; ++y)
        for (int x = 0; x < side; ++x) {
            const double fx = (x + 0.5) * w / side - 0.5, fy = (y + 0.5) * h / side - 0.5;
            const int x0 = std::clamp(int(std::floor(fx)), 0, w - 1), y0 = std::clamp(int(std::floor(fy)), 0, h - 1);
            const int x1 = std::min(x0 + 1, w - 1), y1 = std::min(y0 + 1, h - 1);
            const double tx = std::clamp(fx - x0, 0.0, 1.0), ty = std::clamp(fy - y0, 0.0, 1.0);
            const double wt[4] = {(1 - tx) * (1 - ty), tx * (1 - ty), (1 - tx) * ty, tx * ty};
            const uint8_t* px[4] = {&rgba[(size_t(y0) * w + x0) * 4], &rgba[(size_t(y0) * w + x1) * 4], &rgba[(size_t(y1) * w + x0) * 4],
                                    &rgba[(size_t(y1) * w + x1) * 4]};
            double a = 0, c[3] = {0, 0, 0};
            for (int k = 0; k < 4; ++k) {
                const double wa = wt[k] * px[k][3];
                a += wa;
                for (int ch = 0; ch < 3; ++ch) c[ch] += px[k][ch] * wa;
            }
            uint8_t* out = &img.rgba[(size_t(y) * side + x) * 4];
            for (int ch = 0; ch < 3; ++ch) out[ch] = a > 0 ? uint8_t(std::lround(std::clamp(c[ch] / a, 0.0, 255.0))) : 0;
            out[3] = uint8_t(std::lround(std::clamp(a, 0.0, 255.0)));
        }
    return encodeMinimapDds(img, dds, err);
}

std::string hudImageFolderFor(const std::string& mapPath) {
    if (mapPath.empty()) return {};
    const fs::path p = fs::path(mapPath);
    return (p.parent_path() / (p.stem().string() + ".hud")).string();
}

std::vector<std::string> hudNamesInScript(const std::string& scriptText) {
    std::string lower = scriptText;
    for (char& c : lower) c = char(std::tolower(static_cast<unsigned char>(c)));
    std::set<std::string> names;
    for (const char* call : {"setshader", "precacheshader"}) {
        const std::string key(call);
        size_t pos = 0;
        while ((pos = lower.find(key, pos)) != std::string::npos) {
            size_t at = pos + key.size();
            pos = at;
            while (at < lower.size() && std::isspace(static_cast<unsigned char>(lower[at]))) ++at;
            if (at >= lower.size() || lower[at] != '(') continue;
            ++at;
            while (at < lower.size() && std::isspace(static_cast<unsigned char>(lower[at]))) ++at;
            if (at >= lower.size() || lower[at] != '"') continue;
            const size_t close = lower.find('"', at + 1);
            if (close == std::string::npos) continue;
            const std::string name = lower.substr(at + 1, close - at - 1);
            if (name.rfind("hud_", 0) == 0) names.insert(name);
        }
    }
    return std::vector<std::string>(names.begin(), names.end());
}

}  // namespace iw8r
