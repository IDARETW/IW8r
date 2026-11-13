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


H = 'src/viewport/render.h'
R = 'src/viewport/render.cpp'

# ---------------------------------------------------------------- members
patch(H, '''    ID3D11ShaderResourceView* depthSrv_ = nullptr;''', '''    ID3D11ShaderResourceView* depthSrv_ = nullptr;
    // Game Lighting's HDR scene (docs/PLAN_REMAINING.md 1.2): the lit scene in linear light (alpha marks drawn pixels),
    // then one post pass (exposure, log shaper, LUT) into the frame; editor overlays are drawn after it.
    ID3D11Texture2D* hdrTex_ = nullptr;
    ID3D11RenderTargetView* hdrRtv_ = nullptr;
    ID3D11ShaderResourceView* hdrSrv_ = nullptr;
    ID3D11VertexShader* postVs_ = nullptr;
    ID3D11PixelShader* postPs_ = nullptr;
    ID3D11PixelShader* meterHdrPs_ = nullptr;
    ID3D11RenderTargetView* sceneRtv_ = nullptr;  // where the lit scene goes this frame (hdrRtv_ or rtv_)
    bool hdrFrame_ = false;
    void drawPost();''')
patch(H, '''    bool gameAoDrawn() const { return aoValid_; }''', '''    bool gameAoDrawn() const { return aoValid_; }
    bool gameHdrDrawn() const { return hdrFrame_; }''')

# ---------------------------------------------------------------- shaders
patch(R, '''    if (layerP.x > 0.5 && layerP.w > 0.5) return float4(pow(lerp(float3(1, 1, 1), srgbToLinear3(t.rgb), layerB), 1.0 / 2.2), 1.0);''',
      '''    if (layerP.x > 0.5 && layerP.w > 0.5) {
        const float3 mulLin = lerp(float3(1, 1, 1), srgbToLinear3(t.rgb), layerB);
        return float4(game0.x > 0.5 && gameAo.w > 0.5 ? mulLin : pow(mulLin, 1.0 / 2.2), 1.0);  // HDR: a linear multiply
    }''')
patch(R, '''        if (!stockRef && i.col.a < 0.5) return float4(gamePost(gameFogApply(albedo * gameSkyScale.x, i.w, true)), 1.0);  // sky box''',
      '''        if (!stockRef && i.col.a < 0.5) {  // sky box
            const float3 sky = gameFogApply(albedo * gameSkyScale.x, i.w, true);
            return float4(gameAo.w > 0.5 ? sky : gamePost(sky), 1.0);
        }''')
patch(R, '''        c = gamePost(c);''', '''        if (gameAo.w < 0.5) c = gamePost(c);  // HDR frames: the post pass applies it (drawPost)''')
patch(R, '''void ps_shadow_clip(PSIn i) {''', '''// The HDR scene to the frame: the post chain where something was drawn (alpha > 0); the clear colour elsewhere.
Texture2D<float4> hdrScene : register(t15);
struct VFull { float4 pos : SV_POSITION; };
VFull vs_post(uint id : SV_VertexID) {
    VFull o;
    float2 p = float2((id << 1) & 2, id & 2);
    o.pos = float4(p * float2(2, -2) + float2(-1, 1), 0, 1);
    return o;
}
float4 ps_post(VFull i) : SV_TARGET {
    float4 h = hdrScene.Load(int3(i.pos.xy, 0));
    if (h.a <= 0.0) discard;
    if (gameAo.y > 0.5 || gameAo.z > 0.5) return float4(h.rgb, 1.0);  // debug views: shown as drawn
    return float4(gamePost(max(h.rgb, 0.0)), 1.0);
}
void ps_shadow_clip(PSIn i) {''')
patch(R, '''    return float4(log(max(x, 1e-5)) * w, w, 0, 0);
}
)";''', '''    return float4(log(max(x, 1e-5)) * w, w, 0, 0);
}
// The HDR scene (linear, before exposure): its luminance directly (research 95 section 3), same weights.
float4 ps_meter_hdr(V i) : SV_TARGET {
    float4 c = frame.SampleLevel(lin, i.uv, 0);
    float x = c.a > 0.0 ? dot(max(c.rgb, 0.0), float3(0.2126, 0.7152, 0.0722)) : 0.0;
    float r = saturate(length(i.uv - 0.5) * 1.41421356);
    float w = c.a > 0.0 ? 1.0 - r : 0.0;
    return float4(log(max(x, 1e-5)) * w, w, 0, 0);
}
)";''')
patch(R, '''    if (ok) {
        dev_->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, &meterVs_);
        dev_->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, &meterPs_);
    }''', '''    if (ok) {
        dev_->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, &meterVs_);
        dev_->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, &meterPs_);
        ID3DBlob* hb = nullptr;
        if (SUCCEEDED(D3DCompile(kMeterShader, std::strlen(kMeterShader), "iw8r_meter", nullptr, nullptr, "ps_meter_hdr", "ps_5_0", 0, 0, &hb, nullptr)))
            dev_->CreatePixelShader(hb->GetBufferPointer(), hb->GetBufferSize(), nullptr, &meterHdrPs_);
        safeRelease(hb);
    }''')
patch(R, '''    ctx_->PSSetShader(meterPs_, nullptr, 0);''', '''    const bool hdrMeter = hdrFrame_ && hdrSrv_ && meterHdrPs_;
    ctx_->PSSetShader(hdrMeter ? meterHdrPs_ : meterPs_, nullptr, 0);''')
patch(R, '''    ID3D11ShaderResourceView* srvs[2] = {srv_, invGreySrv_};''', '''    ID3D11ShaderResourceView* srvs[2] = {hdrMeter ? hdrSrv_ : srv_, invGreySrv_};''')
# compile the post shaders with the textured ones
patch(R, '''            ID3DBlob* pps = nullptr;
            if (compileTex("ps_prepass", "ps_5_0", pps)) {''', '''            ID3DBlob* pvs = nullptr;
            ID3DBlob* ppo = nullptr;
            if (compileTex("vs_post", "vs_5_0", pvs) && compileTex("ps_post", "ps_5_0", ppo)) {
                dev_->CreateVertexShader(pvs->GetBufferPointer(), pvs->GetBufferSize(), nullptr, &postVs_);
                dev_->CreatePixelShader(ppo->GetBufferPointer(), ppo->GetBufferSize(), nullptr, &postPs_);
            }
            safeRelease(pvs), safeRelease(ppo);
            ID3DBlob* pps = nullptr;
            if (compileTex("ps_prepass", "ps_5_0", pps)) {''')
patch(R, '''    safeRelease(psPrepass_);
    safeRelease(cbTiles_);''', '''    safeRelease(psPrepass_);
    safeRelease(cbTiles_);
    safeRelease(postVs_), safeRelease(postPs_), safeRelease(meterHdrPs_);''')
# the flag in b4
patch(R, '''    g[50] = albedoShow ? 1.0f : 0.0f;''', '''    g[50] = albedoShow ? 1.0f : 0.0f;
    g[51] = hdrFrame_ ? 1.0f : 0.0f;  // the lit scene goes to the HDR target: no post chain in the shaders''')
# blending keeps the largest alpha (alpha marks the pixels drawn in the HDR scene)
patch(R, '''    bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
    bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    dev_->CreateBlendState(&bd, &blend_);''', '''    // Alpha keeps the largest value: it marks the pixels the HDR scene drew (drawPost); the frame's alpha is unused.
    bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
    bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_MAX;
    bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    dev_->CreateBlendState(&bd, &blend_);''')

# ---------------------------------------------------------------- targets
patch(R, '''    td.Format = DXGI_FORMAT_R8_UNORM;
    td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    for (int k = 0; k < 2; ++k)''', '''    td.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    if (SUCCEEDED(dev_->CreateTexture2D(&td, nullptr, &hdrTex_))) {
        dev_->CreateRenderTargetView(hdrTex_, nullptr, &hdrRtv_);
        dev_->CreateShaderResourceView(hdrTex_, nullptr, &hdrSrv_);
    }
    td.Format = DXGI_FORMAT_R8_UNORM;
    td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    for (int k = 0; k < 2; ++k)''')
patch(R, '''    safeRelease(depthSrv_);
    safeRelease(depth_);''', '''    safeRelease(depthSrv_);
    safeRelease(depth_);
    safeRelease(hdrSrv_), safeRelease(hdrRtv_), safeRelease(hdrTex_);''')

# ---------------------------------------------------------------- the frame
patch(R, '''    ctx_->OMSetRenderTargets(1, &rtv_, dsv_);
    if (!cbTiles_ && dev_) {''', '''    // Game Lighting draws the lit scene into the HDR target (IW8R_NO_HDR: straight into the frame, as before).
    static const bool noHdr = std::getenv("IW8R_NO_HDR") != nullptr;
    hdrFrame_ = game_.on && gameLutSrv_ && hdrRtv_ && postVs_ && postPs_ && !noHdr && !in.capture;
    sceneRtv_ = hdrFrame_ ? hdrRtv_ : rtv_;
    if (hdrFrame_) {
        const float zero[4] = {0, 0, 0, 0};
        ctx_->ClearRenderTargetView(hdrRtv_, zero);
    }
    ctx_->OMSetRenderTargets(1, &sceneRtv_, dsv_);
    if (!cbTiles_ && dev_) {''')
patch(R, '''        inShadowPass_ = false;
        ctx_->OMSetRenderTargets(1, &rtv_, dsv_);''', '''        inShadowPass_ = false;
        ctx_->OMSetRenderTargets(1, &sceneRtv_, dsv_);''')
patch(R, '''    ctx_->PSSetShaderResources(0, 2, none2);
    ctx_->OMSetRenderTargets(1, &rtv_, dsv_);
    aoValid_ = true;''', '''    ctx_->PSSetShaderResources(0, 2, none2);
    ctx_->OMSetRenderTargets(1, &sceneRtv_, dsv_);
    aoValid_ = true;''')
patch(R, '''    if (refVisible_ && !refInst_.empty()) {  // instanced reference props (3.20)
        ctx_->RSSetState(rsTris_);
        ctx_->OMSetDepthStencilState(dsWrite_, 0);
        ctx_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        drawReferenceInstances(in.capture || in.clean ? ReferencePass::Capture : ReferencePass::Screen);
    }
    if (refVisible_ && !refTexInst_.empty()) {  // the same props with their textures
        ctx_->RSSetState(rsTris_);
        ctx_->OMSetDepthStencilState(texOpaqueDs, 0);
        ctx_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        drawReferenceTexInstances();
    }''', '''    if (refVisible_ && !refInst_.empty() && !hdrFrame_) {  // instanced reference props (3.20)
        ctx_->RSSetState(rsTris_);
        ctx_->OMSetDepthStencilState(dsWrite_, 0);
        ctx_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        drawReferenceInstances(in.capture || in.clean ? ReferencePass::Capture : ReferencePass::Screen);
    }
    if (refVisible_ && !refTexInst_.empty()) {  // the same props with their textures
        ctx_->RSSetState(rsTris_);
        ctx_->OMSetDepthStencilState(texOpaqueDs, 0);
        ctx_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        drawReferenceTexInstances();
    }
    if (hdrFrame_) {
        // The lit scene is done: the post chain into the frame, then the unlit pieces and the editor's overlays.
        drawPost();
        if (refVisible_ && !refInst_.empty()) {
            ctx_->RSSetState(rsTris_);
            ctx_->OMSetDepthStencilState(dsWrite_, 0);
            ctx_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            drawReferenceInstances(in.capture || in.clean ? ReferencePass::Capture : ReferencePass::Screen);
        }
    }''')
patch(R, '''void SceneRenderer::drawAo(const float* viewProj) {''', '''void SceneRenderer::drawPost() {
    IW8R_PROF("d.post");
    writeGameCb();  // b4 (exposure), the LUT on t9 and its sampler
    ctx_->OMSetRenderTargets(1, &rtv_, nullptr);
    ctx_->OMSetBlendState(nullptr, nullptr, 0xffffffff);
    ctx_->OMSetDepthStencilState(dsOff_, 0);
    ctx_->RSSetState(rsTris_);
    ctx_->IASetInputLayout(nullptr);
    ctx_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx_->VSSetShader(postVs_, nullptr, 0);
    ctx_->PSSetShader(postPs_, nullptr, 0);
    ctx_->PSSetShaderResources(15, 1, &hdrSrv_);
    ctx_->Draw(3, 0);
    ID3D11ShaderResourceView* none = nullptr;
    ctx_->PSSetShaderResources(15, 1, &none);
    sceneRtv_ = rtv_;
    ctx_->OMSetRenderTargets(1, &rtv_, dsv_);
    const float bf[4] = {0, 0, 0, 0};
    ctx_->OMSetBlendState(blend_, bf, 0xffffffff);
    ctx_->IASetInputLayout(layout_);
    ctx_->VSSetShader(vs_, nullptr, 0);
    ctx_->PSSetShader(ps_, nullptr, 0);
}

void SceneRenderer::drawAo(const float* viewProj) {''')
print('ok')
