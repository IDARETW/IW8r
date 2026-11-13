import io


def patch(p, a, b, n=1):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    if b and s.count(b) >= 1:
        return  # already applied
    assert s.count(a) == n, (p, s.count(a), a[:80])
    s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


H = 'src/viewport/render.h'
R = 'src/viewport/render.cpp'
C = 'src/qtapp/camera_view.cpp'

# ---- the props' constants: one upload routine for the shading pass, the pre-pass and the shadow pass
patch(H, '''    bool createAoResources();''', '''    bool createAoResources();
    // Uploads the textured prop meshes' constants (setReferenceInstParams; raw-alpha textures marked) when changed;
    // true when per-mesh offsets can be bound (cbInstAll_).
    bool uploadInstParams();''')
patch(R, '''    bool instCb = false;
    constexpr size_t kInstPerCb = 256;
    if (propNormals && uploadLayerParams() /* ctx1_ */ && ctx1_) {
        if (instParamsDirty_ || cbInstAllMeshes_ != refTexInst_.size()) {
            instParamsDirty_ = false;
            if (cbInstAllMeshes_ != refTexInst_.size()) {
                for (auto*& b : cbInstAll_) safeRelease(b);
                cbInstAll_.assign((refTexInst_.size() + kInstPerCb - 1) / kInstPerCb, nullptr);
                cbInstAllMeshes_ = refTexInst_.size();
                D3D11_BUFFER_DESC bd{};
                bd.ByteWidth = UINT(kInstPerCb * 256);
                bd.Usage = D3D11_USAGE_DEFAULT;
                bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
                for (auto*& b : cbInstAll_)
                    if (FAILED(dev_->CreateBuffer(&bd, nullptr, &b))) cbInstAllMeshes_ = 0;
            }
            std::vector<float> all(kInstPerCb * 64, 0.0f);
            for (size_t c = 0; c < cbInstAll_.size() && cbInstAllMeshes_; ++c) {
                std::fill(all.begin(), all.end(), 0.0f);
                for (size_t j = 0; j < kInstPerCb && c * kInstPerCb + j < refTexInst_.size(); ++j) {
                    const GpuTexInstanced& gi = refTexInst_[c * kInstPerCb + j];
                    std::memcpy(&all[j * 64], gi.params, sizeof(gi.params));
                    if (textureRawAlpha(gi.texture)) all[j * 64 + 1] = 2.0f;  // opaque: alpha is the specular code
                }
                if (cbInstAll_[c]) ctx_->UpdateSubresource(cbInstAll_[c], 0, nullptr, all.data(), 0, 0);
            }
        }
        instCb = cbInstAllMeshes_ == refTexInst_.size() && !cbInstAll_.empty();
    }
    if (cbBatch_) {''', '''    constexpr size_t kInstPerCb = 256;
    // The props' constants: on the shading pass and the depth pre-pass (raw-alpha meshes are not cut out).
    const bool instCb = (propNormals || (texPsOverride_ && texPsOverride_ == psPrepass_)) && uploadInstParams();
    if (cbBatch_) {''')
patch(R, '''void SceneRenderer::drawReferenceTexInstancesShadow() {''', '''bool SceneRenderer::uploadInstParams() {
    constexpr size_t kInstPerCb = 256;
    if (!uploadLayerParams() || !ctx1_) return false;  // ctx1_: constant buffer offsets
    if (instParamsDirty_ || cbInstAllMeshes_ != refTexInst_.size()) {
        instParamsDirty_ = false;
        if (cbInstAllMeshes_ != refTexInst_.size()) {
            for (auto*& b : cbInstAll_) safeRelease(b);
            cbInstAll_.assign((refTexInst_.size() + kInstPerCb - 1) / kInstPerCb, nullptr);
            cbInstAllMeshes_ = refTexInst_.size();
            D3D11_BUFFER_DESC bd{};
            bd.ByteWidth = UINT(kInstPerCb * 256);
            bd.Usage = D3D11_USAGE_DEFAULT;
            bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            for (auto*& b : cbInstAll_)
                if (FAILED(dev_->CreateBuffer(&bd, nullptr, &b))) cbInstAllMeshes_ = 0;
        }
        std::vector<float> all(kInstPerCb * 64, 0.0f);
        for (size_t c = 0; c < cbInstAll_.size() && cbInstAllMeshes_; ++c) {
            std::fill(all.begin(), all.end(), 0.0f);
            for (size_t j = 0; j < kInstPerCb && c * kInstPerCb + j < refTexInst_.size(); ++j) {
                const GpuTexInstanced& gi = refTexInst_[c * kInstPerCb + j];
                std::memcpy(&all[j * 64], gi.params, sizeof(gi.params));
                if (textureRawAlpha(gi.texture)) all[j * 64 + 1] = 2.0f;  // opaque: alpha is the specular code
            }
            if (cbInstAll_[c]) ctx_->UpdateSubresource(cbInstAll_[c], 0, nullptr, all.data(), 0, 0);
        }
    }
    return cbInstAllMeshes_ == refTexInst_.size() && !cbInstAll_.empty();
}

void SceneRenderer::drawReferenceTexInstancesShadow() {''')
# per-mesh constants on every pass that draws them (not only with the normal maps)
patch(R, '''        if (propNormals) {
            const int n = g.normal >= 0 && size_t(g.normal) < textures_.size() ? g.normal : flatNrmTex_;
            ctx_->PSSetShaderResources(1, 1, &textures_[size_t(n)]);
            const size_t mi = size_t(&g - refTexInst_.data());
            if (instCb && cbInstAll_[mi / kInstPerCb]) {
                const UINT first = UINT((mi % kInstPerCb) * 16), num = 16;
                ctx1_->PSSetConstantBuffers1(7, 1, &cbInstAll_[mi / kInstPerCb], &first, &num);
            }
        }''', '''        if (propNormals) {
            const int n = g.normal >= 0 && size_t(g.normal) < textures_.size() ? g.normal : flatNrmTex_;
            ctx_->PSSetShaderResources(1, 1, &textures_[size_t(n)]);
        }
        if (instCb) {
            const size_t mi = size_t(&g - refTexInst_.data());
            if (cbInstAll_[mi / kInstPerCb]) {
                const UINT first = UINT((mi % kInstPerCb) * 16), num = 16;
                ctx1_->PSSetConstantBuffers1(7, 1, &cbInstAll_[mi / kInstPerCb], &first, &num);
            }
        }''')
# the shadow pass: each prop mesh's constants (raw-alpha = no cut-out), the batch flags zero
patch(R, '''    ctx_->PSSetShader(psShadowClip_, nullptr, 0);  // none: solid
    if (psShadowClip_ && sampler_) ctx_->PSSetSamplers(0, 1, &sampler_);
    const UINT strides[2] = {UINT(sizeof(GpuTexVertex)), UINT(12 * sizeof(float))}, offsets[2] = {0, 0};
    for (const auto& g : refTexInst_) {
        if (!g.vb || !g.inst) continue;
        if (psShadowClip_) {
            const int id = g.texture >= 0 ? g.texture : whiteTex_;
            if (id < 0 || size_t(id) >= textures_.size()) continue;
            ctx_->PSSetShaderResources(0, 1, &textures_[size_t(id)]);
        }''', '''    ctx_->PSSetShader(psShadowClip_, nullptr, 0);  // none: solid
    if (psShadowClip_ && sampler_) ctx_->PSSetSamplers(0, 1, &sampler_);
    if (cbBatch_) {
        const float zero[8] = {0, 0, 0, 0, 0, 0, 0, 0};
        ctx_->UpdateSubresource(cbBatch_, 0, nullptr, zero, 0, 0);
        ctx_->PSSetConstantBuffers(1, 1, &cbBatch_);
    }
    const bool instCb = psShadowClip_ && uploadInstParams();
    const UINT strides[2] = {UINT(sizeof(GpuTexVertex)), UINT(12 * sizeof(float))}, offsets[2] = {0, 0};
    for (const auto& g : refTexInst_) {
        if (!g.vb || !g.inst) continue;
        if (psShadowClip_) {
            const int id = g.texture >= 0 ? g.texture : whiteTex_;
            if (id < 0 || size_t(id) >= textures_.size()) continue;
            ctx_->PSSetShaderResources(0, 1, &textures_[size_t(id)]);
        }
        if (instCb) {
            const size_t mi = size_t(&g - refTexInst_.data());
            if (cbInstAll_[mi / 256]) {
                const UINT first = UINT((mi % 256) * 16), num = 16;
                ctx1_->PSSetConstantBuffers1(7, 1, &cbInstAll_[mi / 256], &first, &num);
            }
        }''')
patch(R, '''    ID3D11Buffer* none[2] = {nullptr, nullptr};
    ctx_->IASetVertexBuffers(0, 2, none, strides, offsets);
    ID3D11ShaderResourceView* noTex[1] = {nullptr};
    ctx_->PSSetShaderResources(0, 1, noTex);
    ctx_->IASetInputLayout(layout_);
    ctx_->VSSetShader(vs_, nullptr, 0);
    ctx_->PSSetShader(nullptr, nullptr, 0);
}''', '''    ID3D11Buffer* none[2] = {nullptr, nullptr};
    ctx_->IASetVertexBuffers(0, 2, none, strides, offsets);
    ID3D11ShaderResourceView* noTex[1] = {nullptr};
    ctx_->PSSetShaderResources(0, 1, noTex);
    if (instCb && cbLayer_) ctx_->PSSetConstantBuffers(7, 1, &cbLayer_);
    ctx_->IASetInputLayout(layout_);
    ctx_->VSSetShader(vs_, nullptr, 0);
    ctx_->PSSetShader(nullptr, nullptr, 0);
}''')

# ---- camera: stock colour and normal images uploaded compressed when nothing edits their pixels
patch(C, '''    if (path->endsWith(".dds", Qt::CaseInsensitive)) {
        // Only the mip level the viewport texture needs is read and decoded: the top level of a 2048x2048 stock
        // image is 16 MB of RGBA and cost 100 ms and more here, to be shrunk to 512 right after.
        decoded = app::loadDdsPreview(path->toStdString(), textureSideNow(), img, &fullW, &fullH);''', '''    if (path->endsWith(".dds", Qt::CaseInsensitive) && colourImageKeptCompressed(material)) {
        // A stock image nothing edits (no cut-out mask, not blended, no tint): uploaded block-compressed as stored,
        // its alpha the specular code (Verdansk with its streamed tiles ran past the texture budget decoded to RGBA).
        app::DdsBlockLevels blocks;
        if (app::loadDdsBlocks(path->toStdString(), textureSideNow(), blocks, &fullW, &fullH)) {
            id = renderer_.createTextureBlocks(blocks.dxgiFormat, blocks.width, blocks.height, blocks.levels);
            if (id >= 0) {
                texSizes_[material] = {fullW, fullH};
                if (textureSideNow() < kViewTextureSide) ++budgetReduced_;
                g_frameLoadMs += QDateTime::currentMSecsSinceEpoch() - loadStart;
                if (shareable) texByPath_[path->toStdString()] = id;
                return texIds_[material] = id;
            }
        }
    }
    if (path->endsWith(".dds", Qt::CaseInsensitive)) {
        // Only the mip level the viewport texture needs is read and decoded: the top level of a 2048x2048 stock
        // image is 16 MB of RGBA and cost 100 ms and more here, to be shrunk to 512 right after.
        decoded = app::loadDdsPreview(path->toStdString(), textureSideNow(), img, &fullW, &fullH);''')
patch(C, '''int CameraView::textureFor(const std::string& material) {''', '''// A stock colour image no step edits: no cut-out mask, not a blended material, no tint (catalogue or the material's own).
bool CameraView::colourImageKeptCompressed(const std::string& material) const {
    static const bool off = qEnvironmentVariableIsSet("IW8R_NO_BC_TEXTURES");  // A/B switch
    if (off) return false;
    if (maskLookup_ && !maskLookup_(material).isEmpty()) return false;
    if (ctx_ && ctx_->scene.blendFor && ctx_->scene.blendFor(material) != 0) return false;
    if (MaterialTints::shared(QCoreApplication::applicationDirPath().toStdString()).find(material)) return false;
    if (factsLookup_ && material.find_first_of("\\x01\\x02") == std::string::npos)
        if (const auto f = factsLookup_(material); f && f->tint >= 0) return false;
    return true;
}

int CameraView::textureFor(const std::string& material) {''')
patch('src/qtapp/camera_view.h', '''    void setBaseMaterialConstants(size_t referenceBatch);''', '''    void setBaseMaterialConstants(size_t referenceBatch);
    bool colourImageKeptCompressed(const std::string& material) const;''')
patch(C, '''    // The same size rule as the colour images (they had always loaded at full preview size, over the budget).
    if (app::loadDdsPreview(path.toStdString(), textureSideNow(), img) && img.width > 0 && img.height > 0)
        id = renderer_.createTexture(img.pixels.data(), img.width, img.height);''', '''    // The same size rule as the colour images (they had always loaded at full preview size, over the budget). BC1 /
    // BC3 / BC7 normal maps go up compressed (every channel as stored); BC5 and the rest are decoded.
    app::DdsBlockLevels blocks;
    if (!qEnvironmentVariableIsSet("IW8R_NO_BC_TEXTURES") && app::loadDdsBlocks(path.toStdString(), textureSideNow(), blocks))
        id = renderer_.createTextureBlocks(blocks.dxgiFormat, blocks.width, blocks.height, blocks.levels);
    if (id < 0 && app::loadDdsPreview(path.toStdString(), textureSideNow(), img) && img.width > 0 && img.height > 0)
        id = renderer_.createTexture(img.pixels.data(), img.width, img.height);''')
print('ok')
