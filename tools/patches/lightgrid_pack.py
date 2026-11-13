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
# no extra interpolant: instanced props have no tangent and no lightmap UV, so their light grid light rides in t
# with lm = (-2, -2) as the mark
patch(R, '''               float4 lgv : TEXCOORD5; };''', ''' };''')
patch(R, '''    o.lgv = float4(0, 0, 0, -1);  // no prop light grid
''', '')
patch(R, '''    // The prop's light grid box (research 105 section 1.4, DC + first order), per vertex with the vertex normal as the
    // game's static-model shader does: E = 0.282095 DC + 4/9 0.488603 (s0 n.z - s2 n.y - s3 n.x), in lightmap units
    // (the pixel shader applies bakedLightScale). w = 1 lit, -1 none (DC r < 0: the probes' fallback).
    if (i.l0.x >= 0.0) {
        const float3 dc = i.l0.xyz, s0 = float3(i.l0.w, i.l1.x, i.l1.y), s2 = float3(i.l1.z, i.l1.w, i.l2.x), s3 = i.l2.yzw;
        o.lgv = float4(max(0.282095 * dc + 0.217157 * (s0 * o.n.z - s2 * o.n.y - s3 * o.n.x), 0.0), 1.0);
    } else {
        o.lgv = float4(0, 0, 0, -1);
    }''', '''    // The prop's light grid box (research 105 section 1.4, DC + first order), per vertex with the vertex normal as the
    // game's static-model shader does: E = 0.282095 DC + 4/9 0.488603 (s0 n.z - s2 n.y - s3 n.x), in lightmap units
    // (the pixel shader applies bakedLightScale). Props have no tangent and no lightmap UV: E rides in t, marked by
    // lm = (-2, -2) (propLight()). DC r < 0: none (the probes' fallback).
    if (i.l0.x >= 0.0) {
        const float3 dc = i.l0.xyz, s0 = float3(i.l0.w, i.l1.x, i.l1.y), s2 = float3(i.l1.z, i.l1.w, i.l2.x), s3 = i.l2.yzw;
        o.t = max(0.282095 * dc + 0.217157 * (s0 * o.n.z - s2 * o.n.y - s3 * o.n.x), 0.0);
        o.lm = float2(-2, -2);
    }''')
patch(R, '''Texture2D revealTex : register(t12);
float4 ps_tex(PSIn i) : SV_TARGET {''', '''Texture2D revealTex : register(t12);
// vs_tex_inst: a prop lit from its light grid box carries that light in t (no tangent) and lm = (-2, -2).
bool propLit(PSIn i) { return i.lm.x < -1.5; }
float4 ps_tex(PSIn i) : SV_TARGET {
    const float3 propLgv = propLit(i) ? i.t : float3(0, 0, 0);
    if (propLit(i)) i.t = float3(0, 0, 0);  // no vertex tangent: the normal map frame from the screen derivatives''')
patch(R, '''        if (!lmOk && stockRef && i.lgv.w > 0.0) {
            indirect = i.lgv.rgb * gameSkyScale.y;  // the prop's light grid box (vs_tex_inst), x bakedLightScale''', '''        if (!lmOk && stockRef && propLit(i)) {
            indirect = propLgv * gameSkyScale.y;  // the prop's light grid box (vs_tex_inst), x bakedLightScale''')
print('ok')
