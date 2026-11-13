import io
def patch(p, pairs):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    for a, b in pairs:
        a = a.replace('\n', nl); b = b.replace('\n', nl)
        assert s.count(a) == 1, (p, s.count(a), a[:100])
        s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)
patch('src/qtapp/main_window.cpp', [
 ('''                                    tv.u = float(uv[0]);
                                    tv.v = float(uv[1]);
                                    tv.rgba = col;
                                    texTris.push_back(tv);''', '''                                    tv.u = float(uv[0]);
                                    tv.v = float(uv[1]);
                                    tv.rgba = col;
                                    // The model's own vertex normals (tree cards bend theirs outward from the crown): Game
                                    // Lighting shades with them (vs_tex_inst turns them by the instance).
                                    if (ms.normal.size() == ms.pos.size()) {
                                        const Vec3 nn = zup(ms.normal[ms.idx[i + size_t(n)]]);
                                        if (length(nn) > 1e-6) {
                                            const Vec3 un = normalize(nn);
                                            tv.nx = float(un.x), tv.ny = float(un.y), tv.nz = float(un.z);
                                        }
                                    }
                                    texTris.push_back(tv);'''),
])
patch('src/viewport/render.cpp', [
 ('''    PSIn o; o.pos = mul(float4(w, 1.0), viewProj); o.uv = i.uv; o.col = i.col; o.w = w; o.n = float3(0, 0, 0); o.t = float3(0, 0, 0); o.lm = float2(-1, -1); return o;''',
  '''    // The model's vertex normal turned by the instance (uniform scale; zero = none: the pixel shader derives one).
    float3 nw = float3(dot(i.r0.xyz, i.n), dot(i.r1.xyz, i.n), dot(i.r2.xyz, i.n));
    PSIn o; o.pos = mul(float4(w, 1.0), viewProj); o.uv = i.uv; o.col = i.col; o.w = w; o.n = dot(nw, nw) > 1e-12 ? normalize(nw) : float3(0, 0, 0); o.t = float3(0, 0, 0); o.lm = float2(-1, -1); return o;'''),
])
print('ok')
