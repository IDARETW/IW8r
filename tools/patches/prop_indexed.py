import io


def patch(p, a, b, n=1):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


# ---------------------------------------------------------------- render.h
RH = 'src/viewport/render.h'
patch(RH, '''struct InstancedTexMesh {
    std::vector<GpuTexVertex> tris;
    // The same triangles held by the caller (a stock prop's converted mesh): used instead of `tris` when set, so a
    // prop rebuild does not copy every model's vertices again (Verdansk: about a gigabyte per copy, 2026-11-13).
    std::shared_ptr<const std::vector<GpuTexVertex>> shared;
    const std::vector<GpuTexVertex>& verts() const { return shared ? *shared : tris; }
    std::vector<float> rows;  // 12 floats per instance''', '''// A textured instanced prop's mesh (model space): indexed, 28 bytes a vertex. The props used to be unindexed
// GpuTexVertex lists (168 bytes a triangle); Verdansk's filled the video memory and, with the kept copies, the commit
// limit of a 16 GB machine (2026-11-13).
struct PropVertex {
    float x, y, z;
    float u, v;
    uint32_t rgba;
    uint32_t normal;  // signed bytes x, y, z (w 0); zero = none (the pixel shader derives one)
};
struct PropMeshData {
    std::vector<PropVertex> vertices;
    std::vector<uint32_t> indices;  // triangles
};
inline uint32_t packPropNormal(float x, float y, float z) {
    const auto b = [](float c) {
        const float s = c < -1.f ? -1.f : c > 1.f ? 1.f : c;
        return uint32_t(uint8_t(int8_t(s >= 0 ? int(s * 127.f + 0.5f) : int(s * 127.f - 0.5f))));
    };
    return b(x) | (b(y) << 8) | (b(z) << 16);
}

struct InstancedTexMesh {
    // Shared with the caller (a stock prop's converted mesh, kept between rebuilds): no copy per rebuild.
    std::shared_ptr<const PropMeshData> mesh;
    std::vector<float> rows;  // 12 floats per instance''')
patch(RH, '''    struct GpuTexInstanced {
        ID3D11Buffer* vb = nullptr;
        ID3D11Buffer* inst = nullptr;
        unsigned vertices = 0, instances = 0;''', '''    struct GpuTexInstanced {
        ID3D11Buffer* vb = nullptr;
        ID3D11Buffer* ib = nullptr;  // PropMeshData::indices (16-bit when the vertices allow)
        DXGI_FORMAT ibFormat = DXGI_FORMAT_R32_UINT;
        ID3D11Buffer* inst = nullptr;
        unsigned vertices = 0, instances = 0;  // vertices: the index count (vertices drawn per instance)''')

# ---------------------------------------------------------------- render.cpp
R = 'src/viewport/render.cpp'
patch(R, '''struct VSInInst { float3 pos : POSITION; float2 uv : TEXCOORD0; float4 col : COLOR; float3 n : NORMAL; float3 t : TANGENT; float2 lm : TEXCOORD4; float4 r0 : INSTROW0; float4 r1 : INSTROW1; float4 r2 : INSTROW2;''',
      '''struct VSInInst { float3 pos : POSITION; float2 uv : TEXCOORD0; float4 col : COLOR; float3 n : NORMAL; float4 r0 : INSTROW0; float4 r1 : INSTROW1; float4 r2 : INSTROW2;''')
patch(R, '''                const D3D11_INPUT_ELEMENT_DESC tiil[] = {
                    {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
                    {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
                    {"COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 20, D3D11_INPUT_PER_VERTEX_DATA, 0},
                    {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
                    {"TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 36, D3D11_INPUT_PER_VERTEX_DATA, 0},
                    {"TEXCOORD", 4, DXGI_FORMAT_R32G32_FLOAT, 0, 48, D3D11_INPUT_PER_VERTEX_DATA, 0},
                    {"INSTROW", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 0, D3D11_INPUT_PER_INSTANCE_DATA, 1},''', '''                // PropVertex (28 bytes): position, uv, colour, normal as signed bytes.
                const D3D11_INPUT_ELEMENT_DESC tiil[] = {
                    {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
                    {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
                    {"COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 20, D3D11_INPUT_PER_VERTEX_DATA, 0},
                    {"NORMAL", 0, DXGI_FORMAT_R8G8B8A8_SNORM, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
                    {"INSTROW", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 0, D3D11_INPUT_PER_INSTANCE_DATA, 1},''')
patch(R, '''                dev_->CreateInputLayout(tiil, 12, tivs->GetBufferPointer(), tivs->GetBufferSize(), &layoutTexInst_);''',
      '''                dev_->CreateInputLayout(tiil, UINT(std::size(tiil)), tivs->GetBufferPointer(), tivs->GetBufferSize(), &layoutTexInst_);''')
patch(R, '''for (auto& g : refTexInst_) safeRelease(g.vb), safeRelease(g.inst), safeRelease(g.visInst), safeRelease(g.bigInst);''',
      '''for (auto& g : refTexInst_) safeRelease(g.vb), safeRelease(g.ib), safeRelease(g.inst), safeRelease(g.visInst), safeRelease(g.bigInst);''', 2)
patch(R, '''        GpuTexInstanced g;  // kept even when empty: the index is the caller's mesh index (textures are set by it)
        const std::vector<GpuTexVertex>& tris = m.verts();
        if (!tris.empty() && count > 0) {
            D3D11_BUFFER_DESC bd{};
            bd.Usage = D3D11_USAGE_IMMUTABLE;
            bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
            bd.ByteWidth = UINT(tris.size() * sizeof(GpuTexVertex));
            D3D11_SUBRESOURCE_DATA init{tris.data(), 0, 0};
            if (SUCCEEDED(dev_->CreateBuffer(&bd, &init, &g.vb))) {''', '''        GpuTexInstanced g;  // kept even when empty: the index is the caller's mesh index (textures are set by it)
        const PropMeshData* pm = m.mesh.get();
        if (pm && !pm->vertices.empty() && pm->indices.size() >= 3 && count > 0) {
            D3D11_BUFFER_DESC bd{};
            bd.Usage = D3D11_USAGE_IMMUTABLE;
            bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
            if (pm->vertices.size() <= 65536) {
                const std::vector<uint16_t> i16(pm->indices.begin(), pm->indices.end());
                bd.ByteWidth = UINT(i16.size() * sizeof(uint16_t));
                D3D11_SUBRESOURCE_DATA ii{i16.data(), 0, 0};
                if (FAILED(dev_->CreateBuffer(&bd, &ii, &g.ib))) g.ib = nullptr;
                g.ibFormat = DXGI_FORMAT_R16_UINT;
            } else {
                bd.ByteWidth = UINT(pm->indices.size() * sizeof(uint32_t));
                D3D11_SUBRESOURCE_DATA ii{pm->indices.data(), 0, 0};
                if (FAILED(dev_->CreateBuffer(&bd, &ii, &g.ib))) g.ib = nullptr;
                g.ibFormat = DXGI_FORMAT_R32_UINT;
            }
            bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
            bd.ByteWidth = UINT(pm->vertices.size() * sizeof(PropVertex));
            D3D11_SUBRESOURCE_DATA init{pm->vertices.data(), 0, 0};
            if (g.ib && SUCCEEDED(dev_->CreateBuffer(&bd, &init, &g.vb))) {''')
patch(R, '''                    g.vertices = unsigned(tris.size());
                    g.instances = unsigned(count);''', '''                    g.vertices = unsigned(pm->indices.size());
                    g.instances = unsigned(count);''')
patch(R, '''                    for (const GpuTexVertex& v : tris) {
                        const float p[3] = {v.x, v.y, v.z};''', '''                    for (const PropVertex& v : pm->vertices) {
                        const float p[3] = {v.x, v.y, v.z};''')
patch(R, '''                } else {
                    safeRelease(g.vb);
                }
            }
        }
        refTexInst_.push_back(g);''', '''                } else {
                    safeRelease(g.vb);
                }
            }
            if (!g.vb) safeRelease(g.ib);
        }
        refTexInst_.push_back(g);''')
# the two draw loops: stride, index buffer, indexed draw
patch(R, '''    const UINT strides[2] = {UINT(sizeof(GpuTexVertex)), UINT(24 * sizeof(float))}, offsets[2] = {0, 0};''',
      '''    const UINT strides[2] = {UINT(sizeof(PropVertex)), UINT(24 * sizeof(float))}, offsets[2] = {0, 0};''', 2)
patch(R, '''        ID3D11Buffer* bufs[2] = {g.vb, prepassBig ? g.bigInst : culled ? g.visInst : g.inst};
        ctx_->IASetVertexBuffers(0, 2, bufs, strides, offsets);
        drawInst(g.vertices, prepassBig ? g.big : culled ? g.visible : g.instances, 0, 0);''', '''        ID3D11Buffer* bufs[2] = {g.vb, prepassBig ? g.bigInst : culled ? g.visInst : g.inst};
        ctx_->IASetVertexBuffers(0, 2, bufs, strides, offsets);
        ctx_->IASetIndexBuffer(g.ib, g.ibFormat, 0);
        drawInstIdx(g.vertices, prepassBig ? g.big : culled ? g.visible : g.instances);''')
patch(R, '''        ID3D11Buffer* bufs[2] = {g.vb, g.inst};
        ctx_->IASetVertexBuffers(0, 2, bufs, strides, offsets);
        drawInst(g.vertices, g.instances, 0, 0);
    }
    ID3D11Buffer* none[2] = {nullptr, nullptr};
    ctx_->IASetVertexBuffers(0, 2, none, strides, offsets);
    ID3D11ShaderResourceView* noTex[1] = {nullptr};''', '''        ID3D11Buffer* bufs[2] = {g.vb, g.inst};
        ctx_->IASetVertexBuffers(0, 2, bufs, strides, offsets);
        ctx_->IASetIndexBuffer(g.ib, g.ibFormat, 0);
        drawInstIdx(g.vertices, g.instances);
    }
    ID3D11Buffer* none[2] = {nullptr, nullptr};
    ctx_->IASetVertexBuffers(0, 2, none, strides, offsets);
    ctx_->IASetIndexBuffer(nullptr, DXGI_FORMAT_R32_UINT, 0);
    ID3D11ShaderResourceView* noTex[1] = {nullptr};''')
patch(R, '''void SceneRenderer::drawInst(unsigned vertices, unsigned instances, unsigned firstVertex, unsigned firstInstance) {''',
      '''void SceneRenderer::drawInstIdx(unsigned indices, unsigned instances) {
    ctx_->DrawIndexedInstanced(indices, instances, 0, 0, 0);
    if (inShadowPass_) {
        ++stats_.shadowDrawCalls;
        return;
    }
    ++stats_.drawCalls;
    stats_.triangles += size_t(indices / 3) * instances;
}

void SceneRenderer::drawInst(unsigned vertices, unsigned instances, unsigned firstVertex, unsigned firstInstance) {''')
patch(R, '''    for (const auto& g : refTexInst_) props += bufferBytes(g.vb), propInst += bufferBytes(g.inst) + bufferBytes(g.visInst) + bufferBytes(g.bigInst);''',
      '''    for (const auto& g : refTexInst_)
        props += bufferBytes(g.vb) + bufferBytes(g.ib), propInst += bufferBytes(g.inst) + bufferBytes(g.visInst) + bufferBytes(g.bigInst);''')
# world: without the extra groups' buffers (refTexVb_ lists them per batch too)
patch(R, '''        std::vector<ID3D11Buffer*> seen;
        for (ID3D11Buffer* b : refTexVb_)''', '''        std::vector<ID3D11Buffer*> seen;
        for (const auto& g : extraGroups_) seen.push_back(g.vb);
        for (ID3D11Buffer* b : refTexVb_)''')
RH2 = 'src/viewport/render.h'
patch(RH2, '''    void drawInst(unsigned vertices, unsigned instances, unsigned firstVertex, unsigned firstInstance);''',
      '''    void drawInst(unsigned vertices, unsigned instances, unsigned firstVertex, unsigned firstInstance);
    void drawInstIdx(unsigned indices, unsigned instances);''')
print('ok')
