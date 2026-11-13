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


R = 'src/viewport/render.cpp'
patch(R, '''               nointerpolation float4 l0 : TEXCOORD5; nointerpolation float4 l1 : TEXCOORD6; nointerpolation float4 l2 : TEXCOORD7; };''',
      '''               float4 lgv : TEXCOORD5; };''')
patch(R, '''    o.l0 = float4(-1, 0, 0, 0); o.l1 = float4(0, 0, 0, 0); o.l2 = float4(0, 0, 0, 0);  // no prop light grid''',
      '''    o.lgv = float4(0, 0, 0, -1);  // no prop light grid''')
patch(R, '''    o.l0 = i.l0; o.l1 = i.l1; o.l2 = i.l2;  // the prop's light grid box (research 105)''',
      '''    // The prop's light grid box (research 105 section 1.4, DC + first order), per vertex with the vertex normal as the
    // game's static-model shader does: E = 0.282095 DC + 4/9 0.488603 (s0 n.z - s2 n.y - s3 n.x), in lightmap units
    // (the pixel shader applies bakedLightScale). w = 1 lit, -1 none (DC r < 0: the probes' fallback).
    if (i.l0.x >= 0.0) {
        const float3 dc = i.l0.xyz, s0 = float3(i.l0.w, i.l1.x, i.l1.y), s2 = float3(i.l1.z, i.l1.w, i.l2.x), s3 = i.l2.yzw;
        o.lgv = float4(max(0.282095 * dc + 0.217157 * (s0 * o.n.z - s2 * o.n.y - s3 * o.n.x), 0.0), 1.0);
    } else {
        o.lgv = float4(0, 0, 0, -1);
    }''')
patch(R, '''        if (!lmOk && stockRef && i.l0.x >= 0.0) {
            // A stock prop's light grid box (research 105 1.4, DC + first order): E = 0.282095 DC + 4/9 0.488603
            // (s0 n.z - s2 n.y - s3 n.x), in lightmap units (x bakedLightScale).
            const float3 dc = i.l0.xyz, s0 = float3(i.l0.w, i.l1.x, i.l1.y), s2 = float3(i.l1.z, i.l1.w, i.l2.x), s3 = i.l2.yzw;
            indirect = max(0.282095 * dc + 0.217157 * (s0 * N.z - s2 * N.y - s3 * N.x), 0.0) * gameSkyScale.y;
        } else if''', '''        if (!lmOk && stockRef && i.lgv.w > 0.0) {
            indirect = i.lgv.rgb * gameSkyScale.y;  // the prop's light grid box (vs_tex_inst), x bakedLightScale
        } else if''')
print('ok')
