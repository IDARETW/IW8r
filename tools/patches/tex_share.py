import io


def patch(p, a, b):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == 1, (p, s.count(a), a[:70])
    s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


patch('src/qtapp/camera_view.cpp', '''        return texIds_[material] = checkerTex_;
    }
    if (!textureLoadAllowed(loadsThisFrame_)) {
        moreToLoad_ = true;  // keep frames short: the rest loads in the next frames
        return -1;
    }''', '''        return texIds_[material] = checkerTex_;
    }
    // Layer, decal and decal volume images ("\\x01" / "\\x02" / "\\x05" keys) are plain files, never tinted or masked
    // per material: one texture per file (mp_aniyah_tac decoded the same layer images once per material, 37,000
    // textures, and ran out of video memory).
    const bool shareable = material.find_first_of("\\x01\\x02\\x05") != std::string::npos;
    if (shareable)
        if (const auto it = texByPath_.find(path->toStdString()); it != texByPath_.end()) return texIds_[material] = it->second;
    if (!textureLoadAllowed(loadsThisFrame_)) {
        moreToLoad_ = true;  // keep frames short: the rest loads in the next frames
        return -1;
    }''')
patch('src/qtapp/camera_view.cpp', '''    return texIds_[material] = id;''', '''    if (shareable && id >= 0) texByPath_[path->toStdString()] = id;
    return texIds_[material] = id;''')
patch('src/qtapp/camera_view.cpp', '''    if (path.isEmpty()) return nrmIds_[material] = -1;
    if (!textureLoadAllowed(loadsThisFrame_)) {
        moreToLoad_ = true;
        return -1;
    }
    ++loadsThisFrame_;
    const qint64 loadStart = QDateTime::currentMSecsSinceEpoch();
    int id = -1;
    app::Rgba8Image img;
    if (app::loadDdsPreview(path.toStdString(), kViewTextureSide, img) && img.width > 0 && img.height > 0)
        id = renderer_.createTexture(img.pixels.data(), img.width, img.height);
    g_frameLoadMs += QDateTime::currentMSecsSinceEpoch() - loadStart;
    return nrmIds_[material] = id;''', '''    if (path.isEmpty()) return nrmIds_[material] = -1;
    // Normal maps are plain files: one texture per file (shared between the materials and layers that use it).
    if (const auto it = nrmByPath_.find(path.toStdString()); it != nrmByPath_.end()) return nrmIds_[material] = it->second;
    if (!textureLoadAllowed(loadsThisFrame_)) {
        moreToLoad_ = true;
        return -1;
    }
    ++loadsThisFrame_;
    const qint64 loadStart = QDateTime::currentMSecsSinceEpoch();
    int id = -1;
    app::Rgba8Image img;
    // The same size rule as the colour images (they had always loaded at full preview size, over the budget).
    if (app::loadDdsPreview(path.toStdString(), textureSideNow(), img) && img.width > 0 && img.height > 0)
        id = renderer_.createTexture(img.pixels.data(), img.width, img.height);
    g_frameLoadMs += QDateTime::currentMSecsSinceEpoch() - loadStart;
    if (id >= 0) nrmByPath_[path.toStdString()] = id;
    return nrmIds_[material] = id;''')
patch('src/qtapp/camera_view.h', '''    std::unordered_map<std::string, int> texIds_;  // resolved: >= 0 texture, -1 flat colour''', '''    std::unordered_map<std::string, int> texIds_;  // resolved: >= 0 texture, -1 flat colour
    std::unordered_map<std::string, int> texByPath_, nrmByPath_;  // shared images by file (textureFor, normalFor)''')
print('ok')
