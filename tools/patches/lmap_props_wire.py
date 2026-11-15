import io

# Lightmapped stock props in Game Lighting (research 105 section 3.2, research 132 G7 rest): a placement with model
# flag 2 gets the light record -4, its lightmap page and rectangle (offset u, v, scale u, v); its vertices carry the
# model's lightmap coordinates (ACTS _LMAPUV) as two unorm16; vs_tex_inst writes the world's lightmap encoding (page +
# u, v) into lm, and the prop pass marks the stock lightmap as bound, so the stock world shader's lightmap branch
# lights them as the game does. The model cache fetches again the drawn lightmapped models cached before _LMAPUV.
# IW8R_NO_PROP_LIGHTMAP: the probes' fallback as before (A/B).

ROOT = 'E:/IW8-Radiant/'


def patch(p, a, b, count=1):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == count, (p, s.count(a), a[:70])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


# ---- model cache
MH = 'src/qtapp/model_cache.h'
patch(MH, '''        int selfVis = -1;
    };''', '''        int selfVis = -1;
        // The GLB's lightmap coordinates (_LMAPUV, ACTS fork 834568b9) of a lightmapped model: 1 present, 0 checked and
        // absent, -1 not known.
        int lmapUv = -1;
    };''')
patch(MH, '''    // Runs the geometry export again for cached models whose GLB predates the self visibility (Entry::selfVis -1),''',
      '''    // Runs the geometry export again for cached models whose GLB predates the self visibility (Entry::selfVis -1) or,
    // for a lightmapped model (a tml / tmlc material), its lightmap coordinates (Entry::lmapUv -1),''')
MC = 'src/qtapp/model_cache.cpp'
patch(MC, '''        e.selfVis = o.contains("selfvis") ? o.value("selfvis").toInt() : -1;''', '''        e.selfVis = o.contains("selfvis") ? o.value("selfvis").toInt() : -1;
        e.lmapUv = o.contains("lmapuv") ? o.value("lmapuv").toInt() : -1;''')
patch(MC, '''        if (e.selfVis >= 0) o.insert("selfvis", e.selfVis);''', '''        if (e.selfVis >= 0) o.insert("selfvis", e.selfVis);
        if (e.lmapUv >= 0) o.insert("lmapuv", e.lmapUv);''')
patch(MC, '''// Whether a cached GLB's JSON chunk names the _SELFVIS attribute (the ACTS fork's self visibility).
int glbSelfVis(const QString& path) {''', '''// Whether a cached GLB's JSON chunk names an attribute (the ACTS fork's "_SELFVIS", "_LMAPUV").
int glbHas(const QString& path, const char* attribute) {''')
patch(MC, '''    return f.read(qint64(len)).contains("\\"_SELFVIS\\"") ? 1 : 0;
}''', '''    return f.read(qint64(len)).contains(QByteArray("\\"") + attribute + "\\"") ? 1 : 0;
}
int glbSelfVis(const QString& path) { return glbHas(path, "_SELFVIS"); }
int glbLmapUv(const QString& path) { return glbHas(path, "_LMAPUV"); }
// A lightmapped static model (research 105 section 3.1): one of its surfaces has a tml / tmlc material.
bool lightmappedModel(const QStringList& materials) {
    for (const QString& m : materials)
        if (m.section('/', 0, 0).startsWith("tml")) return true;
    return false;
}''')
patch(MC, '''        if (it == index_.end() || it->second.failed || it->second.selfVis >= 0 || queued_.count(n) || !refreshed_.insert(n).second) continue;''',
      '''        if (it == index_.end() || it->second.failed || queued_.count(n)) continue;
        const bool lmapUnknown = it->second.lmapUv < 0 && lightmappedModel(it->second.materials);
        if ((it->second.selfVis >= 0 && !lmapUnknown) || !refreshed_.insert(n).second) continue;''')
patch(MC, '''            if (const auto it = index_.find(w.name); it != index_.end()) it->second.selfVis = glbSelfVis(it->second.glbPath);''',
      '''            if (const auto it = index_.find(w.name); it != index_.end())
                it->second.selfVis = glbSelfVis(it->second.glbPath), it->second.lmapUv = glbLmapUv(it->second.glbPath);''')
patch(MC, '''            e.selfVis = glbSelfVis(w.glb);''', '''            e.selfVis = glbSelfVis(w.glb);
            e.lmapUv = glbLmapUv(w.glb);''')

# ---- prop vertex, layout, shader
RH = 'src/viewport/render.h'
patch(RH, '''    uint32_t selfVis = 0;
};''', '''    uint32_t selfVis = 0;
    // A lightmapped model's lightmap u, v (two unorm16, research 105 section 3.2) within its placement's rectangle.
    uint32_t lmapUv = 0;
};''')
R = 'src/viewport/render.cpp'
patch(R, '''struct VSInInst { float3 pos : POSITION; float2 uv : TEXCOORD0; float4 col : COLOR; float3 n : NORMAL; float4 vcol : COLOR1; float4 sv : SELFVIS; float4 r0 : INSTROW0; float4 r1 : INSTROW1; float4 r2 : INSTROW2;''',
      '''struct VSInInst { float3 pos : POSITION; float2 uv : TEXCOORD0; float4 col : COLOR; float3 n : NORMAL; float4 vcol : COLOR1; float4 sv : SELFVIS; float2 lmuv : LMAPUV; float4 r0 : INSTROW0; float4 r1 : INSTROW1; float4 r2 : INSTROW2;''')
patch(R, '''    if (i.l0.x < -2.5) {
        // Per vertex from the box in the atlas''', '''    if (i.l0.x < -3.5) {
        // A lightmapped prop (research 105 section 3.2): the world lightmap's encoding, page + u (the PS splits it),
        // of offset + the model's lightmap uv x scale; the stock lightmap branch of the PS lights it.
        o.lm = float2(i.l0.y + min(i.l0.z + i.lmuv.x * i.l1.x, 0.99999), i.l0.w + i.lmuv.y * i.l1.y);
        o.lgvBack = float3(0, 0, 0);
    } else if (i.l0.x < -2.5) {
        // Per vertex from the box in the atlas''')
patch(R, '''                // PropVertex (36 bytes): position, uv, colour, normal as signed bytes, the model's vertex colour, the self
                // visibility as signed bytes.''', '''                // PropVertex (40 bytes): position, uv, colour, normal as signed bytes, the model's vertex colour, the self
                // visibility as signed bytes, the lightmap uv as unorm16.''')
patch(R, '''                    {"SELFVIS", 0, DXGI_FORMAT_R8G8B8A8_SNORM, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0},''',
      '''                    {"SELFVIS", 0, DXGI_FORMAT_R8G8B8A8_SNORM, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0},
                    {"LMAPUV", 0, DXGI_FORMAT_R16G16_UNORM, 0, 36, D3D11_INPUT_PER_VERTEX_DATA, 0},''')
# the prop passes: the stock lightmap is bound for lightmapped props (lm >= 0 only on those)
patch(R, '''        const float flags[8] = {propNormals ? 1.0f : 0.0f, soft ? -1.0f : 0.0f, 0, 0, 0, 0, game_.skyIsStock ? 1.0f : 0.0f, 0};''',
      '''        const float flags[8] = {propNormals ? 1.0f : 0.0f, soft ? -1.0f : 0.0f, 0, 0, 0, 0, game_.skyIsStock ? 1.0f : 0.0f,
                                stockLmSrv_[0] && game_.skyIsStock ? 1.0f : 0.0f};''', count=2)

# ---- MainWindow: mesh conversion, light key, record
MWH = 'src/qtapp/main_window.h'
patch(MWH, '''    struct PropLightKey {
        uint32_t lightingIndex = 0xFFFFFFFF;
        uint16_t transient = 0;
        bool probe = false, first = false;
    };''', '''    struct PropLightKey {
        uint32_t lightingIndex = 0xFFFFFFFF;
        uint16_t transient = 0;
        bool probe = false, first = false;
        int lmapPage = -1;                       // a lightmapped placement's page and rectangle (StockModelInstance)
        std::array<float, 4> lmapRect{0, 0, 0, 0};
    };''')
MW = 'src/qtapp/main_window.cpp'
patch(MW, '''        PropLightKey key;
        key.lightingIndex = in.lightingIndex;
        key.transient = in.worldTransient;
        key.probe = (in.modelFlags & 1u) != 0;''', '''        PropLightKey key;
        key.lightingIndex = in.lightingIndex;
        key.transient = in.worldTransient;
        key.probe = (in.modelFlags & 1u) != 0;
        key.lmapPage = in.lmapPage;
        key.lmapRect = in.lmapRect;''')
patch(MW, '''    static const bool noPropLgv = std::getenv("IW8R_NO_PROP_LGV") != nullptr;  // A/B: the probes' light as before
    if (noPropLgv || !k.probe) return false;''', '''    static const bool noPropLgv = std::getenv("IW8R_NO_PROP_LGV") != nullptr;  // A/B: the probes' light as before
    static const bool noPropLm = std::getenv("IW8R_NO_PROP_LIGHTMAP") != nullptr;
    if (!noPropLm && k.lmapPage >= 0 && camera_ && camera_->hasStockLightmap()) {
        // A lightmapped prop (research 105 section 3.2): -4, its lightmap page, offset u, v, scale u, v.
        out[0] = -4.0f;
        out[1] = float(k.lmapPage);
        out[2] = k.lmapRect[0], out[3] = k.lmapRect[1], out[4] = k.lmapRect[2], out[5] = k.lmapRect[3];
        return true;
    }
    if (noPropLgv || !k.probe) return false;''')
patch(MW, '''                        // The self visibility of a surface flagged for it (0x40; research 105 section 4).
                        static const bool noSelfVis = std::getenv("IW8R_NO_SELFVIS") != nullptr;''',
      '''                        // The self visibility of a surface flagged for it (0x40; research 105 section 4).
                        static const bool noSelfVis = std::getenv("IW8R_NO_SELFVIS") != nullptr;
                        const bool lmapUv = ms.lmapUv.size() == nv;  // a lightmapped model's lightmap coordinates''')
patch(MW, '''                            if (selfVis) {
                                const auto& s = ms.selfVis[v];''', '''                            if (lmapUv) {
                                const auto u16 = [](float c) { return uint32_t(std::clamp(c, 0.0f, 1.0f) * 65535.0f + 0.5f); };
                                pv.lmapUv = u16(ms.lmapUv[v][0]) | (u16(ms.lmapUv[v][1]) << 16);
                            }
                            if (selfVis) {
                                const auto& s = ms.selfVis[v];''')
patch(MW, '''                if (const auto e = modelCacheStock_->lookup(qs(in.model)); !noSelfVis && e && e->selfVis < 0) {''',
      '''                if (const auto e = modelCacheStock_->lookup(qs(in.model));
                    !noSelfVis && e && (e->selfVis < 0 || (e->lmapUv < 0 && (in.modelFlags & 2u)))) {''')
print('ok')
