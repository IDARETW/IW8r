import io

# Slot-126 layer masks are stored sRGB (188 BC1_UNORM_SRGB, 47 R8G8B8A8_UNORM_SRGB of the cached ones): the game samples
# them through that format, so the GPU hands the shader linear values. The editor uploaded every image as UNORM and read
# the raw bytes, which made masked layers (vehicle dirt, painted parts) too strong. Data masks now load through a
# "\x07<image>" key, whose file's sRGB format is kept (blocks: the _SRGB format; RGBA: linearised bytes).

ROOT = 'E:/IW8-Radiant/'


def patch(p, a, b, count=1):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    if s.count(a) == 0 and b in s:
        return  # already applied
    assert s.count(a) == count, (p, s.count(a), a[:70])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


TH = 'src/viewport/texture_decode.h'
patch(TH, '''bool loadDdsPreview(const std::string& path, int maxSide, Rgba8Image& out, int* fullWidth = nullptr,
                    int* fullHeight = nullptr, std::string* err = nullptr);
''', '''bool loadDdsPreview(const std::string& path, int maxSide, Rgba8Image& out, int* fullWidth = nullptr,
                    int* fullHeight = nullptr, std::string* err = nullptr);

// True when the DDS at `path` (UTF-8) has a DX10 header with an _SRGB format (29, 72, 75, 78, 91, 93, 99): the game
// samples it through that format, so its shaders see linear values.
bool ddsIsSrgb(const std::string& path);
// The same test on header bytes in memory (at least 148 bytes; false otherwise).
bool ddsHeaderIsSrgb(const std::string& headerBytes);
''')
TC = 'src/viewport/texture_decode.cpp'
patch(TC, '''bool loadDdsBlocks(const std::string& path, int maxSide, DdsBlockLevels& out, int* fullWidth, int* fullHeight, std::string* err) {''',
      '''bool ddsHeaderIsSrgb(const std::string& f) {
    if (f.size() < 148 || f.compare(0, 4, "DDS ") != 0 || std::memcmp(f.data() + 84, "DX10", 4) != 0) return false;
    switch (readU32(f, 128)) {
        case 29: case 72: case 75: case 78: case 91: case 93: case 99: return true;
        default: return false;
    }
}

bool ddsIsSrgb(const std::string& path) {
    const std::filesystem::path file(std::u8string(reinterpret_cast<const char8_t*>(path.data()), path.size()));
    std::ifstream in(file, std::ios::binary);
    if (!in) return false;
    std::string header(148, '\\0');
    in.read(header.data(), 148);
    return in.gcount() == 148 && ddsHeaderIsSrgb(header);
}

bool loadDdsBlocks(const std::string& path, int maxSide, DdsBlockLevels& out, int* fullWidth, int* fullHeight, std::string* err) {''')

R = 'src/viewport/render.cpp'
patch(R, '''    const UINT blockBytes = dxgiFormat == DXGI_FORMAT_BC1_UNORM ? 8 : 16;''',
      '''    const UINT blockBytes = dxgiFormat == DXGI_FORMAT_BC1_UNORM || dxgiFormat == DXGI_FORMAT_BC1_UNORM_SRGB ? 8 : 16;''')

MW = 'src/qtapp/main_window.cpp'
patch(MW, '''            // A stock decal volume image ("\\x05<image>": colour or mask, research 99): any catalogued image.
            if (!m.empty() && m[0] == '\\x05') {''', '''            // A stock decal volume image ("\\x05<image>": colour or mask, research 99): any catalogued image. "\\x07<image>":
            // a data mask (a model layer's slot 126 / tint mask), the same file, read through its own sRGB format.
            if (!m.empty() && (m[0] == '\\x05' || m[0] == '\\x07')) {''')
patch(MW, '''            if (!m.empty() && (m[0] == '\\x05' || m[0] == '\\x06')) return;  // requested by the lookup / a file''',
      '''            if (!m.empty() && (m[0] == '\\x05' || m[0] == '\\x06' || m[0] == '\\x07')) return;  // requested by the lookup / a file''')

CC = 'src/qtapp/camera_view.cpp'
patch(CC, '''m.find_first_of("\\x01\\x02\\x05\\x06") != std::string::npos) return std::nullopt;''',
      '''m.find_first_of("\\x01\\x02\\x05\\x06\\x07") != std::string::npos) return std::nullopt;''')
patch(CC, '''m.find_first_of("\\x01\\x02\\x05\\x06") != std::string::npos) return true;''',
      '''m.find_first_of("\\x01\\x02\\x05\\x06\\x07") != std::string::npos) return true;''')
patch(CC, '''base.find_first_of("\\x01\\x02\\x05\\x06") != std::string::npos) return true;''',
      '''base.find_first_of("\\x01\\x02\\x05\\x06\\x07") != std::string::npos) return true;''')
patch(CC, '''        const std::string mk = "\\x05" + facts->layerMaskImage;''',
      '''        const std::string mk = "\\x07" + facts->layerMaskImage;  // stored sRGB: read linear, as the game's sampler does''')
patch(CC, '''            const std::string tk = "\\x05" + facts->layerTintMaskImage[size_t(L)];''',
      '''            const std::string tk = "\\x07" + facts->layerTintMaskImage[size_t(L)];''')
patch(CC, '''    const bool shareable = material.find_first_of("\\x01\\x02\\x05\\x06") != std::string::npos;
    if (shareable)
        if (const auto it = texByPath_.find(path->toStdString()); it != texByPath_.end()) return texIds_[material] = it->second;''',
      '''    const bool shareable = material.find_first_of("\\x01\\x02\\x05\\x06\\x07") != std::string::npos;
    // A data mask ("\\x07") stored in an sRGB format: the game's sampler linearises it, so the upload does too. Shared
    // under its own path key (the same file as a colour image stays as stored).
    const bool srgbData = material[0] == '\\x07' && path->endsWith(".dds", Qt::CaseInsensitive) && app::ddsIsSrgb(path->toStdString());
    const std::string pathKey = (srgbData ? "srgb:" : "") + path->toStdString();
    if (shareable)
        if (const auto it = texByPath_.find(pathKey); it != texByPath_.end()) return texIds_[material] = it->second;''')
patch(CC, '''        if (const auto it = texByPathBc_.find(path->toStdString()); it != texByPathBc_.end()) {''',
      '''        if (const auto it = texByPathBc_.find(pathKey); it != texByPathBc_.end()) {''')
patch(CC, '''            id = renderer_.createTextureBlocks(blocks.dxgiFormat, blocks.width, blocks.height, blocks.levels);
            if (id >= 0) {
                texSizes_[material] = {fullW, fullH};
                if (textureSideNow() < kViewTextureSide) ++budgetReduced_;
                g_frameLoadMs += QDateTime::currentMSecsSinceEpoch() - loadStart;
                if (shareable) texByPath_[path->toStdString()] = id;
                texByPathBc_[path->toStdString()] = id;''',
      '''            // BC1 / BC2 / BC3 / BC7 UNORM + 1 = the _SRGB format.
            id = renderer_.createTextureBlocks(blocks.dxgiFormat + (srgbData ? 1 : 0), blocks.width, blocks.height, blocks.levels);
            if (id >= 0) {
                texSizes_[material] = {fullW, fullH};
                if (textureSideNow() < kViewTextureSide) ++budgetReduced_;
                g_frameLoadMs += QDateTime::currentMSecsSinceEpoch() - loadStart;
                if (shareable) texByPath_[pathKey] = id;
                texByPathBc_[pathKey] = id;''')
patch(CC, '''                    img.pixels[(size_t(y) * size_t(img.width) + size_t(x)) * 4 + 3] = a;
                }
        }
    } else {''', '''                    img.pixels[(size_t(y) * size_t(img.width) + size_t(x)) * 4 + 3] = a;
                }
            if (srgbData) {  // a data mask stored sRGB: the linear values the game's sampler returns
                static const auto lin = [] {
                    std::array<uint8_t, 256> t{};
                    for (int v = 0; v < 256; ++v) {
                        const double s = v / 255.0, l = s <= 0.04045 ? s / 12.92 : std::pow((s + 0.055) / 1.055, 2.4);
                        t[size_t(v)] = uint8_t(std::lround(l * 255.0));
                    }
                    return t;
                }();
                for (size_t k = 0; k + 3 < img.pixels.size(); k += 4)
                    for (int c = 0; c < 3; ++c) img.pixels[k + size_t(c)] = lin[img.pixels[k + size_t(c)]];
            }
        }
    } else {''')
print('ok')
patch(CC, '''    if (shareable && id >= 0) texByPath_[path->toStdString()] = id;''',
      '''    if (shareable && id >= 0) texByPath_[pathKey] = id;''')
print('ok2')
T = 'tests/texture_decode_tests.cpp'
patch(T, '''TEST(dds_rgba8_roundtrip) {''', '''TEST(dds_srgb_formats_detected) {
    // Slot-126 layer masks are BC1_UNORM_SRGB (72) or R8G8B8A8_UNORM_SRGB (29): the game reads them linear.
    const std::string payload(8, '\\0');
    CHECK(ddsHeaderIsSrgb(makeDds(4, 4, "DX10", 72, payload)));
    CHECK(ddsHeaderIsSrgb(makeDds(4, 4, "DX10", 29, payload)));
    CHECK(ddsHeaderIsSrgb(makeDds(4, 4, "DX10", 99, payload)));
    CHECK(!ddsHeaderIsSrgb(makeDds(4, 4, "DX10", 71, payload)));  // BC1_UNORM
    CHECK(!ddsHeaderIsSrgb(makeDds(4, 4, "DX10", 80, payload)));  // BC4_UNORM (tint masks)
    CHECK(!ddsHeaderIsSrgb(makeDds(4, 4, "DXT1", 0, payload)));   // legacy header: no format to tell
    CHECK(!ddsHeaderIsSrgb(std::string(100, '\\0')));
}

TEST(dds_rgba8_roundtrip) {''')
print('ok3')
