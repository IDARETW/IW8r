import io

# Viewport: your own materials with a normal map and a specular map draw them in View > Game Lighting, the way the
# build's world shader does (the converter's SourceSunSpecular, style iw3: specular map x lerp(.8, 4, (1 - |N.V|)^2.5)
# x (probe + .625 x lobe x sun^(1/2.2) x shadow), added in gamma space; the normal image read as an IW3 slope map).

ROOT = 'E:/IW8-Radiant/'


def read(p):
    return io.open(ROOT + p, encoding='utf-8', newline='').read()


def write(p, s):
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s)


def patch(p, a, b, n=1):
    s = read(p)
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    write(p, s.replace(a, b))


# ---- batches carry the material's specular map
RH = 'src/viewport/render.h'
patch(RH, '''    // 5.23 wind preview: foliage batches sway in the vertex shader; baseZ is the lowest point of the batch (no sway there).
    bool sway = false;
    float baseZ = 0;
};''', '''    // 5.23 wind preview: foliage batches sway in the vertex shader; baseZ is the lowest point of the batch (no sway there).
    bool sway = false;
    float baseZ = 0;
    // An own material's specular map (RGB reflectance, alpha gloss; createTexture), -1 = none. Game Lighting draws its
    // highlight and reflection as the build's world shader does.
    int specTexture = -1;
};''')
patch(RH, '''    int texture = -1, normalTexture = -1, emissiveTexture = -1;
    int shader = -1;''', '''    int texture = -1, normalTexture = -1, emissiveTexture = -1, specTexture = -1;
    int shader = -1;''')
patch('src/viewport/scene.h', '''    // Emissive map of a material for the textured view (3.17 / 5.24): a texture id, or -1.
    std::function<int(const std::string& material)> emissiveFor;''', '''    // Emissive map of a material for the textured view (3.17 / 5.24): a texture id, or -1.
    std::function<int(const std::string& material)> emissiveFor;
    // Specular map of an own material (TexturedBatch::specTexture): a texture id, or -1.
    std::function<int(const std::string& material)> specFor;''')
CG = 'src/viewport/camera_geometry.cpp'
patch(CG, '''    if (scene->emissiveFor && !scene->captureMode && t.material) b->emissiveTexture = scene->emissiveFor(*t.material);''',
      '''    if (scene->emissiveFor && !scene->captureMode && t.material) b->emissiveTexture = scene->emissiveFor(*t.material);
    if (scene->specFor && !scene->captureMode && t.material) b->specTexture = scene->specFor(*t.material);''')
patch(CG, '''                    nb->emissiveTexture = scene.emissiveFor && !scene.captureMode && t.material ? scene.emissiveFor(*t.material) : -1;''',
      '''                    nb->emissiveTexture = scene.emissiveFor && !scene.captureMode && t.material ? scene.emissiveFor(*t.material) : -1;
                    nb->specTexture = scene.specFor && !scene.captureMode && t.material ? scene.specFor(*t.material) : -1;''')
patch(CG, '''        t.emissiveTexture = b->emissiveTexture;''', '''        t.emissiveTexture = b->emissiveTexture;
        t.specTexture = b->specTexture;''')
patch(CG, '''(uint64_t(bool(scene.textureSizeFor)) << 20));''', '''(uint64_t(bool(scene.textureSizeFor)) << 20) | (uint64_t(bool(scene.specFor)) << 21));''')
patch('src/viewport/scene.cpp', '''                    if (scene.emissiveFor && !scene.captureMode) (*textured)[b->second].emissiveTexture = scene.emissiveFor(g->polyMaterial[i]);''',
      '''                    if (scene.emissiveFor && !scene.captureMode) (*textured)[b->second].emissiveTexture = scene.emissiveFor(g->polyMaterial[i]);
                    if (scene.specFor && !scene.captureMode) (*textured)[b->second].specTexture = scene.specFor(g->polyMaterial[i]);''')

# ---- the renderer binds it and the shader draws it
R = 'src/viewport/render.cpp'
patch(R, '''Texture2D emi0 : register(t3);
''', '''Texture2D emi0 : register(t3);
Texture2D spec0 : register(t29);  // an own material's specular map (batchFlags2.w = 1 on a batch that is not the stock reference)
''')
patch(R, '''            float3 light = indirect * aoOwn + visibility * saturate(dot(N, gameSun.xyz)) * gameSunCol.rgb;
            c = albedo * light;
#endif''', '''            float3 light = indirect * aoOwn + visibility * saturate(dot(N, gameSun.xyz)) * gameSunCol.rgb;
            c = albedo * light;
            if (batchFlags2.w > 0.5) {
                // A material with a specular map, as the build's world shader draws it (the converter's
                // SourceSunSpecular, map_surface_realtime.hlsl): specular map x lerp(.8, 4, (1 - |N.V|)^2.5) x
                // (probe + .625 x lobe x sun^(1/2.2) x shadow), added in gamma space. The lobe is exp((R.L - .99925) x
                // power), power 2^(9.3775 gloss) + 7, widened by the normal's screen variance with its energy kept.
                // The probe: the map's captured one when there is one, else the build's default probe (grey 128).
                const float4 sp = spec0.Sample(samp0, uvS);
                const float3 Vo = normalize(eyeStart.xyz - i.w);
                const float3 Ro = reflect(-Vo, N);
                const float3 ndx = ddx(N), ndy = ddy(N);
                const float variance = min(0.5 * (dot(ndx, ndx) + dot(ndy, ndy)), 0.18);
                const float srcPower = exp2(sp.a * 9.3775177) + 7.0;
                const float power = max(2.0 / (2.0 / (srcPower + 2.0) + variance) - 2.0, 1.0);
                const float lobe = saturate(exp((dot(Ro, gameSun.xyz) - 0.99925) * power)) * (power + 2.0) / (srcPower + 2.0);
                const float response = lerp(0.8, 4.0, pow(saturate(1.0 - abs(dot(Vo, N))), 2.5));
                const float3 probe = batchFlags2.y > 0.5 ? probeCube.SampleLevel(samp0, Ro, clamp(6.0 - 8.0 * sp.a, 0.0, 6.0)).rgb
                                                         : float3(0.50196, 0.50196, 0.50196);
                const float3 sunTerm = 0.625 * lobe * pow(max(gameSunCol.rgb, 0.0), 1.0 / 2.2) * visibility;
                c = pow(pow(max(c, 0.0), 1.0 / 2.2) + sp.rgb * response * (probe + sunTerm), 2.2);
            }
#endif''')
patch(R, '''            ctx_->PSSetShaderResources(3, 1, &esrv);
            if (cbBatch_) {
                const float flags[8] = {hasNormal ? 1.0f : 0.0f, b.blend == 0 && textureRawAlpha(b.texture) ? -2.0f : float(b.blend), b.sway ? 1.0f : 0.0f, b.baseZ,
                                        hasEmissive ? 1.0f : 0.0f, probeSrv_ ? 1.0f : 0.0f, 0, 0};''', '''            ctx_->PSSetShaderResources(3, 1, &esrv);
            const bool hasSpec = game_.on && b.specTexture >= 0 && size_t(b.specTexture) < textures_.size();
            ID3D11ShaderResourceView* ssrv = hasSpec ? textures_[size_t(b.specTexture)] : nullptr;
            ctx_->PSSetShaderResources(29, 1, &ssrv);
            if (cbBatch_) {
                const float flags[8] = {hasNormal ? 1.0f : 0.0f, b.blend == 0 && textureRawAlpha(b.texture) ? -2.0f : float(b.blend), b.sway ? 1.0f : 0.0f, b.baseZ,
                                        hasEmissive ? 1.0f : 0.0f, probeSrv_ ? 1.0f : 0.0f, 0, hasSpec ? 1.0f : 0.0f};''')

# ---- the camera view loads the two images of an own material
CH = 'src/qtapp/camera_view.h'
patch(CH, '''    void setMaterialEmissive(MaskLookup lookup) { emissiveLookup_ = std::move(lookup); }''',
      '''    void setMaterialEmissive(MaskLookup lookup) { emissiveLookup_ = std::move(lookup); }
    // The specular image file of an own material ("" = none): any format the material library reads.
    void setMaterialSpecular(MaskLookup lookup) { specLookup_ = std::move(lookup); }
    // Tests: the texture ids of a material's normal and specular maps once they are loaded (-1 = none, -2 = not yet).
    std::pair<int, int> materialMapIds(const std::string& material);''')
patch(CH, '''    std::unordered_map<std::string, int> emiIds_;''', '''    std::unordered_map<std::string, int> emiIds_;
    MaskLookup specLookup_;
    int specFor(const std::string& material);
    std::unordered_map<std::string, int> specIds_;''')
CC = 'src/qtapp/camera_view.cpp'
patch(CC, '''        ctx_->scene.shaderFor = shaderLookup_ ?''', '''        ctx_->scene.specFor = ctx_->texturedView && specLookup_ && ctx_->normalMaps ? std::function<int(const std::string&)>([this](const std::string& m) { return specFor(m); })
                                                                                      : std::function<int(const std::string&)>();
        ctx_->scene.shaderFor = shaderLookup_ ?''')
patch(CC, '''    for (auto it = emiIds_.begin(); it != emiIds_.end();) {
        if (it->second < 0) it = emiIds_.erase(it);
        else ++it;
    }''', '''    for (auto it = emiIds_.begin(); it != emiIds_.end();) {
        if (it->second < 0) it = emiIds_.erase(it);
        else ++it;
    }
    for (auto it = specIds_.begin(); it != specIds_.end();) {
        if (it->second < 0) it = specIds_.erase(it);
        else ++it;
    }''')
patch(CC, '''        if (a.texture != b.texture || a.normalTexture != b.normalTexture || a.emissiveTexture != b.emissiveTexture || a.blend != b.blend ||''',
      '''        if (a.texture != b.texture || a.normalTexture != b.normalTexture || a.emissiveTexture != b.emissiveTexture || a.specTexture != b.specTexture ||
            a.blend != b.blend ||''')
patch(CC, '''    // Normal maps are plain files: one texture per file (shared between the materials and layers that use it).
    if (const auto it = nrmByPath_.find(path.toStdString()); it != nrmByPath_.end()) return nrmIds_[material] = it->second;
    if (!textureLoadAllowed(loadsThisFrame_)) {
        moreToLoad_ = true;
        return -1;
    }
    ++loadsThisFrame_;''', '''    // Normal maps are plain files: one texture per file (shared between the materials and layers that use it).
    if (const auto it = nrmByPath_.find(path.toStdString()); it != nrmByPath_.end()) return nrmIds_[material] = it->second;
    if (!textureLoadAllowed(loadsThisFrame_)) {
        moreToLoad_ = true;
        return -1;
    }
    ++loadsThisFrame_;
    if (path.startsWith("slope:")) {
        // An own material's normal image. The build reads it as an IW3 slope map (slope x = 4.08 R - 2.08, slope y =
        // 4.0645 G - 2.0645, the converter's world shader); packed here the way the stock normal maps are, which is
        // what the viewport's shaders decode. No gloss in it (that is the specular map's alpha), full occlusion.
        int w = 0, h = 0, id = -1;
        std::vector<uint8_t> px;
        std::string err;
        if (decodeImageFile(path.mid(6).toStdString(), w, h, px, &err)) {
            for (size_t k = 0; k + 3 < px.size(); k += 4) {
                uint8_t g = 0, a = 0;
                replayNormalPack(4.08 * px[k] / 255.0 - 2.08, 4.06451607 * px[k + 1] / 255.0 - 2.06451607, g, a);
                px[k] = 0, px[k + 1] = g, px[k + 2] = 255, px[k + 3] = a;
            }
            id = renderer_.createTexture(px.data(), w, h);
        }
        if (id >= 0) nrmByPath_[path.toStdString()] = id;
        return nrmIds_[material] = id;
    }''')
patch(CC, '''int CameraView::normalFor(const std::string& material) {''', '''int CameraView::specFor(const std::string& material) {
    if (material.empty() || isIw8ToolMaterial(material)) return -1;
    if (auto it = specIds_.find(material); it != specIds_.end()) return it->second;
    // Only once the colour image is up, as the other maps.
    if (auto t = texIds_.find(material); t == texIds_.end() || t->second < 0 || t->second == checkerTex_) return -1;
    const QString path = specLookup_ ? specLookup_(material) : QString();
    if (path.isEmpty()) return specIds_[material] = -1;
    if (!textureLoadAllowed(loadsThisFrame_)) {
        moreToLoad_ = true;
        return -1;
    }
    ++loadsThisFrame_;
    // The image as it is stored: the build's shader reads the specular map's bytes without a colour conversion.
    int w = 0, h = 0, id = -1;
    std::vector<uint8_t> px;
    std::string err;
    if (decodeImageFile(path.toStdString(), w, h, px, &err)) id = renderer_.createTexture(px.data(), w, h);
    return specIds_[material] = id;
}

std::pair<int, int> CameraView::materialMapIds(const std::string& material) {
    textureFor(material);
    loadsThisFrame_ = 0;
    const int n = normalFor(material);
    loadsThisFrame_ = 0;
    const int s = specFor(material);
    return {nrmIds_.count(material) ? n : -2, specIds_.count(material) ? s : -2};
}

int CameraView::normalFor(const std::string& material) {''')
s = read(CC)
if '#include "normal_pack.h"' not in s:
    s = s.replace('#include "custom_materials.h"', '#include "custom_materials.h"\n#include "normal_pack.h"', 1)
write(CC, s)

M = 'src/qtapp/main_window.cpp'
patch(M, '''    camera_->setMaterialNormals([this](const std::string& m) -> QString {''', '''    camera_->setMaterialSpecular([this](const std::string& m) -> QString {
        if (const CustomMaterial* cm = customMaterials_ ? customMaterials_->find(m) : nullptr) return qs(cm->specImage);
        return {};
    });
    camera_->setMaterialNormals([this](const std::string& m) -> QString {
        // An own material's normal image ("slope:<file>": the build reads it as an IW3 slope map).
        if (const CustomMaterial* cm = customMaterials_ ? customMaterials_->find(m) : nullptr)
            return cm->normalImage.empty() ? QString() : "slope:" + qs(cm->normalImage);''')

T = 'src/qtapp/qt_gui_test.cpp'
patch(T, '''        r.check(cm && cm->blend && cm->twoSided && cm->width == 64 && cm->surfaceType > 0,
                "the library reloads with the new material (blend, two-sided, 64 units, glass surface)");''', '''        r.check(cm && cm->blend && cm->twoSided && cm->width == 64 && cm->surfaceType > 0,
                "the library reloads with the new material (blend, two-sided, 64 units, glass surface)");
        {
            // A material with a normal and a specular image: the camera view loads both (Game Lighting draws them).
            QImage nrm(32, 32, QImage::Format_RGBA8888), spc(32, 32, QImage::Format_RGBA8888);
            nrm.fill(QColor(130, 129, 255, 255));  // slopes near 0: a flat normal in the build's slope layout
            spc.fill(QColor(220, 220, 220, 200));
            const QString nsrc = QDir(outDir).absoluteFilePath("shiny_n_src.png"), ssrc = QDir(outDir).absoluteFilePath("shiny_s_src.png");
            nrm.save(nsrc);
            spc.save(ssrc);
            w.editCustomMaterial(QString(), [&](iw8r::qt::MaterialEditorDialog& d) {
                d.setName("custom/test_shiny");
                d.setImage(0, src);
                d.setImage(1, nsrc);
                d.setImage(2, ssrc);
            });
            const iw8r::CustomMaterial* shiny = w.customMaterials() ? w.customMaterials()->find("custom/test_shiny") : nullptr;
            std::pair<int, int> ids{-2, -2}, plain{-2, -2};
            for (int tries = 0; tries < 8 && (ids.first == -2 || ids.second == -2); ++tries) ids = w.cameraView()->materialMapIds("custom/test_shiny");
            for (int tries = 0; tries < 8 && plain.second == -2; ++tries) plain = w.cameraView()->materialMapIds("custom/test_glass");
            r.check(shiny && !shiny->normalImage.empty() && !shiny->specImage.empty() && ids.first >= 0 && ids.second >= 0 && ids.first != ids.second &&
                        plain.first == -1 && plain.second == -1,
                    QString("an own material's normal and specular images are loaded for the camera view (ids %1, %2; a material without them: %3, %4)")
                        .arg(ids.first).arg(ids.second).arg(plain.first).arg(plain.second));
        }''')
print('ok')
