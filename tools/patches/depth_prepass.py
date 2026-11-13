import io


def patch(p, pairs):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    for a, b in pairs:
        a = a.replace('\n', nl); b = b.replace('\n', nl)
        assert s.count(a) == 1, (p, s.count(a), a[:90])
        s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


patch('src/viewport/render.h', [
 ('''    ID3D11Texture2D* envBrdfTex_ = nullptr;''', '''    ID3D11Texture2D* envBrdfTex_ = nullptr;
    // Game Lighting's depth pre-pass (the game shader is costly and its alpha clip stops early depth rejection): depth
    // first through the clip shader with colour writes off, then the shading pass tests depth without writing it.
    struct ID3D11BlendState* blendNoColor_ = nullptr;
    ID3D11PixelShader* texPsOverride_ = nullptr;  // set during the pre-pass: drawReferenceTexInstances uses it'''),
])

patch('src/viewport/render.cpp', [
 # blend state without colour writes
 ('''    dev_->CreateBlendState(&bd, &blend_);''', '''    dev_->CreateBlendState(&bd, &blend_);
    {
        D3D11_BLEND_DESC nb{};
        nb.RenderTarget[0].BlendEnable = FALSE;
        nb.RenderTarget[0].RenderTargetWriteMask = 0;
        dev_->CreateBlendState(&nb, &blendNoColor_);
    }'''),
 ('''    safeRelease(dsWrite_);''', '''    safeRelease(dsWrite_);
    safeRelease(blendNoColor_);'''),
 # instanced textured props: the override pixel shader
 ('''    ctx_->IASetInputLayout(layoutTexInst_);
    ctx_->VSSetShader(vsTexInst_, nullptr, 0);
    ctx_->PSSetShader(psTex_, nullptr, 0);''', '''    ctx_->IASetInputLayout(layoutTexInst_);
    ctx_->VSSetShader(vsTexInst_, nullptr, 0);
    ctx_->PSSetShader(texPsOverride_ ? texPsOverride_ : psTex_, nullptr, 0);'''),
 # the pre-pass, before the textured scene batches
 ('''    texCulledLast_ = 0;
    if (!drawList_.empty()) {
        // One vertex buffer per batch; one draw per texture, or one per run of visible chunks.''', '''    texCulledLast_ = 0;
    // Game Lighting: depth pre-pass of the opaque textured geometry (scene batches, the stock world, the stock props).
    const bool prepass = game_.on && psShadowClip_ && blendNoColor_ && vsTex_ && layoutTex_ && sampler_;
    if (prepass) {
        IW8R_PROF("d.prepass");
        ctx_->OMSetBlendState(blendNoColor_, bf, 0xffffffff);
        ctx_->OMSetDepthStencilState(dsWrite_, 0);
        ctx_->RSSetState(rsTris_);
        ctx_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx_->IASetInputLayout(layoutTex_);
        ctx_->VSSetShader(vsTex_, nullptr, 0);
        ctx_->PSSetShader(psShadowClip_, nullptr, 0);
        ctx_->PSSetSamplers(0, 1, &sampler_);
        if (cbBatch_) ctx_->VSSetConstantBuffers(1, 1, &cbBatch_);
        if (cbWind_) {
            ctx_->UpdateSubresource(cbWind_, 0, nullptr, wind_, 0, 0);
            ctx_->VSSetConstantBuffers(3, 1, &cbWind_);
        }
        for (const BatchDraw& d : drawList_) {
            const SceneBatch& b = *d.b;
            StreamGpu& g = *d.g;
            if (b.blend || b.texture < 0 || size_t(b.texture) >= textures_.size()) continue;
            if (cbBatch_) {  // the same vertex inputs as the shading pass (sway), so the depths match exactly
                const float flags[8] = {0, float(b.blend), b.sway ? 1.0f : 0.0f, b.baseZ, 0, 0, 0, 0};
                ctx_->UpdateSubresource(cbBatch_, 0, nullptr, flags, 0, 0);
            }
            ctx_->PSSetShaderResources(0, 1, &textures_[size_t(b.texture)]);
            ctx_->IASetVertexBuffers(0, 1, &g.vb, &tstride, &offset);
            if (refCulling_ && g.chunked && !g.chunks.empty()) {
                ctx_->IASetIndexBuffer(g.ib, DXGI_FORMAT_R32_UINT, 0);
                forVisibleRuns(frustum, g.chunks, [&](uint32_t first, uint32_t n) { drawIdx(n * 3, first * 3); });
            } else {
                drawN(UINT(b.v.size()), 0);
            }
        }
        if (vbRefTex_ && refVisible_) {
            if (cbBatch_) {
                const float flags[8] = {0, 0, 0, 0, 0, 0, 1.0f, 0};
                ctx_->UpdateSubresource(cbBatch_, 0, nullptr, flags, 0, 0);
            }
            ctx_->IASetVertexBuffers(0, 1, &vbRefTex_, &tstride, &offset);
            for (size_t k = 0; k < refTexIds_.size(); ++k) {
                const int id = refTexIds_[k] >= 0 ? refTexIds_[k] : whiteTex_;
                if (id < 0 || size_t(id) >= textures_.size()) continue;
                ctx_->PSSetShaderResources(0, 1, &textures_[size_t(id)]);
                if (refCulling_ && k < refTexChunks_.size() && !refTexChunks_[k].empty())
                    forVisibleRuns(frustum, refTexChunks_[k], [&](uint32_t first, uint32_t n) { drawN(n * 3, refTexFirst_[k] + first * 3); });
                else
                    drawN(refTexCount_[k], refTexFirst_[k]);
            }
        }
        if (refVisible_ && !refTexInst_.empty()) {
            texPsOverride_ = psShadowClip_;
            drawReferenceTexInstances();
            texPsOverride_ = nullptr;
        }
        ID3D11ShaderResourceView* noTex[1] = {nullptr};
        ctx_->PSSetShaderResources(0, 1, noTex);
        ctx_->OMSetBlendState(blend_, bf, 0xffffffff);
        ctx_->IASetInputLayout(layout_);
        ctx_->VSSetShader(vs_, nullptr, 0);
        ctx_->PSSetShader(ps_, nullptr, 0);
    }
    // After the pre-pass the opaque textured geometry only tests depth (equal depths pass LESS_EQUAL).
    ID3D11DepthStencilState* texOpaqueDs = prepass ? dsTest_ : dsWrite_;
    if (!drawList_.empty()) {
        // One vertex buffer per batch; one draw per texture, or one per run of visible chunks.'''),
 ('''        ctx_->PSSetShader(psTex_, nullptr, 0);
        ctx_->PSSetSamplers(0, 1, &sampler_);
        ctx_->RSSetState(rsTris_);
        ctx_->OMSetDepthStencilState(dsWrite_, 0);
        ctx_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        if (cbBatch_) ctx_->PSSetConstantBuffers(1, 1, &cbBatch_);''', '''        ctx_->PSSetShader(psTex_, nullptr, 0);
        ctx_->PSSetSamplers(0, 1, &sampler_);
        ctx_->RSSetState(rsTris_);
        ctx_->OMSetDepthStencilState(texOpaqueDs, 0);
        ctx_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        if (cbBatch_) ctx_->PSSetConstantBuffers(1, 1, &cbBatch_);'''),
 ('''        ctx_->PSSetShader(psTex_, nullptr, 0);
        ctx_->PSSetSamplers(0, 1, &sampler_);
        ctx_->RSSetState(rsTris_);
        ctx_->OMSetDepthStencilState(dsWrite_, 0);
        ctx_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx_->IASetVertexBuffers(0, 1, &vbRefTex_, &tstride, &offset);''', '''        ctx_->PSSetShader(psTex_, nullptr, 0);
        ctx_->PSSetSamplers(0, 1, &sampler_);
        ctx_->RSSetState(rsTris_);
        ctx_->OMSetDepthStencilState(texOpaqueDs, 0);
        ctx_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx_->IASetVertexBuffers(0, 1, &vbRefTex_, &tstride, &offset);'''),
 ('''    if (refVisible_ && !refTexInst_.empty()) {  // the same props with their textures
        ctx_->RSSetState(rsTris_);
        ctx_->OMSetDepthStencilState(dsWrite_, 0);''', '''    if (refVisible_ && !refTexInst_.empty()) {  // the same props with their textures
        ctx_->RSSetState(rsTris_);
        ctx_->OMSetDepthStencilState(texOpaqueDs, 0);'''),
])
print('ok')
