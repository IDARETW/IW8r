#include "streamed_images.h"

#include <cstdio>

#include <charconv>
#include <fstream>
#include <string_view>
#include <system_error>
#include <vector>

namespace iw8r {
namespace {

std::vector<std::string_view> splitTabs(std::string_view line) {
    std::vector<std::string_view> out;
    size_t start = 0;
    for (;;) {
        const size_t tab = line.find('\t', start);
        if (tab == std::string_view::npos) {
            out.push_back(line.substr(start));
            return out;
        }
        out.push_back(line.substr(start, tab - start));
        start = tab + 1;
    }
}

// Whole field must be an unsigned decimal integer (no sign, no spaces).
bool parseInt(std::string_view text, int& out) {
    if (text.empty()) return false;
    int value = 0;
    const char* first = text.data();
    const char* last = text.data() + text.size();
    const auto [end, ec] = std::from_chars(first, last, value);
    if (ec != std::errc{} || end != last) return false;
    out = value;
    return true;
}

bool parseRow(std::string_view line, StreamedImage& row) {
    if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
    const std::vector<std::string_view> f = splitTabs(line);
    if (f.size() != 6) return false;
    if (f[0].empty()) return false;
    int streamed = 0;
    if (!parseInt(f[1], row.fullW) || !parseInt(f[2], row.fullH) || !parseInt(f[3], row.residentW) ||
        !parseInt(f[4], row.residentH) || !parseInt(f[5], streamed))
        return false;
    if (streamed != 0 && streamed != 1) return false;
    if (row.fullW <= 0 || row.fullH <= 0 || row.residentW <= 0 || row.residentH <= 0) return false;
    if (row.residentW > row.fullW || row.residentH > row.fullH) return false;
    row.image.assign(f[0].data(), f[0].size());
    row.streamed = streamed == 1;
    return true;
}

}  // namespace

bool StreamedImageCatalog::load(const std::string& tsvPath, std::string* err) {
    images_.clear();
    byStem_.clear();
    skipped_ = 0;
    std::ifstream in(tsvPath, std::ios::binary);
    if (!in) {
        if (err) *err = "cannot open " + tsvPath;
        return false;
    }
    std::string line;
    while (std::getline(in, line)) {
        std::string_view view(line);
        if (!view.empty() && view.back() == '\r') view.remove_suffix(1);
        if (view.empty() || view.front() == '#') continue;
        if (view.substr(0, 6) == "image\t" && view.find("full_width") != std::string_view::npos) continue;  // header row
        StreamedImage row;
        if (!parseRow(view, row)) {
            ++skipped_;
            continue;
        }
        if (images_.count(row.image)) {
            ++skipped_;
            continue;
        }
        images_.emplace(row.image, row);
        byStem_.emplace(streamedCacheStem(row.image), row.image);
    }
    return true;
}

const StreamedImage* StreamedImageCatalog::findByCacheStem(const std::string& stem) const {
    const auto it = byStem_.find(stem);
    return it == byStem_.end() ? nullptr : find(it->second);
}

std::string streamedCacheStem(const std::string& image) {
    std::string s = image;
    for (char& c : s)
        if (c == '&' || c == '~') c = '_';
    return s;
}

bool readDdsSize(const std::string& path, int& width, int& height) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    unsigned char h[20] = {};
    const size_t n = std::fread(h, 1, sizeof h, f);
    std::fclose(f);
    if (n < 20 || h[0] != 'D' || h[1] != 'D' || h[2] != 'S' || h[3] != ' ') return false;
    const auto u32 = [&](int o) { return int(unsigned(h[o]) | unsigned(h[o + 1]) << 8 | unsigned(h[o + 2]) << 16 | unsigned(h[o + 3]) << 24); };
    height = u32(12);
    width = u32(16);
    return width > 0 && height > 0;
}

const StreamedImage* StreamedImageCatalog::find(const std::string& image) const {
    const auto it = images_.find(image);
    return it == images_.end() ? nullptr : &it->second;
}

std::string streamedQualityText(const StreamedImage& image) {
    const std::string full = std::to_string(image.fullW) + "x" + std::to_string(image.fullH);
    if (!image.streamed) return "not streamed: full " + full;
    const std::string resident = std::to_string(image.residentW) + "x" + std::to_string(image.residentH);
    return "streamed: editor gets " + resident + " of " + full;
}

}  // namespace iw8r
