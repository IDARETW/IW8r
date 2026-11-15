import io

# Research 138 section 6: lit effect sets write alpha a x (1 - absorption x transmittance); on the smoke and mist sets
# slot 0 is BC1 (punch-through alpha only) and slot 68 a one-channel BC4 sheet of the same name. The sprite's alpha is
# the colour image's alpha x slot 68's red (INFERRED: the density), or slot 27's opacity when the set has one.

ROOT = 'E:/IW8-Radiant/'


def patch(p, a, b, count=1):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    if s.count(a) == 0 and b in s:
        return
    assert s.count(a) == count, (p, s.count(a), a[:70])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


CH = 'src/qtapp/camera_view.h'
patch(CH, '''        std::string colorFile, alphaFile, emissiveFile;''', '''        std::string colorFile, alphaFile, emissiveFile;
        std::string densityFile;    // slot 68 (one channel): x the colour's alpha (lit sets without slot 27)''')
MW = 'src/qtapp/main_window.cpp'
patch(MW, '''        pm.emissiveFile = e->emissiveDds.toStdString();''', '''        pm.emissiveFile = e->emissiveDds.toStdString();
        pm.densityFile = e->transmissionDds.toStdString();''')
CC = 'src/qtapp/camera_view.cpp'
patch(CC, '''    const bool withMask = pm.lit && !pm.alphaFile.empty();
    if (withMask && !decodedTexture(pm.alphaFile, kViewTextureSide, 1, m)) {
        moreToLoad_ = true;
        return -1;
    }''', '''    const bool withMask = pm.lit && !pm.alphaFile.empty();
    if (withMask && !decodedTexture(pm.alphaFile, kViewTextureSide, 1, m)) {
        moreToLoad_ = true;
        return -1;
    }
    DecodedTexture dn;
    const bool withDensity = pm.lit && !withMask && !pm.densityFile.empty();
    if (withDensity && !decodedTexture(pm.densityFile, kViewTextureSide, 1, dn)) {
        moreToLoad_ = true;
        return -1;
    }''')
patch(CC, '''    } else if (!pm.lit) {
        for (size_t k = 3; k < img.pixels.size(); k += 4) img.pixels[k] = 255;
    }''', '''    } else if (withDensity && dn.imageOk && dn.image.width > 0 && dn.image.height > 0) {
        for (int y = 0; y < img.height; ++y)
            for (int x = 0; x < img.width; ++x) {
                const int mx = int(int64_t(x) * dn.image.width / img.width), my = int(int64_t(y) * dn.image.height / img.height);
                uint8_t& a = img.pixels[(size_t(y) * size_t(img.width) + size_t(x)) * 4 + 3];
                a = uint8_t((int(a) * int(dn.image.pixels[(size_t(my) * size_t(dn.image.width) + size_t(mx)) * 4]) + 127) / 255);
            }
    } else if (!pm.lit) {
        for (size_t k = 3; k < img.pixels.size(); k += 4) img.pixels[k] = 255;
    }''')
print('ok')
