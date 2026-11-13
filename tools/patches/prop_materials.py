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

patch(H, '''    void setReferenceInstTexture(size_t mesh, int textureId) {
        if (mesh < refTexInst_.size()) refTexInst_[mesh].texture = textureId;
    }''', '''    void setReferenceInstTexture(size_t mesh, int textureId) {
        if (mesh < refTexInst_.size()) refTexInst_[mesh].texture = textureId;
    }
    // A textured prop mesh's normal map (-1: the flat one) and material constants (setReferenceBaseParams layout).
    void setReferenceInstNormal(size_t mesh, int textureId) {
        if (mesh < refTexInst_.size()) refTexInst_[mesh].normal = textureId;
    }
    void setReferenceInstParams(size_t mesh, const float params[16]) {
        if (mesh >= refTexInst_.size()) return;
        std::memcpy(refTexInst_[mesh].params, params, sizeof(refTexInst_[mesh].params));
        instParamsDirty_ = true;
    }''')
patch(H, '''        float lodOut = 0;  // InstancedTexMesh::lodOut
        ID3D11Buffer* visInst = nullptr;''', '''        float lodOut = 0;  // InstancedTexMesh::lodOut
        int normal = -1;            // setReferenceInstNormal
        float params[16] = {};      // setReferenceInstParams
        ID3D11Buffer* visInst = nullptr;''')
patch(H, '''    std::vector<ID3D11Buffer*> cbLayerAll_;  // 64 KB each: 256 batches (a 1.3 MB buffer lost the device on Hideout)''',
      '''    std::vector<ID3D11Buffer*> cbLayerAll_;  // 64 KB each: 256 batches (a 1.3 MB buffer lost the device on Hideout)
    std::vector<ID3D11Buffer*> cbInstAll_;   // the same for the textured prop meshes' material constants
    size_t cbInstAllMeshes_ = 0;
    bool instParamsDirty_ = true;''')

patch(R, '''    for (auto*& b : cbLayerAll_) safeRelease(b);
    cbLayerAll_.clear();
    if (ctx1_) ctx1_->Release(), ctx1_ = nullptr;''', '''    for (auto*& b : cbLayerAll_) safeRelease(b);
    cbLayerAll_.clear();
    for (auto*& b : cbInstAll_) safeRelease(b);
    cbInstAll_.clear();
    cbInstAllMeshes_ = 0;
    if (ctx1_) ctx1_->Release(), ctx1_ = nullptr;''')

# the instanced draw: normal maps and per-mesh constants on the main pass
patch(R, '''    if (cbBatch_) {
        const float flags[8] = {0, soft ? -1.0f : 0.0f, 0, 0, 0, 0, game_.skyIsStock ? 1.0f : 0.0f, 0};
        ctx_->UpdateSubresource(cbBatch_, 0, nullptr, flags, 0, 0);''', '''    // Main pass: each mesh's normal map (the flat one without) and its material constants at its offset in b7.
    const bool propNormals = !texPsOverride_ && flatNrmTex_ >= 0 && size_t(flatNrmTex_) < textures_.size();
    bool instCb = false;
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
                for (size_t j = 0; j < kInstPerCb && c * kInstPerCb + j < refTexInst_.size(); ++j)
                    std::memcpy(&all[j * 64], refTexInst_[c * kInstPerCb + j].params, sizeof(refTexInst_[0].params));
                if (cbInstAll_[c]) ctx_->UpdateSubresource(cbInstAll_[c], 0, nullptr, all.data(), 0, 0);
            }
        }
        instCb = cbInstAllMeshes_ == refTexInst_.size() && !cbInstAll_.empty();
    }
    if (cbBatch_) {
        const float flags[8] = {propNormals ? 1.0f : 0.0f, soft ? -1.0f : 0.0f, 0, 0, 0, 0, game_.skyIsStock ? 1.0f : 0.0f, 0};
        ctx_->UpdateSubresource(cbBatch_, 0, nullptr, flags, 0, 0);''')
patch(R, '''        ctx_->PSSetShaderResources(0, 1, &textures_[size_t(id)]);
        ID3D11Buffer* bufs[2] = {g.vb, culled ? g.visInst : g.inst};
        ctx_->IASetVertexBuffers(0, 2, bufs, strides, offsets);
        drawInst(g.vertices, culled ? g.visible : g.instances, 0, 0);
    }''', '''        ctx_->PSSetShaderResources(0, 1, &textures_[size_t(id)]);
        if (propNormals) {
            const int n = g.normal >= 0 && size_t(g.normal) < textures_.size() ? g.normal : flatNrmTex_;
            ctx_->PSSetShaderResources(1, 1, &textures_[size_t(n)]);
            const size_t mi = size_t(&g - refTexInst_.data());
            if (instCb && cbInstAll_[mi / kInstPerCb]) {
                const UINT first = UINT((mi % kInstPerCb) * 16), num = 16;
                ctx1_->PSSetConstantBuffers1(7, 1, &cbInstAll_[mi / kInstPerCb], &first, &num);
            }
        }
        ID3D11Buffer* bufs[2] = {g.vb, culled ? g.visInst : g.inst};
        ctx_->IASetVertexBuffers(0, 2, bufs, strides, offsets);
        drawInst(g.vertices, culled ? g.visible : g.instances, 0, 0);
    }
    if (instCb && cbLayer_) ctx_->PSSetConstantBuffers(7, 1, &cbLayer_);  // the plain (zero) layer buffer
    if (propNormals) {
        ID3D11ShaderResourceView* noN = nullptr;
        ctx_->PSSetShaderResources(1, 1, &noN);
    }''')

# shader: a tangent frame from the screen derivatives when the vertex has none (props)
patch(R, '''            float2 sd = nm.ga * 2.0 - 1.0;
            float2 oct = float2(sd.x + sd.y, sd.x - sd.y) * 0.5;
            float2 slope = oct / max(0.02, 1.0 - abs(oct.x) - abs(oct.y));
            float3 T = normalize(i.t - Nf * dot(i.t, Nf)), B = cross(Nf, T);
            N = normalize(T * slope.x + B * slope.y + Nf);''', '''            float2 sd = nm.ga * 2.0 - 1.0;
            float2 oct = float2(sd.x + sd.y, sd.x - sd.y) * 0.5;
            float2 slope = oct / max(0.02, 1.0 - abs(oct.x) - abs(oct.y));
            float3 Tt = i.t - Nf * dot(i.t, Nf);
            float3 T, B;
            if (dot(Tt, Tt) > 1e-8) {
                T = normalize(Tt), B = cross(Nf, T);
            } else {
                // No vertex tangent (placed props): the texture frame from the screen derivatives.
                float3 dp1 = ddx(i.w), dp2 = ddy(i.w);
                float2 du1 = ddx(uvS), du2 = ddy(uvS);
                float3 p2 = cross(dp2, Nf), p1 = cross(Nf, dp1);
                T = p2 * du1.x + p1 * du2.x, B = p2 * du1.y + p1 * du2.y;
                float sc = rsqrt(max(max(dot(T, T), dot(B, B)), 1e-20));
                T *= sc, B *= sc;
            }
            N = normalize(T * slope.x + B * slope.y + Nf);''')

# camera: prop meshes get their normal map and constants
C = 'src/qtapp/camera_view.cpp'
patch(C, '''                const int id = textureFor(texInstMaterials_[k]);
                if (texIds_.count(texInstMaterials_[k])) {
                    texInstResolved_[k] = true;
                    if (id >= 0) renderer_.setReferenceInstTexture(k, id == checkerTex_ ? -1 : id);  // as the world: grey, not the checker
                } else {''', '''                const int id = textureFor(texInstMaterials_[k]);
                if (texIds_.count(texInstMaterials_[k])) {
                    if (id >= 0) renderer_.setReferenceInstTexture(k, id == checkerTex_ ? -1 : id);  // as the world: grey, not the checker
                    // Its normal map (Game Lighting: gloss, cavity, relief) and the material's constants.
                    const int nid = id >= 0 && id != checkerTex_ ? normalFor(texInstMaterials_[k]) : -1;
                    if (id >= 0 && id != checkerTex_ && !nrmIds_.count(texInstMaterials_[k])) {
                        pending = true;
                        continue;
                    }
                    if (nid >= 0) renderer_.setReferenceInstNormal(k, nid);
                    if (const auto p = materialConstantsFor(texInstMaterials_[k])) renderer_.setReferenceInstParams(k, p->data());
                    texInstResolved_[k] = true;
                } else {''')
patch(C, '''void CameraView::setBaseMaterialConstants(size_t k) {
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
}''', '''std::optional<std::array<float, 16>> CameraView::materialConstantsFor(const std::string& m) const {
    // The stock material's layer-0 constants (spec / gloss remap, normal strength) for the Game Lighting shader.
    if (m.empty() || !factsLookup_ || m.find_first_of("\\x01\\x02\\x05\\x06") != std::string::npos) return std::nullopt;
    const auto f = factsLookup_(m);
    if (!f || f->constants.layer.empty()) return std::nullopt;
    const auto& c = f->constants.layer[0];
    const bool masked = maskLookup_ && !maskLookup_(m).isEmpty();
    const bool blended = ctx_ && ctx_->scene.blendFor && ctx_->scene.blendFor(m) != 0;
    return std::array<float, 16>{0.0f, !masked && !blended ? 1.0f : 0.0f, 0.0f, 0.0f, c.specScale, c.specBias, c.glossScale, c.glossBias,
                                 0.0f, 0.0f, 0.0f, 0.0f, c.normalStrength, 1.0f, 0.0f, 0.0f};
}

void CameraView::setBaseMaterialConstants(size_t k) {
    if (k >= refTexMaterials_.size()) return;
    if (const auto p = materialConstantsFor(refTexMaterials_[k])) renderer_.setReferenceBaseParams(k, p->data());
}''')
patch('src/qtapp/camera_view.h', '''    void setBaseMaterialConstants(size_t referenceBatch);''', '''    void setBaseMaterialConstants(size_t referenceBatch);
    std::optional<std::array<float, 16>> materialConstantsFor(const std::string& material) const;''')
print('ok')
