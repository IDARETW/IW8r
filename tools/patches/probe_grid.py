import io


def patch(p, pairs):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    for a, b in pairs:
        a = a.replace('\n', nl); b = b.replace('\n', nl)
        assert s.count(a) == 1, (p, s.count(a), a[:100])
        s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


patch('src/viewport/render.cpp', [
 ('''cbuffer Probes : register(b5) { float4 probeCount; ProbeBox probeBoxes[128]; };''',
  '''cbuffer Probes : register(b5) { float4 probeCount; float4 probeGridMin; float4 probeGridCell; ProbeBox probeBoxes[128]; };
// A grid over the boxes (setStockProbes): up to four candidate boxes per cell in priority order (255 = none), or the
// nearest box where none covers the cell. probeCount.yzw = grid size, probeGridCell.w = 1 when the grid is bound.
Texture3D<uint4> probeGrid : register(t13);'''),
 ('''    int n = int(probeCount.x);
    // The box nearest to p (largest inside margin, least outside): far terrain outside every box takes its probe for
    // the weight the boxes around p leave (owner 2026-11-12: Hideout's far ridge was black without one).
    float nearRem = -1e30;
    float nearSlice = 0;
    for (int k = 0; k < n && wsum < 0.999; ++k) {
        ProbeBox b = probeBoxes[k];''', '''    int n = int(probeCount.x);
    // The box nearest to p (largest inside margin, least outside): far terrain outside every box takes its probe for
    // the weight the boxes around p leave (owner 2026-11-12: Hideout's far ridge was black without one).
    float nearRem = -1e30;
    float nearSlice = 0;
    // With the grid: only the cell's candidates (Hideout's 48 boxes per pixel per layer pass cost 40 ms a frame).
    const bool grid = probeGridCell.w > 0.5;
    uint4 cand = uint4(0, 0, 0, 0);
    if (grid) {
        int3 dims = int3(probeCount.yzw);
        int3 cell = clamp(int3(floor((p - probeGridMin.xyz) / probeGridCell.xyz)), int3(0, 0, 0), dims - 1);
        cand = probeGrid.Load(int4(cell, 0));
        n = 4;
    }
    for (int j = 0; j < n && wsum < 0.999; ++j) {
        int k = j;
        if (grid) {
            k = int(j == 0 ? cand.x : j == 1 ? cand.y : j == 2 ? cand.z : cand.w);
            if (k >= 255) break;
        }
        ProbeBox b = probeBoxes[k];'''),
 ('''        bd.ByteWidth = 16 + 80 * kMaxProbeBoxes;''', '''        bd.ByteWidth = 48 + 80 * kMaxProbeBoxes;'''),
 ('''    std::vector<float> cb(size_t(4 + 20 * kMaxProbeBoxes), 0.0f);''', '''    safeRelease(probeGridSrv_), safeRelease(probeGridTex_);
    std::vector<float> cb(size_t(12 + 20 * kMaxProbeBoxes), 0.0f);'''),
 ('''        float* e = cb.data() + 4 + 20 * n;''', '''        float* e = cb.data() + 12 + 20 * n;'''),
 ('''    cb[0] = perfOff("probes") ? 0.0f : float(n);  // benchmark switch: no probes
    upload();
}''', '''    cb[0] = perfOff("probes") ? 0.0f : float(n);  // benchmark switch: no probes
    // The candidate grid over the boxes' world bounds (the fallback box, as large as the map, left out of the bounds).
    if (n > 4 && !perfOff("probegrid")) {
        struct WB {
            float lo[3], hi[3];
        };
        std::vector<WB> wb(n);
        float glo[3] = {1e30f, 1e30f, 1e30f}, ghi[3] = {-1e30f, -1e30f, -1e30f};
        for (size_t i = 0; i < n; ++i) {
            const float* e = cb.data() + 12 + 20 * i;
            for (int c = 0; c < 3; ++c) {
                float ext = 0;
                for (int a = 0; a < 3; ++a) ext += std::fabs(e[4 + 4 * a + c]) * e[7 + 4 * a];
                wb[i].lo[c] = e[c] - ext, wb[i].hi[c] = e[c] + ext;
            }
            if (i + 1 < n || n == 1)
                for (int c = 0; c < 3; ++c) glo[c] = std::min(glo[c], wb[i].lo[c]), ghi[c] = std::max(ghi[c], wb[i].hi[c]);
        }
        int dims[3];
        float cell[3];
        const float vol = std::max((ghi[0] - glo[0]) * (ghi[1] - glo[1]) * (ghi[2] - glo[2]), 1.0f);
        const float side = std::cbrt(vol / 65536.0f);  // about 64 x 64 x 16 cells
        for (int c = 0; c < 3; ++c) {
            dims[c] = std::clamp(int(std::ceil((ghi[c] - glo[c]) / std::max(side, 1.0f))), 1, 128);
            cell[c] = std::max((ghi[c] - glo[c]) / float(dims[c]), 1.0f);
        }
        std::vector<uint8_t> g(size_t(dims[0]) * dims[1] * dims[2] * 4, 255);
        for (int z = 0; z < dims[2]; ++z)
            for (int y = 0; y < dims[1]; ++y)
                for (int x = 0; x < dims[0]; ++x) {
                    const float clo[3] = {glo[0] + x * cell[0], glo[1] + y * cell[1], glo[2] + z * cell[2]};
                    const float chi[3] = {clo[0] + cell[0], clo[1] + cell[1], clo[2] + cell[2]};
                    uint8_t* q = &g[((size_t(z) * dims[1] + y) * dims[0] + x) * 4];
                    int used = 0;
                    for (size_t i = 0; i < n && used < 4; ++i) {
                        bool touch = true;
                        for (int c = 0; c < 3; ++c) touch = touch && wb[i].lo[c] <= chi[c] && wb[i].hi[c] >= clo[c];
                        if (touch) q[used++] = uint8_t(i);
                    }
                    if (used == 0) {
                        // Nearest box by its oriented margin at the cell centre (the shader's fallback).
                        const float p[3] = {0.5f * (clo[0] + chi[0]), 0.5f * (clo[1] + chi[1]), 0.5f * (clo[2] + chi[2])};
                        float best = -1e30f;
                        size_t bi = 0;
                        for (size_t i = 0; i < n; ++i) {
                            const float* e = cb.data() + 12 + 20 * i;
                            const float d[3] = {p[0] - e[0], p[1] - e[1], p[2] - e[2]};
                            float m = 1e30f;
                            for (int a = 0; a < 3; ++a) {
                                const float lp = d[0] * e[4 + 4 * a] + d[1] * e[5 + 4 * a] + d[2] * e[6 + 4 * a];
                                m = std::min(m, e[7 + 4 * a] - std::fabs(lp));
                            }
                            if (m > best) best = m, bi = i;
                        }
                        q[0] = uint8_t(bi);
                    }
                }
        D3D11_TEXTURE3D_DESC t3{};
        t3.Width = UINT(dims[0]), t3.Height = UINT(dims[1]), t3.Depth = UINT(dims[2]);
        t3.MipLevels = 1;
        t3.Format = DXGI_FORMAT_R8G8B8A8_UINT;
        t3.Usage = D3D11_USAGE_IMMUTABLE;
        t3.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA gi{g.data(), UINT(dims[0]) * 4, UINT(dims[0]) * UINT(dims[1]) * 4};
        if (SUCCEEDED(dev_->CreateTexture3D(&t3, &gi, &probeGridTex_)) &&
            SUCCEEDED(dev_->CreateShaderResourceView(probeGridTex_, nullptr, &probeGridSrv_))) {
            cb[1] = float(dims[0]), cb[2] = float(dims[1]), cb[3] = float(dims[2]);
            cb[4] = glo[0], cb[5] = glo[1], cb[6] = glo[2];
            cb[8] = cell[0], cb[9] = cell[1], cb[10] = cell[2], cb[11] = 1.0f;
        } else {
            safeRelease(probeGridTex_);
        }
    }
    upload();
}'''),
 ('''    ID3D11ShaderResourceView* gl[3] = {gameLutSrv_, envBrdfSrv_, probeArrSrv_};
    ctx_->PSSetShaderResources(9, 3, gl);''', '''    ID3D11ShaderResourceView* gl[3] = {gameLutSrv_, envBrdfSrv_, probeArrSrv_};
    ctx_->PSSetShaderResources(9, 3, gl);
    ctx_->PSSetShaderResources(13, 1, &probeGridSrv_);'''),
 ('''    safeRelease(probeArrSrv_), safeRelease(probeArrTex_), safeRelease(cbProbes_), safeRelease(cbLmRects_);''',
  '''    safeRelease(probeArrSrv_), safeRelease(probeArrTex_), safeRelease(cbProbes_), safeRelease(cbLmRects_);
    safeRelease(probeGridSrv_), safeRelease(probeGridTex_);'''),
])
s = io.open('src/viewport/render.h', encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
a = '    ID3D11Buffer* cbLayer_ = nullptr;'
assert s.count(a) == 1
s = s.replace(a, a + nl + '    struct ID3D11Texture3D* probeGridTex_ = nullptr;  // setStockProbes: candidate boxes per cell' + nl +
              '    ID3D11ShaderResourceView* probeGridSrv_ = nullptr;', 1)
io.open('src/viewport/render.h', 'w', encoding='utf-8', newline='').write(s)
print('ok')
