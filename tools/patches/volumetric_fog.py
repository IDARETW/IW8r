import io


def patch(p, a, b, n=1):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


# ---------------------------------------------------------------- core: the vision's volumetric fields
GH = 'src/core/game_look.h'
patch(GH, '''    double grain = 0;            // tonemapGrainStrength (LNQNRRPNPP) x r_filmGrainAtten 0.25
};''', '''    double grain = 0;            // tonemapGrainStrength (LNQNRRPNPP) x r_filmGrainAtten 0.25
    // The volumetric fog (research 109 section 2): on when volumetricAirDensity > 0 (vision default 0). sigma per unit =
    // air x 0.01 x exp(max(z - heightBase, 0) x -ln2 / heightHalf) (heightHalf 0 = uniform).
    double volAir = 0, volAbsorption = 0, volAlbedo[3] = {1, 1, 1}, volAmbient = 1, volSun = 1, volAnisotropy = 0;
    double volHeightBase = 0, volHeightHalf = 0;
};''')
GC = 'src/core/game_look.cpp'
patch(GC, '''    double grain = 0;
    num("LNQNRRPNPP", grain);''', '''    num("NSKNLMKQSR", p.volAir);         // volumetricAirDensity
    num("MQMPKLKK", p.volAbsorption);    // volumetricAbsorption
    num("MQRSTPRORQ", p.volAmbient);     // volumetricAmbientBrightness
    num("LKNNNKTPTM", p.volSun);         // volumetricSunBrightness
    num("MTNSRNMTKN", p.volAnisotropy);  // volumetricSunAnisotropy
    num("NPKPLTKRNS", p.volHeightBase);  // volumetricHeightFogBaseHeight
    num("MLPOMNQPPK", p.volHeightHalf);  // volumetricHeightFogHalfPlaneDistance
    if (const auto it = v.find("NKKRQROOQK"); it != v.end()) {  // volumetricAlbedo
        double a[3];
        if (std::sscanf(it->second.c_str(), "%lf %lf %lf", &a[0], &a[1], &a[2]) == 3)
            for (int k = 0; k < 3; ++k) p.volAlbedo[k] = a[k];
    }
    p.volAir = std::max(p.volAir, 0.0);
    double grain = 0;
    num("LNQNRRPNPP", grain);''')

# ---------------------------------------------------------------- renderer
RH = 'src/viewport/render.h'
patch(RH, '''        float fogK[8][4] = {};  // the game's fog constants cb7[131..138] (core gameFogConstants, research 111)''',
      '''        float fogK[8][4] = {};  // the game's fog constants cb7[131..138] (core gameFogConstants, research 111)
        // The volumetric fog (research 109 section 2): albedo rgb + air x 0.01; (absorption, ambient brightness, height
        // base, height exponent -ln2 / half or 0); (sun brightness, anisotropy g, on, 0).
        float vol[3][4] = {};''')
patch(RH, '''    ID3D11Buffer* cbVeil_ = nullptr;''', '''    ID3D11Buffer* cbVeil_ = nullptr;
    ID3D11Buffer* cbVol_ = nullptr;  // b13: the volumetric fog (drawPost)
    float lastViewProj_[16] = {};    // the frame's view (drawPost's volumetric march)''')

R = 'src/viewport/render.cpp'
patch(R, '''float4 ps_post(VFull i) : SV_TARGET {
    float4 h = hdrScene.Load(int3(i.pos.xy, 0));
    if (h.a <= 0.0) discard;
    if (gameAo.y > 0.5 || gameAo.z > 0.5 || (gameSkyScale.z > 0.5 && gameSkyScale.z < 1.5)) return float4(h.rgb, 1.0);  // debug views
    float3 c = max(h.rgb, 0.0);''', '''// The game's volumetric fog (research 109 section 2, a march of its froxel rule per pixel): 32 samples over the 128
// slices of its four cascades (128 / 384 / 640 / 1024 units, 4 to 2180 units), each sigma = air x 0.01 x the height
// falloff, the sun's light by the shadow map and the Henyey-Greenstein phase clipped at 1, the ambient per slice
// without the slice thickness (as the game's shader), then out = scene x T + S. The sky takes the whole volume.
Texture2D<float> volDepth : register(t28);
cbuffer Volume : register(b13) { row_major float4x4 volInvVP; float4 volEye; float4 volSunDir; float4 volSunRad; float4 volAlbedo;
                                 float4 volP; float4 volQ; float4 volAmb; };
float3 volumetricFog(float2 px, float3 c) {
    if (volQ.z < 0.5) return c;
    const float z = volDepth.Load(int3(px, 0));
    const float2 ndc = float2(px.x / max(postFx0.y, 1.0) * 2.0 - 1.0, 1.0 - px.y / max(postFx0.z, 1.0) * 2.0);
    const float4 wp = mul(float4(ndc, z, 1.0), volInvVP);
    float3 V = wp.xyz / wp.w - volEye.xyz;
    float D = length(V);
    V /= max(D, 1e-3);
    if (z >= 0.999999) D = 1e9;
    const float g = volQ.y, cth = dot(V, volSunDir.xyz);
    const float ph = min((1.0 - g * g) / (12.566371 * pow(max(1.0 + g * g - 2.0 * g * cth, 1e-4), 1.5)), 1.0);
    float T = 1.0;
    float3 S = 0;
    [loop] for (int k = 0; k < 32; ++k) {
        const float s = (k * 4 + 2) / 128.0;
        const float d = 4.0 + dot(saturate((s - float4(0.0, 0.25, 0.5, 0.75)) * 4.0), float4(128, 384, 640, 1024));
        if (d > D) break;
        const float dz = (k < 8 ? 128.0 : k < 16 ? 384.0 : k < 24 ? 640.0 : 1024.0) / 32.0 * 4.0;
        const float3 P = volEye.xyz + V * d;
        const float sig = volAlbedo.w * exp(max(P.z - volP.z, 0.0) * volP.w);
        const float3 sun = sunShadow(P) * volQ.x * ph * 3.141593 * volSunRad.rgb * dz;
        const float3 amb = volP.y * volAmb.rgb * 4.0;
        S += T * volAlbedo.rgb * sig * (sun + amb);
        T *= exp(-(sig + volP.x) * dz);
    }
    return c * T + S;
}
float4 ps_post(VFull i) : SV_TARGET {
    float4 h = hdrScene.Load(int3(i.pos.xy, 0));
    if (h.a <= 0.0) discard;
    if (gameAo.y > 0.5 || gameAo.z > 0.5 || (gameSkyScale.z > 0.5 && gameSkyScale.z < 1.5)) return float4(h.rgb, 1.0);  // debug views
    float3 c = volumetricFog(i.pos.xy, max(h.rgb, 0.0));''')
patch(R, '''    writeGameCb();  // b4 (exposure), the LUT on t9 and its sampler
    ctx_->OMSetRenderTargets(1, &rtv_, nullptr);''', '''    writeGameCb();  // b4 (exposure), the LUT on t9 and its sampler
    {
        // The volumetric fog's constants (b13) and inputs: the depth (t28), the sun shadow maps (t2, s1) and the
        // frame's constants (b0: the shadow matrices).
        float cb[44] = {};
        const bool volOn = game_.on && game_.vol[2][2] > 0.5f && depthSrv_ && postFxEnabled_ && invert4(lastViewProj_, cb);
        if (volOn) {
            cb[16] = fog_[0], cb[17] = fog_[1], cb[18] = fog_[2];
            cb[20] = game_.sunToward[0], cb[21] = game_.sunToward[1], cb[22] = game_.sunToward[2];
            cb[24] = game_.sunColor[0], cb[25] = game_.sunColor[1], cb[26] = game_.sunColor[2];
            std::memcpy(cb + 28, game_.vol, sizeof(game_.vol));
            cb[40] = game_.sky[0], cb[41] = game_.sky[1], cb[42] = game_.sky[2];
        }
        if (!cbVol_ && dev_) {
            D3D11_BUFFER_DESC bd{};
            bd.ByteWidth = sizeof(cb);
            bd.Usage = D3D11_USAGE_DEFAULT;
            bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            dev_->CreateBuffer(&bd, nullptr, &cbVol_);
        }
        if (cbVol_) {
            ctx_->UpdateSubresource(cbVol_, 0, nullptr, cb, 0, 0);
            ctx_->PSSetConstantBuffers(13, 1, &cbVol_);
        }
        ctx_->PSSetConstantBuffers(0, 1, &cb_);
        ID3D11ShaderResourceView* d = volOn ? depthSrv_ : nullptr;
        ctx_->PSSetShaderResources(28, 1, &d);
        if (shadowSrv_ && cmpSampler_) {
            ctx_->PSSetShaderResources(2, 1, &shadowSrv_);
            ctx_->PSSetSamplers(1, 1, &cmpSampler_);
        }
    }
    ctx_->OMSetRenderTargets(1, &rtv_, nullptr);''')
patch(R, '''    ctx_->PSSetShaderResources(15, 1, &none);
    ctx_->PSSetShaderResources(27, 1, &none);
    sceneRtv_ = rtv_;''', '''    ctx_->PSSetShaderResources(15, 1, &none);
    ctx_->PSSetShaderResources(27, 1, &none);
    ctx_->PSSetShaderResources(28, 1, &none);
    sceneRtv_ = rtv_;''')
patch(R, '''        keepFrameForSsr(viewProj);
        drawPost();''', '''        keepFrameForSsr(viewProj);
        std::memcpy(lastViewProj_, viewProj, sizeof(lastViewProj_));
        drawPost();''')
patch(R, '''    safeRelease(cbGame_);''', '''    safeRelease(cbGame_);
    safeRelease(cbVol_);''')

C = 'src/qtapp/camera_view.cpp'
patch(C, '''            g.veil = float(stockPostFx_.veil);''', '''            g.veil = float(stockPostFx_.veil);
            const GamePostFx& p = stockPostFx_;
            if (p.volAir > 0) {  // the volumetric fog (research 109 section 2)
                const float hExp = p.volHeightHalf != 0 ? float(-0.693147 / std::max(p.volHeightHalf, 0.1)) : 0.0f;
                const float vol[3][4] = {{float(p.volAlbedo[0]), float(p.volAlbedo[1]), float(p.volAlbedo[2]), float(p.volAir * 0.01)},
                                         {float(p.volAbsorption), float(p.volAmbient), float(p.volHeightBase), hExp},
                                         {float(p.volSun), float(p.volAnisotropy), 1.0f, 0.0f}};
                std::memcpy(g.vol, vol, sizeof(vol));
            }''')
print('ok')
