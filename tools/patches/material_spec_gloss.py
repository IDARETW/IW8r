import io


def patch(p, a, b, n=1):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


H = 'src/viewport/render.h'
R = 'src/viewport/render.cpp'

# --- renderer: base batch constants + one upload routine -------------------------------------------------------
patch(H, '''    void setReferenceLayer(size_t batch, int revealTexture, const float params[16]);''',
      '''    void setReferenceLayer(size_t batch, int revealTexture, const float params[16]);
    // A base batch's material constants (research 95), in the layer constant layout with layerP.x = 0:
    // layerP.y = 1 when the colour alpha holds the specular code (192..254), layerTint = (spec scale, spec bias, gloss
    // scale, gloss bias), layerUvO = (normal strength, 1 = constants given).
    void setReferenceBaseParams(size_t batch, const float params[16]);''')
patch(H, '''    float terrainCut_[3] = {0, 0, 0};''', '''    float terrainCut_[3] = {0, 0, 0};
    // Uploads every batch's 16 constants into the 64 KB chunks (when changed); true when per-batch offsets can be bound.
    bool uploadLayerParams();''')

patch(R, '''void SceneRenderer::setReferenceLayer(size_t batch, int revealTexture, const float params[16]) {''',
      '''void SceneRenderer::setReferenceBaseParams(size_t batch, const float params[16]) {
    if (batch >= refLayers_.size() || refLayers_[batch].layer > 0 || refLayers_[batch].decal) return;
    std::memcpy(refLayers_[batch].params, params, sizeof(refLayers_[batch].params));
    layerParamsDirty_ = true;
}

bool SceneRenderer::uploadLayerParams() {
    if (!ctx1_ && ctx_ && dev_) {
        D3D11_FEATURE_DATA_D3D11_OPTIONS o{};
        if (SUCCEEDED(dev_->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS, &o, sizeof(o))) && o.ConstantBufferOffsetting)
            ctx_->QueryInterface(__uuidof(ID3D11DeviceContext1), reinterpret_cast<void**>(&ctx1_));
    }
    constexpr size_t kPerCb = 256;  // batches per 64 KB buffer (256 bytes each)
    if (ctx1_ && (layerParamsDirty_ || cbLayerAllBatches_ != refLayers_.size())) {
        layerParamsDirty_ = false;
        const size_t chunks = (refLayers_.size() + kPerCb - 1) / kPerCb;
        if (cbLayerAllBatches_ != refLayers_.size()) {
            for (auto*& b : cbLayerAll_) safeRelease(b);
            cbLayerAll_.assign(chunks, nullptr);
            cbLayerAllBatches_ = refLayers_.size();
            D3D11_BUFFER_DESC bd{};
            bd.ByteWidth = UINT(kPerCb * 256);
            bd.Usage = D3D11_USAGE_DEFAULT;
            bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            for (auto*& b : cbLayerAll_)
                if (FAILED(dev_->CreateBuffer(&bd, nullptr, &b))) cbLayerAllBatches_ = 0;
        }
        std::vector<float> all(kPerCb * 64, 0.0f);  // 256 bytes (16 constants) per batch
        for (size_t c = 0; c < cbLayerAll_.size() && cbLayerAllBatches_; ++c) {
            std::fill(all.begin(), all.end(), 0.0f);
            for (size_t j = 0; j < kPerCb && c * kPerCb + j < refLayers_.size(); ++j)
                std::memcpy(&all[j * 64], refLayers_[c * kPerCb + j].params, sizeof(refLayers_[0].params));
            if (cbLayerAll_[c]) ctx_->UpdateSubresource(cbLayerAll_[c], 0, nullptr, all.data(), 0, 0);
        }
    }
    return ctx1_ && !cbLayerAll_.empty() && cbLayerAllBatches_ == refLayers_.size();
}

void SceneRenderer::setReferenceLayer(size_t batch, int revealTexture, const float params[16]) {''')

patch(R, '''            ID3D11Buffer* boundRefVb = vbRefTex_;
            if (!ctx1_ && ctx_ && dev_) {
                D3D11_FEATURE_DATA_D3D11_OPTIONS o{};
                if (SUCCEEDED(dev_->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS, &o, sizeof(o))) && o.ConstantBufferOffsetting)
                    ctx_->QueryInterface(__uuidof(ID3D11DeviceContext1), reinterpret_cast<void**>(&ctx1_));
            }
            constexpr size_t kPerCb = 256;  // batches per 64 KB buffer (256 bytes each)
            if (ctx1_ && (layerParamsDirty_ || cbLayerAllBatches_ != refLayers_.size())) {
                layerParamsDirty_ = false;
                const size_t chunks = (refLayers_.size() + kPerCb - 1) / kPerCb;
                if (cbLayerAllBatches_ != refLayers_.size()) {
                    for (auto*& b : cbLayerAll_) safeRelease(b);
                    cbLayerAll_.assign(chunks, nullptr);
                    cbLayerAllBatches_ = refLayers_.size();
                    D3D11_BUFFER_DESC bd{};
                    bd.ByteWidth = UINT(kPerCb * 256);
                    bd.Usage = D3D11_USAGE_DEFAULT;
                    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
                    for (auto*& b : cbLayerAll_)
                        if (FAILED(dev_->CreateBuffer(&bd, nullptr, &b))) cbLayerAllBatches_ = 0;
                }
                std::vector<float> all(kPerCb * 64, 0.0f);  // 256 bytes (16 constants) per batch
                for (size_t c = 0; c < cbLayerAll_.size() && cbLayerAllBatches_; ++c) {
                    std::fill(all.begin(), all.end(), 0.0f);
                    for (size_t j = 0; j < kPerCb && c * kPerCb + j < refLayers_.size(); ++j)
                        std::memcpy(&all[j * 64], refLayers_[c * kPerCb + j].params, sizeof(refLayers_[0].params));
                    if (cbLayerAll_[c]) ctx_->UpdateSubresource(cbLayerAll_[c], 0, nullptr, all.data(), 0, 0);
                }
            }
            const bool offsetCb = ctx1_ && !cbLayerAll_.empty() && cbLayerAllBatches_ == refLayers_.size();''',
      '''            ID3D11Buffer* boundRefVb = vbRefTex_;
            constexpr size_t kPerCb = 256;  // batches per 64 KB buffer (256 bytes each)
            const bool offsetCb = uploadLayerParams();''')

# base loop: bind each base batch's constants (then back to the zero buffer)
patch(R, '''        refCulledLast_ = 0;
        ID3D11ShaderResourceView* bound = nullptr;
        for (size_t k = 0; k < refTexIds_.size(); ++k) {
            if (k < refLayers_.size() && (refLayers_[k].layer > 0 || refLayers_[k].decal)) continue;  // layers, decals: below
            const int id = refTexIds_[k] >= 0 ? refTexIds_[k] : whiteTex_;
            if (id < 0 || size_t(id) >= textures_.size()) continue;
            const auto bind = [&] {
                // Batches that share a texture (the plain one while images load) do not bind it again.
                if (bound != textures_[size_t(id)]) ctx_->PSSetShaderResources(0, 1, &textures_[size_t(id)]), bound = textures_[size_t(id)];
                bindNormal(k);
            };''', '''        refCulledLast_ = 0;
        ID3D11ShaderResourceView* bound = nullptr;
        // Each base batch's material constants (spec / gloss remaps; setReferenceBaseParams) at its offset in b7.
        const bool baseCb = uploadLayerParams();
        constexpr size_t kBasePerCb = 256;
        for (size_t k = 0; k < refTexIds_.size(); ++k) {
            if (k < refLayers_.size() && (refLayers_[k].layer > 0 || refLayers_[k].decal)) continue;  // layers, decals: below
            const int id = refTexIds_[k] >= 0 ? refTexIds_[k] : whiteTex_;
            if (id < 0 || size_t(id) >= textures_.size()) continue;
            const auto bind = [&] {
                // Batches that share a texture (the plain one while images load) do not bind it again.
                if (bound != textures_[size_t(id)]) ctx_->PSSetShaderResources(0, 1, &textures_[size_t(id)]), bound = textures_[size_t(id)];
                bindNormal(k);
                if (baseCb && cbLayerAll_[k / kBasePerCb]) {
                    const UINT first = UINT((k % kBasePerCb) * 16), num = 16;
                    ctx1_->PSSetConstantBuffers1(7, 1, &cbLayerAll_[k / kBasePerCb], &first, &num);
                }
            };''')
patch(R, '''            bind();
            drawN(refTexCount_[k], refTexFirst_[k]);
        }''', '''            bind();
            drawN(refTexCount_[k], refTexFirst_[k]);
        }
        if (baseCb && cbLayer_) ctx_->PSSetConstantBuffers(7, 1, &cbLayer_);  // the plain (zero) layer buffer''')

# --- shader --------------------------------------------------------------------------------------------------
patch(R, '''        float3 N = Nf;
        if (batchFlags.x > 0.5 && dot(i.n, i.n) > 0.5) {
            float4 nm = nrm0.Sample(samp0, uvS);
            float2 sd = nm.ga * 2.0 - 1.0;''', '''        float3 N = Nf;
        // A stock base batch with its material constants (layerP.x = 0, layerUvO.y = 1; research 95): gloss from the
        // normal map's red channel, cavity from its blue, the normal scaled by the material's strength.
        const bool baseMat = layerP.x < 0.5 && layerUvO.y > 0.5;
        float nmGloss = -1.0, nmCavity = 1.0;
        if (batchFlags.x > 0.5 && dot(i.n, i.n) > 0.5) {
            float4 nm = nrm0.Sample(samp0, uvS);
            if (baseMat) {
                nmGloss = nm.r, nmCavity = nm.b;
                nm.ga = (nm.ga - 0.5) * layerUvO.x + 0.5;
            }
            float2 sd = nm.ga * 2.0 - 1.0;''')
patch(R, '''            float3 Vs = normalize(eyeStart.xyz - i.w);
            const float3 f0 = 0.04;
            const float gp = 1.0 - sqrt(1.0 - 0.3);''', '''            float3 Vs = normalize(eyeStart.xyz - i.w);
            // The material (research 95 "Surface decode"): s = alpha x spec scale + bias (the colour alpha keeps the
            // code as 192..254 on opaque stock images), metal = sat(s - .1) / .9, f0 = metal x albedo + min(s, .1);
            // gloss = red x gloss scale + bias. Without constants: a dielectric of gloss .3.
            float3 f0 = 0.04;
            float glossM = 0.3;
            float3 diffAlbedo = albedo;
            if (baseMat) {
                if (layerP.y > 0.5 && t.a < 0.999) {
                    const float sCode = saturate((t.a * 255.0 - 192.0) / 62.0) * layerTint.x + layerTint.y;
                    const float metal = saturate(sCode - 0.1) * 1.111111;
                    f0 = metal * albedo + min(sCode, 0.1);
                    diffAlbedo = albedo * max(1.0 - metal, 0.0);
                }
                if (nmGloss >= 0.0) glossM = saturate(nmGloss * layerTint.z + layerTint.w);
            }
            const float gp = 1.0 - sqrt(1.0 - glossM);''')
patch(R, '''                S = lerp(0.18 * mixed, P, 1.0);  // specular occlusion 1: no GTAO here''',
      '''                // Specular occlusion: the game's gtso_lut(max(sqrt(1 - ao), sqrt(1 - cavity)), ...) without GTAO;
                // the cavity stands in for it (INFERRED shape).
                S = lerp(0.18 * mixed, P, baseMat ? nmCavity : 1.0);''')
patch(R, '''            c = albedo * indirect * kd * (1.0 - fmax) + Fenv * S;''',
      '''            c = diffAlbedo * indirect * kd * (1.0 - fmax) + Fenv * S;''')
patch(R, '''                     (albedo * (1.0 - max(fl.r, max(fl.g, fl.b))) + F * Dg * Vg * 1.0390625);''',
      '''                     (diffAlbedo * (1.0 - max(fl.r, max(fl.g, fl.b))) + F * Dg * Vg * 1.0390625);''')

# --- camera: keep the spec code in opaque stock colour images; give base batches their constants --------------
C = 'src/qtapp/camera_view.cpp'
patch(C, '''            const bool softMask = ctx_ && ctx_->scene.blendFor && ctx_->scene.blendFor(material) != 0;
            for (int y = 0; y < img.height; ++y)
                for (int x = 0; x < img.width; ++x) {
                    uint8_t a = 255;''', '''            const bool softMask = ctx_ && ctx_->scene.blendFor && ctx_->scene.blendFor(material) != 0;
            // Opaque stock images keep their specular code (research 95: metal / f0) as 192..254: never under any
            // cut-out threshold, and 255 still means "no code" (masked images, editor textures).
            const bool keepSpec = !haveMask && !softMask;
            for (int y = 0; y < img.height; ++y)
                for (int x = 0; x < img.width; ++x) {
                    uint8_t a = 255;
                    if (keepSpec) {
                        const uint8_t s = img.pixels[(size_t(y) * size_t(img.width) + size_t(x)) * 4 + 3];
                        a = uint8_t(192 + (int(s) * 62 + 127) / 255);
                    }''')
patch(C, '''                    if (nid >= 0) renderer_.setReferenceNormal(k, nid);''', '''                    if (nid >= 0) renderer_.setReferenceNormal(k, nid);
                    setBaseMaterialConstants(k);''')
patch(C, '''bool CameraView::resolveReferenceLayer(size_t k) {''', '''void CameraView::setBaseMaterialConstants(size_t k) {
    // The stock material's layer-0 constants (spec / gloss remap, normal strength) for the Game Lighting shader.
    if (k >= refTexMaterials_.size() || !factsLookup_) return;
    const std::string& m = refTexMaterials_[k];
    if (m.empty() || m.find_first_of("\\x01\\x02\\x05\\x06") != std::string::npos) return;
    const auto f = factsLookup_(m);
    if (!f || f->constants.layer.empty()) return;
    const auto& c = f->constants.layer[0];
    const bool masked = maskLookup_ && !maskLookup_(m).isEmpty();
    const bool blended = ctx_ && ctx_->scene.blendFor && ctx_->scene.blendFor(m) != 0;
    const float p[16] = {0.0f, !masked && !blended ? 1.0f : 0.0f, 0.0f, 0.0f, c.specScale, c.specBias, c.glossScale, c.glossBias,
                         0.0f, 0.0f, 0.0f, 0.0f, c.normalStrength, 1.0f, 0.0f, 0.0f};
    renderer_.setReferenceBaseParams(k, p);
}

bool CameraView::resolveReferenceLayer(size_t k) {''')
patch('src/qtapp/camera_view.h', '''    void setMaterialFacts(FactsLookup lookup) { factsLookup_ = std::move(lookup); }''',
      '''    void setMaterialFacts(FactsLookup lookup) { factsLookup_ = std::move(lookup); }
    void setBaseMaterialConstants(size_t referenceBatch);''')
print('ok')
