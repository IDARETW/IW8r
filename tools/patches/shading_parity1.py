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
# the game's normal decode (research 95 "Surface decode": normalise the octa vector, then z = 1 - 0.6 |xy|^2)
patch(R, '''            float2 sd = nm.ga * 2.0 - 1.0;
            float2 oct = float2(sd.x + sd.y, sd.x - sd.y) * 0.5;
            float2 slope = oct / max(0.02, 1.0 - abs(oct.x) - abs(oct.y));
            float3 Tt = i.t - Nf * dot(i.t, Nf);''', '''            float2 sd = nm.ga * 2.0 - 1.0;
            float2 oct = float2(sd.x + sd.y, sd.x - sd.y) * 0.5;
            // The stock decode (research 95, W:425-428): the octahedral direction normalised, its z replaced by
            // 1 - 0.6 |xy|^2 (the normal map's relief is a little flatter than the plain decode).
            const float2 nxy = normalize(float3(oct, max(1.0 - abs(oct.x) - abs(oct.y), 0.0))).xy;
            const float3 nT = normalize(float3(nxy, 1.0 - 0.6 * dot(nxy, nxy)));
            float2 slope = nT.xy / max(nT.z, 0.02);
            float3 Tt = i.t - Nf * dot(i.t, Nf);''')
# sun specular x cavity (research 95 "Sun")
patch(R, '''                     (diffAlbedo * (1.0 - max(fl.r, max(fl.g, fl.b))) + F * Dg * Vg * 1.0390625);''',
      '''                     (diffAlbedo * (1.0 - max(fl.r, max(fl.g, fl.b))) + F * Dg * Vg * 1.0390625 * (baseMat ? nmCavity : 1.0));''')
# probes on every pass (layers and decals too), as the game's world shader
patch(R, '''            if (probeCount.x > 0.5 && layerP.x < 0.5) {  // layer and decal passes: the lightmap along R (they re-light per pass)''',
      '''            static const bool probesOnLayers = true;
            if (probeCount.x > 0.5 && (layerP.x < 0.5 || probesOnLayers)) {  // every pass, layers and decals too (research 95)''')
print('ok')
