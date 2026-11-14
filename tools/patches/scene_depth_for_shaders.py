import io

# Scene depth as an input of custom surface shaders (owner 2026-11-14: toon lines must be seamless; lines found on
# screen from the scene's depth are continuous by construction). Viewport side and the shared shader interface:
#   IW8RSurface.pixel / viewDepth, and float iw8rSceneDepth(float2 pixel): the view depth of what is drawn at a pixel
#   (world units along the view, 1e9 for the sky). The viewport keeps the finished frame's depth at full size and
#   hands it to the next frame (as its reflections do); a frame drawn from a new view asks for one more frame.

ROOT = 'E:/IW8-Radiant/'


def read(p):
    return io.open(ROOT + p, encoding='utf-8', newline='').read()


def write(p, s):
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s)


def patch(p, a, b, count=1):
    s = read(p)
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == count, (p, s.count(a), a[:70])
    write(p, s.replace(a, b))


# ---- core: the struct and the checks ----
patch('src/core/custom_shader.h', '''    float  edgePixels; // distance to the nearest edge of the brush face, in screen pixels (1e6: none near, or no data)''',
      '''    float2 pixel;      // this pixel's position on screen (pixel centres at .5)
    float  viewDepth;  // this pixel's depth along the view, world units (compare with iw8rSceneDepth)
    float  edgePixels; // distance to the nearest edge of the brush face, in screen pixels (1e6: none near, or no data)''')
patch('src/core/custom_shader.h', '''// A shader file may hold two functions (at least one):''',
      '''// Besides IW8RSurface a surface shader may call
//   float iw8rSceneDepth(float2 pixel)
// the view depth (world units along the view; 1e9 for the sky or outside the screen) of what is drawn at a pixel of
// the screen. Each host defines it: the viewport from the frame before (a moving camera sees it one frame late), the
// built map from the game's scene depth. A shader finds outlines with it by comparing a pixel's neighbours
// (data/shaders/toon.iw8shader.hlsl); a value at `s.pixel` that is not near `s.viewDepth` means the host has no
// usable depth right now.
bool customShaderUsesSceneDepth(const std::string& code);

// A shader file may hold two functions (at least one):''')
patch('src/core/custom_shader.cpp', '''bool customShaderUsesEdges(const std::string& code) {''',
      '''bool customShaderUsesSceneDepth(const std::string& code) {
    static const std::regex re(R"(\\biw8rSceneDepth\\b)");
    return std::regex_search(withoutComments(code), re);
}

bool customShaderUsesEdges(const std::string& code) {''')

# ---- viewport shader source ----
patch('src/viewport/render.cpp', '''Texture2D<float4> prevColor : register(t16);
Texture2D<float> prevZ : register(t17);''', '''Texture2D<float4> prevColor : register(t16);
Texture2D<float> prevZ : register(t17);
// The scene depth custom surface shaders may read (core custom_shader iw8rSceneDepth): the frame before, at full
// size, as linear view depth (1e9: sky). Unbound (no frame kept yet): Load gives 0, which reads as "no depth".
Texture2D<float> iw8rSceneZ : register(t31);
float iw8rSceneDepth(float2 pixel) {
    uint zw, zh;
    iw8rSceneZ.GetDimensions(zw, zh);
    if (pixel.x < 0.0 || pixel.y < 0.0 || pixel.x >= float(zw) || pixel.y >= float(zh)) return 1e9;
    return iw8rSceneZ.Load(int3(int2(pixel), 0));
}''')
patch('src/viewport/render.cpp', '''            s.faceNormal = dot(i.n, i.n) > 0.5 ? normalize(i.n) : N;
            iw8rEdgeFields(s, i.edge, ddx(i.w), ddy(i.w));
            c = iw8rSurface(s);''', '''            s.faceNormal = dot(i.n, i.n) > 0.5 ? normalize(i.n) : N;
            s.pixel = i.pos.xy;
            s.viewDepth = mul(float4(i.w, 1.0), viewProj).w;
            iw8rEdgeFields(s, i.edge, ddx(i.w), ddy(i.w));
            c = iw8rSurface(s);''')
patch('src/viewport/render.cpp', '''float ps_prevz(V i) : SV_TARGET {''', '''// The same at full size, one pixel each: the scene depth custom surface shaders read in the next frame.
float ps_scenez(V i) : SV_TARGET {
    const int2 q = int2(i.pos.xy);
    const float d = depthTex.Load(int3(q, 0));
    if (d >= 0.999999) return 1e9;
    return mul(float4(worldAt((float2(q) + 0.5) / aoPrm.zw, d), 1.0), aoVp).w;
}
float ps_prevz(V i) : SV_TARGET {''')

# ---- renderer: resources ----
patch('src/viewport/render.h', '''    ID3D11PixelShader* prevZPs_ = nullptr;''', '''    ID3D11PixelShader* prevZPs_ = nullptr;
    // The finished frame's depth at full size, for custom surface shaders in the next frame (iw8rSceneDepth, t31).
    ID3D11Texture2D* sceneZTex_ = nullptr;
    ID3D11RenderTargetView* sceneZRtv_ = nullptr;
    ID3D11ShaderResourceView* sceneZSrv_ = nullptr;
    ID3D11PixelShader* sceneZPs_ = nullptr;
    bool sceneZValid_ = false;     // it holds a frame
    bool sceneZStale_ = false;     // ... drawn from another view than the frame just drawn
    float sceneZViewProj_[16] = {};
    void keepSceneDepth(const float* viewProj);''')
patch('src/viewport/render.h', '''    bool referenceTomeCull() const { return !tomeSurfBits_.empty() || !tomeModelBits_.empty(); }''',
      '''    // Custom surface shaders read the frame before (iw8rSceneDepth). True after a frame that was drawn from another
    // view than the depth it read, while a custom shader is in use: one more frame shows the settled picture.
    bool sceneDepthStale() const { return sceneZStale_ && !customPs_.empty(); }
    bool referenceTomeCull() const { return !tomeSurfBits_.empty() || !tomeModelBits_.empty(); }''')
patch('src/viewport/render.cpp', '''    safeRelease(prevZSrv_), safeRelease(prevZRtv_), safeRelease(prevZTex_);
    prevValid_ = false;''', '''    safeRelease(prevZSrv_), safeRelease(prevZRtv_), safeRelease(prevZTex_);
    safeRelease(sceneZSrv_), safeRelease(sceneZRtv_), safeRelease(sceneZTex_);
    sceneZValid_ = false;
    prevValid_ = false;''')
patch('src/viewport/render.cpp', '''        if (compile("ps_prevz", "ps_5_0", zb)) dev_->CreatePixelShader(zb->GetBufferPointer(), zb->GetBufferSize(), nullptr, &prevZPs_);''',
      '''        if (compile("ps_prevz", "ps_5_0", zb)) dev_->CreatePixelShader(zb->GetBufferPointer(), zb->GetBufferSize(), nullptr, &prevZPs_);
        safeRelease(zb);
        if (compile("ps_scenez", "ps_5_0", zb)) dev_->CreatePixelShader(zb->GetBufferPointer(), zb->GetBufferSize(), nullptr, &sceneZPs_);''')
patch('src/viewport/render.cpp', '''    safeRelease(prevZPs_), safeRelease(prevColorPs_), safeRelease(cbPrev_);''',
      '''    safeRelease(prevZPs_), safeRelease(prevColorPs_), safeRelease(cbPrev_);
    safeRelease(sceneZPs_);''')
patch('src/viewport/render.cpp', '''        hd.Format = DXGI_FORMAT_R32_FLOAT;
        hd.MipLevels = 1;
        hd.MiscFlags = 0;
        if (SUCCEEDED(dev_->CreateTexture2D(&hd, nullptr, &prevZTex_))) {
            dev_->CreateRenderTargetView(prevZTex_, nullptr, &prevZRtv_);
            dev_->CreateShaderResourceView(prevZTex_, nullptr, &prevZSrv_);
        }''', '''        hd.Format = DXGI_FORMAT_R32_FLOAT;
        hd.MipLevels = 1;
        hd.MiscFlags = 0;
        if (SUCCEEDED(dev_->CreateTexture2D(&hd, nullptr, &prevZTex_))) {
            dev_->CreateRenderTargetView(prevZTex_, nullptr, &prevZRtv_);
            dev_->CreateShaderResourceView(prevZTex_, nullptr, &prevZSrv_);
        }
        hd.Width = td.Width, hd.Height = td.Height;
        if (SUCCEEDED(dev_->CreateTexture2D(&hd, nullptr, &sceneZTex_))) {
            dev_->CreateRenderTargetView(sceneZTex_, nullptr, &sceneZRtv_);
            dev_->CreateShaderResourceView(sceneZTex_, nullptr, &sceneZSrv_);
        }
        sceneZValid_ = false;''')

# ---- renderer: keep at the frame's end, bind in the next frame ----
patch('src/viewport/render.cpp', '''void SceneRenderer::keepFrameForSsr(const float* viewProj) {''',
      '''// The finished frame's depth as linear view depth at full size (ps_scenez), for custom surface shaders in the next
// frame. Only while a custom shader exists.
void SceneRenderer::keepSceneDepth(const float* viewProj) {
    if (customPs_.empty() || !sceneZRtv_ || !sceneZPs_ || !depthSrv_ || !aoVs_ || !createAoResources()) {
        sceneZValid_ = false;
        return;
    }
    float cb[40];
    std::memcpy(cb, viewProj, 64);
    if (!invert4(viewProj, cb + 16)) {
        sceneZValid_ = false;
        return;
    }
    cb[32] = fog_[0], cb[33] = fog_[1], cb[34] = fog_[2], cb[35] = 0;
    cb[36] = 40.0f, cb[37] = 1.0f, cb[38] = float(w_), cb[39] = float(h_);
    ctx_->UpdateSubresource(cbAo_, 0, nullptr, cb, 0, 0);
    ID3D11ShaderResourceView* none = nullptr;
    ctx_->PSSetShaderResources(31, 1, &none);  // this frame's input becomes the output
    D3D11_VIEWPORT fv{0, 0, float(w_), float(h_), 0, 1};
    ctx_->RSSetViewports(1, &fv);
    ctx_->OMSetBlendState(nullptr, nullptr, 0xffffffff);
    ctx_->OMSetDepthStencilState(dsOff_, 0);
    ctx_->RSSetState(rsTris_);
    ctx_->IASetInputLayout(nullptr);
    ctx_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx_->VSSetShader(aoVs_, nullptr, 0);
    ctx_->PSSetConstantBuffers(8, 1, &cbAo_);
    ctx_->OMSetRenderTargets(1, &sceneZRtv_, nullptr);
    ctx_->PSSetShader(sceneZPs_, nullptr, 0);
    ctx_->PSSetShaderResources(0, 1, &depthSrv_);
    ctx_->Draw(3, 0);
    ctx_->PSSetShaderResources(0, 1, &none);
    ctx_->OMSetRenderTargets(1, &sceneRtv_, nullptr);
    // The frame just drawn read the depth of the view before: stale when that was another view.
    sceneZStale_ = !sceneZValid_ || std::memcmp(sceneZViewProj_, viewProj, 64) != 0;
    std::memcpy(sceneZViewProj_, viewProj, 64);
    sceneZValid_ = true;
}

void SceneRenderer::keepFrameForSsr(const float* viewProj) {''')
patch('src/viewport/render.cpp', '''        keepFrameForSsr(viewProj);
        std::memcpy(lastViewProj_, viewProj, sizeof(lastViewProj_));''', '''        keepSceneDepth(viewProj);
        keepFrameForSsr(viewProj);
        std::memcpy(lastViewProj_, viewProj, sizeof(lastViewProj_));''')
patch('src/viewport/render.cpp', '''        ID3D11ShaderResourceView* ps[2] = {ssrNow ? prevColorSrv_ : nullptr, ssrNow ? prevZSrv_ : nullptr};
        ctx_->PSSetShaderResources(16, 2, ps);''', '''        ID3D11ShaderResourceView* ps[2] = {ssrNow ? prevColorSrv_ : nullptr, ssrNow ? prevZSrv_ : nullptr};
        ctx_->PSSetShaderResources(16, 2, ps);
        ID3D11ShaderResourceView* sceneZ = hdrFrame_ && sceneZValid_ && !customPs_.empty() ? sceneZSrv_ : nullptr;
        ctx_->PSSetShaderResources(31, 1, &sceneZ);''')
print('ok')
