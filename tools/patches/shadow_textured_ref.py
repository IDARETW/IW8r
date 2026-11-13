import io


def patch(p, a, b, n=1):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    if b and s.count(b) >= 1:
        return
    assert s.count(a) == n, (p, s.count(a), a[:80])
    s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


R = 'src/viewport/render.cpp'
patch(R, '''        if (vbRef_ && refCount_ && refVisible_) {
            ctx_->IASetInputLayout(layout_);
            ctx_->VSSetShader(vs_, nullptr, 0);
            ctx_->IASetVertexBuffers(0, 1, &vbRef_, &stride, &offset);
            if (cull && !refChunks_.empty())
                forVisibleRuns(iw8r::frustumFromViewProj(sm), refChunks_, [&](uint32_t first, uint32_t n) { drawN(n * 3, first * 3); });
            else
                drawN(UINT(refCount_), 0);
        }''', '''        static const bool flatShadowRef = std::getenv("IW8R_FLAT_SHADOW_REF") != nullptr;  // A/B: the old caster list
        if (vbRefTex_ && refVisible_ && vsTex_ && layoutTex_ && psShadowClip_ && sampler_ && !flatShadowRef) {
            // The stock world casts from its textured batches: opaque and cut-out base batches only. Decals (puddles,
            // potholes, dirt sheets floating above the ground: Verdansk's streamed tiles hold thousands) and layer
            // passes cast nothing, as in game; cut-out surfaces (fences, grates) keep their holes.
            ctx_->IASetInputLayout(layoutTex_);
            ctx_->VSSetShader(vsTex_, nullptr, 0);
            ctx_->PSSetShader(psShadowClip_, nullptr, 0);
            ctx_->PSSetSamplers(0, 1, &sampler_);
            if (cbBatch_) {
                const float flags[8] = {0, 0, 0, 0, 0, 0, 1.0f, 0};
                ctx_->UpdateSubresource(cbBatch_, 0, nullptr, flags, 0, 0);
                ctx_->VSSetConstantBuffers(1, 1, &cbBatch_);
                ctx_->PSSetConstantBuffers(1, 1, &cbBatch_);
            }
            const bool shCb = uploadLayerParams();
            const iw8r::Frustum sf = iw8r::frustumFromViewProj(sm);
            ctx_->IASetVertexBuffers(0, 1, &vbRefTex_, &tstride, &offset);
            for (size_t k = 0; k < refTexIds_.size() && (refTexExtraFrom_ == ~size_t(0) || k < refTexExtraFrom_); ++k) {
                if (k < refLayers_.size() && (refLayers_[k].layer > 0 || refLayers_[k].decal)) continue;
                const int id = refTexIds_[k] >= 0 ? refTexIds_[k] : whiteTex_;
                if (id < 0 || size_t(id) >= textures_.size()) continue;
                ctx_->PSSetShaderResources(0, 1, &textures_[size_t(id)]);
                if (shCb && cbLayerAll_[k / 256]) {
                    const UINT first = UINT((k % 256) * 16), num = 16;
                    ctx1_->PSSetConstantBuffers1(7, 1, &cbLayerAll_[k / 256], &first, &num);
                }
                if (cull && k < refTexChunks_.size() && !refTexChunks_[k].empty())
                    forVisibleRuns(sf, refTexChunks_[k], [&](uint32_t first, uint32_t n) { drawN(n * 3, refTexFirst_[k] + first * 3); });
                else
                    drawN(refTexCount_[k], refTexFirst_[k]);
            }
            if (shCb && cbLayer_) ctx_->PSSetConstantBuffers(7, 1, &cbLayer_);
            ID3D11ShaderResourceView* noT = nullptr;
            ctx_->PSSetShaderResources(0, 1, &noT);
            ctx_->PSSetShader(nullptr, nullptr, 0);
            ctx_->IASetInputLayout(layout_);
            ctx_->VSSetShader(vs_, nullptr, 0);
        } else if (vbRef_ && refCount_ && refVisible_) {
            ctx_->IASetInputLayout(layout_);
            ctx_->VSSetShader(vs_, nullptr, 0);
            ctx_->IASetVertexBuffers(0, 1, &vbRef_, &stride, &offset);
            if (cull && !refChunks_.empty())
                forVisibleRuns(iw8r::frustumFromViewProj(sm), refChunks_, [&](uint32_t first, uint32_t n) { drawN(n * 3, first * 3); });
            else
                drawN(UINT(refCount_), 0);
        }''')
# a batch that turns out to be a decal (or gets its texture) changes the casters: redraw the shadow map
patch(R, '''    L.decal = true;
    L.sortKey = sortKey;
    L.ready = true;
    L.reveal = revealTexture;
    std::memcpy(L.params, params, sizeof(L.params));
    refDecalDirty_ = true;
    layerParamsDirty_ = true;''', '''    if (!L.decal) shadowDirty_ = nearDirty_ = true;  // it no longer casts (the shadow pass skips decals)
    L.decal = true;
    L.sortKey = sortKey;
    L.ready = true;
    L.reveal = revealTexture;
    std::memcpy(L.params, params, sizeof(L.params));
    refDecalDirty_ = true;
    layerParamsDirty_ = true;''')
print('ok')
