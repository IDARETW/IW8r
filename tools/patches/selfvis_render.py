import io

# SELFVIS in Game Lighting (research 105 section 4, research 133 section 7, research 95 "Static models"): a stock prop
# vertex carries its self visibility (bent normal x amount, cone) in 4 signed bytes; vs_tex_inst turns it by the
# placement; the stock PS's ambient occlusion becomes min(|sv| vis(c, w), screen AO), c = dot(normalize(sv), N) .5 + .5,
# which then feeds the multi-bounce ambient and the specular occlusion as before. MainWindow packs it for surfaces
# flagged 0x40 whose GLB has it, and asks the model cache to fetch again the drawn models cached before it.
# IW8R_NO_SELFVIS: neither.

ROOT = 'E:/IW8-Radiant/'


def patch(p, a, b, count=1):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == count, (p, s.count(a), a[:70])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


H = 'src/viewport/render.h'
patch(H, '''    uint32_t color = 0xFFFFFFFFu;
};''', '''    uint32_t color = 0xFFFFFFFFu;
    // The static-model self visibility (research 105 section 4) as signed bytes: xyz the bent normal times the visible
    // amount, w = 1 + 126 x the cone value (0 = none).
    uint32_t selfVis = 0;
};''')

C = 'src/viewport/render.cpp'
patch(C, '''               float3 lgvBack : TEXCOORD5; float3 edge : TEXCOORD6; float4 vc : TEXCOORD7; };  // vc: a prop's vertex colour (linear; research 133); w = 1 on props''',
      '''               float3 lgvBack : TEXCOORD5; float3 edge : TEXCOORD6; float4 vc : TEXCOORD7;  // vc: a prop's vertex colour (linear; research 133); w = 1 on props
               float4 sv : TEXCOORD8; };  // a stock prop's self visibility (world bent normal x amount, cone; w < 0: none)''')
patch(C, '''    o.lgvBack = float3(0, 0, 0);
    o.vc = float4(1, 1, 1, 0);''', '''    o.lgvBack = float3(0, 0, 0);
    o.vc = float4(1, 1, 1, 0);
    o.sv = float4(0, 0, 0, -1);''')
patch(C, '''struct VSInInst { float3 pos : POSITION; float2 uv : TEXCOORD0; float4 col : COLOR; float3 n : NORMAL; float4 vcol : COLOR1; float4 r0 : INSTROW0; float4 r1 : INSTROW1; float4 r2 : INSTROW2;''',
      '''struct VSInInst { float3 pos : POSITION; float2 uv : TEXCOORD0; float4 col : COLOR; float3 n : NORMAL; float4 vcol : COLOR1; float4 sv : SELFVIS; float4 r0 : INSTROW0; float4 r1 : INSTROW1; float4 r2 : INSTROW2;''')
patch(C, '''    o.vc = float4(i.vcol.rgb <= 0.04045 ? i.vcol.rgb / 12.92 : pow((i.vcol.rgb + 0.055) / 1.055, 2.4), 1.0);  // w: a prop''',
      '''    o.vc = float4(i.vcol.rgb <= 0.04045 ? i.vcol.rgb / 12.92 : pow((i.vcol.rgb + 0.055) / 1.055, 2.4), 1.0);  // w: a prop
    // Self visibility (research 105 section 4): turned by the placement like the normal, its length kept.
    o.sv = float4(0, 0, 0, -1);
    if (i.sv.w > 0.004) {
        const float3 d = float3(dot(i.r0.xyz, i.sv.xyz), dot(i.r1.xyz, i.sv.xyz), dot(i.r2.xyz, i.sv.xyz));
        const float l = length(d);
        o.sv = float4(l > 1e-8 ? d * (length(i.sv.xyz) / l) : float3(0, 0, 0), saturate((i.sv.w * 127.0 - 1.0) / 126.0));
    }''')
patch(C, '''        const float aoV = gameAo.x > 0.5 ? lerp(1.0, aoMap.Load(int3(i.pos.xy, 0)), gameSunCol.w) : 1.0;  // x the AO strength''',
      '''        float aoV = gameAo.x > 0.5 ? lerp(1.0, aoMap.Load(int3(i.pos.xy, 0)), gameSunCol.w) : 1.0;  // x the AO strength''')
patch(C, '''        // Without a lightmap texel (props, models, new brushes): on a stock map the game lights them from its light grid;''',
      '''        // A stock prop's self visibility (research 95 "Static models", research 105 section 4): the occlusion is the
        // smaller of |sv| x vis(c, w) and the screen AO; it feeds the multi-bounce ambient and the specular occlusion.
        if (stockRef && i.sv.w >= 0.0) {
            const float svLen = length(i.sv.xyz);
            const float c = svLen > 1e-6 ? dot(i.sv.xyz / svLen, N) * 0.5 + 0.5 : 0.5, w = i.sv.w;
            const float vis = saturate((1.04951 * c - 0.049585 * c * c - 0.007353) + w * (2.96123 * c * c - 2.96096 * c + 0.006773) +
                                       w * w * (1.85757 * c - 2.85775 * c * c + 0.022416));
            aoV = min(svLen * vis, aoV);
        }
        // Without a lightmap texel (props, models, new brushes): on a stock map the game lights them from its light grid;''')
patch(C, '''                // PropVertex (32 bytes): position, uv, colour, normal as signed bytes, the model's vertex colour.''',
      '''                // PropVertex (36 bytes): position, uv, colour, normal as signed bytes, the model's vertex colour, the self
                // visibility as signed bytes.''')
patch(C, '''                    {"COLOR", 1, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 28, D3D11_INPUT_PER_VERTEX_DATA, 0},
                    {"INSTROW", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 0, D3D11_INPUT_PER_INSTANCE_DATA, 1},''',
      '''                    {"COLOR", 1, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 28, D3D11_INPUT_PER_VERTEX_DATA, 0},
                    {"SELFVIS", 0, DXGI_FORMAT_R8G8B8A8_SNORM, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0},
                    {"INSTROW", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 0, D3D11_INPUT_PER_INSTANCE_DATA, 1},''')

M = 'src/qtapp/main_window.cpp'
patch(M, '''                        const std::string prefix = ms.material.substr(0, ms.material.find('/'));
                        const bool vertCol = ms.color.size() == nv && prefix.find('c') != std::string::npos &&
                                             (prefix.rfind("m", 0) == 0 || prefix.rfind("tm", 0) == 0);''',
      '''                        const std::string prefix = ms.material.substr(0, ms.material.find('/'));
                        const bool vertCol = ms.color.size() == nv && prefix.find('c') != std::string::npos &&
                                             (prefix.rfind("m", 0) == 0 || prefix.rfind("tm", 0) == 0);
                        // The self visibility of a surface flagged for it (0x40; research 105 section 4).
                        static const bool noSelfVis = std::getenv("IW8R_NO_SELFVIS") != nullptr;
                        const bool selfVis = !noSelfVis && ms.selfVis.size() == nv && (ms.sourceFlags & 0x40u) != 0;''')
patch(M, '''                            Vec3 sn = shadeN[v];
                            if (ownNormals) {''', '''                            if (selfVis) {
                                const auto& s = ms.selfVis[v];
                                const Vec3 d = zup(Vec3{s[0], s[1], s[2]});
                                const auto sb = [](double c) {
                                    const double k = std::clamp(c, -1.0, 1.0) * 127.0;
                                    return uint32_t(uint8_t(int8_t(k >= 0 ? int(k + 0.5) : int(k - 0.5))));
                                };
                                pv.selfVis = sb(d.x) | (sb(d.y) << 8) | (sb(d.z) << 16) |
                                             (uint32_t(1 + int(std::clamp(double(s[3]), 0.0, 1.0) * 126.0 + 0.5)) << 24);
                            }
                            Vec3 sn = shadeN[v];
                            if (ownNormals) {''')
print('ok')
