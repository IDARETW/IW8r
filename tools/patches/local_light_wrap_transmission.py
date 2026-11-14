import io

# Plan 1.4 (research 107 sections 1 and 2, read again in the listings on 2026-11-14): the game's lit shaders have two
# forms of a lamp's diffuse light.
#   plain (world W, static model SM; research 106): the bulb-capped irradiance with the horizon term, as drawn so far;
#   wrap materials (W2 / Wm, techset bit 100a) and transmission materials (L / Lw):
#       diffuse  = pow(att, e) x cap x wrapF(N.L, w')      cap = saturate(bulbRadius^2 / (d + collimationOffset)^2)
#       specular = att x cap x saturate(N.L)               w'  = clamp(sqrt(w^2 + h^2), 1e-6, 1.25), h = .77 s + .33 s^2,
#                                                          s   = bulbRadius / distance; w = 0 and e = 1 without wrap
#     (88d201db.asm:1041-1093, e2969e82.asm:1269-1327: att is the falloff x shadow product, e the sun's per-channel
#      shadow power). Transmission materials add the lamp seen through the surface: the sun's back lobe with the lamp's
#      point picked along the eye ray, x the sphere-energy factor (research 107 section 1, L:1158-1231).
# The viewport drew every material with the plain form.

P = 'E:/IW8-Radiant/src/viewport/render.cpp'
s = io.open(P, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'


def rep(a, b):
    global s
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == 1, (s.count(a), a[:80])
    s = s.replace(a, b)


rep('''void gameLocalLight(GameLight l, float3 P, float3 N, float3 V, float3 diffuseColor, float3 f0, float gp, float cavity,
                    inout float3 diffSum, inout float3 specSum) {''', '''// wrap: the material's wrap diffuse (rgb, .w = 1 when it has one); trans: its transmission colour (.w = 1 when it has
// one). Those two kinds of material take the lamp's diffuse light by the wrap rule (research 107 section 2.2), and a
// transmission material also lets the lamp shine through (section 1).
void gameLocalLight(GameLight l, float3 P, float3 N, float3 V, float3 diffuseColor, float3 f0, float gp, float cavity,
                    float4 wrap, float4 trans, inout float3 diffSum, inout float3 specSum) {''')
rep('''    const float NdotL = dot(N, Ld), s2 = l.bulbRadius * l.bulbRadius / (dd * dd), y = NdotL * rsqrt(s2);
    const float E = att * ((1.0 - y * y > 0.0) ? pow(saturate(s2 * (0.5 + 0.5 * y)), 1.5) : saturate(NdotL * s2));
    if (E <= 0.0) return;''', '''    const float NdotL = dot(N, Ld), s2 = l.bulbRadius * l.bulbRadius / (dd * dd), y = NdotL * rsqrt(s2);
    // Plain materials: the bulb-capped irradiance with its horizon term, for the diffuse and the specular alike.
    float Es = att * ((1.0 - y * y > 0.0) ? pow(saturate(s2 * (0.5 + 0.5 * y)), 1.5) : saturate(NdotL * s2));
    float3 Ed = Es;
    const bool wrapKind = wrap.w > 0.5 || trans.w > 0.5;
    float cap = 1.0;
    if (wrapKind) {
        // Wrap and transmission materials: the cap by the distance to the bulb, the wrap merged with the bulb's horizon
        // softening, and on wrap materials the falloff x shadow product raised per channel as the sun's shadow is.
        const float dc = d + l.collimationOffset;
        cap = saturate(l.bulbRadius * l.bulbRadius / max(dc * dc, 1e-8));
        const float sb = l.bulbRadius / dd, h = 0.77 * sb + 0.33 * sb * sb;
        const float3 w0 = wrap.w > 0.5 ? wrap.rgb : float3(0, 0, 0);
        const float3 wq = clamp(sqrt(w0 * w0 + h * h), 1e-6, 1.25);
        float3 attC = att;
        if (wrap.w > 0.5) {
            const float mw = min(w0.r, min(w0.g, w0.b));
            const float3 kw = 1.0 + 1.35 * (w0 - mw);
            attC = pow(saturate(att), float3(kw.b * kw.g, kw.b * kw.r, kw.g * kw.r));
        }
        Ed = attC * cap * wrap3(NdotL, wq);
        Es = att * cap * saturate(NdotL);
    }
    const float E = Es;
    if (E <= 0.0 && max(Ed.r, max(Ed.g, Ed.b)) <= 0.0 && trans.w < 0.5) return;''')
rep('''    diffSum += diffuseColor * l.diffuse * E * (1.0 - max(Fl.r, max(Fl.g, Fl.b)));
    specSum += l.diffuse * F * (D * Vis * norm * E * cavity);''', '''    diffSum += diffuseColor * l.diffuse * Ed * (1.0 - max(Fl.r, max(Fl.g, Fl.b)));
    specSum += l.diffuse * F * (D * Vis * norm * E * cavity);
    if (trans.w > 0.5) {
        // The lamp through the surface: lit from behind (N.L < 0), the lamp's point picked along the eye ray continued
        // through the surface, a GGX lobe of the surface's roughness about -N, the sphere-light energy factor.
        const float nlb = -NdotL;
        if (nlb > 0.0 && att > 0.0) {
            const float3 Rb = -V;
            const float aB = dot(l.bulbDir, Rb);
            float tb = clamp((aB * dot(Rb, QP) - dot(l.bulbDir, QP)) / max(dot(l.bulbDir, l.bulbDir) - aB * aB, 1e-12), -1.5, 1.5);
            tb -= tb * tb * tb * (4.0 / 27.0);
            const float3 Sb = QP + tb * l.bulbDir, perpB = Rb * dot(Sb, Rb) - Sb;
            float xb = l.bulbRadius * rsqrt(max(dot(perpB, perpB), 1e-8));
            xb *= rsqrt(xb * xb + 0.125);
            const float3 Hb = normalize(normalize(Sb + perpB * xb) - R);
            const float hb = dot(-N, Hb), qb = hb * hb * (a2 - 1.0) + 1.0;
            const float Db = a2 / max(qb * qb, 1e-8);
            const float Vb = 1.0 / ((nlb + sqrt(a2 + (1.0 - a2) * nlb * nlb)) * (NdotV + sqrt(a2 + (1.0 - a2) * NdotV * NdotV)));
            const float normB = ap / (ap + 0.375892 * l.bulbRadius * rsqrt(max(dot(Sb, Sb), 1e-8)));
            const float2 ABb = gameEnvBrdf(saturate(nlb), gp);
            const float3 Flb = f0 * ABb.x + ABb.y;
            diffSum += l.diffuse * trans.rgb * (att * cap * nlb * Db * Vb * normB * (1.0 - max(Flb.r, max(Flb.g, Flb.b))));
        }
    }''')
rep('''                        gameLocalLight(gameLights[wd * 32u + b], i.w, N, Vs, diffAlbedo, f0, gp, baseMat ? nmCavity : 1.0, dSum, sSum);''',
    '''                        gameLocalLight(gameLights[wd * 32u + b], i.w, N, Vs, diffAlbedo, f0, gp, baseMat ? nmCavity : 1.0, matW, lampT, dSum, sSum);''')
rep('''            if (localInfo.x > 0.5) {
                const uint2 tile = uint2(i.pos.xy) / uint(localInfo.z);''', '''            if (localInfo.x > 0.5) {
                // The transmission colour for the lamps (the image x the material's constant), as the sun's above.
                const float4 lampT = matTr.w > 0.5 ? float4(srgbToLinear3(tr0.Sample(samp0, uvS).rgb) * matTr.rgb, 1.0) : float4(0, 0, 0, 0);
                const uint2 tile = uint2(i.pos.xy) / uint(localInfo.z);''')
io.open(P, 'w', encoding='utf-8', newline='').write(s)
print('ok')
