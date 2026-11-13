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
H = 'src/viewport/render.h'
patch(R, '''    g[43] = noLayerProbes ? 0.0f : 1.0f;''', '''    g[43] = noLayerProbes ? 0.0f : 1.0f;
    static const bool ssrShow = std::getenv("IW8R_SSR_SHOW") != nullptr;  // debug: SSR materials and their hits
    g[42] = ssrShow ? 1.0f : 0.0f;''')
patch(R, '''            const bool ssrMat = layerP.x < 0.5 ? layerP.z > 0.5 : (layerP.x < 1.5 && layerTint.w > 0.5);
            if (ssrMat && prevSize.z > 0.5 && probeCount.x > 0.5 && gp >= 0.3) {''', '''            const bool ssrMat = layerP.x < 0.5 ? layerP.z > 0.5 : (layerP.x < 1.5 && layerTint.w > 0.5);
            float ssrDbg = 0.0;  // debug view: 0.25 flagged, 0.5 traced, 1 hit
            if (ssrMat) ssrDbg = 0.25;
            if (ssrMat && prevSize.z > 0.5 && probeCount.x > 0.5 && gp >= 0.3) {
                ssrDbg = 0.5;''')
patch(R, '''                        if (sc.a > 0.0) S = lerp(S, sc.rgb, wF * edge);''', '''                        if (sc.a > 0.0) S = lerp(S, sc.rgb, wF * edge), ssrDbg = 1.0;''')
patch(R, '''            c = diffAlbedo * indirect * kd * (1.0 - fmax) * aoMB + Fenv * S;''', '''            if (gameSkyScale.z > 0.5) return float4(ssrDbg, ssrMat ? gp : 0.0, prevSize.z, 1.0);
            c = diffAlbedo * indirect * kd * (1.0 - fmax) * aoMB + Fenv * S;''')
patch(H, '''    std::array<size_t, 3> baseMaterialStats() const {''', '''    // Batches whose material traces screen-space reflections: {base, layers}.
    std::array<size_t, 2> ssrBatchStats() const {
        std::array<size_t, 2> r{0, 0};
        for (const RefLayer& L : refLayers_) {
            if (L.layer == 0 && !L.decal && L.params[2] > 0.5f) ++r[0];
            if ((L.layer > 0 || L.decal) && L.params[0] > 0.5f && L.params[0] < 1.5f && L.params[7] > 0.5f) ++r[1];
        }
        return r;
    }
    std::array<size_t, 3> baseMaterialStats() const {''')
patch('src/qtapp/camera_view.h', '''    bool gameHdrDrawn() const { return renderer_.gameHdrDrawn(); }''', '''    bool gameHdrDrawn() const { return renderer_.gameHdrDrawn(); }
    std::array<size_t, 2> ssrBatchStats() const { return renderer_.ssrBatchStats(); }''')
patch('src/qtapp/main.cpp', '''                report << QString("game_ao drawn %1 hdr %2").arg(cam->gameAoDrawn() ? 1 : 0).arg(cam->gameHdrDrawn() ? 1 : 0);''',
      '''                report << QString("game_ao drawn %1 hdr %2").arg(cam->gameAoDrawn() ? 1 : 0).arg(cam->gameHdrDrawn() ? 1 : 0);
                const auto sb = cam->ssrBatchStats();
                report << QString("ssr_batches base %1 layers %2").arg(sb[0]).arg(sb[1]);''')
print('ok')
