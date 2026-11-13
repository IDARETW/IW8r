import io


def patch(p, pairs):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    for a, b, *cnt in pairs:
        n = cnt[0] if cnt else 1
        a = a.replace('\n', nl); b = b.replace('\n', nl)
        assert s.count(a) == n, (p, s.count(a), a[:100])
        s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


# ------------------------------------------------ scene: per-triangle tangents; the layer weight in the tangent length
patch('src/viewport/scene.cpp', [
 ('''            const uint32_t rgba = g | (g << 8) | (g << 16) | (0xffu << 24);
            for (uint32_t k : {a, b, c}) {
                // ImportedMesh::uv is OBJ convention (v flipped); the textured camera uses the game's v.
                GpuTexVertex v{float(m.pos[k].x), float(m.pos[k].y), float(m.pos[k].z), m.uv[k][0], 1.0f - m.uv[k][1], rgba};
                v.nx = float(n.x), v.ny = float(n.y), v.nz = float(n.z);''',
  '''            const uint32_t rgba = g | (g << 8) | (g << 16) | (0xffu << 24);
            // The texture tangent of the triangle (the direction of +u in the game's uv), for the stock normal maps: the
            // world's own tangent frames are not read. Any direction across the face where the uv is degenerate.
            const auto tangentOf = [&](float u0, float v0, float u1, float v1, float u2, float v2) {
                const Vec3 e1 = m.pos[b] - m.pos[a], e2 = m.pos[c] - m.pos[a];
                const double du1 = u1 - u0, dv1 = v1 - v0, du2 = u2 - u0, dv2 = v2 - v0;
                const double det = du1 * dv2 - du2 * dv1;
                Vec3 t = std::fabs(det) > 1e-12 ? (e1 * dv2 - e2 * dv1) * (1.0 / det) : e1;
                t = t - n * dot(t, n);
                if (length(t) < 1e-9) t = std::fabs(n.z) < 0.9 ? cross(n, Vec3{0, 0, 1}) : cross(n, Vec3{1, 0, 0});
                return normalize(t);
            };
            const Vec3 tan0 = tangentOf(float(m.uv[a][0]), 1.0f - float(m.uv[a][1]), float(m.uv[b][0]), 1.0f - float(m.uv[b][1]),
                                        float(m.uv[c][0]), 1.0f - float(m.uv[c][1]));
            std::vector<Vec3> tanL;
            for (int L = 1; L <= int(layerDst.size()); ++L) {
                const auto& ua = world.layerUv[mi][size_t(a) * size_t(layers) + size_t(L)];
                const auto& ub = world.layerUv[mi][size_t(b) * size_t(layers) + size_t(L)];
                const auto& uc = world.layerUv[mi][size_t(c) * size_t(layers) + size_t(L)];
                tanL.push_back(tangentOf(ua[0], 1.0f - ua[1], ub[0], 1.0f - ub[1], uc[0], 1.0f - uc[1]));
            }
            for (uint32_t k : {a, b, c}) {
                // ImportedMesh::uv is OBJ convention (v flipped); the textured camera uses the game's v.
                GpuTexVertex v{float(m.pos[k].x), float(m.pos[k].y), float(m.pos[k].z), m.uv[k][0], 1.0f - m.uv[k][1], rgba};
                v.nx = float(n.x), v.ny = float(n.y), v.nz = float(n.z);
                v.tx = float(tan0.x), v.ty = float(tan0.y), v.tz = float(tan0.z);'''),
 ('''                    lv.tx = stockLayerWeight(vcol ? (*vcol)[k] : 0xFFFFFFFFu, vcolHi ? (*vcolHi)[k] : 0xFFFFFFFFu, L);
                    lv.ty = lv.tz = 0;''',
  '''                    // The layer's own tangent (its uv), its length 1 + the blend weight (the shader takes length - 1).
                    const float w = stockLayerWeight(vcol ? (*vcol)[k] : 0xFFFFFFFFu, vcolHi ? (*vcolHi)[k] : 0xFFFFFFFFu, L);
                    const Vec3 tl = tanL[size_t(L - 1)] * (1.0 + double(w));
                    lv.tx = float(tl.x), lv.ty = float(tl.y), lv.tz = float(tl.z);'''),
])
s = io.open('src/viewport/scene.h', encoding='utf-8', newline='').read()
s = s.replace('the layers below it with its own texture coordinates (uv) and its blend weight in the vertex tangent x (tx).',
              'the layers below it with its own texture coordinates (uv); its tangent (from its uv) has length 1 + its blend weight.')
io.open('src/viewport/scene.h', 'w', encoding='utf-8', newline='').write(s)

# ------------------------------------------------ shader + renderer
patch('src/viewport/render.cpp', [
 ('''        float w = saturate(i.t.x), k = layerP.z;''', '''        float w = saturate(length(i.t) - 1.0), k = layerP.z;  // the tangent's length is 1 + the weight'''),
 ('''            float4 nm = nrm0.Sample(samp0, i.uv);''', '''            float4 nm = nrm0.Sample(samp0, uvS);'''),
 ('''        float4 nm = nrm0.Sample(samp0, i.uv);''', '''        float4 nm = nrm0.Sample(samp0, uvS);'''),
 # the main stock reference loop: normal maps on (flat where a batch has none)
 ('''        if (cbBatch_) {
            const float flags[8] = {0, 0, 0, 0, 0, 0, 1.0f, stockLmSrv_[0] ? 1.0f : 0.0f};
            ctx_->UpdateSubresource(cbBatch_, 0, nullptr, flags, 0, 0);
            ctx_->PSSetConstantBuffers(1, 1, &cbBatch_);
            ctx_->VSSetConstantBuffers(1, 1, &cbBatch_);
        }
        ID3D11ShaderResourceView* noNrm = nullptr;
        ctx_->PSSetShaderResources(1, 1, &noNrm);''', '''        // x = 1: every stock batch samples t1, its normal map or the flat one (research 95 encoding: g, a = 0.5).
        const bool refNormals = flatNrmTex_ >= 0 && size_t(flatNrmTex_) < textures_.size();
        if (cbBatch_) {
            const float flags[8] = {refNormals ? 1.0f : 0.0f, 0, 0, 0, 0, 0, 1.0f, stockLmSrv_[0] ? 1.0f : 0.0f};
            ctx_->UpdateSubresource(cbBatch_, 0, nullptr, flags, 0, 0);
            ctx_->PSSetConstantBuffers(1, 1, &cbBatch_);
            ctx_->VSSetConstantBuffers(1, 1, &cbBatch_);
        }
        ID3D11ShaderResourceView* noNrm = nullptr;
        ctx_->PSSetShaderResources(1, 1, &noNrm);
        ID3D11ShaderResourceView* boundNrm = nullptr;
        const auto bindNormal = [&](size_t k) {
            if (!refNormals) return;
            const int n = k < refNrmIds_.size() && refNrmIds_[k] >= 0 && size_t(refNrmIds_[k]) < textures_.size() ? refNrmIds_[k] : flatNrmTex_;
            if (boundNrm != textures_[size_t(n)]) ctx_->PSSetShaderResources(1, 1, &textures_[size_t(n)]), boundNrm = textures_[size_t(n)];
        };'''),
 ('''            const auto bind = [&] {
                // Batches that share a texture (the plain one while images load) do not bind it again.
                if (bound != textures_[size_t(id)]) ctx_->PSSetShaderResources(0, 1, &textures_[size_t(id)]), bound = textures_[size_t(id)];
            };''', '''            const auto bind = [&] {
                // Batches that share a texture (the plain one while images load) do not bind it again.
                if (bound != textures_[size_t(id)]) ctx_->PSSetShaderResources(0, 1, &textures_[size_t(id)]), bound = textures_[size_t(id)];
                bindNormal(k);
            };'''),
 ('''                const float flags[8] = {0, 1.0f, 0, 0, 0, 0, 1.0f, stockLmSrv_[0] ? 1.0f : 0.0f};  // y = 1: blended, no clip''',
  '''                const float flags[8] = {refNormals ? 1.0f : 0.0f, 1.0f, 0, 0, 0, 0, 1.0f, stockLmSrv_[0] ? 1.0f : 0.0f};  // y = 1: blended, no clip'''),
 ('''                ctx_->PSSetShaderResources(12, 1, &textures_[size_t(rv)]);''', '''                ctx_->PSSetShaderResources(12, 1, &textures_[size_t(rv)]);
                bindNormal(k);'''),
 ('''            ID3D11ShaderResourceView* noRev = nullptr;
            ctx_->PSSetShaderResources(12, 1, &noRev);''', '''            ID3D11ShaderResourceView* noRev = nullptr;
            ctx_->PSSetShaderResources(12, 1, &noRev);
            ctx_->PSSetShaderResources(1, 1, &noRev);'''),
 ('''        ID3D11ShaderResourceView* none2[1] = {nullptr};
        ctx_->PSSetShaderResources(0, 1, none2);''', '''        ID3D11ShaderResourceView* none2[1] = {nullptr};
        ctx_->PSSetShaderResources(0, 1, none2);
        ctx_->PSSetShaderResources(1, 1, none2);'''),
 ('''    refLayers_.assign(batches.size(), RefLayer{});
    refLayerOrder_.clear();''', '''    refLayers_.assign(batches.size(), RefLayer{});
    refNrmIds_.assign(batches.size(), -1);
    refLayerOrder_.clear();
    if (flatNrmTex_ < 0 && dev_) {
        const uint8_t flat[16] = {128, 128, 255, 128, 128, 128, 255, 128, 128, 128, 255, 128, 128, 128, 255, 128};
        flatNrmTex_ = createTexture(flat, 2, 2);
    }'''),
])
patch('src/viewport/render.h', [
 ('''    void setReferenceLayer(size_t batch, int revealTexture, const float params[16]);''',
  '''    void setReferenceLayer(size_t batch, int revealTexture, const float params[16]);
    // A stock batch's normal map (the flat one until then).
    void setReferenceNormal(size_t batch, int textureId) {
        if (batch < refNrmIds_.size()) refNrmIds_[batch] = textureId;
    }'''),
 ('''    std::vector<size_t> refLayerOrder_;  // layer batches by layer, then batch''', '''    std::vector<size_t> refLayerOrder_;  // layer batches by layer, then batch
    std::vector<int> refNrmIds_;         // per stock batch: its normal map texture, -1 = the flat one
    int flatNrmTex_ = -1;'''),
])

# ------------------------------------------------ camera: normal maps of stock batches and layers
patch('src/qtapp/camera_view.cpp', [
 ('''                const int id = textureFor(refTexMaterials_[k]);
                if (texIds_.count(refTexMaterials_[k])) {
                    refTexResolved_[k] = true;
                    if (id >= 0) renderer_.setReferenceTexture(k, id);
                } else {
                    pending = true;
                }''', '''                const int id = textureFor(refTexMaterials_[k]);
                if (texIds_.count(refTexMaterials_[k])) {
                    if (id >= 0) renderer_.setReferenceTexture(k, id);
                    // Its normal map next (normalFor waits for the colour image and the per-frame budget).
                    const int nid = id >= 0 && id != checkerTex_ ? normalFor(refTexMaterials_[k]) : -1;
                    if (id >= 0 && id != checkerTex_ && !nrmIds_.count(refTexMaterials_[k])) {
                        pending = true;
                        continue;
                    }
                    if (nid >= 0) renderer_.setReferenceNormal(k, nid);
                    refTexResolved_[k] = true;
                } else {
                    pending = true;
                }'''),
 ('''    if (cid < 0 || cid == checkerTex_) return true;  // no colour image: left out rather than drawn as the checker''',
  '''    if (cid < 0 || cid == checkerTex_) return true;  // no colour image: left out rather than drawn as the checker
    const int nid = normalFor(ck);
    if (!nrmIds_.count(ck)) return false;  // waits for the per-frame budget
    if (nid >= 0) renderer_.setReferenceNormal(k, nid);'''),
])
patch('src/qtapp/main_window.cpp', [
 ('''    camera_->setMaterialNormals([this](const std::string& m) -> QString {
        if (auto e = matCache_->lookup(qs(m)); e && !e->failed) return e->normalDds;''',
  '''    camera_->setMaterialNormals([this](const std::string& m) -> QString {
        // A layer of a layered stock material ("<material>\\x01<L>"): that layer's normal map.
        if (const size_t sep = m.find('\\x01'); sep != std::string::npos && sep > 0) {
            const auto imgs = matCache_->layerImages(qs(m.substr(0, sep)));
            const int L = std::atoi(m.c_str() + sep + 1);
            return L >= 0 && size_t(L) < imgs.size() ? imgs[size_t(L)].normalDds : QString();
        }
        if (auto e = matCache_->lookup(qs(m)); e && !e->failed) return e->normalDds;'''),
])
print('ok')
