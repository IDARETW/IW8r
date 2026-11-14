import io

# HUD images are written as the minimap is: DXT5 with mips, square, a power of two. The converter's image reader for
# the map's own images takes BC1 / BC2 / BC3 only (the first end-to-end build failed on an uncompressed DDS).

ROOT = 'E:/IW8-Radiant/'


def patch(p, a, b, n=1):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


patch('src/core/minimap.h', '''bool writeMinimapDds(const MinimapImage& img, const std::string& ddsPath, std::string* err);''',
      '''bool writeMinimapDds(const MinimapImage& img, const std::string& ddsPath, std::string* err);
// The same file as bytes (the map's own HUD images use the same form, core hud_images).
bool encodeMinimapDds(const MinimapImage& img, std::vector<uint8_t>& file, std::string* err);''')
M = 'src/core/minimap.cpp'
patch(M, '''bool writeMinimapDds(const MinimapImage& img, const std::string& ddsPath, std::string* err) {
    const auto fail = [&](std::string message) {''', '''bool writeMinimapDds(const MinimapImage& img, const std::string& ddsPath, std::string* err) {
    std::vector<uint8_t> file;
    if (!encodeMinimapDds(img, file, err)) return false;
    std::ofstream out(ddsPath, std::ios::binary | std::ios::trunc);
    if (!out) {
        if (err) *err = "cannot open " + ddsPath + " for writing";
        return false;
    }
    out.write(reinterpret_cast<const char*>(file.data()), static_cast<std::streamsize>(file.size()));
    if (!out) {
        if (err) *err = "failed to write " + ddsPath;
        return false;
    }
    return true;
}

bool encodeMinimapDds(const MinimapImage& img, std::vector<uint8_t>& file, std::string* err) {
    const auto fail = [&](std::string message) {''')
patch(M, '''    std::vector<uint8_t> file(128 + body.size(), 0);
    std::memcpy(file.data(), "DDS ", 4);''', '''    file.assign(128 + body.size(), 0);
    std::memcpy(file.data(), "DDS ", 4);''')
patch(M, '''    std::memcpy(file.data() + 128, body.data(), body.size());

    std::ofstream out(ddsPath, std::ios::binary | std::ios::trunc);
    if (!out) return fail("cannot open " + ddsPath + " for writing");
    out.write(reinterpret_cast<const char*>(file.data()), static_cast<std::streamsize>(file.size()));
    if (!out) return fail("failed to write " + ddsPath);
    return true;
}''', '''    std::memcpy(file.data() + 128, body.data(), body.size());
    return true;
}''')

patch('src/core/hud_images.h', '''// The folder for a map file:''', '''// The picture as the DDS the converter takes for a map's own images: DXT5 with mips, square, a power of two (the
// form of the minimap image). The picture is scaled to a square of the next power of two of its longer side, 4 to
// 1024; drawn with its own width and height by setshader it has its shape again.
bool encodeHudImageDds(const std::string& picturePath, std::vector<uint8_t>& dds, std::string* err);

// The folder for a map file:''')
patch('src/core/hud_images.h', '''#include <string>
#include <vector>
''', '''#include <cstdint>
#include <string>
#include <vector>
''')
H = 'src/core/hud_images.cpp'
patch(H, '''std::string hudImageFolderFor(const std::string& mapPath) {''', '''bool encodeHudImageDds(const std::string& picturePath, std::vector<uint8_t>& dds, std::string* err) {
    int w = 0, h = 0;
    std::vector<uint8_t> rgba;
    if (!decodeImageFile(picturePath, w, h, rgba, err)) return false;
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

std::string hudImageFolderFor(const std::string& mapPath) {''')
patch(H, '''#include <algorithm>
#include <cctype>
''', '''#include <algorithm>
#include <cctype>
#include <cmath>
''')
patch(H, '''#include <system_error>
''', '''#include <system_error>

#include "custom_materials.h"
#include "minimap.h"
''')
patch('src/core/compile_iw8.cpp', '''            if (encodeDdsFromImage(h.path, false, true, dds, &imageErr)) {
                customFiles.push_back({root / "extracted" / "images" / (h.name + ".dds"), std::string(dds.begin(), dds.end())});
                rep.hudImages.push_back(h.name);''', '''            if (encodeHudImageDds(h.path, dds, &imageErr)) {
                customFiles.push_back({root / "extracted" / "images" / (h.name + ".dds"), std::string(dds.begin(), dds.end())});
                rep.hudImages.push_back(h.name);''')
T = 'tests/compile_iw8_tests.cpp'
patch(T, '''        CHECK(f.gcount() == 4 && std::string(magic, 4) == "DDS ");
        CHECK(fs::file_size(images / name) > 16 * 16 * 4);  // 16 x 16 BGRA with its mips''',
      '''        CHECK(f.gcount() == 4 && std::string(magic, 4) == "DDS ");
        // DXT5 with mips, 16 x 16: the form of the minimap image, which the converter's reader takes.
        char header[124] = {};
        f.read(header, 124);
        CHECK(f.gcount() == 124 && std::string(header + 80, 4) == "DXT5" && header[8] == 16 && header[12] == 16 && header[24] == 5);
        CHECK(fs::file_size(images / name) == 128 + 256 + 64 + 16 + 16 + 16);''')
print('ok')
