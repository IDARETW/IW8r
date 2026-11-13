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

# ---- the instanced mesh carries a light term per instance
patch(H, '''    // Stock foliage: the model's last LOD distance (inches). The game stops drawing a placement beyond it (the grass
    // dissolves there with a dither); 0 = always drawn.
    float lodOut = 0;''', '''    // Stock foliage: the model's last LOD distance (inches). The game stops drawing a placement beyond it (the grass
    // dissolves there with a dither); 0 = always drawn.
    float lodOut = 0;
    // Per instance, the prop's light from its light grid box (research 105): 12 floats, DC rgb then the three first-
    // order terms per channel (slots 0, 2, 3), before bakedLightScale; DC r < 0 = none (the probes' fallback). Empty:
    // none for every instance.
    std::vector<float> light;''')
patch(H, '''        std::vector<float> rows;
        float centre[3] = {0, 0, 0}, radius = 0;''', '''        std::vector<float> rows;
        std::vector<float> light;  // InstancedTexMesh::light (12 per instance)
        float centre[3] = {0, 0, 0}, radius = 0;''')

# ---- shaders
patch(R, '''struct PSIn { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; float4 col : COLOR; float3 w : TEXCOORD1; float3 n : TEXCOORD2; float3 t : TEXCOORD3; float2 lm : TEXCOORD4; };
PSIn vs_tex(VSIn i) {''', '''struct PSIn { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; float4 col : COLOR; float3 w : TEXCOORD1; float3 n : TEXCOORD2; float3 t : TEXCOORD3; float2 lm : TEXCOORD4;
               nointerpolation float4 l0 : TEXCOORD5; nointerpolation float4 l1 : TEXCOORD6; nointerpolation float4 l2 : TEXCOORD7; };
PSIn vs_tex(VSIn i) {''')
patch(R, '''    o.pos = mul(float4(p, 1.0), viewProj); o.uv = i.uv; o.col = i.col; o.w = p; o.n = i.n; o.t = i.t; o.lm = i.lm; return o;
}''', '''    o.pos = mul(float4(p, 1.0), viewProj); o.uv = i.uv; o.col = i.col; o.w = p; o.n = i.n; o.t = i.t; o.lm = i.lm;
    o.l0 = float4(-1, 0, 0, 0); o.l1 = float4(0, 0, 0, 0); o.l2 = float4(0, 0, 0, 0);  // no prop light grid
    return o;
}''')
patch(R, '''struct VSInInst { float3 pos : POSITION; float2 uv : TEXCOORD0; float4 col : COLOR; float3 n : NORMAL; float3 t : TANGENT; float2 lm : TEXCOORD4; float4 r0 : INSTROW0; float4 r1 : INSTROW1; float4 r2 : INSTROW2; };''',
      '''struct VSInInst { float3 pos : POSITION; float2 uv : TEXCOORD0; float4 col : COLOR; float3 n : NORMAL; float3 t : TANGENT; float2 lm : TEXCOORD4; float4 r0 : INSTROW0; float4 r1 : INSTROW1; float4 r2 : INSTROW2;
                  float4 l0 : INSTROW3; float4 l1 : INSTROW4; float4 l2 : INSTROW5; };''')
patch(R, '''    PSIn o; o.pos = mul(float4(w, 1.0), viewProj); o.uv = i.uv; o.col = i.col; o.w = w; o.n = dot(nw, nw) > 1e-12 ? normalize(nw) : float3(0, 0, 0); o.t = float3(0, 0, 0); o.lm = float2(-1, -1); return o;''',
      '''    PSIn o; o.pos = mul(float4(w, 1.0), viewProj); o.uv = i.uv; o.col = i.col; o.w = w; o.n = dot(nw, nw) > 1e-12 ? normalize(nw) : float3(0, 0, 0); o.t = float3(0, 0, 0); o.lm = float2(-1, -1);
    o.l0 = i.l0; o.l1 = i.l1; o.l2 = i.l2;  // the prop's light grid box (research 105)
    return o;''')
# the light where no lightmap: the prop's own box first, then the probes
patch(R, '''        if (!lmOk && gameSky.w > 0.5 && probeCount.x > 0.5)
            indirect = stockProbeLight(i.w, N, 0.0) * dot(gameSky.rgb, float3(0.2126, 0.7152, 0.0722));''', '''        if (!lmOk && stockRef && i.l0.x >= 0.0) {
            // A stock prop's light grid box (research 105 1.4, DC + first order): E = 0.282095 DC + 4/9 0.488603
            // (s0 n.z - s2 n.y - s3 n.x), in lightmap units (x bakedLightScale).
            const float3 dc = i.l0.xyz, s0 = float3(i.l0.w, i.l1.x, i.l1.y), s2 = float3(i.l1.z, i.l1.w, i.l2.x), s3 = i.l2.yzw;
            indirect = max(0.282095 * dc + 0.217157 * (s0 * N.z - s2 * N.y - s3 * N.x), 0.0) * gameSkyScale.y;
        } else if (!lmOk && gameSky.w > 0.5 && probeCount.x > 0.5)
            indirect = stockProbeLight(i.w, N, 0.0) * dot(gameSky.rgb, float3(0.2126, 0.7152, 0.0722));''')
# the input layout of the textured instances: 6 rows
patch(R, '''                    {"INSTROW", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 0, D3D11_INPUT_PER_INSTANCE_DATA, 1},
                    {"INSTROW", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 16, D3D11_INPUT_PER_INSTANCE_DATA, 1},
                    {"INSTROW", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 32, D3D11_INPUT_PER_INSTANCE_DATA, 1},''', '''                    {"INSTROW", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 0, D3D11_INPUT_PER_INSTANCE_DATA, 1},
                    {"INSTROW", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 16, D3D11_INPUT_PER_INSTANCE_DATA, 1},
                    {"INSTROW", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 32, D3D11_INPUT_PER_INSTANCE_DATA, 1},
                    {"INSTROW", 3, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 48, D3D11_INPUT_PER_INSTANCE_DATA, 1},
                    {"INSTROW", 4, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 64, D3D11_INPUT_PER_INSTANCE_DATA, 1},
                    {"INSTROW", 5, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 80, D3D11_INPUT_PER_INSTANCE_DATA, 1},''')
print('ok part 1')

patch(R, '''                dev_->CreateInputLayout(tiil, 9, tivs->GetBufferPointer(), tivs->GetBufferSize(), &layoutTexInst_);''',
      '''                dev_->CreateInputLayout(tiil, 12, tivs->GetBufferPointer(), tivs->GetBufferSize(), &layoutTexInst_);''')
patch(R, '''                bd.ByteWidth = UINT(count * 12 * sizeof(float));
                D3D11_SUBRESOURCE_DATA iinit{m.rows.data(), 0, 0};
                if (SUCCEEDED(dev_->CreateBuffer(&bd, &iinit, &g.inst))) {
                    g.vertices = unsigned(m.tris.size());
                    g.instances = unsigned(count);
                    g.visible = g.instances;
                    g.rows = m.rows;''', '''                // 24 floats per instance on the GPU: the 3 transform rows, then the prop's light (research 105).
                g.light = m.light.size() == count * 12 ? m.light : std::vector<float>(count * 12, 0.0f);
                if (m.light.size() != count * 12)
                    for (size_t i = 0; i < count; ++i) g.light[i * 12] = -1.0f;  // none
                std::vector<float> inst24(count * 24);
                for (size_t i = 0; i < count; ++i) {
                    std::memcpy(&inst24[i * 24], &m.rows[i * 12], 12 * sizeof(float));
                    std::memcpy(&inst24[i * 24 + 12], &g.light[i * 12], 12 * sizeof(float));
                }
                bd.ByteWidth = UINT(count * 24 * sizeof(float));
                D3D11_SUBRESOURCE_DATA iinit{inst24.data(), 0, 0};
                if (SUCCEEDED(dev_->CreateBuffer(&bd, &iinit, &g.inst))) {
                    g.vertices = unsigned(m.tris.size());
                    g.instances = unsigned(count);
                    g.visible = g.instances;
                    g.rows = m.rows;''')
patch(R, '''            keep.insert(keep.end(), r, r + 12);
            {''', '''            keep.insert(keep.end(), r, r + 12);
            keep.insert(keep.end(), &g.light[i * 12], &g.light[i * 12] + 12);
            {''')
patch(R, '''                if (dist <= rad || rad >= dist * pixelAngle * bigPx) keepBig.insert(keepBig.end(), r, r + 12);''',
      '''                if (dist <= rad || rad >= dist * pixelAngle * bigPx)
                    keepBig.insert(keepBig.end(), r, r + 12), keepBig.insert(keepBig.end(), &g.light[i * 12], &g.light[i * 12] + 12);''')
patch(R, '''        g.big = unsigned(keepBig.size() / 12);''', '''        g.big = unsigned(keepBig.size() / 24);''')
patch(R, '''        g.visible = unsigned(keep.size() / 12);''', '''        g.visible = unsigned(keep.size() / 24);''')
patch(R, '''    const UINT strides[2] = {UINT(sizeof(GpuTexVertex)), UINT(12 * sizeof(float))}, offsets[2] = {0, 0};''',
      '''    const UINT strides[2] = {UINT(sizeof(GpuTexVertex)), UINT(24 * sizeof(float))}, offsets[2] = {0, 0};''', 2)
print('ok part 2')
