import io


def patch(p, a, b, n=1):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


CH = 'src/qtapp/camera_view.h'
patch(CH, '''    int defaultTextureBudgetMB() const { const int v = videoMemoryMB(); return v > 0 ? std::clamp(v * 2 / 5, 1024, 4096) : 1024; }''',
      '''    // 2/5 of the video memory, and no more than 1/8 of the machine's memory: Windows charges video memory to the
    // process's commit, so on a 16 GB machine Verdansk's 3 GB of textures (the 8 GB card's budget) helped run the
    // commit limit out (2026-11-13).
    int defaultTextureBudgetMB() const {
        const int v = videoMemoryMB(), ram = systemMemoryMB();
        int mb = v > 0 ? std::clamp(v * 2 / 5, 1024, 4096) : 1024;
        if (ram > 0) mb = std::min(mb, std::max(1024, ram / 8));
        return mb;
    }
    static int systemMemoryMB();''')
patch(CH, '''    std::unordered_map<std::string, int> texByPath_, nrmByPath_;  // shared images by file (textureFor, normalFor)''',
      '''    std::unordered_map<std::string, int> texByPath_, nrmByPath_;  // shared images by file (textureFor, normalFor)
    std::unordered_map<std::string, int> texByPathBc_;  // untouched stock colour images kept compressed, by file''')

C = 'src/qtapp/camera_view.cpp'
patch(C, '''int CameraView::textureSideNow() const {''', '''int CameraView::systemMemoryMB() {
    MEMORYSTATUSEX ms{};
    ms.dwLength = sizeof(ms);
    return GlobalMemoryStatusEx(&ms) ? int(ms.ullTotalPhys / (1024 * 1024)) : 0;
}

int CameraView::textureSideNow() const {''')
patch(C, '''    if (path->endsWith(".dds", Qt::CaseInsensitive) && colourImageKeptCompressed(material)) {
        // A stock image nothing edits (no cut-out mask, not blended, no tint): uploaded block-compressed as stored,
        // its alpha the specular code (Verdansk with its streamed tiles ran past the texture budget decoded to RGBA).
        app::DdsBlockLevels blocks;''', '''    if (path->endsWith(".dds", Qt::CaseInsensitive) && colourImageKeptCompressed(material)) {
        // A stock image nothing edits (no cut-out mask, not blended, no tint): uploaded block-compressed as stored,
        // its alpha the specular code (Verdansk with its streamed tiles ran past the texture budget decoded to RGBA).
        // Nothing per material changes it, so materials that use the same file share one texture.
        if (const auto it = texByPathBc_.find(path->toStdString()); it != texByPathBc_.end()) {
            --loadsThisFrame_;
            return texIds_[material] = it->second;
        }
        app::DdsBlockLevels blocks;''')
patch(C, '''                if (shareable) texByPath_[path->toStdString()] = id;
                return texIds_[material] = id;''', '''                if (shareable) texByPath_[path->toStdString()] = id;
                texByPathBc_[path->toStdString()] = id;
                return texIds_[material] = id;''')
# emissive: the budget's size rule, compressed when it can be
patch(C, '''    int id = -1;
    app::Rgba8Image img;
    if (app::loadDdsPreview(path.toStdString(), kViewTextureSide, img) && img.width > 0 && img.height > 0)
        id = renderer_.createTexture(img.pixels.data(), img.width, img.height);
    return emiIds_[material] = id;''', '''    int id = -1;
    app::Rgba8Image img;
    // The texture budget's size rule as the other images (emissive images always loaded at full preview size).
    app::DdsBlockLevels blocks;
    if (!qEnvironmentVariableIsSet("IW8R_NO_BC_TEXTURES") && app::loadDdsBlocks(path.toStdString(), textureSideNow(), blocks))
        id = renderer_.createTextureBlocks(blocks.dxgiFormat, blocks.width, blocks.height, blocks.levels);
    if (id < 0 && app::loadDdsPreview(path.toStdString(), textureSideNow(), img) && img.width > 0 && img.height > 0)
        id = renderer_.createTexture(img.pixels.data(), img.width, img.height);
    return emiIds_[material] = id;''')
print('ok')
