import io


def patch(p, pairs):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    for a, b in pairs:
        a = a.replace('\n', nl); b = b.replace('\n', nl)
        assert s.count(a) == 1, (p, s.count(a), a[:80])
        s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


patch('src/viewport/render.h', [
 ('''    bool hasGameLightmap() const { return gameLmSrv_[0] != nullptr; }''',
  '''    bool hasGameLightmap() const { return gameLmSrv_[0] != nullptr; }
    // The stock reference world's own lightmap (Game Lighting on a stock map, research 94 / 95): four floats per texel,
    // a = (ambient part f x C, 1), b = (directional part (1 - f) x C / max(D.z, .001), 0), d = (the tangent-space
    // direction D, 0). The reference vertices carry its UVs. null = none.
    void setStockLightmap(int width, int height, const float* a, const float* b, const float* d);
    bool hasStockLightmap() const { return stockLmSrv_[0] != nullptr; }'''),
 ('''    ID3D11Texture3D* gameLutTex_ = nullptr;''', '''    ID3D11Texture2D* stockLmTex_[3] = {nullptr, nullptr, nullptr};
    ID3D11ShaderResourceView* stockLmSrv_[3] = {nullptr, nullptr, nullptr};
    void writeGameCb();
    ID3D11Texture3D* gameLutTex_ = nullptr;'''),
])

patch('src/viewport/render.cpp', [
 # shader: stock reference branch
 ('''        float3 albedo = srgbToLinear3(t.rgb) * i.col.rgb;  // i.col: selection and brush entity tints (white otherwise)
        if (i.col.a < 0.5) return float4(gamePost(albedo), 1.0);  // sky box: unlit''',
  '''        // batchFlags2.z: the stock reference world (its vertex colours are an editor shade, not a tint), .w: its own
        // lightmap is bound (the stock world shader's formula, tangent-space direction).
        const bool stockRef = batchFlags2.z > 0.5;
        float3 albedo = srgbToLinear3(t.rgb) * (stockRef ? float3(1, 1, 1) : i.col.rgb);  // i.col: selection and brush entity tints
        if (!stockRef && i.col.a < 0.5) return float4(gamePost(albedo), 1.0);  // sky box: unlit'''),
 ('''        if (game0.z > 0.5 && i.lm.x >= 0) {
            float4 a = gameLmA.SampleLevel(lutSamp, i.lm, 0);
            float3 b = gameLmB.SampleLevel(lutSamp, i.lm, 0).rgb;
            float3 d = gameLmD.SampleLevel(lutSamp, i.lm, 0).xyz;
            indirect = a.rgb * saturate(dot(N, Nf)) + b * saturate(dot(normalize(d + 1e-6), N));
            visibility = a.a;
        }''', '''        if ((stockRef ? batchFlags2.w > 0.5 : game0.z > 0.5) && i.lm.x >= 0) {
            float4 a = gameLmA.SampleLevel(lutSamp, i.lm, 0);
            float3 b = gameLmB.SampleLevel(lutSamp, i.lm, 0).rgb;
            float3 d = gameLmD.SampleLevel(lutSamp, i.lm, 0).xyz;
            if (stockRef) {
                // Stock: light = f C + (1 - f) C / D.z x sat(N . Dw), D in the surface's tangent frame (here the texture
                // frame from the derivatives; on a flat normal N . Dw = D.z whatever the frame).
                float3 dp1 = ddx(i.w), dp2 = ddy(i.w);
                float2 du1 = ddx(i.uv), du2 = ddy(i.uv);
                float3 p2 = cross(dp2, Nf), p1 = cross(Nf, dp1);
                float3 T = p2 * du1.x + p1 * du2.x, B = p2 * du1.y + p1 * du2.y;
                float s = rsqrt(max(max(dot(T, T), dot(B, B)), 1e-20));
                float3 Dw = normalize(T * s * d.x + B * s * d.y + Nf * d.z + 1e-6);
                indirect = a.rgb + b * saturate(dot(N, Dw));
            } else {
                indirect = a.rgb * saturate(dot(N, Nf)) + b * saturate(dot(normalize(d + 1e-6), N));
                visibility = a.a;
            }
        }'''),
 # release
 ('''    safeRelease(gameLutSrv_);
    safeRelease(gameLutTex_);
    safeRelease(meterSrv_)''', '''    safeRelease(gameLutSrv_);
    safeRelease(gameLutTex_);
    for (int k = 0; k < 3; ++k) safeRelease(stockLmSrv_[k]), safeRelease(stockLmTex_[k]);
    safeRelease(meterSrv_)'''),
 # setter + cb writer
 ('''void SceneRenderer::setGameTonemapLut(int size, const uint8_t* rgba) {''', '''void SceneRenderer::setStockLightmap(int width, int height, const float* a, const float* b, const float* d) {
    for (int k = 0; k < 3; ++k) safeRelease(stockLmSrv_[k]), safeRelease(stockLmTex_[k]);
    if (!dev_ || width < 1 || height < 1 || !a || !b || !d) return;
    const float* src[3] = {a, b, d};
    for (int k = 0; k < 3; ++k) {
        D3D11_TEXTURE2D_DESC td{};
        td.Width = UINT(width), td.Height = UINT(height);
        td.MipLevels = td.ArraySize = 1;
        td.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_IMMUTABLE;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        // Half floats: a 2048 x 1024 stock lightmap is 16 MB per plane in float32.
        std::vector<uint16_t> half(size_t(width) * size_t(height) * 4);
        for (size_t i = 0; i < half.size(); ++i) half[i] = floatToHalf(src[k][i]);
        D3D11_SUBRESOURCE_DATA init{half.data(), UINT(width) * 8, 0};
        if (FAILED(dev_->CreateTexture2D(&td, &init, &stockLmTex_[k])) ||
            FAILED(dev_->CreateShaderResourceView(stockLmTex_[k], nullptr, &stockLmSrv_[k]))) {
            for (int j = 0; j < 3; ++j) safeRelease(stockLmSrv_[j]), safeRelease(stockLmTex_[j]);
            return;
        }
    }
}

void SceneRenderer::writeGameCb() {
    if (!cbGame_) return;
    const bool on = game_.on && gameLutSrv_ != nullptr;
    const float g[16] = {on ? 1.0f : 0.0f, game_.lutScale, on && gameLmSrv_[0] ? 1.0f : 0.0f, 0,
                         game_.sunToward[0], game_.sunToward[1], game_.sunToward[2], 0,
                         game_.sunColor[0], game_.sunColor[1], game_.sunColor[2], 0,
                         game_.sky[0], game_.sky[1], game_.sky[2], 0};
    ctx_->UpdateSubresource(cbGame_, 0, nullptr, g, 0, 0);
    ctx_->PSSetConstantBuffers(4, 1, &cbGame_);
    ID3D11ShaderResourceView* gl = gameLutSrv_;
    ctx_->PSSetShaderResources(9, 1, &gl);
    if (lutSampler_) ctx_->PSSetSamplers(2, 1, &lutSampler_);
}

void SceneRenderer::setGameTonemapLut(int size, const uint8_t* rgba) {'''),
 ('''        if (cbGame_) {
            const bool on = game_.on && gameLutSrv_ != nullptr;
            const float g[16] = {on ? 1.0f : 0.0f, game_.lutScale, on && gameLmSrv_[0] ? 1.0f : 0.0f, 0,
                                 game_.sunToward[0], game_.sunToward[1], game_.sunToward[2], 0,
                                 game_.sunColor[0], game_.sunColor[1], game_.sunColor[2], 0,
                                 game_.sky[0], game_.sky[1], game_.sky[2], 0};
            ctx_->UpdateSubresource(cbGame_, 0, nullptr, g, 0, 0);
            ctx_->PSSetConstantBuffers(4, 1, &cbGame_);
            ID3D11ShaderResourceView* gs[4] = {gameLmSrv_[0], gameLmSrv_[1], gameLmSrv_[2], gameLutSrv_};
            ctx_->PSSetShaderResources(6, 4, gs);
            if (lutSampler_) ctx_->PSSetSamplers(2, 1, &lutSampler_);
        }''', '''        if (cbGame_) {
            writeGameCb();
            ID3D11ShaderResourceView* gs[3] = {gameLmSrv_[0], gameLmSrv_[1], gameLmSrv_[2]};
            ctx_->PSSetShaderResources(6, 3, gs);
        }'''),
 # reference draw: flags + bind stock lightmap
 ('''        ctx_->IASetVertexBuffers(0, 1, &vbRefTex_, &tstride, &offset);
        if (cbBatch_) {
            const float flags[8] = {0, 0, 0, 0, 0, 0, 0, 0};''', '''        ctx_->IASetVertexBuffers(0, 1, &vbRefTex_, &tstride, &offset);
        writeGameCb();
        {
            ID3D11ShaderResourceView* sl[3] = {stockLmSrv_[0], stockLmSrv_[1], stockLmSrv_[2]};
            ctx_->PSSetShaderResources(6, 3, sl);
        }
        if (cbBatch_) {
            const float flags[8] = {0, 0, 0, 0, 0, 0, 1.0f, stockLmSrv_[0] ? 1.0f : 0.0f};'''),
])

# floatToHalf helper in the anonymous namespace of render.cpp
s = io.open('src/viewport/render.cpp', encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
if 'uint16_t floatToHalf(' not in s:
    a = 'template <class T>' + nl + 'void safeRelease(T*& p) {'
    assert s.count(a) == 1
    helper = '''// IEEE binary16 from float (round to nearest; the lightmap values are finite and positive or small).
uint16_t floatToHalf(float f) {
    uint32_t x;
    std::memcpy(&x, &f, 4);
    const uint32_t sign = (x >> 16) & 0x8000u;
    int exp = int((x >> 23) & 0xff) - 127 + 15;
    uint32_t mant = x & 0x7fffffu;
    if (exp <= 0) {
        if (exp < -10) return uint16_t(sign);
        mant |= 0x800000u;
        const uint32_t shift = uint32_t(14 - exp);
        return uint16_t(sign | ((mant + (1u << (shift - 1))) >> shift));
    }
    if (exp >= 31) return uint16_t(sign | 0x7bffu);  // clamp to the largest finite half
    const uint32_t h = sign | (uint32_t(exp) << 10) | (mant >> 13);
    return uint16_t(h + ((mant >> 12) & 1u));
}

'''.replace('\n', nl)
    s = s.replace(a, helper + a)
    io.open('src/viewport/render.cpp', 'w', encoding='utf-8', newline='').write(s)
print('ok')
