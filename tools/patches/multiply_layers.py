import io
def patch(p, pairs):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    for a, b in pairs:
        a = a.replace('\n', nl); b = b.replace('\n', nl)
        assert s.count(a) == 1, (p, s.count(a), a[:100])
        s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)
patch('src/viewport/render.cpp', [
 ('''    if (layerB >= 0.0) outA = layerB;''', '''    if (layerB >= 0.0) outA = layerB;
    // Colour mode multiply (layerP.w = 1; puddles, edge wear): below x lerp(1, layer, b), drawn with a multiplying blend
    // over the shaded result (in display space: the factor's 1/2.2 power, as the post chain is close to a gamma there).
    if (layerP.x > 0.5 && layerP.w > 0.5) return float4(pow(lerp(float3(1, 1, 1), srgbToLinear3(t.rgb), layerB), 1.0 / 2.2), 1.0);'''),
 ('''    bd.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
    bd.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
    dev_->CreateBlendState(&bd, &blendAdd_);''', '''    bd.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
    bd.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
    dev_->CreateBlendState(&bd, &blendAdd_);
    {
        D3D11_BLEND_DESC mb = bd;  // dst x src colour (stock multiply layers), alpha kept
        mb.RenderTarget[0].SrcBlend = D3D11_BLEND_ZERO;
        mb.RenderTarget[0].DestBlend = D3D11_BLEND_SRC_COLOR;
        mb.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ZERO;
        mb.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
        dev_->CreateBlendState(&mb, &blendMul_);
    }'''),
 ('''    safeRelease(blendAdd_);''', '''    safeRelease(blendAdd_);
    safeRelease(blendMul_);'''),
 ('''                ctx_->UpdateSubresource(cbLayer_, 0, nullptr, L.params, 0, 0);''', '''                ctx_->UpdateSubresource(cbLayer_, 0, nullptr, L.params, 0, 0);
                {
                    const float bfz[4] = {0, 0, 0, 0};
                    ctx_->OMSetBlendState(L.params[3] > 0.5f && blendMul_ ? blendMul_ : blend_, bfz, 0xffffffff);
                }'''),
 ('''            const float zero[16] = {};
            ctx_->UpdateSubresource(cbLayer_, 0, nullptr, zero, 0, 0);''', '''            const float zero[16] = {};
            ctx_->UpdateSubresource(cbLayer_, 0, nullptr, zero, 0, 0);
            {
                const float bfz[4] = {0, 0, 0, 0};
                ctx_->OMSetBlendState(blend_, bfz, 0xffffffff);
            }'''),
])
patch('src/viewport/render.h', [
 ('''    ID3D11BlendState* blendAdd_ = nullptr;  // additive (ONE, ONE) for blend 2 batches''',
  '''    ID3D11BlendState* blendAdd_ = nullptr;  // additive (ONE, ONE) for blend 2 batches
    ID3D11BlendState* blendMul_ = nullptr;  // dst x src colour: stock layers in colour mode multiply'''),
])
patch('src/qtapp/camera_view.cpp', [
 ('''    // Colour modes 2 and 3 leave the colour below; multiply (1) is not drawn yet (its normal / gloss blend only).
    if (lc.colorMode != 0) return true;''', '''    // Colour modes 2 and 3 leave the colour below (their normal / gloss blend only): not drawn. Multiply (1) is.
    if (lc.colorMode >= 2) return true;'''),
 ('''    const float p[16] = {1.0f, float(lc.flags), lc.sharpness, 0.0f,''', '''    const float p[16] = {1.0f, float(lc.flags), lc.sharpness, lc.colorMode == 1 ? 1.0f : 0.0f,'''),
])
print('ok')
