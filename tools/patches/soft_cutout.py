import io


def patch(p, a, b, n=1):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


R = 'src/viewport/render.cpp'
patch(R, '''    if (batchFlags.y < 0.5) clip(t.a - (game0.x > 0.5 && batchFlags2.z > 0.5 ? 0.5 : 0.3));''',
      '''    if (batchFlags.y < 0.5) {
        const float cutAt = game0.x > 0.5 && batchFlags2.z > 0.5 ? 0.5 : 0.3;
        if (batchFlags.y < -0.5) {
            // Placed props (blend mode -1): the cut-out edge is anti-aliased from the alpha's screen slope and blended,
            // depth still written. The game's TAA smooths its sub-pixel grass blades the same way; a hard cut leaves
            // speckled noise (Speedball grass: 2.7x the in-game pixel-to-pixel contrast).
            const float cov = saturate((t.a - cutAt) / max(fwidth(t.a), 1e-4) + 0.5);
            clip(cov - 1.0 / 255.0);
            outA = cov;
        } else {
            clip(t.a - cutAt);
        }
    }''')
patch(R, '''    if (batchFlags.y < 0.5) {
        const float cutAt''', '''    if (batchFlags.y < 0.5) {
        const float cutAt''')
# outA must exist before: move its declaration above the cut
patch(R, '''    float outA = 1.0;
    // Cut-outs: the stock alpha-tested shaders keep opacity above 128 / 255 (research 100); the editor's own materials
    // keep the editor's 0.3.
    if (batchFlags.y < 0.5) {''', '''    float outA = 1.0;
    // Cut-outs: the stock alpha-tested shaders keep opacity above 128 / 255 (research 100); the editor's own materials
    // keep the editor's 0.3.
    if (batchFlags.y < 0.5) {''')
patch(R, '''        } else {
            clip(t.a - cutAt);
        }
    }
    else outA = t.a > 0.99 ? 0.6 : t.a;''', '''        } else {
            clip(t.a - cutAt);
        }
    } else {
        outA = t.a > 0.99 ? 0.6 : t.a;
    }''')
# the instanced draw: blend mode -1 and the alpha blend state (the depth prepass override keeps its own shader)
patch(R, '''    if (cbBatch_) {
        const float flags[8] = {0, 0, 0, 0, 0, 0, game_.skyIsStock ? 1.0f : 0.0f, 0};
        ctx_->UpdateSubresource(cbBatch_, 0, nullptr, flags, 0, 0);''', '''    static const bool hardCut = std::getenv("IW8R_HARD_CUTOUT") != nullptr;
    const bool soft = !texPsOverride_ && !hardCut && blend_;
    if (soft) {
        const float bf[4] = {0, 0, 0, 0};
        ctx_->OMSetBlendState(blend_, bf, 0xffffffff);
    }
    if (cbBatch_) {
        const float flags[8] = {0, soft ? -1.0f : 0.0f, 0, 0, 0, 0, game_.skyIsStock ? 1.0f : 0.0f, 0};
        ctx_->UpdateSubresource(cbBatch_, 0, nullptr, flags, 0, 0);''')
print('ok')
patch(R, '''    const bool soft = !texPsOverride_ && !hardCut && blend_;
    if (soft) {''', '''    const bool soft = !texPsOverride_ && !hardCut && blend_;
    ID3D11BlendState* prevBlend = nullptr;
    float prevFactor[4] = {0, 0, 0, 0};
    UINT prevMask = 0xffffffff;
    ctx_->OMGetBlendState(&prevBlend, prevFactor, &prevMask);
    if (soft) {''')
patch(R, '''    ID3D11Buffer* none[2] = {nullptr, nullptr};
    ctx_->IASetVertexBuffers(0, 2, none, strides, offsets);
    ID3D11ShaderResourceView* noTex[1] = {nullptr};
    ctx_->PSSetShaderResources(0, 1, noTex);
    ctx_->IASetInputLayout(layout_);
    ctx_->VSSetShader(vs_, nullptr, 0);
    ctx_->PSSetShader(ps_, nullptr, 0);
}''', '''    ID3D11Buffer* none[2] = {nullptr, nullptr};
    ctx_->IASetVertexBuffers(0, 2, none, strides, offsets);
    ID3D11ShaderResourceView* noTex[1] = {nullptr};
    ctx_->PSSetShaderResources(0, 1, noTex);
    ctx_->IASetInputLayout(layout_);
    ctx_->VSSetShader(vs_, nullptr, 0);
    ctx_->PSSetShader(ps_, nullptr, 0);
    ctx_->OMSetBlendState(prevBlend, prevFactor, prevMask);
    if (prevBlend) prevBlend->Release();
}''')
print('ok2')
