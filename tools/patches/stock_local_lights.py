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


RH = 'src/viewport/render.h'
R = 'src/viewport/render.cpp'

# ---------------- header
patch(RH, '''    // The stock specular occlusion table (gtso_lut, one byte per texel, x fastest; research 95), t18.''',
      '''    // Stock local lights (research 106): the map's packed spot / omni records (core stock_lights.h). Each frame the
    // nearest 256 in view are uploaded (t20) and binned to 32-pixel screen tiles (t22); Game Lighting shades the stock
    // reference and its props with them. Empty = none. IW8R_NO_STOCK_LIGHTS turns them off (A/B).
    void setStockLocalLights(std::vector<iw8r::StockGameLight> lights) { stockLights_ = std::move(lights); }
    size_t stockLocalLightCount() const { return stockLights_.size(); }
    size_t stockLocalLightsDrawn() const { return stockLightsDrawn_; }
    // The map's IES strip (*ieslookup level 0: width R8 texels, texel 0 white), t21. null = every light white.
    void setIesStrip(int width, const uint8_t* r8);
    // The stock specular occlusion table (gtso_lut, one byte per texel, x fastest; research 95), t18.''')
patch(RH, '''    ID3D11ShaderResourceView* gtsoSrv_ = nullptr;''', '''    ID3D11ShaderResourceView* gtsoSrv_ = nullptr;
    // Stock local lights (setStockLocalLights): all of the map's; per frame the drawn ones and the tile masks.
    void prepareStockLocalLights(const float* viewProj);
    std::vector<iw8r::StockGameLight> stockLights_;
    size_t stockLightsDrawn_ = 0;
    bool stockLightsOn_ = false;
    std::vector<std::pair<float, uint32_t>> stockLightOrder_;
    std::vector<uint32_t> stockTileMask_;
    ID3D11Buffer* stockLightBuf_ = nullptr;
    ID3D11ShaderResourceView* stockLightSrv_ = nullptr;
    ID3D11Buffer* stockTileBuf_ = nullptr;
    ID3D11ShaderResourceView* stockTileSrv_ = nullptr;
    size_t stockTileCap_ = 0;  // uints
    ID3D11Buffer* cbLocal_ = nullptr;
    ID3D11Texture1D* iesTex_ = nullptr;
    ID3D11ShaderResourceView* iesSrv_ = nullptr;''')
patch(RH, '''#include "reference_markers.h"''', '''#include "reference_markers.h"
#include "stock_lights.h"''')

# ---------------- shader
patch(R, '''Texture2D revealTex : register(t12);
// vs_tex_inst: a prop lit from its light grid box carries that light in t (no tangent) and lm = (-2, -2).''', '''// Stock local lights (research 106): Replay's packed records (96 bytes), the nearest 256 in view; per 32-pixel screen
// tile 8 words of bits (bit k = record k). localInfo = (on, tiles across, tile size, IES strip bound).
struct GameLight { float3 diffuse; float collimationOffset; float3 dir; uint packed; float3 bulbDir; float bulbRadius;
                   float3 pos; float invRadius; float4 spot; float3 uvIrHeat; uint iesLookup; };
StructuredBuffer<GameLight> gameLights : register(t20);
Texture1D<float> iesStrip : register(t21);
StructuredBuffer<uint> lightTiles : register(t22);
cbuffer LocalLights : register(b11) { float4 localInfo; };
float3 glSegPoint(GameLight l, float3 p) {
    return l.pos + clamp(dot(p - l.pos, l.bulbDir) / max(dot(l.bulbDir, l.bulbDir), 1e-20), -1.0, 1.0) * l.bulbDir;
}
// One light (research 106 section 3.2 / 5.3, world space): tube closest point, spot apex, IES, smoothstep window,
// bulb-capped diffuse at a representative point, GGX specular toward the tube with the sphere-light normalisation.
void gameLocalLight(GameLight l, float3 P, float3 N, float3 V, float3 diffuseColor, float3 f0, float gp, float cavity,
                    inout float3 diffSum, inout float3 specSum) {
    const float3 Q = glSegPoint(l, P), v = Q - P;
    const float d = length(v);
    if (d * l.invRadius >= 1.0 || d < 1e-4) return;
    const float3 L = v / d;
    const bool isSpot = ((l.packed >> 8) & 3u) == 2u;
    const float3 Lc = isSpot ? normalize(Q + l.dir * l.spot.z - P) : L;
    const float front = (!isSpot || dot(v, l.dir) > 0.0) ? 1.0 : 0.0;
    const float cosA = dot(l.dir, Lc), s2a = 1.0 - min(cosA * cosA, 1.0);
    const float u = cosA > 0.0 ? 0.5 * s2a : 1.0 - 0.5 * s2a;
    float prof = 1.0;
    if (localInfo.w > 0.5 && l.iesLookup != 0u) {
        const float2 ies = f16tof32(uint2(l.iesLookup & 0xffffu, l.iesLookup >> 16));
        prof = iesStrip.SampleLevel(lutSamp, ies.x + u * ies.y, 0);
    }
    const float spotF = saturate(cosA * l.spot.x + l.spot.y);
    const float w = saturate(l.spot.w * (1.0 - d * l.invRadius)) * (isSpot ? spotF : 1.0);
    const float att = prof * front * w * w * (3.0 - 2.0 * w);  // no shadow yet (research 106 section 4)
    if (att <= 0.0) return;
    const float3 X = P + (N + L) * (d / max(1.0 + dot(N, L), 1e-3));
    const float3 vd = glSegPoint(l, X) - P;
    const float dd = max(length(vd), 1e-4);
    const float3 Ld = vd / dd;
    const float NdotL = dot(N, Ld), s2 = l.bulbRadius * l.bulbRadius / (dd * dd), y = NdotL * rsqrt(s2);
    const float E = att * ((1.0 - y * y > 0.0) ? pow(saturate(s2 * (0.5 + 0.5 * y)), 1.5) : saturate(NdotL * s2));
    if (E <= 0.0) return;
    const float3 R = reflect(-V, N), QP = Q - P;
    const float aR = dot(l.bulbDir, R);
    float t = clamp((aR * dot(R, QP) - dot(l.bulbDir, QP)) / max(dot(l.bulbDir, l.bulbDir) - aR * aR, 1e-12), -1.5, 1.5);
    t -= t * t * t * (4.0 / 27.0);
    const float3 S = QP + t * l.bulbDir, perp = R * dot(S, R) - S;
    float x = l.bulbRadius * rsqrt(max(dot(perp, perp), 1e-8));
    x *= rsqrt(x * x + 0.125);
    const float3 Hs = normalize(normalize(S + perp * x) + V);
    const float a2 = 2.0 / (2.0 + exp2(20.0 * gp)), NdotH = dot(N, Hs), NdotV = saturate(dot(N, V));
    const float dq = NdotH * NdotH * (a2 - 1.0) + 1.0;
    const float D = a2 / max(dq * dq, 1e-8);
    const float Vis = 1.0 / ((abs(NdotL) + sqrt(a2 + (1.0 - a2) * NdotL * NdotL)) * (NdotV + sqrt(a2 + (1.0 - a2) * NdotV * NdotV)));
    const float ap = pow(a2, 0.748925), norm = ap / (ap + 0.375892 * l.bulbRadius * rsqrt(max(dot(S, S), 1e-8)));
    const float3 F = f0 + (1.0 - f0) * pow(1.0 - abs(dot(V, normalize(Ld + V))), 5.0);
    const float2 AB = gameEnvBrdf(saturate(NdotL), gp);
    const float3 Fl = f0 * AB.x + AB.y;
    diffSum += diffuseColor * l.diffuse * E * (1.0 - max(Fl.r, max(Fl.g, Fl.b)));
    specSum += l.diffuse * F * (D * Vis * norm * E * cavity);
}
Texture2D revealTex : register(t12);
// vs_tex_inst: a prop lit from its light grid box carries that light in t (no tangent) and lm = (-2, -2).''')
patch(R, '''                c += gameSunCol.rgb * visibility * saturate(nl) *
                     (diffAlbedo * (1.0 - max(fl.r, max(fl.g, fl.b))) + F * Dg * Vg * 1.0390625 * (baseMat ? nmCavity : 1.0));
            }''', '''                c += gameSunCol.rgb * visibility * saturate(nl) *
                     (diffAlbedo * (1.0 - max(fl.r, max(fl.g, fl.b))) + F * Dg * Vg * 1.0390625 * (baseMat ? nmCavity : 1.0));
            }
            // The map's local lights (research 106): the drawn ones of this pixel's 32-pixel tile; no AO, as the game.
            if (localInfo.x > 0.5) {
                const uint2 tile = uint2(i.pos.xy) / uint(localInfo.z);
                const uint base = (tile.y * uint(localInfo.y) + tile.x) * 8u;
                float3 dSum = 0, sSum = 0;
                [loop] for (uint wd = 0; wd < 8u; ++wd) {
                    uint bits = lightTiles[base + wd];
                    [loop] while (bits != 0u) {
                        const uint b = firstbitlow(bits);
                        bits &= bits - 1u;
                        gameLocalLight(gameLights[wd * 32u + b], i.w, N, Vs, diffAlbedo, f0, gp, baseMat ? nmCavity : 1.0, dSum, sSum);
                    }
                }
                c += dSum + sSum;
            }''')

# ---------------- CPU: selection, binning, upload
patch(R, '''void SceneRenderer::setGtsoLut(int width, int height, int depth, const uint8_t* r) {''', '''void SceneRenderer::setIesStrip(int width, const uint8_t* r8) {
    safeRelease(iesSrv_), safeRelease(iesTex_);
    if (!dev_ || width < 2 || !r8) return;
    D3D11_TEXTURE1D_DESC td{};
    td.Width = UINT(width);
    td.MipLevels = td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8_UNORM;
    td.Usage = D3D11_USAGE_IMMUTABLE;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA init{r8, UINT(width), 0};
    if (FAILED(dev_->CreateTexture1D(&td, &init, &iesTex_)) || FAILED(dev_->CreateShaderResourceView(iesTex_, nullptr, &iesSrv_)))
        safeRelease(iesTex_);
}

void SceneRenderer::prepareStockLocalLights(const float* M) {
    constexpr uint32_t kMaxLights = 256, kTile = 32;
    stockLightsOn_ = false;
    stockLightsDrawn_ = 0;
    if (!dev_) return;
    if (!cbLocal_) {
        D3D11_BUFFER_DESC bd{};
        bd.ByteWidth = 16;
        bd.Usage = D3D11_USAGE_DEFAULT;
        bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        if (FAILED(dev_->CreateBuffer(&bd, nullptr, &cbLocal_))) return;
    }
    float info[4] = {0, 0, float(kTile), iesSrv_ ? 1.0f : 0.0f};
    static const bool off = std::getenv("IW8R_NO_STOCK_LIGHTS") != nullptr;
    const auto done = [&] { ctx_->UpdateSubresource(cbLocal_, 0, nullptr, info, 0, 0); };
    if (off || !game_.on || stockLights_.empty() || !M || w_ <= 0 || h_ <= 0) return done();
    // The game's rule (research 106 section 2.1): the lights in view, nearest to the camera first, at most 256.
    const iw8r::Frustum fr = iw8r::frustumFromViewProj(M);
    const float* eye = fog_;
    stockLightOrder_.clear();
    for (uint32_t i = 0; i < uint32_t(stockLights_.size()); ++i) {
        const iw8r::StockGameLight& l = stockLights_[i];
        const float r = l.invRadius > 0 ? 1.0f / l.invRadius : 0.0f;
        bool out = false;
        for (const auto& pl : fr.planes)
            if (pl.n.x * l.pos[0] + pl.n.y * l.pos[1] + pl.n.z * l.pos[2] - pl.d > r) {
                out = true;
                break;
            }
        if (out) continue;
        const float dx = l.pos[0] - eye[0], dy = l.pos[1] - eye[1], dz = l.pos[2] - eye[2];
        stockLightOrder_.push_back({dx * dx + dy * dy + dz * dz, i});
    }
    if (stockLightOrder_.empty()) return done();
    const size_t n = std::min<size_t>(stockLightOrder_.size(), kMaxLights);
    std::partial_sort(stockLightOrder_.begin(), stockLightOrder_.begin() + ptrdiff_t(n), stockLightOrder_.end());
    if (!stockLightBuf_) {
        D3D11_BUFFER_DESC bd{};
        bd.ByteWidth = UINT(kMaxLights * sizeof(iw8r::StockGameLight));
        bd.Usage = D3D11_USAGE_DYNAMIC;
        bd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        bd.StructureByteStride = UINT(sizeof(iw8r::StockGameLight));
        if (FAILED(dev_->CreateBuffer(&bd, nullptr, &stockLightBuf_)) ||
            FAILED(dev_->CreateShaderResourceView(stockLightBuf_, nullptr, &stockLightSrv_))) {
            safeRelease(stockLightBuf_);
            return done();
        }
    }
    const uint32_t tx = (uint32_t(w_) + kTile - 1) / kTile, ty = (uint32_t(h_) + kTile - 1) / kTile;
    const size_t words = size_t(tx) * ty * 8;
    if (words > stockTileCap_) {
        safeRelease(stockTileSrv_), safeRelease(stockTileBuf_);
        stockTileCap_ = 0;
        D3D11_BUFFER_DESC bd{};
        bd.ByteWidth = UINT(words * 4);
        bd.Usage = D3D11_USAGE_DYNAMIC;
        bd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        bd.StructureByteStride = 4;
        if (FAILED(dev_->CreateBuffer(&bd, nullptr, &stockTileBuf_)) ||
            FAILED(dev_->CreateShaderResourceView(stockTileBuf_, nullptr, &stockTileSrv_))) {
            safeRelease(stockTileBuf_);
            return done();
        }
        stockTileCap_ = words;
    }
    // Each light's screen rectangle: its bounding box projected (the whole screen when a corner is behind the eye).
    stockTileMask_.assign(words, 0u);
    D3D11_MAPPED_SUBRESOURCE ml;
    if (FAILED(ctx_->Map(stockLightBuf_, 0, D3D11_MAP_WRITE_DISCARD, 0, &ml))) return done();
    auto* dst = static_cast<iw8r::StockGameLight*>(ml.pData);
    for (size_t k = 0; k < n; ++k) {
        const iw8r::StockGameLight& l = stockLights_[stockLightOrder_[k].second];
        dst[k] = l;
        const float r = 1.0f / l.invRadius;
        float x0 = 1e30f, y0 = 1e30f, x1 = -1e30f, y1 = -1e30f;
        bool behind = false;
        for (int c = 0; c < 8 && !behind; ++c) {
            const float p[3] = {l.pos[0] + ((c & 1) ? r : -r), l.pos[1] + ((c & 2) ? r : -r), l.pos[2] + ((c & 4) ? r : -r)};
            const float cx = p[0] * M[0] + p[1] * M[4] + p[2] * M[8] + M[12];
            const float cy = p[0] * M[1] + p[1] * M[5] + p[2] * M[9] + M[13];
            const float cw = p[0] * M[3] + p[1] * M[7] + p[2] * M[11] + M[15];
            if (cw <= 1e-3f) {
                behind = true;
                break;
            }
            const float sx = (cx / cw * 0.5f + 0.5f) * float(w_), sy = (0.5f - 0.5f * cy / cw) * float(h_);
            x0 = std::min(x0, sx), x1 = std::max(x1, sx), y0 = std::min(y0, sy), y1 = std::max(y1, sy);
        }
        if (behind) x0 = 0, y0 = 0, x1 = float(w_ - 1), y1 = float(h_ - 1);
        x0 = std::max(x0, 0.0f), y0 = std::max(y0, 0.0f), x1 = std::min(x1, float(w_ - 1)), y1 = std::min(y1, float(h_ - 1));
        if (x0 > x1 || y0 > y1) continue;
        const uint32_t a0 = uint32_t(x0) / kTile, a1 = uint32_t(x1) / kTile, b0 = uint32_t(y0) / kTile, b1 = uint32_t(y1) / kTile;
        const uint32_t bit = 1u << (k & 31u), word = uint32_t(k >> 5);
        for (uint32_t ty2 = b0; ty2 <= b1; ++ty2)
            for (uint32_t tx2 = a0; tx2 <= a1; ++tx2) stockTileMask_[(size_t(ty2) * tx + tx2) * 8 + word] |= bit;
    }
    ctx_->Unmap(stockLightBuf_, 0);
    D3D11_MAPPED_SUBRESOURCE mt;
    if (FAILED(ctx_->Map(stockTileBuf_, 0, D3D11_MAP_WRITE_DISCARD, 0, &mt))) return done();
    std::memcpy(mt.pData, stockTileMask_.data(), words * 4);
    ctx_->Unmap(stockTileBuf_, 0);
    info[0] = 1, info[1] = float(tx);
    stockLightsOn_ = true;
    stockLightsDrawn_ = n;
    done();
}

void SceneRenderer::setGtsoLut(int width, int height, int depth, const uint8_t* r) {''')
# per frame, before anything is drawn
patch(R, '''    hdrFrame_ = game_.on && gameLutSrv_ && hdrRtv_ && postVs_ && postPs_ && !noHdr && !in.capture;''',
      '''    hdrFrame_ = game_.on && gameLutSrv_ && hdrRtv_ && postVs_ && postPs_ && !noHdr && !in.capture;
    prepareStockLocalLights(viewProj);''')
# bound with the rest of Game Lighting
patch(R, '''    ctx_->PSSetShaderResources(18, 1, &gtsoSrv_);''', '''    ctx_->PSSetShaderResources(18, 1, &gtsoSrv_);
    {
        ID3D11ShaderResourceView* ll[3] = {stockLightsOn_ ? stockLightSrv_ : nullptr, iesSrv_, stockLightsOn_ ? stockTileSrv_ : nullptr};
        ctx_->PSSetShaderResources(20, 3, ll);
        if (cbLocal_) ctx_->PSSetConstantBuffers(11, 1, &cbLocal_);
    }''')
# released with the rest
patch(R, '''    safeRelease(envBrdfSrv_), safeRelease(envBrdfTex_);
    safeRelease(gtsoSrv_), safeRelease(gtsoTex_);
''', '''    safeRelease(envBrdfSrv_), safeRelease(envBrdfTex_);
    safeRelease(gtsoSrv_), safeRelease(gtsoTex_);
    safeRelease(stockLightSrv_), safeRelease(stockLightBuf_), safeRelease(stockTileSrv_), safeRelease(stockTileBuf_);
    safeRelease(cbLocal_), safeRelease(iesSrv_), safeRelease(iesTex_);
    stockTileCap_ = 0;
''', 1)
print('ok')
