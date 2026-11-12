#include "material_tints.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace iw8r {

bool MaterialTints::parse(const std::string& text) {
    tints_.clear();
    std::istringstream in(text);
    for (std::string line; std::getline(in, line);) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        const size_t tab = line.find('\t');
        if (tab == std::string::npos || line.size() < tab + 7) continue;
        const std::string hex = line.substr(tab + 1, 6);
        if (!std::all_of(hex.begin(), hex.end(), [](unsigned char c) { return std::isxdigit(c) != 0; })) continue;
        const unsigned long v = std::stoul(hex, nullptr, 16);
        std::string name = line.substr(0, tab);
        std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        tints_[name] = {uint8_t(v >> 16), uint8_t(v >> 8), uint8_t(v)};
    }
    return true;
}

bool MaterialTints::load(const std::string& tsvPath, std::string* err) {
    std::ifstream f(tsvPath, std::ios::binary);
    if (!f) {
        if (err) *err = "cannot read " + tsvPath;
        return false;
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    return parse(ss.str());
}

std::optional<std::array<uint8_t, 3>> MaterialTints::find(const std::string& material) const {
    std::string name = material;
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    const auto it = tints_.find(name);
    if (it == tints_.end()) return std::nullopt;
    return it->second;
}

const MaterialTints& MaterialTints::shared(const std::string& startDir) {
    static const MaterialTints tints = [&] {
        MaterialTints t;
        std::error_code ec;
        std::filesystem::path d = std::filesystem::absolute(startDir, ec);
        for (int up = 0; up < 5 && !d.empty(); ++up, d = d.parent_path()) {
            const auto f = d / "data" / "asset_catalog" / "iw8_material_tints.tsv";
            if (std::filesystem::is_regular_file(f, ec)) {
                t.load(f.string());
                break;
            }
            if (d == d.parent_path()) break;
        }
        return t;
    }();
    return tints;
}

void applyMaterialTint(uint8_t* rgba, size_t texels, const std::array<uint8_t, 3>& tint) {
    for (size_t i = 0; i < texels; ++i)
        for (int c = 0; c < 3; ++c) {
            const unsigned v = (unsigned(rgba[i * 4 + size_t(c)]) * tint[size_t(c)] + 127u) / 255u;
            rgba[i * 4 + size_t(c)] = uint8_t(std::min(v, 255u));
        }
}

}  // namespace iw8r
