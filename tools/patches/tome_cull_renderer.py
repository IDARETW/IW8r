import io

# Renderer: "cull by tome" (plan M2.2). The stock reference draws only what the Umbra tome's query made visible:
# world surfaces in the vertex shader (a bit per GfxWorld surface, the vertices carry their surface), props in the
# per-frame placement cull (a bit per static model collection). Sun shadows still come from everything, as in game.


def patch(p, a, b, n=1):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


H = 'E:/IW8-Radiant/src/viewport/render.h'
patch(H, '''    int lodGroup = -1;
    float lodNear = 0, lodFar = 0;
};

struct TexturedBatch {''', '''    int lodGroup = -1;
    float lodNear = 0, lodFar = 0;
    // Per instance, the placement's static model collection in the stock map (the Umbra tome's class 1 user ID), or
    // 0xFFFFFFFF for one the tome does not know (clutter, scripted objects). Empty: none known.
    std::vector<uint32_t> ids;
};

struct TexturedBatch {''')
patch(H, '''        std::vector<float> rows;
        std::vector<float> light;  // InstancedTexMesh::light (12 per instance)
        float centre[3] = {0, 0, 0}, radius = 0;''', '''        std::vector<float> rows;
        std::vector<float> light;  // InstancedTexMesh::light (12 per instance)
        std::vector<uint32_t> ids; // InstancedTexMesh::ids in GPU order (empty: none)
        float centre[3] = {0, 0, 0}, radius = 0;''')
patch(H, '''    bool setReferenceTexInstances(const std::vector<InstancedTexMesh>& meshes);
''', '''    bool setReferenceTexInstances(const std::vector<InstancedTexMesh>& meshes);
    // Cull by tome (View > Umbra Visibility): one bit per GfxWorld surface and one per static model collection, set =
    // drawn. The reference world's triangles of a hidden surface and the props of a hidden collection are not drawn on
    // screen (they still cast sun shadows); what the tome does not know is drawn. Empty vectors turn it off.
    void setReferenceTomeVisibility(std::vector<uint32_t> surfaceBits, std::vector<uint32_t> modelBits);
    bool referenceTomeCull() const { return !tomeSurfBits_.empty() || !tomeModelBits_.empty(); }
    size_t tomeCulledProps() const { return tomeCulledProps_; }  // placements the tome hid in the last frame
''')
patch(H, '''    float wind_[4] = {0, 0, 0, 0};''', '''    float wind_[4] = {0, 0, 0, 0};  // w = 1 while the tome cull is on (the vertex shader reads the surface bits)
    std::vector<uint32_t> tomeSurfBits_, tomeModelBits_;
    bool tomeDirty_ = false;
    ID3D11Buffer* tomeBuf_ = nullptr;
    ID3D11ShaderResourceView* tomeSrv_ = nullptr;
    size_t tomeBufWords_ = 0;
    size_t tomeCulledProps_ = 0;''')

C = 'E:/IW8-Radiant/src/viewport/render.cpp'
patch(C, '''PSIn vs_tex(VSIn i) {
    PSIn o;''', '''Buffer<uint> tomeVis : register(t30);  // cull by tome: one bit per GfxWorld surface (wind.w = 1 while bound)
PSIn vs_tex(VSIn i) {
    PSIn o;''')
patch(C, '''    o.lgvBack = float3(0, 0, 0);
    o.edge = i.edge.xyz;''', '''    o.lgvBack = float3(0, 0, 0);
    o.edge = i.edge.xyz;
    if (wind.w > 0.5 && i.edge.a < 0.5) {
        // A stock reference vertex: its surface + 1 (referenceSurfaceTag). A surface the tome hides goes off screen.
        const uint tag = uint(i.edge.r * 255.0 + 0.5) | (uint(i.edge.g * 255.0 + 0.5) << 8) | (uint(i.edge.b * 255.0 + 0.5) << 16);
        if (tag != 0u) {
            const uint s = tag - 1u;
            if (((tomeVis[s >> 5] >> (s & 31u)) & 1u) == 0u) o.pos = float4(0, 0, 0, -1);
        }
    }''')
patch(C, '''    safeRelease(cbWind_);''', '''    safeRelease(cbWind_);
    safeRelease(tomeSrv_);
    safeRelease(tomeBuf_);
    tomeBufWords_ = 0;
    tomeDirty_ = true;''')
# Upload and bind before the textured passes (both places that set the wind buffer).
patch(C, '''        if (cbWind_) {
            ctx_->UpdateSubresource(cbWind_, 0, nullptr, wind_, 0, 0);
            ctx_->VSSetConstantBuffers(3, 1, &cbWind_);
        }''', '''        bindTomeVisibility();
        if (cbWind_) {
            ctx_->UpdateSubresource(cbWind_, 0, nullptr, wind_, 0, 0);
            ctx_->VSSetConstantBuffers(3, 1, &cbWind_);
        }''', n=2)
patch(H, '''    void setReferenceTomeVisibility(std::vector<uint32_t> surfaceBits, std::vector<uint32_t> modelBits);
''', '''    void setReferenceTomeVisibility(std::vector<uint32_t> surfaceBits, std::vector<uint32_t> modelBits);
    void bindTomeVisibility();  // (internal) the surface bits for the vertex shader, uploaded when they changed
''')
patch(C, '''void SceneRenderer::cullTexInstances(const float viewProj[16], const float eye[3], float pixelAngle) {''', '''void SceneRenderer::setReferenceTomeVisibility(std::vector<uint32_t> surfaceBits, std::vector<uint32_t> modelBits) {
    tomeSurfBits_ = std::move(surfaceBits);
    tomeModelBits_ = std::move(modelBits);
    tomeDirty_ = true;
}

void SceneRenderer::bindTomeVisibility() {
    wind_[3] = 0.0f;
    if (!dev_ || tomeSurfBits_.empty()) return;
    if (!tomeBuf_ || tomeBufWords_ < tomeSurfBits_.size()) {
        safeRelease(tomeSrv_);
        safeRelease(tomeBuf_);
        tomeBufWords_ = std::max<size_t>(tomeSurfBits_.size(), 64);
        D3D11_BUFFER_DESC bd{};
        bd.ByteWidth = UINT(tomeBufWords_ * 4);
        bd.Usage = D3D11_USAGE_DEFAULT;
        bd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        if (FAILED(dev_->CreateBuffer(&bd, nullptr, &tomeBuf_))) {
            tomeBufWords_ = 0;
            return;
        }
        D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
        sd.Format = DXGI_FORMAT_R32_UINT;
        sd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        sd.Buffer.FirstElement = 0;
        sd.Buffer.NumElements = UINT(tomeBufWords_);
        if (FAILED(dev_->CreateShaderResourceView(tomeBuf_, &sd, &tomeSrv_))) {
            safeRelease(tomeBuf_);
            tomeBufWords_ = 0;
            return;
        }
        tomeDirty_ = true;
    }
    if (tomeDirty_) {
        std::vector<uint32_t> words(tomeBufWords_, 0xFFFFFFFFu);  // past the tome's surfaces: drawn
        std::copy(tomeSurfBits_.begin(), tomeSurfBits_.end(), words.begin());
        ctx_->UpdateSubresource(tomeBuf_, 0, nullptr, words.data(), 0, 0);
        tomeDirty_ = false;
    }
    ctx_->VSSetShaderResources(30, 1, &tomeSrv_);
    wind_[3] = 1.0f;
}

void SceneRenderer::cullTexInstances(const float viewProj[16], const float eye[3], float pixelAngle) {
    // Cull by tome: a placement whose collection the tome hides (ids past the bits, or unknown, are drawn).
    tomeCulledProps_ = 0;
    const auto tomeHides = [&](const GpuTexInstanced& m, size_t i) {
        if (tomeModelBits_.empty() || i >= m.ids.size()) return false;
        const uint32_t id = m.ids[i];
        if (id == 0xFFFFFFFFu || (id >> 5) >= tomeModelBits_.size()) return false;
        return ((tomeModelBits_[id >> 5] >> (id & 31u)) & 1u) == 0u;
    };''')
# LOD groups: the lead mesh's placements.
patch(C, '''            PlaceEval& e = ev[i];
            e.rad = radius * std::max(sc, 1e-6f);
            e.out = false;''', '''            PlaceEval& e = ev[i];
            e.rad = radius * std::max(sc, 1e-6f);
            e.out = tomeHides(lead, i);
            if (e.out) ++tomeCulledProps_;''')
patch(C, '''            const float rad = g.radius * std::max(sc, 1e-6f);
            bool out = false;
            for (const auto& pl : fr.planes)''', '''            const float rad = g.radius * std::max(sc, 1e-6f);
            bool out = tomeHides(g, i);
            if (out) ++tomeCulledProps_;
            for (const auto& pl : fr.planes)''')
patch(C, '''                const bool haveLight = m.light.size() == count * 12;
                g.rows.resize(count * 12);''', '''                const bool haveLight = m.light.size() == count * 12;
                if (m.ids.size() == count) {
                    g.ids.resize(count);
                    for (size_t i = 0; i < count; ++i) g.ids[i] = m.ids[g.order[i]];
                }
                g.rows.resize(count * 12);''')
print('ok')
