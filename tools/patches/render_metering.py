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
 ('''    // The game's tone map LUT (core game_look buildGameTonemapLut): size^3 RGBA8, red fastest.
    void setGameTonemapLut(int size, const uint8_t* rgba);''', '''    // The game's tone map LUT (core game_look buildGameTonemapLut): size^3 RGBA8, red fastest.
    void setGameTonemapLut(int size, const uint8_t* rgba);
    // Auto exposure metering (stock maps' visions meter the frame, research 95): after each game-look frame a small
    // pass turns the shown pixels back into scene luminance (inverseGrey: 256 LUT inputs for display greys 0..1, from
    // core gameTonemapInverseGrey, divided by lutScale) and averages their logarithm, centre-weighted. The result is read
    // back a few frames later without waiting for the GPU. null turns it off.
    void setGameMetering(const float* inverseGrey256);
    // The latest metered log-average luminance (false until one is available). Each value is returned once.
    bool takeMeteredLuminance(double& avg);'''),
 ('''    GameLook game_;''', '''    GameLook game_;
    bool metering_ = false;
    ID3D11Texture2D* meterTex_ = nullptr;
    ID3D11RenderTargetView* meterRtv_ = nullptr;
    ID3D11ShaderResourceView* meterSrv_ = nullptr;
    ID3D11Texture2D* meterStage_[3] = {nullptr, nullptr, nullptr};
    bool meterStageBusy_[3] = {false, false, false};
    unsigned meterNext_ = 0;
    ID3D11Texture1D* invGreyTex_ = nullptr;
    ID3D11ShaderResourceView* invGreySrv_ = nullptr;
    ID3D11VertexShader* meterVs_ = nullptr;
    ID3D11PixelShader* meterPs_ = nullptr;
    ID3D11Buffer* cbMeter_ = nullptr;
    bool meterReady_ = false;
    double meterValue_ = 0;
    bool meterHas_ = false;
    bool createMeterResources();
    void meterFrame();'''),
])

meter_shader = '''
// Auto exposure metering (setGameMetering): the shown frame back to scene luminance, log-averaged with a centre weight.
const char* kMeterShader = R"(
Texture2D frame : register(t0);
Texture1D invGrey : register(t1);
SamplerState lin : register(s0);
cbuffer Meter : register(b0) { float4 meter; };  // x = 1 / LUT input scale
struct V { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; };
V vs_full(uint id : SV_VertexID) {
    V o;
    float2 p = float2((id << 1) & 2, id & 2);
    o.uv = p;
    o.pos = float4(p * float2(2, -2) + float2(-1, 1), 0, 1);
    return o;
}
float4 ps_meter(V i) : SV_TARGET {
    float3 c = frame.SampleLevel(lin, i.uv, 0).rgb;
    float d = saturate(dot(c, float3(0.2126, 0.7152, 0.0722)));
    float x = invGrey.SampleLevel(lin, d * (255.0 / 256.0) + 0.5 / 256.0, 0).r * meter.x;
    float r = saturate(length(i.uv - 0.5) * 1.41421356);
    float w = 1.0 - r;
    return float4(log(max(x, 1e-5)) * w, w, 0, 0);
}
)";
'''

patch('src/viewport/render.cpp', [
 ('''template <class T>
void safeRelease(T*& p) {''', meter_shader.lstrip('\n') + '''
template <class T>
void safeRelease(T*& p) {'''),
 ('''    safeRelease(gameLutSrv_);
    safeRelease(gameLutTex_);
    releaseStreams();''', '''    safeRelease(gameLutSrv_);
    safeRelease(gameLutTex_);
    safeRelease(meterSrv_), safeRelease(meterRtv_), safeRelease(meterTex_);
    for (auto*& s : meterStage_) safeRelease(s);
    safeRelease(invGreySrv_), safeRelease(invGreyTex_);
    safeRelease(meterVs_), safeRelease(meterPs_), safeRelease(cbMeter_);
    releaseStreams();'''),
 ('''void SceneRenderer::resize(int w, int h) {''', '''void SceneRenderer::setGameMetering(const float* inverseGrey256) {
    safeRelease(invGreySrv_);
    safeRelease(invGreyTex_);
    metering_ = false;
    meterHas_ = false;
    if (!inverseGrey256 || !dev_) return;
    D3D11_TEXTURE1D_DESC td{};
    td.Width = 256;
    td.MipLevels = td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R32_FLOAT;
    td.Usage = D3D11_USAGE_IMMUTABLE;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA init{inverseGrey256, 0, 0};
    if (FAILED(dev_->CreateTexture1D(&td, &init, &invGreyTex_)) || FAILED(dev_->CreateShaderResourceView(invGreyTex_, nullptr, &invGreySrv_))) {
        safeRelease(invGreyTex_);
        return;
    }
    metering_ = createMeterResources();
}

bool SceneRenderer::createMeterResources() {
    if (meterReady_) return true;
    ID3DBlob* vsb = nullptr;
    ID3DBlob* psb = nullptr;
    ID3DBlob* errb = nullptr;
    const bool ok = SUCCEEDED(D3DCompile(kMeterShader, std::strlen(kMeterShader), "iw8r_meter", nullptr, nullptr, "vs_full", "vs_5_0", 0, 0, &vsb, &errb)) &&
                    SUCCEEDED(D3DCompile(kMeterShader, std::strlen(kMeterShader), "iw8r_meter", nullptr, nullptr, "ps_meter", "ps_5_0", 0, 0, &psb, &errb));
    if (ok) {
        dev_->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, &meterVs_);
        dev_->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, &meterPs_);
    }
    safeRelease(vsb), safeRelease(psb), safeRelease(errb);
    if (!meterVs_ || !meterPs_) return false;
    D3D11_TEXTURE2D_DESC td{};
    td.Width = td.Height = 256;
    td.MipLevels = 9;  // 256 .. 1
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R32G32_FLOAT;
    td.SampleDesc.Count = 1;
    td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    td.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
    if (FAILED(dev_->CreateTexture2D(&td, nullptr, &meterTex_)) || FAILED(dev_->CreateRenderTargetView(meterTex_, nullptr, &meterRtv_)) ||
        FAILED(dev_->CreateShaderResourceView(meterTex_, nullptr, &meterSrv_)))
        return false;
    D3D11_TEXTURE2D_DESC sd{};
    sd.Width = sd.Height = 1;
    sd.MipLevels = sd.ArraySize = 1;
    sd.Format = DXGI_FORMAT_R32G32_FLOAT;
    sd.SampleDesc.Count = 1;
    sd.Usage = D3D11_USAGE_STAGING;
    sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    for (auto*& s : meterStage_)
        if (FAILED(dev_->CreateTexture2D(&sd, nullptr, &s))) return false;
    D3D11_BUFFER_DESC bd{};
    bd.ByteWidth = 16;
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    if (FAILED(dev_->CreateBuffer(&bd, nullptr, &cbMeter_))) return false;
    meterReady_ = true;
    return true;
}

void SceneRenderer::meterFrame() {
    // Collect finished readbacks first (never waits: DO_NOT_WAIT).
    for (unsigned k = 0; k < 3; ++k) {
        if (!meterStageBusy_[k]) continue;
        D3D11_MAPPED_SUBRESOURCE m;
        if (ctx_->Map(meterStage_[k], 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &m) == S_OK) {
            const float* v = static_cast<const float*>(m.pData);
            if (v[1] > 1e-6f) meterValue_ = std::exp(double(v[0]) / double(v[1])), meterHas_ = true;
            ctx_->Unmap(meterStage_[k], 0);
            meterStageBusy_[k] = false;
        }
    }
    const unsigned slot = meterNext_ % 3;
    if (meterStageBusy_[slot]) return;  // the GPU is behind: skip this frame's measurement
    ++meterNext_;
    D3D11_VIEWPORT vp{0, 0, 256, 256, 0, 1};
    ctx_->RSSetViewports(1, &vp);
    ctx_->OMSetRenderTargets(1, &meterRtv_, nullptr);
    ctx_->OMSetBlendState(nullptr, nullptr, 0xffffffff);
    ctx_->OMSetDepthStencilState(dsOff_, 0);
    ctx_->RSSetState(rsTris_);
    ctx_->IASetInputLayout(nullptr);
    ctx_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx_->VSSetShader(meterVs_, nullptr, 0);
    ctx_->PSSetShader(meterPs_, nullptr, 0);
    const float cb[4] = {1.0f / std::max(game_.lutScale, 1e-6f), 0, 0, 0};
    ctx_->UpdateSubresource(cbMeter_, 0, nullptr, cb, 0, 0);
    ctx_->PSSetConstantBuffers(0, 1, &cbMeter_);
    ID3D11ShaderResourceView* srvs[2] = {srv_, invGreySrv_};
    ctx_->PSSetShaderResources(0, 2, srvs);
    if (lutSampler_) ctx_->PSSetSamplers(0, 1, &lutSampler_);
    ctx_->Draw(3, 0);
    ID3D11RenderTargetView* none = nullptr;
    ctx_->OMSetRenderTargets(1, &none, nullptr);
    ID3D11ShaderResourceView* nulls[2] = {nullptr, nullptr};
    ctx_->PSSetShaderResources(0, 2, nulls);
    ctx_->GenerateMips(meterSrv_);
    ctx_->CopySubresourceRegion(meterStage_[slot], 0, 0, 0, 0, meterTex_, 8, nullptr);
    meterStageBusy_[slot] = true;
    D3D11_VIEWPORT full{0, 0, float(w_), float(h_), 0, 1};
    ctx_->RSSetViewports(1, &full);
}

bool SceneRenderer::takeMeteredLuminance(double& avg) {
    if (!meterHas_) return false;
    avg = meterValue_;
    meterHas_ = false;
    return true;
}

void SceneRenderer::resize(int w, int h) {'''),
 ('''    ID3D11RenderTargetView* none = nullptr;
    ctx_->OMSetRenderTargets(1, &none, nullptr);
    // Buffers of streams this frame did not use are released right away''', '''    ID3D11RenderTargetView* none = nullptr;
    ctx_->OMSetRenderTargets(1, &none, nullptr);
    if (metering_ && game_.on && srv_ && !in.capture) meterFrame();
    // Buffers of streams this frame did not use are released right away'''),
])

patch('src/core/game_look.h', [
 ('''// The LUT itself: size^3 RGBA8 texels, red fastest, indexed by gameLutCoordinate per channel.
std::vector<uint8_t> buildGameTonemapLut(int size = 64);''', '''// The LUT itself: size^3 RGBA8 texels, red fastest, indexed by gameLutCoordinate per channel.
std::vector<uint8_t> buildGameTonemapLut(int size = 64);
// The curve's inverse for greys: n LUT inputs (exposed linear values) whose display grey is i / (n - 1); the top
// entries stop where the curve flattens (a clipped pixel reads as that brightest value).
std::vector<float> gameTonemapInverseGrey(int n = 256);'''),
])
patch('src/core/game_look.cpp', [
 ('''double srgbToLinear(double c) {''', '''std::vector<float> gameTonemapInverseGrey(int n) {
    // The grey curve sampled on a log grid, then inverted by search (it rises monotonically).
    std::vector<double> xs, ds;
    for (double lx = -10; lx <= 10; lx += 0.01) {
        const double x = std::exp2(lx), in[3] = {x, x, x};
        double out[3];
        gameTonemap(in, out);
        xs.push_back(x), ds.push_back(out[1]);
    }
    std::vector<float> inv(size_t(std::max(n, 2)));
    for (size_t i = 0; i < inv.size(); ++i) {
        const double d = std::min(double(i) / double(inv.size() - 1), 0.995);
        size_t k = 0;
        while (k + 1 < ds.size() && ds[k + 1] < d) ++k;
        const double t = k + 1 < ds.size() && ds[k + 1] > ds[k] ? std::clamp((d - ds[k]) / (ds[k + 1] - ds[k]), 0.0, 1.0) : 0.0;
        inv[i] = float(k + 1 < xs.size() ? xs[k] + (xs[k + 1] - xs[k]) * t : xs.back());
    }
    return inv;
}

double srgbToLinear(double c) {'''),
])
patch('tests/game_look_tests.cpp', [
 ('''    const double a = gameAdaptExposure(1.0, 4.0, 0.1, 1.0);''', '''    // The grey inverse undoes the curve (display -> exposed value -> display).
    const std::vector<float> inv = gameTonemapInverseGrey(256);
    const double x = inv[128], in[3] = {x, x, x};
    double out[3];
    gameTonemap(in, out);
    CHECK(inv.size() == 256 && std::fabs(out[1] - 128.0 / 255.0) < 0.01 && inv[200] > inv[100] && inv[0] < 0.01);
    const double a = gameAdaptExposure(1.0, 4.0, 0.1, 1.0);'''),
])
print('ok')
