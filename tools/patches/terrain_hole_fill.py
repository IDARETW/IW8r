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


# ---------------------------------------------------------------- core: only the cut-out cells
patch('src/core/stock_super_terrain.h', '''    int cutoutStride = 0;
};''', '''    int cutoutStride = 0;
    // Keep only the cells the cutout clears (the hole filler of a big map: drawn where no streamed tile is loaded).
    bool onlyCutCells = false;
};''')
patch('src/core/stock_super_terrain.cpp', '''    } else if (cut) {
        for (int b = 0; b + 1 < n; ++b)
            for (int a = 0; a + 1 < n; ++a) keep[size_t(b) * size_t(n - 1) + size_t(a)] = coarseKept(*cutout, a, b, cells, stride);
    }''', '''    } else if (cut) {
        for (int b = 0; b + 1 < n; ++b)
            for (int a = 0; a + 1 < n; ++a) keep[size_t(b) * size_t(n - 1) + size_t(a)] = coarseKept(*cutout, a, b, cells, stride);
    }
    if (opt.onlyCutCells)
        for (auto& k : keep) k = cut ? uint8_t(!k) : uint8_t(0);''')
patch('tests/stock_super_terrain_tests.cpp', '''    o.cutoutStride = 2;
    CHECK(buildStockTerrainMesh(t, 0, g, &c, o, m));
    CHECK(m.cellsKept == 13);  // 16 - (0,0), (1,0), (1,1)
    o.cutoutStride = 0;''', '''    o.cutoutStride = 2;
    CHECK(buildStockTerrainMesh(t, 0, g, &c, o, m));
    CHECK(m.cellsKept == 13);  // 16 - (0,0), (1,0), (1,1)
    o.cutoutStride = 0;
    // The hole filler: only the cleared cells (5 at stride 1); none without a cutout.
    o.onlyCutCells = true;
    CHECK(buildStockTerrainMesh(t, 0, g, &c, o, m));
    CHECK(m.cellsKept == 5);
    CHECK(buildStockTerrainMesh(t, 0, g, nullptr, o, m));
    CHECK(m.cellsKept == 0 && m.mesh.idx.empty());
    o.onlyCutCells = false;''')

# ---------------------------------------------------------------- main window: the filler mesh, the tile boxes
M = 'src/qtapp/main_window.cpp'
MH = 'src/qtapp/main_window.h'
patch(M, '''void appendStockSuperTerrain(const QString& zone, const QString& zoneDir, const StockDecalTools& tools,
                             std::vector<app::ReferenceTexBatch>& out, StockGroundGrid* ground = nullptr,
                             std::shared_ptr<StockTerrainLayerState>* layersOut = nullptr);''', '''// holeFill: also the cells the cutouts clear, marked (vertex alpha 253) so the shaders drop them where a streamed tile
// is loaded (its geometry fills the hole; elsewhere the terrain shows instead of the sky).
void appendStockSuperTerrain(const QString& zone, const QString& zoneDir, const StockDecalTools& tools,
                             std::vector<app::ReferenceTexBatch>& out, StockGroundGrid* ground = nullptr,
                             std::shared_ptr<StockTerrainLayerState>* layersOut = nullptr, bool holeFill = false);''')
patch(M, '''void appendStockSuperTerrain(const QString& zone, const QString& zoneDir, const StockDecalTools& tools,
                             std::vector<app::ReferenceTexBatch>& out, StockGroundGrid* ground,
                             std::shared_ptr<StockTerrainLayerState>* layersOut) {''', '''void appendStockSuperTerrain(const QString& zone, const QString& zoneDir, const StockDecalTools& tools,
                             std::vector<app::ReferenceTexBatch>& out, StockGroundGrid* ground,
                             std::shared_ptr<StockTerrainLayerState>* layersOut, bool holeFill) {''')
patch(M, '''    const Vec3 light = normalize(Vec3{0.35, 0.55, 0.75});
    for (const StockTerrainMesh& tm : meshes.meshes) {
        const ImportedMesh& m = tm.mesh;
        if (m.idx.empty()) continue;
        app::ReferenceTexBatch rb;
        // The surface's baked albedo, by its file ("\\x06<path>"); a plain colour when it was not exported.''', '''    const Vec3 light = normalize(Vec3{0.35, 0.55, 0.75});
    StockTerrainMeshes fill;
    if (holeFill) {
        StockTerrainBuildOptions fo = opt;
        fo.onlyCutCells = true;
        buildStockTerrainMeshes(terrain, heights, cutouts, fo, fill, nullptr);
    }
    for (int pass = 0; pass < 2; ++pass)
    for (const StockTerrainMesh& tm : (pass == 0 ? meshes : fill).meshes) {
        const ImportedMesh& m = tm.mesh;
        if (m.idx.empty()) continue;
        const uint32_t alpha = pass == 0 ? 0xfeu : 0xfdu;  // 254 the coarse terrain, 253 its hole filler
        app::ReferenceTexBatch rb;
        // The surface's baked albedo, by its file ("\\x06<path>"); a plain colour when it was not exported.''')
patch(M, '''            app::GpuTexVertex v{float(p.x), float(p.y), float(p.z), 0, 0, g | (g << 8) | (g << 16) | (0xfeu << 24)};''',
      '''            app::GpuTexVertex v{float(p.x), float(p.y), float(p.z), 0, 0, g | (g << 8) | (g << 16) | (alpha << 24)};''')
patch(M, '''        if (world) appendStockSuperTerrain(zone, zoneDir, tools, *texBatches, &terrainGround, &terrainLayers);  // research 102''',
      '''        if (world) appendStockSuperTerrain(zone, zoneDir, tools, *texBatches, &terrainGround, &terrainLayers, !streamZones->empty());  // research 102''')
# the boxes of the loaded tiles go to the camera with the extras
patch(M, '''    std::vector<app::ReferenceTexBatch> all;  // for the diagnosis below only''', '''    {
        // The loaded tiles' areas: the terrain's hole filler is not drawn there.
        std::vector<std::array<float, 4>> boxes;
        for (const auto& [index, t] : streamTiles_)
            for (const StreamedWorldZone& z : streamZones_)
                if (z.index == index && z.bounds.valid()) {
                    boxes.push_back({float(z.bounds.mins.x), float(z.bounds.mins.y), float(z.bounds.maxs.x), float(z.bounds.maxs.y)});
                    break;
                }
        camera_->setStreamedTileBoxes(std::move(boxes));
    }
    std::vector<app::ReferenceTexBatch> all;  // for the diagnosis below only''')
patch('src/qtapp/camera_view.h', '''    void setReferenceExtraGroups(std::vector<ExtraGroup> groups) {''', '''    // The areas (x0, y0, x1, y1) of the streamed world tiles drawn now: the terrain's hole filler is dropped there.
    void setStreamedTileBoxes(std::vector<std::array<float, 4>> boxes) {
        renderer_.setStreamedTileBoxes(boxes);
        update();
    }
    void setReferenceExtraGroups(std::vector<ExtraGroup> groups) {''')

# ---------------------------------------------------------------- renderer: b9, the discard
H = 'src/viewport/render.h'
R = 'src/viewport/render.cpp'
patch(H, '''    void setTerrainCut(float x, float y, float radius) { terrainCut_[0] = x, terrainCut_[1] = y, terrainCut_[2] = radius; }''',
      '''    void setTerrainCut(float x, float y, float radius) { terrainCut_[0] = x, terrainCut_[1] = y, terrainCut_[2] = radius; }
    // The loaded streamed tiles' areas (x0, y0, x1, y1; at most 256): the terrain's hole filler (vertex alpha 253) is
    // discarded inside them.
    void setStreamedTileBoxes(const std::vector<std::array<float, 4>>& boxes) {
        tileBoxes_ = boxes;
        if (tileBoxes_.size() > 256) tileBoxes_.resize(256);
        tileBoxesDirty_ = true;
    }''')
patch(H, '''    float terrainCut_[3] = {0, 0, 0};''', '''    float terrainCut_[3] = {0, 0, 0};
    std::vector<std::array<float, 4>> tileBoxes_;
    bool tileBoxesDirty_ = true;
    ID3D11Buffer* cbTiles_ = nullptr;  // b9: count, then the boxes''')
patch(R, '''    safeRelease(psPrepass_);''', '''    safeRelease(psPrepass_);
    safeRelease(cbTiles_);''')
patch(R, '''cbuffer Layer : register(b7) { float4 layerP; float4 layerTint; float4 layerUvT; float4 layerUvO; };''',
      '''cbuffer Layer : register(b7) { float4 layerP; float4 layerTint; float4 layerUvT; float4 layerUvO; };
cbuffer Tiles : register(b9) { float4 tileCount; float4 tileBox[256]; };  // the loaded streamed tiles (x0, y0, x1, y1)
// The coarse terrain's hole filler (vertex alpha 253) where a streamed tile is loaded: its geometry fills the hole.
bool tileFillHidden(float4 col, float3 w) {
    if (tileCount.x < 0.5 || abs(col.a - 253.0 / 255.0) > 0.0015 || batchFlags2.z < 0.5) return false;
    const int n = int(tileCount.x);
    for (int k = 0; k < n; ++k)
        if (all(w.xy >= tileBox[k].xy) && all(w.xy <= tileBox[k].zw)) return true;
    return false;
}''')
patch(R, '''void ps_prepass(PSIn i) {
    if (!rawSpecAlpha()) clip(tex0.Sample(samp0, i.uv).a - (game0.x > 0.5 && batchFlags2.z > 0.5 ? 0.5 : 0.3));''', '''void ps_prepass(PSIn i) {
    if (tileFillHidden(i.col, i.w)) discard;
    if (!rawSpecAlpha()) clip(tex0.Sample(samp0, i.uv).a - (game0.x > 0.5 && batchFlags2.z > 0.5 ? 0.5 : 0.3));''')
patch(R, '''    if (gameTerrainCut.w > 0.5 && batchFlags2.z > 0.5 && i.col.a > 0.99 && i.col.a < 0.998 && distance(i.w.xy, gameTerrainCut.xy) < gameTerrainCut.z) discard;
    float layerB = -1.0;''', '''    if (gameTerrainCut.w > 0.5 && batchFlags2.z > 0.5 && i.col.a > 0.99 && i.col.a < 0.998 && distance(i.w.xy, gameTerrainCut.xy) < gameTerrainCut.z) discard;
    if (tileFillHidden(i.col, i.w)) discard;
    float layerB = -1.0;''')
# upload and bind b9 at the start of every frame
patch(R, '''    ctx_->OMSetRenderTargets(1, &rtv_, dsv_);
    ctx_->ClearRenderTargetView(rtv_, in.clear);''', '''    ctx_->OMSetRenderTargets(1, &rtv_, dsv_);
    if (!cbTiles_ && dev_) {
        D3D11_BUFFER_DESC bd{};
        bd.ByteWidth = 16 + 256 * 16;
        bd.Usage = D3D11_USAGE_DEFAULT;
        bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        if (SUCCEEDED(dev_->CreateBuffer(&bd, nullptr, &cbTiles_))) tileBoxesDirty_ = true;
    }
    if (cbTiles_) {
        if (tileBoxesDirty_) {
            std::vector<float> tb(4 + 256 * 4, 0.0f);
            tb[0] = float(tileBoxes_.size());
            for (size_t k = 0; k < tileBoxes_.size(); ++k)
                for (int c = 0; c < 4; ++c) tb[4 + k * 4 + size_t(c)] = tileBoxes_[k][size_t(c)];
            ctx_->UpdateSubresource(cbTiles_, 0, nullptr, tb.data(), 0, 0);
            tileBoxesDirty_ = false;
        }
        ctx_->PSSetConstantBuffers(9, 1, &cbTiles_);
    }
    ctx_->ClearRenderTargetView(rtv_, in.clear);''')
print('ok')
