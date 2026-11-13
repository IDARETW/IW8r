import io


def patch(p, a, b, n=1):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


RH = 'src/viewport/render.h'
patch(RH, '''        ID3D11Buffer* inst = nullptr;
        unsigned vertices = 0, instances = 0;  // vertices: the index count (vertices drawn per instance)''', '''        ID3D11Buffer* inst = nullptr;
        unsigned vertices = 0, instances = 0;  // vertices: the index count (vertices drawn per instance)
        // The placements sorted into 2,048-unit cells: a shadow map draws only the cells in its view (the lamp maps and
        // the near sun cascade drew every placement of the map: Verdansk's 1.2 million, 263 ms a frame, 2026-11-13).
        // `order`: GPU placement i is the caller's placement order[i] (rows and light are kept in GPU order).
        struct InstCell {
            iw8r::AABB box;
            uint32_t first = 0, count = 0;
        };
        std::vector<InstCell> cells;
        std::vector<uint32_t> order;''')
patch(RH, '''    void drawReferenceTexInstancesShadow();''', '''    void drawReferenceTexInstancesShadow(const iw8r::Frustum* view = nullptr);  // view: only the cells inside it''')

R = 'src/viewport/render.cpp'
patch(R, '''            if (g.ib && SUCCEEDED(dev_->CreateBuffer(&bd, &init, &g.vb))) {
                // 24 floats per instance on the GPU: the 3 transform rows, then the prop's light (research 105).
                g.light = m.light.size() == count * 12 ? m.light : std::vector<float>(count * 12, 0.0f);
                if (m.light.size() != count * 12)
                    for (size_t i = 0; i < count; ++i) g.light[i * 12] = -1.0f;  // none
                std::vector<float> inst24(count * 24);
                for (size_t i = 0; i < count; ++i) {
                    std::memcpy(&inst24[i * 24], &m.rows[i * 12], 12 * sizeof(float));
                    std::memcpy(&inst24[i * 24 + 12], &g.light[i * 12], 12 * sizeof(float));
                }''', '''            if (g.ib && SUCCEEDED(dev_->CreateBuffer(&bd, &init, &g.vb))) {
                // The placements in cell order (stable: the meshes of one LOD group, which hold the same placements in
                // the same order, get the same order).
                constexpr double kCell = 2048.0;
                std::vector<uint32_t> key(count);
                for (size_t i = 0; i < count; ++i) {
                    const auto cellOf = [&](float v) { return uint32_t(std::clamp(int(std::floor(v / kCell)) + 32768, 0, 65535)); };
                    key[i] = (cellOf(m.rows[i * 12 + 3]) << 16) | cellOf(m.rows[i * 12 + 7]);
                }
                g.order.resize(count);
                for (size_t i = 0; i < count; ++i) g.order[i] = uint32_t(i);
                std::stable_sort(g.order.begin(), g.order.end(), [&](uint32_t a, uint32_t b) { return key[a] < key[b]; });
                // 24 floats per instance on the GPU: the 3 transform rows, then the prop's light (research 105).
                const bool haveLight = m.light.size() == count * 12;
                g.rows.resize(count * 12);
                g.light.assign(count * 12, 0.0f);
                for (size_t i = 0; i < count; ++i) {
                    const size_t s = g.order[i];
                    std::memcpy(&g.rows[i * 12], &m.rows[s * 12], 12 * sizeof(float));
                    if (haveLight) std::memcpy(&g.light[i * 12], &m.light[s * 12], 12 * sizeof(float));
                    else g.light[i * 12] = -1.0f;  // none
                }
                std::vector<float> inst24(count * 24);
                for (size_t i = 0; i < count; ++i) {
                    std::memcpy(&inst24[i * 24], &g.rows[i * 12], 12 * sizeof(float));
                    std::memcpy(&inst24[i * 24 + 12], &g.light[i * 12], 12 * sizeof(float));
                }''')
patch(R, '''                    g.visible = g.instances;
                    g.rows = m.rows;
                    g.lodOut = m.lodOut;''', '''                    g.visible = g.instances;
                    g.lodOut = m.lodOut;''')
patch(R, '''                    g.radius = std::sqrt(r2);
                    D3D11_BUFFER_DESC dd = bd;''', '''                    g.radius = std::sqrt(r2);
                    // The cells: each one's placements' bounding spheres in world space.
                    for (size_t i = 0; i < count; ++i) {
                        if (i == 0 || key[g.order[i]] != key[g.order[i - 1]]) g.cells.push_back({iw8r::AABB{}, uint32_t(i), 0});
                        GpuTexInstanced::InstCell& c = g.cells.back();
                        ++c.count;
                        const float* r = &g.rows[i * 12];
                        double wc[3], scale = 0;
                        for (int k = 0; k < 3; ++k) {
                            wc[k] = r[k * 4 + 0] * g.centre[0] + r[k * 4 + 1] * g.centre[1] + r[k * 4 + 2] * g.centre[2] + r[k * 4 + 3];
                            scale = std::max(scale, std::sqrt(double(r[k * 4 + 0]) * r[k * 4 + 0] + double(r[k * 4 + 1]) * r[k * 4 + 1] +
                                                              double(r[k * 4 + 2]) * r[k * 4 + 2]));
                        }
                        const double rad = g.radius * scale * 1.0001 + 1.0;
                        c.box.add(iw8r::Vec3{wc[0] - rad, wc[1] - rad, wc[2] - rad});
                        c.box.add(iw8r::Vec3{wc[0] + rad, wc[1] + rad, wc[2] + rad});
                    }
                    D3D11_BUFFER_DESC dd = bd;''')
# Light updates arrive in the caller's order.
patch(R, '''    std::vector<float> inst24(count * 24);
    for (size_t i = 0; i < count; ++i) {
        std::memcpy(&inst24[i * 24], &g.rows[i * 12], 12 * sizeof(float));
        std::memcpy(&inst24[i * 24 + 12], &light[i * 12], 12 * sizeof(float));
    }''', '''    std::vector<float> gpuLight(count * 12);
    for (size_t i = 0; i < count; ++i) {
        const size_t s = i < g.order.size() ? g.order[i] : i;  // the caller's order -> the cells' order
        std::memcpy(&gpuLight[i * 12], &light[s * 12], 12 * sizeof(float));
    }
    std::vector<float> inst24(count * 24);
    for (size_t i = 0; i < count; ++i) {
        std::memcpy(&inst24[i * 24], &g.rows[i * 12], 12 * sizeof(float));
        std::memcpy(&inst24[i * 24 + 12], &gpuLight[i * 12], 12 * sizeof(float));
    }''')
patch(R, '''    safeRelease(g.inst);
    g.inst = fresh;
    g.light = light;  // the culling copies it into the visible-placement buffers every frame''', '''    safeRelease(g.inst);
    g.inst = fresh;
    g.light = std::move(gpuLight);  // the culling copies it into the visible-placement buffers every frame''')
# The shadow draw: only the cells in the view.
patch(R, '''void SceneRenderer::drawReferenceTexInstancesShadow() {''', '''void SceneRenderer::drawReferenceTexInstancesShadow(const iw8r::Frustum* view) {''')
patch(R, '''        ID3D11Buffer* bufs[2] = {g.vb, g.inst};
        ctx_->IASetVertexBuffers(0, 2, bufs, strides, offsets);
        ctx_->IASetIndexBuffer(g.ib, DXGI_FORMAT(g.ibFormat), 0);
        drawInstIdx(g.vertices, g.instances);
    }''', '''        ID3D11Buffer* bufs[2] = {g.vb, g.inst};
        ctx_->IASetVertexBuffers(0, 2, bufs, strides, offsets);
        ctx_->IASetIndexBuffer(g.ib, DXGI_FORMAT(g.ibFormat), 0);
        if (!view || g.cells.empty()) {
            drawInstIdx(g.vertices, g.instances);
            continue;
        }
        // Runs of neighbouring cells in the view, one draw each.
        uint32_t runFirst = 0, runCount = 0;
        for (const auto& c : g.cells) {
            if (iw8r::frustumIntersectsAabb(*view, c.box)) {
                if (runCount && runFirst + runCount == c.first) runCount += c.count;
                else {
                    if (runCount) drawInstIdx(g.vertices, runCount, runFirst);
                    runFirst = c.first, runCount = c.count;
                }
            }
        }
        if (runCount) drawInstIdx(g.vertices, runCount, runFirst);
    }''')
patch(R, '''void SceneRenderer::drawInstIdx(unsigned indices, unsigned instances) {
    ctx_->DrawIndexedInstanced(indices, instances, 0, 0, 0);''', '''void SceneRenderer::drawInstIdx(unsigned indices, unsigned instances, unsigned firstInstance) {
    ctx_->DrawIndexedInstanced(indices, instances, 0, 0, firstInstance);''')
patch(RH, '''    void drawInstIdx(unsigned indices, unsigned instances);''', '''    void drawInstIdx(unsigned indices, unsigned instances, unsigned firstInstance = 0);''')
# drawCasters: the frustum for the culled maps
patch(R, '''        // The textured props (trees, crates, poles) cast too: their leaves through the cut-out test.
        if (refVisible_) drawReferenceTexInstancesShadow();''', '''        // The textured props (trees, crates, poles) cast too: their leaves through the cut-out test. The near cascade
        // and the lamp maps draw the placements in their view only.
        if (refVisible_) {
            const iw8r::Frustum pf = iw8r::frustumFromViewProj(sm);
            drawReferenceTexInstancesShadow(cull ? &pf : nullptr);
        }''')
print('ok')
