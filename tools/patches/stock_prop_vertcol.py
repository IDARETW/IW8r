import io

# Research 133 section 4: static models of a VERTCOL material type (the `c` in the prefix: tmco, tm2co, tmlc, mco,
# m2co) multiply their albedo by the vertex colour (GLB COLOR_0, stored sRGB, linearised as the game's VS does). The
# prop vertex gets the colour (PropVertex 32 bytes, COLOR1), the textured PSIn carries it (vc), and Game Lighting
# multiplies the stock albedo by it. White for every other vertex.

ROOT = 'E:/IW8-Radiant/'


def patch(p, a, b, count=1):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == count, (p, s.count(a), a[:70])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


patch('src/viewport/render.h', '''    uint32_t rgba;
    uint32_t normal;  // signed bytes x, y, z (w 0); zero = none (the pixel shader derives one)
};''', '''    uint32_t rgba;
    uint32_t normal;  // signed bytes x, y, z (w 0); zero = none (the pixel shader derives one)
    // The model's own vertex colour (sRGB bytes) of a VERTCOL material: x the albedo in Game Lighting (research 133
    // section 4); white for the others.
    uint32_t color = 0xFFFFFFFFu;
};''')
R = 'src/viewport/render.cpp'
patch(R, '''struct PSIn { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; float4 col : COLOR; float3 w : TEXCOORD1; float3 n : TEXCOORD2; float3 t : TEXCOORD3; float2 lm : TEXCOORD4;
               float3 lgvBack : TEXCOORD5; float3 edge : TEXCOORD6; };''',
      '''struct PSIn { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; float4 col : COLOR; float3 w : TEXCOORD1; float3 n : TEXCOORD2; float3 t : TEXCOORD3; float2 lm : TEXCOORD4;
               float3 lgvBack : TEXCOORD5; float3 edge : TEXCOORD6; float3 vc : TEXCOORD7; };  // vc: a prop's vertex colour (linear; research 133)''')
patch(R, '''    o.lgvBack = float3(0, 0, 0);
    o.edge = i.edge.xyz;''', '''    o.lgvBack = float3(0, 0, 0);
    o.vc = float3(1, 1, 1);
    o.edge = i.edge.xyz;''')
patch(R, '''struct VSInInst { float3 pos : POSITION; float2 uv : TEXCOORD0; float4 col : COLOR; float3 n : NORMAL; float4 r0 : INSTROW0; float4 r1 : INSTROW1; float4 r2 : INSTROW2;''',
      '''struct VSInInst { float3 pos : POSITION; float2 uv : TEXCOORD0; float4 col : COLOR; float3 n : NORMAL; float4 vcol : COLOR1; float4 r0 : INSTROW0; float4 r1 : INSTROW1; float4 r2 : INSTROW2;''')
patch(R, '''o.t = float3(0, 0, 0); o.lm = float2(-1, -1); o.edge = float3(1, 0, 0);
    // The prop's light grid box''', '''o.t = float3(0, 0, 0); o.lm = float2(-1, -1); o.edge = float3(1, 0, 0);
    // The vertex colour of a VERTCOL model, stored sRGB: linear as the game's static-model VS (research 133 section 4).
    o.vc = i.vcol.rgb <= 0.04045 ? i.vcol.rgb / 12.92 : pow((i.vcol.rgb + 0.055) / 1.055, 2.4);
    // The prop's light grid box''')
patch(R, '''                // PropVertex (28 bytes): position, uv, colour, normal as signed bytes.''',
      '''                // PropVertex (32 bytes): position, uv, colour, normal as signed bytes, the model's vertex colour.''')
patch(R, '''                    {"NORMAL", 0, DXGI_FORMAT_R8G8B8A8_SNORM, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
                    {"INSTROW", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 0, D3D11_INPUT_PER_INSTANCE_DATA, 1},''',
      '''                    {"NORMAL", 0, DXGI_FORMAT_R8G8B8A8_SNORM, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
                    {"COLOR", 1, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 28, D3D11_INPUT_PER_VERTEX_DATA, 0},
                    {"INSTROW", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 0, D3D11_INPUT_PER_INSTANCE_DATA, 1},''')
patch(R, '''        float3 albedo = srgbToLinear3(t.rgb) * (stockRef ? float3(1, 1, 1) : i.col.rgb);  // i.col: selection and brush entity tints''',
      '''        float3 albedo = srgbToLinear3(t.rgb) * (stockRef ? i.vc : i.col.rgb);  // i.col: selection and brush entity tints; vc: a prop's vertex colour''')

M = 'src/qtapp/main_window.cpp'
patch(M, '''                        if (pm->indices.empty()) continue;
                        pm->vertices.resize(nv);''', '''                        if (pm->indices.empty()) continue;
                        pm->vertices.resize(nv);
                        // A VERTCOL material (the `c` in the prefix: tmco, tm2co, tmlc, mco, m2co) multiplies its
                        // albedo by the vertex colour (research 133 section 4).
                        const std::string prefix = ms.material.substr(0, ms.material.find('/'));
                        const bool vertCol = ms.color.size() == nv && prefix.find('c') != std::string::npos &&
                                             (prefix.rfind("m", 0) == 0 || prefix.rfind("tm", 0) == 0);''')
patch(M, '''                            pv.normal = 0;
                            Vec3 sn = shadeN[v];''', '''                            pv.normal = 0;
                            if (vertCol) {
                                const auto byte = [](float c) { return uint32_t(std::clamp(c, 0.0f, 1.0f) * 255.0f + 0.5f); };
                                pv.color = byte(ms.color[v][0]) | (byte(ms.color[v][1]) << 8) | (byte(ms.color[v][2]) << 16) | 0xff000000u;
                            }
                            Vec3 sn = shadeN[v];''')
print('ok')
