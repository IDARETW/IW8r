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
patch(R, '''            if (probeCount.x > 0.5 && (layerP.x < 0.5 || (probesOnLayers && layerUvO.w < 0.5))) {  // every pass but the terrain patch (research 95)
                // Probes are luminance-normalised: relit by the lightmap along the reflection (research 95).
                float3 P = stockProbeLight(i.w, R, gp) * dot(Lr, float3(0.2126, 0.7152, 0.0722));
                float3 mixed = lerp(P, Lr, 0.85);
                S = lerp(0.18 * mixed, P, so);  // so: the specular occlusion above
            }''', '''            // The rough-surface fallback (research 107 section 4.2: cb7[186] = (-10, 2) on every map): below g' 0.1 the
            // probe is not read at all (the result is then the plain S above), up to g' 0.2 it fades in.
            const float probeFb = saturate(2.0 - 10.0 * gp);
            if (probeFb < 1.0 && probeCount.x > 0.5 && (layerP.x < 0.5 || (probesOnLayers && layerUvO.w < 0.5))) {  // every pass but the terrain patch (research 95)
                // Probes are luminance-normalised: relit by the lightmap along the reflection (research 95).
                float3 P = stockProbeLight(i.w, R, gp) * dot(Lr, float3(0.2126, 0.7152, 0.0722));
                P = lerp(P, Lr, probeFb);
                float3 mixed = lerp(P, Lr, 0.85);
                S = lerp(0.18 * mixed, P, so);  // so: the specular occlusion above
            }''')
print('ok')
