import io


def patch(p, a, b, n=1):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


R = 'src/viewport/render.cpp'
# cbuffer: one more float4
patch(R, '''cbuffer Game : register(b4) { float4 game0; float4 gameSun; float4 gameSunCol; float4 gameSky; float4 gameFog[6]; float4 gameSkyScale; };''',
      '''cbuffer Game : register(b4) { float4 game0; float4 gameSun; float4 gameSunCol; float4 gameSky; float4 gameFog[6]; float4 gameSkyScale; float4 gameTerrainCut; };''')
patch(R, '''            bd.ByteWidth = 176;  // game0, sun toward, sun colour, unbaked light, fog[6], sky scale''',
      '''            bd.ByteWidth = 192;  // game0, sun toward, sun colour, unbaked light, fog[6], sky scale, terrain cut''')
patch(R, '''    float g[44] = {''', '''    float g[48] = {''')
patch(R, '''    g[40] = game_.skyScale;
    g[41] = game_.bakedScale;''', '''    g[40] = game_.skyScale;
    g[41] = game_.bakedScale;
    // The coarse super terrain is cut away inside the near-camera layer patch (x, y, radius, on).
    g[44] = terrainCut_[0], g[45] = terrainCut_[1], g[46] = terrainCut_[2], g[47] = terrainCut_[2] > 0 ? 1.0f : 0.0f;''')
# the discard: coarse terrain vertices carry alpha 254
patch(R, '''float4 ps_tex(PSIn i) : SV_TARGET {
    float layerB = -1.0;''', '''float4 ps_tex(PSIn i) : SV_TARGET {
    // The coarse super terrain (vertex alpha 254) inside the near-camera layer patch: the patch draws the ground there.
    if (gameTerrainCut.w > 0.5 && i.col.a > 0.99 && i.col.a < 0.998 && distance(i.w.xy, gameTerrainCut.xy) < gameTerrainCut.z) discard;
    float layerB = -1.0;''')
# the patch's base layer writes depth (params[14] = 1)
patch(R, '''                {
                    ID3D11BlendState* want = L.params[3] > 0.5f && blendMul_ ? blendMul_ : blend_;''', '''                {
                    // The terrain patch's base layer stands in for the cut-away coarse terrain: it writes depth.
                    ID3D11DepthStencilState* wantDs = L.params[14] > 0.5f ? dsWrite_ : dsTest_;
                    if (wantDs != lastDs) ctx_->OMSetDepthStencilState(wantDs, 0), lastDs = wantDs;
                    ID3D11BlendState* want = L.params[3] > 0.5f && blendMul_ ? blendMul_ : blend_;''')
patch(R, '''            ID3D11BlendState* lastBlend = nullptr;''', '''            ID3D11BlendState* lastBlend = nullptr;
            ID3D11DepthStencilState* lastDs = dsTest_;''')
patch('src/viewport/render.h', '''    void setReferenceLayer(size_t batch, int revealTexture, const float params[16]);''',
      '''    void setReferenceLayer(size_t batch, int revealTexture, const float params[16]);
    // The near-camera terrain layer patch's core (x, y, radius; radius 0 = none): the coarse terrain is not drawn there.
    void setTerrainCut(float x, float y, float radius) { terrainCut_[0] = x, terrainCut_[1] = y, terrainCut_[2] = radius; }''')
patch('src/viewport/render.h', '''    std::vector<size_t> refDecalOrder_;  // decal batches by sortKey, layer, batch''',
      '''    std::vector<size_t> refDecalOrder_;  // decal batches by sortKey, layer, batch
    float terrainCut_[3] = {0, 0, 0};''')

# scene / camera: base flag
patch('src/viewport/scene.h', '''    uint32_t terrainFlags = 0;  // the layer shader's flags: 16 reveal ramp, 8 alpha (research 97 / 103)
    float terrainK = 0;''', '''    uint32_t terrainFlags = 0;  // the layer shader's flags: 16 reveal ramp, 8 alpha (research 97 / 103)
    float terrainK = 0;
    bool terrainBase = false;  // the patch's base layer: writes depth (it replaces the coarse terrain cut away there)''')
patch('src/qtapp/camera_view.h', '''        bool terrainLayer = false;
        uint32_t flags = 0;''', '''        bool terrainLayer = false, terrainBase = false;
        uint32_t flags = 0;''')
patch('src/qtapp/camera_view.cpp', '''            dv.terrainLayer = b.terrainLayer, dv.flags = b.terrainFlags, dv.k = b.terrainK;''',
      '''            dv.terrainLayer = b.terrainLayer, dv.flags = b.terrainFlags, dv.k = b.terrainK, dv.terrainBase = b.terrainBase;''')
patch('src/qtapp/camera_view.cpp', '''                        const float p[16] = {1.0f, float(dv.flags), dv.k, 0.0f, 1.0f, 1.0f, 1.0f, 0.0f,
                                             1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f};''',
      '''                        const float p[16] = {1.0f, float(dv.flags), dv.k, 0.0f, 1.0f, 1.0f, 1.0f, 0.0f,
                                             1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, dv.terrainBase ? 1.0f : 0.0f, 0.0f};''')
patch('src/qtapp/camera_view.h', '''    void setReferenceDecalExtras(std::vector<app::ReferenceTexBatch> batches) {''',
      '''    void setTerrainCut(double x, double y, double radius) {
        renderer_.setTerrainCut(float(x), float(y), float(radius));
        update();
    }
    void setReferenceDecalExtras(std::vector<app::ReferenceTexBatch> batches) {''')

# main window: coarse terrain alpha 254; base flag; the cut
M = 'src/qtapp/main_window.cpp'
patch(M, '''            app::GpuTexVertex v{float(p.x), float(p.y), float(p.z), 0, 0, g | (g << 8) | (g << 16) | (0xffu << 24)};
            if (k < tm.albedoUv.size())''', '''            // Alpha 254 marks the coarse super terrain (cut away inside the near-camera layer patch).
            app::GpuTexVertex v{float(p.x), float(p.y), float(p.z), 0, 0, g | (g << 8) | (g << 16) | (0xfeu << 24)};
            if (k < tm.albedoUv.size())''')
patch(M, '''        const auto addLayer = [&](const ImportedMesh& m, const std::function<double(size_t)>& weight, const std::string& color,
                              const std::string& reveal, uint32_t flags, double k) {''', '''        const auto addLayer = [&](const ImportedMesh& m, const std::function<double(size_t)>& weight, const std::string& color,
                              const std::string& reveal, uint32_t flags, double k, bool isBase) {''')
patch(M, '''            rb.terrainK = float(k);''', '''            rb.terrainK = float(k);
            rb.terrainBase = isBase;''')
patch(M, '''        addLayer(patch.base, [](size_t) { return 1.0; }, base.colorImage, std::string(), 0, 0);''',
      '''        // The base at full weight out to the cut (0.75 x radius, where the fade starts), fading beyond it.
        addLayer(patch.base, [](size_t) { return 1.0; }, base.colorImage, std::string(), 0, 0, true);''')
patch(M, '''                 ls.alphaReveal ? ls.revealImage : std::string(), flags, ls.revealK);''',
      '''                 ls.alphaReveal ? ls.revealImage : std::string(), flags, ls.revealK, false);''')
patch(M, '''            self->terrainLayerBatches_ = std::move(*batches);
            self->pushDecalExtras();''', '''            self->terrainLayerBatches_ = std::move(*batches);
            self->camera_->setTerrainCut(c.x, c.y, self->terrainLayerBatches_.empty() ? 0.0 : 0.74 * 6000.0);
            self->pushDecalExtras();''')
patch(M, '''        QMetaObject::invokeMethod(qApp, [self, batches, zone] {''', '''        QMetaObject::invokeMethod(qApp, [self, batches, zone, c] {''')
print('ok')
