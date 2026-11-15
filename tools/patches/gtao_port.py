import io

# Research 135 (research 132 G14): the game's GTAO in place of the editor's stand-in SSAO. A linear view depth max
# pyramid (mip 0: clip w with the editor's 1.5-step depth offset taken off, as ps_scenez; mips 1-4: max of 2 x 2, as the
# game's floatZ chain), then the kernel of research 135 section 5 (4 slices x 2 sides x 4 quadratic steps on mips 1-4,
# 100-unit radius clamped to 512 px at 1080p, falloff 50..200, thickness decay, the cosine-weighted arc integral, the
# 8..16 px distance fade, normals from depth with the smaller-step rule), a 4 x 4 interleaved rotation / jitter, and
# the existing 4 x 4 depth-aware blur. IW8R_AO_SSAO: the old SSAO (A/B).

ROOT = 'E:/IW8-Radiant/'


def patch(p, a, b, count=1):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == count, (p, s.count(a), a[:70])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


C = 'src/viewport/render.cpp'
# 1. HLSL: the old kernel becomes ps_ao_ssao; the pyramid and GTAO are added after it.
patch(C, '''float depthAt(int2 p) { return depthTex.Load(int3(clamp(p, int2(0, 0), int2(aoPrm.zw) - 1), 0)); }
float ps_ao(V i) : SV_TARGET {''', '''float depthAt(int2 p) { return depthTex.Load(int3(clamp(p, int2(0, 0), int2(aoPrm.zw) - 1), 0)); }
float ps_ao_ssao(V i) : SV_TARGET {''')
patch(C, '''    float ao = 1.0 - occ / 12.0;
    ao = lerp(ao, 1.0, saturate((dist - 6000.0) / 4000.0));
    return pow(saturate(ao), aoPrm.y);
}''', '''    float ao = 1.0 - occ / 12.0;
    ao = lerp(ao, 1.0, saturate((dist - 6000.0) / 4000.0));
    return pow(saturate(ao), aoPrm.y);
}
// ---- GTAO (research 135) ----
Texture2D<float> zPyr : register(t3);  // linear view depth, max pyramid (mips 0-4); 1e9 = sky
Texture2D<float> zUp : register(t4);   // ps_aozdown: the mip above
SamplerState aoPoint : register(s0);
float faceStepAo(float d, float a1, float a2, float b1, float b2) {
    const float bendA = abs(a2 - 2.0 * a1 + d), bendB = abs(b2 - 2.0 * b1 + d);
    const float stepA = abs(a1 - d), stepB = abs(b1 - d);
    if (min(bendA, bendB) > 4e-7 + 0.1 * min(stepA, stepB)) return min(stepA, stepB);
    return bendA < bendB ? stepA : bendB < bendA ? stepB : min(stepA, stepB);
}
// Mip 0: clip w of the scene with the faces' 1.5-step depth offset taken off (ps_scenez, without its 16-bit rounding).
float ps_aoz(V i) : SV_TARGET {
    const int2 q = int2(i.pos.xy);
    float d = depthTex.Load(int3(q, 0));
    if (d >= 0.999999) return 1e9;
    const float stepX = faceStepAo(d, depthAt(q + int2(-1, 0)), depthAt(q + int2(-2, 0)), depthAt(q + int2(1, 0)), depthAt(q + int2(2, 0)));
    const float stepY = faceStepAo(d, depthAt(q + int2(0, -1)), depthAt(q + int2(0, -2)), depthAt(q + int2(0, 1)), depthAt(q + int2(0, 2)));
    d -= 1.5 * max(stepX, stepY);
    return mul(float4(worldAt((float2(q) + 0.5) / aoPrm.zw, d), 1.0), aoVp).w;
}
// Mips 1-4: the farther of each 2 x 2 (the game's floatZ max chain).
float ps_aozdown(V i) : SV_TARGET {
    uint w, h;
    zUp.GetDimensions(w, h);
    const int2 p = int2(i.pos.xy) * 2, last = int2(w, h) - 1;
    return max(max(zUp.Load(int3(min(p, last), 0)), zUp.Load(int3(min(p + int2(1, 0), last), 0))),
               max(zUp.Load(int3(min(p + int2(0, 1), last), 0)), zUp.Load(int3(min(p + int2(1, 1), last), 0))));
}
float gtaoAcos(float x) { const float r = (1.57079633 - 0.156583 * abs(x)) * sqrt(saturate(1.0 - abs(x))); return x >= 0.0 ? r : 3.14159265 - r; }
float ps_ao(V i) : SV_TARGET {
    // tan of the half fields of view from the projection (symmetric, no jitter): clip x = view x / tanX.
    const float tanX = 1.0 / length(float3(aoVp[0][0], aoVp[1][0], aoVp[2][0]));
    const float tanY = 1.0 / length(float3(aoVp[0][1], aoVp[1][1], aoVp[2][1]));
    const int2 px = int2(i.pos.xy);
    const float2 size = aoPrm.zw, texel = 1.0 / size, uv = (float2(px) + 0.5) * texel;
    const float z = zPyr.Load(int3(px, 0));
    if (z >= 65500.0) return 1.0;
    // View space: x right, y up, z forward.
    #define VPOS(u, zz) float3((2.0 * (u).x - 1.0) * tanX * (zz), (1.0 - 2.0 * (u).y) * tanY * (zz), (zz))
    const float3 P = VPOS(uv, z), V = normalize(-P), Pb = float3(P.xy, 0.9975 * z);
    // The normal from depth (research 135 section 2.8): on each axis the neighbour with the smaller step.
    const int2 last = int2(size) - 1;
    const float zl = zPyr.Load(int3(clamp(px - int2(1, 0), int2(0, 0), last), 0)), zr = zPyr.Load(int3(clamp(px + int2(1, 0), int2(0, 0), last), 0));
    const float zu = zPyr.Load(int3(clamp(px - int2(0, 1), int2(0, 0), last), 0)), zd = zPyr.Load(int3(clamp(px + int2(0, 1), int2(0, 0), last), 0));
    const float3 ex = abs(zr - z) < abs(z - zl) ? VPOS(uv + float2(texel.x, 0), zr) - P : P - VPOS(uv - float2(texel.x, 0), zl);
    const float3 ey = abs(zd - z) < abs(z - zu) ? VPOS(uv + float2(0, texel.y), zd) - P : P - VPOS(uv - float2(0, texel.y), zu);
    float3 N = cross(ex, ey);
    if (dot(N, N) < 1e-20) return 1.0;
    N = normalize(N);
    if (dot(N, V) < 0.0) N = -N;
    const float2 Ruv = min(512.0 * (size.y / 1080.0) * texel, 50.0 / (z * float2(tanX, tanY)));
    const float lodK = length(Ruv * size * 0.5) / 32.0;
    // The game's blue-noise rotation and jitter, averaged over time; here 16 interleaved states over 4 x 4 pixels
    // (the blur below covers one period).
    const uint2 q4 = uint2(px) & 3u;
    const uint q = (q4.x * 4u + q4.y) * 7u % 16u;
    const float rot = (float(q) + 0.5) / 16.0, jit = frac(float(q) * 0.618034 + 0.5);
    float vis = 0.0, norm = 0.0;
    [unroll] for (int s = 0; s < 4; ++s) {
        const float phi = (float(s) + rot) * 0.78539816;
        const float2 dir = float2(cos(phi), sin(phi));
        float hc[2];
        [unroll] for (int side = 0; side < 2; ++side) {
            const float sg = side == 0 ? 1.0 : -1.0;
            float hmax = -1.0, hN = 200.0;
            [unroll] for (int j = 0; j < 4; ++j) {
                float t = (float(j) + jit) * 0.25;
                t *= t;
                const float2 su = uv + sg * dir * (2.0 * texel + t * Ruv);
                const float zs = zPyr.SampleLevel(aoPoint, su, 1.0 + min(3.0, lodK * t));
                const float3 d = VPOS(su, zs) - Pb;
                const float len = max(length(d), 1e-4), dn = dot(d, N);
                const float w = lerp(saturate(2.0 - len / 50.0), saturate(4.0 / 3.0 - len / 150.0), saturate(zs / 50.0 - 1.0));
                const float h = w * (dot(d, V) / len + 1.0) - 1.0;
                if (h > hmax) hN = -dn;
                const float decay = saturate(0.1 * (-hN - dn) - 0.5) * saturate(0.2 * dn + 1.0);
                hmax = max(h, hmax - decay);
            }
            hc[side] = hmax;
        }
        const float3 D = float3(dir.x, -dir.y, 0.0), ax = cross(D, V);
        const float3 pn = N - ax * dot(N, ax);
        const float pl = length(pn), cn = saturate(dot(pn, V) / max(pl, 1e-6));
        const float n = (dot(D, pn) >= dot(D, V) * dot(V, pn)) ? -gtaoAcos(cn) : gtaoAcos(cn), sn = sin(n);
        const float h0 = n + clamp(-gtaoAcos(hc[0]) - n, -1.57079633, 1.57079633);
        const float h1 = n + clamp(gtaoAcos(hc[1]) - n, -1.57079633, 1.57079633);
        vis += pl * 0.25 * ((cn + 2.0 * h0 * sn - cos(2.0 * h0 - n)) + (cn + 2.0 * h1 * sn - cos(2.0 * h1 - n)));
        norm += pl * (cn + n * sn);
    }
    #undef VPOS
    const float v = saturate(vis / max(norm, 1e-6));
    const float Rq = min(Ruv.x * size.x, Ruv.y * size.y) * 0.5;  // half-res pixels, as the game's fade
    return lerp(1.0, v, saturate((Rq - 4.0) * 0.25));
}''')
# 2. compile the new shaders; the old one stays as the A/B
patch(C, '''        if (compile("ps_prevcolor", "ps_5_0", cb)) dev_->CreatePixelShader(cb->GetBufferPointer(), cb->GetBufferSize(), nullptr, &prevColorPs_);
        safeRelease(zb), safeRelease(cb);''', '''        if (compile("ps_prevcolor", "ps_5_0", cb)) dev_->CreatePixelShader(cb->GetBufferPointer(), cb->GetBufferSize(), nullptr, &prevColorPs_);
        safeRelease(zb), safeRelease(cb);
        // GTAO (research 135): the depth pyramid's two passes and the old SSAO as the A/B.
        ID3DBlob *g1 = nullptr, *g2 = nullptr, *g3 = nullptr;
        if (compile("ps_aoz", "ps_5_0", g1)) dev_->CreatePixelShader(g1->GetBufferPointer(), g1->GetBufferSize(), nullptr, &aoZPs_);
        if (compile("ps_aozdown", "ps_5_0", g2)) dev_->CreatePixelShader(g2->GetBufferPointer(), g2->GetBufferSize(), nullptr, &aoZDownPs_);
        if (compile("ps_ao_ssao", "ps_5_0", g3)) dev_->CreatePixelShader(g3->GetBufferPointer(), g3->GetBufferSize(), nullptr, &aoSsaoPs_);
        safeRelease(g1), safeRelease(g2), safeRelease(g3);
        D3D11_SAMPLER_DESC sd{};
        sd.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
        sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.MaxLOD = D3D11_FLOAT32_MAX;
        dev_->CreateSamplerState(&sd, &aoPointSampler_);''')
patch(C, '''    safeRelease(aoVs_), safeRelease(aoPs_), safeRelease(aoBlurPs_), safeRelease(cbAo_);''',
      '''    safeRelease(aoVs_), safeRelease(aoPs_), safeRelease(aoBlurPs_), safeRelease(cbAo_);
    safeRelease(aoZPs_), safeRelease(aoZDownPs_), safeRelease(aoSsaoPs_), safeRelease(aoPointSampler_);''')
# 3. the pyramid's texture with the AO targets
patch(C, '''    for (int k = 0; k < 2; ++k) safeRelease(aoSrv_[k]), safeRelease(aoRtv_[k]), safeRelease(aoTex_[k]);''',
      '''    for (int k = 0; k < 2; ++k) safeRelease(aoSrv_[k]), safeRelease(aoRtv_[k]), safeRelease(aoTex_[k]);
    for (int k = 0; k < kAoZMips; ++k) safeRelease(aoZRtv_[k]), safeRelease(aoZMipSrv_[k]);
    safeRelease(aoZSrv_), safeRelease(aoZTex_);''')
patch(C, '''    for (int k = 0; k < 2; ++k)
        if (SUCCEEDED(dev_->CreateTexture2D(&td, nullptr, &aoTex_[k]))) {
            dev_->CreateRenderTargetView(aoTex_[k], nullptr, &aoRtv_[k]);
            dev_->CreateShaderResourceView(aoTex_[k], nullptr, &aoSrv_[k]);
        }
}''', '''    for (int k = 0; k < 2; ++k)
        if (SUCCEEDED(dev_->CreateTexture2D(&td, nullptr, &aoTex_[k]))) {
            dev_->CreateRenderTargetView(aoTex_[k], nullptr, &aoRtv_[k]);
            dev_->CreateShaderResourceView(aoTex_[k], nullptr, &aoSrv_[k]);
        }
    {
        // GTAO's linear depth max pyramid (research 135): R32F, mips 0-4, a render target and a view per mip.
        D3D11_TEXTURE2D_DESC zd = td;
        zd.Format = DXGI_FORMAT_R32_FLOAT;
        zd.MipLevels = UINT(kAoZMips);
        if (SUCCEEDED(dev_->CreateTexture2D(&zd, nullptr, &aoZTex_))) {
            dev_->CreateShaderResourceView(aoZTex_, nullptr, &aoZSrv_);
            for (int k = 0; k < kAoZMips; ++k) {
                D3D11_RENDER_TARGET_VIEW_DESC rd{};
                rd.Format = zd.Format;
                rd.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
                rd.Texture2D.MipSlice = UINT(k);
                dev_->CreateRenderTargetView(aoZTex_, &rd, &aoZRtv_[k]);
                D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
                sd.Format = zd.Format;
                sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
                sd.Texture2D.MostDetailedMip = UINT(k);
                sd.Texture2D.MipLevels = 1;
                dev_->CreateShaderResourceView(aoZTex_, &sd, &aoZMipSrv_[k]);
            }
        }
    }
}''')
# 4. the AO pass: pyramid, then GTAO (or the old SSAO), then the blur
patch(C, '''    ID3D11ShaderResourceView* none2[2] = {nullptr, nullptr};
    ctx_->PSSetShaderResources(14, 1, none2);  // last frame's AO is about to be written
    ctx_->OMSetRenderTargets(1, &aoRtv_[0], nullptr);
    ctx_->OMSetBlendState(nullptr, nullptr, 0xffffffff);
    ctx_->OMSetDepthStencilState(dsOff_, 0);
    ctx_->RSSetState(rsTris_);
    ctx_->IASetInputLayout(nullptr);
    ctx_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx_->VSSetShader(aoVs_, nullptr, 0);
    ctx_->PSSetShader(aoPs_, nullptr, 0);
    ctx_->PSSetConstantBuffers(8, 1, &cbAo_);
    ID3D11ShaderResourceView* in0[2] = {depthSrv_, nullptr};
    ctx_->PSSetShaderResources(0, 2, in0);
    ctx_->Draw(3, 0);''', '''    ID3D11ShaderResourceView* none2[2] = {nullptr, nullptr};
    ctx_->PSSetShaderResources(14, 1, none2);  // last frame's AO is about to be written
    ctx_->OMSetBlendState(nullptr, nullptr, 0xffffffff);
    ctx_->OMSetDepthStencilState(dsOff_, 0);
    ctx_->RSSetState(rsTris_);
    ctx_->IASetInputLayout(nullptr);
    ctx_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx_->VSSetShader(aoVs_, nullptr, 0);
    ctx_->PSSetConstantBuffers(8, 1, &cbAo_);
    ID3D11ShaderResourceView* in0[2] = {depthSrv_, nullptr};
    ctx_->PSSetShaderResources(0, 2, in0);
    static const bool oldSsao = std::getenv("IW8R_AO_SSAO") != nullptr;  // A/B: the stand-in SSAO
    const bool gtao = !oldSsao && aoZTex_ && aoZPs_ && aoZDownPs_ && aoPointSampler_ && aoZRtv_[kAoZMips - 1];
    if (gtao) {
        // The depth pyramid (research 135): mip 0 from the scene depth, mips 1-4 by 2 x 2 max.
        D3D11_VIEWPORT prevVp{};
        UINT nvp = 1;
        ctx_->RSGetViewports(&nvp, &prevVp);
        ID3D11ShaderResourceView* noZ[2] = {nullptr, nullptr};
        ctx_->PSSetShaderResources(3, 2, noZ);
        for (int k = 0; k < kAoZMips; ++k) {
            D3D11_VIEWPORT vp{0, 0, float(std::max(1, w_ >> k)), float(std::max(1, h_ >> k)), 0, 1};
            ctx_->RSSetViewports(1, &vp);
            ctx_->OMSetRenderTargets(1, &aoZRtv_[k], nullptr);
            ctx_->PSSetShader(k == 0 ? aoZPs_ : aoZDownPs_, nullptr, 0);
            if (k > 0) ctx_->PSSetShaderResources(4, 1, &aoZMipSrv_[k - 1]);
            ctx_->Draw(3, 0);
            ctx_->PSSetShaderResources(4, 1, noZ);
        }
        ctx_->OMSetRenderTargets(0, nullptr, nullptr);
        ctx_->RSSetViewports(1, &prevVp);
        ctx_->PSSetShaderResources(3, 1, &aoZSrv_);
        ctx_->PSSetSamplers(0, 1, &aoPointSampler_);
    }
    ctx_->OMSetRenderTargets(1, &aoRtv_[0], nullptr);
    ctx_->PSSetShader(gtao ? aoPs_ : (aoSsaoPs_ ? aoSsaoPs_ : aoPs_), nullptr, 0);
    ctx_->Draw(3, 0);
    if (gtao) {
        ID3D11ShaderResourceView* noZ = nullptr;
        ctx_->PSSetShaderResources(3, 1, &noZ);
    }''')

H = 'src/viewport/render.h'
s = io.open(ROOT + H, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
# members next to the AO targets
import re
m = re.search(r'\n(\s*)ID3D11ShaderResourceView\* aoSrv_\[2\][^\n]*\n', s)
assert m, 'aoSrv_ member not found'
ins = (m.group(1) + 'static constexpr int kAoZMips = 5;  // GTAO depth pyramid: mips 0-4 (research 135)' + nl +
       m.group(1) + 'ID3D11Texture2D* aoZTex_ = nullptr;' + nl +
       m.group(1) + 'ID3D11RenderTargetView* aoZRtv_[kAoZMips] = {};' + nl +
       m.group(1) + 'ID3D11ShaderResourceView* aoZMipSrv_[kAoZMips] = {};' + nl +
       m.group(1) + 'ID3D11ShaderResourceView* aoZSrv_ = nullptr;' + nl +
       m.group(1) + 'ID3D11PixelShader* aoZPs_ = nullptr;' + nl +
       m.group(1) + 'ID3D11PixelShader* aoZDownPs_ = nullptr;' + nl +
       m.group(1) + 'ID3D11PixelShader* aoSsaoPs_ = nullptr;  // the stand-in SSAO (IW8R_AO_SSAO)' + nl +
       m.group(1) + 'ID3D11SamplerState* aoPointSampler_ = nullptr;' + nl)
s = s[:m.end()] + ins + s[m.end():]
io.open(ROOT + H, 'w', encoding='utf-8', newline='').write(s)
print('ok')
