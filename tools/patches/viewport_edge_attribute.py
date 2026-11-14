import io

# Viewport: brush face vertices carry the packed edge data (core face_edges packEdge) and the custom shader hook fills
# the IW8RSurface edge inputs from it (the same helper as the build).


def patch(p, a, b, n=1):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


H = 'E:/IW8-Radiant/src/viewport/render.h'
patch(H, '''    float lu = -1, lv = -1;
};''', '''    float lu = -1, lv = -1;
    // Edge data of a brush face for custom surface shaders (core face_edges packEdge); 0xFFFFFFFF = none.
    uint32_t edge = 0xFFFFFFFFu;
};''')

C = 'E:/IW8-Radiant/src/viewport/render.cpp'
patch(C, '''struct VSIn { float3 pos : POSITION; float2 uv : TEXCOORD0; float4 col : COLOR; float3 n : NORMAL; float3 t : TANGENT; float2 lm : TEXCOORD4; };
struct PSIn { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; float4 col : COLOR; float3 w : TEXCOORD1; float3 n : TEXCOORD2; float3 t : TEXCOORD3; float2 lm : TEXCOORD4;
               float3 lgvBack : TEXCOORD5; };''', '''struct VSIn { float3 pos : POSITION; float2 uv : TEXCOORD0; float4 col : COLOR; float3 n : NORMAL; float3 t : TANGENT; float2 lm : TEXCOORD4;
              float4 edge : TEXCOORD6; };
struct PSIn { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; float4 col : COLOR; float3 w : TEXCOORD1; float3 n : TEXCOORD2; float3 t : TEXCOORD3; float2 lm : TEXCOORD4;
               float3 lgvBack : TEXCOORD5; float3 edge : TEXCOORD6; };''')
patch(C, '''    o.pos = mul(float4(p, 1.0), viewProj); o.uv = i.uv; o.col = i.col; o.w = p; o.n = i.n; o.t = i.t; o.lm = i.lm;
    o.lgvBack = float3(0, 0, 0);''', '''    o.pos = mul(float4(p, 1.0), viewProj); o.uv = i.uv; o.col = i.col; o.w = p; o.n = i.n; o.t = i.t; o.lm = i.lm;
    o.lgvBack = float3(0, 0, 0);
    o.edge = i.edge.xyz;''')
patch(C, '''o.t = float3(0, 0, 0); o.lm = float2(-1, -1);''', '''o.t = float3(0, 0, 0); o.lm = float2(-1, -1); o.edge = float3(1, 0, 0);''')
patch(C, '''            s.ambient = indirect * aoV, s.gloss = 0.3;
            c = iw8rSurface(s);''', '''            s.ambient = indirect * aoV, s.gloss = 0.3;
            s.faceNormal = dot(i.n, i.n) > 0.5 ? normalize(i.n) : N;
            iw8rEdgeFields(s, i.edge, ddx(i.w), ddy(i.w));
            c = iw8rSurface(s);''')
patch(C, '''                {"TEXCOORD", 4, DXGI_FORMAT_R32G32_FLOAT, 0, 48, D3D11_INPUT_PER_VERTEX_DATA, 0},
            };
            dev_->CreateInputLayout(til, 6,''', '''                {"TEXCOORD", 4, DXGI_FORMAT_R32G32_FLOAT, 0, 48, D3D11_INPUT_PER_VERTEX_DATA, 0},
                {"TEXCOORD", 6, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 56, D3D11_INPUT_PER_VERTEX_DATA, 0},
            };
            dev_->CreateInputLayout(til, 7,''')
print('ok')
