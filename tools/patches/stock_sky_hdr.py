import io

# Research 132 G10: the stock sky in Game Lighting keeps its HDR range. The sky converter's faces were 8-bit sRGB with
# the 99.5th percentile at 0.9 and everything brighter (the sun's surroundings, cloud edges) clipped; the veil around
# the sun came out weaker than in game, and 256 texels were soft. For a stock sky in Game Lighting the faces are now
# RGBM (colour / multiplier in alpha, range 16) at 512 texels; the shader's sky branch multiplies by the alpha and the
# camera folds the range into the sky scale. Other views and own maps (whose build writes the 8-bit cube) unchanged.

ROOT = 'E:/IW8-Radiant/'


def patch(p, a, b, count=1):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == count, (p, s.count(a), a[:70])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


patch('src/core/sky_image.h', '''    float rotation[3] = {0, 0, 0};
};''', '''    float rotation[3] = {0, 0, 0};
    // HDR sources: RGBM faces for the editor's Game Lighting (research 132 G10): the colour times the exposure divided
    // by a multiplier M (stored in alpha as M / rgbmRange, rounded up to 1/255), sRGB-encoded; decode
    // srgbToLinear(rgb) * alpha * rgbmRange. Nothing clips below rgbmRange. Not for the converter (its cube is 8-bit).
    bool rgbm = false;
    float rgbmRange = 16.0f;
};''')
patch('src/core/sky_image.h', '''    float meanLuminance = 0.0f, highLuminance = 0.0f;  // source linear luminance: mean and 99.5th percentile''',
      '''    float meanLuminance = 0.0f, highLuminance = 0.0f;  // source linear luminance: mean and 99.5th percentile
    bool rgbm = false;  // the faces are RGBM (SkyConvertOptions::rgbm on an HDR source)''')
C = 'src/core/sky_image.cpp'
patch(C, '''                uint8_t bytes[3];
                for (int c = 0; c < 3; ++c) {
                    const float s = img.hdr ? linearToSrgb(std::clamp(rgb[c] * exposure, 0.0f, 1.0f)) : std::clamp(rgb[c], 0.0f, 1.0f);
                    bytes[c] = uint8_t(std::lround(s * 255.0f));
                }
                cube.push_back(char(bytes[2]));  // B
                cube.push_back(char(bytes[1]));  // G
                cube.push_back(char(bytes[0]));  // R
                cube.push_back(char(0xFF));''', '''                uint8_t bytes[3];
                uint8_t alpha = 0xFF;
                if (img.hdr && opt.rgbm) {
                    // RGBM: the brightest channel sets the multiplier; the rest is the colour at that scale.
                    const float range = std::max(opt.rgbmRange, 1.0f);
                    float v[3];
                    for (int c = 0; c < 3; ++c) v[c] = std::max(rgb[c] * exposure, 0.0f);
                    const float mx = std::max({v[0], v[1], v[2]});
                    const int m8 = std::clamp(int(std::ceil(mx / range * 255.0f)), 1, 255);
                    const float m = float(m8) / 255.0f * range;
                    for (int c = 0; c < 3; ++c) bytes[c] = uint8_t(std::lround(linearToSrgb(std::clamp(v[c] / m, 0.0f, 1.0f)) * 255.0f));
                    alpha = uint8_t(m8);
                } else {
                    for (int c = 0; c < 3; ++c) {
                        const float s = img.hdr ? linearToSrgb(std::clamp(rgb[c] * exposure, 0.0f, 1.0f)) : std::clamp(rgb[c], 0.0f, 1.0f);
                        bytes[c] = uint8_t(std::lround(s * 255.0f));
                    }
                }
                cube.push_back(char(bytes[2]));  // B
                cube.push_back(char(bytes[1]));  // G
                cube.push_back(char(bytes[0]));  // R
                cube.push_back(char(alpha));''')
patch(C, '''    inf.exposure = exposure;''', '''    inf.exposure = exposure;
    inf.rgbm = img.hdr && opt.rgbm;''')

# renderer: the raw-alpha flag for a texture created from pixels
patch('src/viewport/render.h', '''    bool textureRawAlpha(int id) const { return id >= 0 && size_t(id) < texRawAlpha_.size() && texRawAlpha_[size_t(id)]; }''',
      '''    bool textureRawAlpha(int id) const { return id >= 0 && size_t(id) < texRawAlpha_.size() && texRawAlpha_[size_t(id)]; }
    // A texture whose alpha is data (an RGBM sky face): never cut out by it.
    void setTextureRawAlpha(int id, bool on) {
        if (id >= 0 && size_t(id) < texRawAlpha_.size()) texRawAlpha_[size_t(id)] = on ? 1 : 0;
    }''')
patch('src/viewport/render.cpp', '''            const float3 sky = gameFogApply(albedo * gameSkyScale.x, i.w, true);''',
      '''            // t.a: an RGBM face's multiplier (research 132 G10; 1 on an 8-bit face), the range folded into the scale.
            const float3 sky = gameFogApply(albedo * t.a * gameSkyScale.x, i.w, true);''')

# camera: RGBM faces for a stock sky in Game Lighting
CV = 'src/qtapp/camera_view.cpp'
patch(CV, '''        const QString skyKey = sky.isEmpty() ? QString() : sky + QString("|%1 %2 %3|%4").arg(skyRot[0]).arg(skyRot[1]).arg(skyRot[2]).arg(skyBrightness);''',
      '''        // A stock sky in Game Lighting keeps its HDR range (RGBM faces, research 132 G10); the other views keep 8 bits.
        const bool skyHdr = gameStockLook() && gameLookOn();
        const QString skyKey = sky.isEmpty() ? QString()
                                             : sky + QString("|%1 %2 %3|%4|%5").arg(skyRot[0]).arg(skyRot[1]).arg(skyRot[2]).arg(skyBrightness).arg(skyHdr);''')
patch(CV, '''            SkyConvertOptions so;
            so.faceSize = 256;''', '''            SkyConvertOptions so;
            so.faceSize = skyHdr ? 512 : 256;
            so.rgbm = skyHdr;''')
patch(CV, '''                skyExposure_ = skyInfo.hdr ? skyInfo.exposure : 0.0f;''', '''                skyExposure_ = skyInfo.hdr ? skyInfo.exposure : 0.0f;
                skyRgbmRange_ = skyInfo.rgbm ? so.rgbmRange : 1.0f;''')
patch(CV, '''                            rgba[k + 3] = 255;
                        }
                        skyTex_.push_back(renderer_.createTexture(rgba.data(), fs, fs));''', '''                            rgba[k + 3] = skyInfo.rgbm ? src[k + 3] : 255;  // RGBM: the multiplier
                        }
                        skyTex_.push_back(renderer_.createTexture(rgba.data(), fs, fs));
                        if (skyInfo.rgbm) renderer_.setTextureRawAlpha(skyTex_.back(), true);''')
patch(CV, '''        if (stock && skyExposure_ > 0) g.skyScale = float((stockSkyIntensity_ > 0 ? stockSkyIntensity_ : 1.0) / skyExposure_);''',
      '''        if (stock && skyExposure_ > 0)
            g.skyScale = float((stockSkyIntensity_ > 0 ? stockSkyIntensity_ : 1.0) / skyExposure_ * skyRgbmRange_);  // RGBM: x range''')
patch('src/qtapp/camera_view.h', '''    QElapsedTimer maskClock_;''', '''    QElapsedTimer maskClock_;
    float skyRgbmRange_ = 1.0f;  // the sky faces are RGBM with this range (research 132 G10); 1 = 8-bit faces''')
print('ok')
