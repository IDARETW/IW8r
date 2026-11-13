import io


def patch(p, pairs):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    for a, b in pairs:
        a = a.replace('\n', nl); b = b.replace('\n', nl)
        assert s.count(a) == 1, (p, s.count(a), a[:100])
        s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


patch('src/viewport/render.h', [
 ('''    struct GpuTexInstanced {
        ID3D11Buffer* vb = nullptr;
        ID3D11Buffer* inst = nullptr;
        unsigned vertices = 0, instances = 0;
        int texture = -1;
    };''', '''    struct GpuTexInstanced {
        ID3D11Buffer* vb = nullptr;
        ID3D11Buffer* inst = nullptr;
        unsigned vertices = 0, instances = 0;
        int texture = -1;
        // Screen culling (cullTexInstances): the placements on the CPU, the model's bounding sphere in model space, and
        // a dynamic buffer of the placements that pass (drawn instead of `inst` while culling is on).
        std::vector<float> rows;
        float centre[3] = {0, 0, 0}, radius = 0;
        ID3D11Buffer* visInst = nullptr;
        unsigned visible = 0;
    };
    // Each frame on screen: props outside the view, or smaller than about a pixel there, are left out (Hideout drew 21 M
    // prop triangles a frame at full detail). The sun shadow pass keeps them all.
    void cullTexInstances(const float viewProj[16], const float eye[3], float pixelAngle);
    bool instCull_ = true;
    float pixelAngle_ = 0.0015f;  // setPixelAngle

public:
    // The angle one pixel spans at the frame's centre (instance culling); the camera sets it from its FOV.
    void setPixelAngle(float a) { pixelAngle_ = a > 0 ? a : 0.0015f; }

private:
    size_t instCulledLast_ = 0;'''),
])
patch('src/viewport/render.cpp', [
 ('''                if (SUCCEEDED(dev_->CreateBuffer(&bd, &iinit, &g.inst))) {
                    g.vertices = unsigned(m.tris.size());
                    g.instances = unsigned(count);
                } else {
                    safeRelease(g.vb);
                }
            }
        }
        refTexInst_.push_back(g);''', '''                if (SUCCEEDED(dev_->CreateBuffer(&bd, &iinit, &g.inst))) {
                    g.vertices = unsigned(m.tris.size());
                    g.instances = unsigned(count);
                    g.visible = g.instances;
                    g.rows = m.rows;
                    // The model's bounding sphere (box centre, half diagonal).
                    float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
                    for (const GpuTexVertex& v : m.tris) {
                        const float p[3] = {v.x, v.y, v.z};
                        for (int c = 0; c < 3; ++c) lo[c] = std::min(lo[c], p[c]), hi[c] = std::max(hi[c], p[c]);
                    }
                    float r2 = 0;
                    for (int c = 0; c < 3; ++c) g.centre[c] = 0.5f * (lo[c] + hi[c]), r2 += 0.25f * (hi[c] - lo[c]) * (hi[c] - lo[c]);
                    g.radius = std::sqrt(r2);
                    D3D11_BUFFER_DESC dd = bd;
                    dd.Usage = D3D11_USAGE_DYNAMIC;
                    dd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
                    dev_->CreateBuffer(&dd, nullptr, &g.visInst);
                } else {
                    safeRelease(g.vb);
                }
            }
        }
        refTexInst_.push_back(g);'''),
 ('''bool SceneRenderer::setReferenceTexInstances(const std::vector<InstancedTexMesh>& meshes) {
    for (auto& g : refTexInst_) safeRelease(g.vb), safeRelease(g.inst);''', '''void SceneRenderer::cullTexInstances(const float viewProj[16], const float eye[3], float pixelAngle) {
    const iw8r::Frustum fr = iw8r::frustumFromViewProj(viewProj);
    instCulledLast_ = 0;
    std::vector<float> keep;
    for (GpuTexInstanced& g : refTexInst_) {
        if (!g.vb || !g.inst || !g.visInst || g.rows.empty()) continue;
        keep.clear();
        const size_t n = g.rows.size() / 12;
        for (size_t i = 0; i < n; ++i) {
            const float* r = &g.rows[i * 12];
            const float c[3] = {r[0] * g.centre[0] + r[1] * g.centre[1] + r[2] * g.centre[2] + r[3],
                                r[4] * g.centre[0] + r[5] * g.centre[1] + r[6] * g.centre[2] + r[7],
                                r[8] * g.centre[0] + r[9] * g.centre[1] + r[10] * g.centre[2] + r[11]};
            const float sc = std::sqrt(r[0] * r[0] + r[4] * r[4] + r[8] * r[8]);
            const float rad = g.radius * std::max(sc, 1e-6f);
            bool out = false;
            for (const auto& pl : fr.planes)
                if (pl.n.x * c[0] + pl.n.y * c[1] + pl.n.z * c[2] - pl.d > rad) {
                    out = true;
                    break;
                }
            if (!out) {
                const float dx = c[0] - eye[0], dy = c[1] - eye[1], dz = c[2] - eye[2];
                const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
                out = dist > rad && rad < dist * pixelAngle;  // under about a pixel across
            }
            if (out) {
                ++instCulledLast_;
                continue;
            }
            keep.insert(keep.end(), r, r + 12);
        }
        g.visible = unsigned(keep.size() / 12);
        if (g.visible) {
            D3D11_MAPPED_SUBRESOURCE ms;
            if (SUCCEEDED(ctx_->Map(g.visInst, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) {
                std::memcpy(ms.pData, keep.data(), keep.size() * sizeof(float));
                ctx_->Unmap(g.visInst, 0);
            } else {
                g.visible = g.instances;  // fall back to every placement
            }
        }
    }
}

bool SceneRenderer::setReferenceTexInstances(const std::vector<InstancedTexMesh>& meshes) {
    for (auto& g : refTexInst_) safeRelease(g.vb), safeRelease(g.inst), safeRelease(g.visInst);'''),
 # draw: the visible list on screen
 ('''        ctx_->PSSetShaderResources(0, 1, &textures_[size_t(id)]);
        ID3D11Buffer* bufs[2] = {g.vb, g.inst};
        ctx_->IASetVertexBuffers(0, 2, bufs, strides, offsets);
        drawInst(g.vertices, g.instances, 0, 0);
    }
    ID3D11Buffer* none[2] = {nullptr, nullptr};
    ctx_->IASetVertexBuffers(0, 2, none, strides, offsets);
    ID3D11ShaderResourceView* noTex[1] = {nullptr};
    ctx_->PSSetShaderResources(0, 1, noTex);
    ctx_->IASetInputLayout(layout_);
    ctx_->VSSetShader(vs_, nullptr, 0);
    ctx_->PSSetShader(ps_, nullptr, 0);
}''', '''        const bool culled = instCull_ && g.visInst && g.visible <= g.instances;
        if (culled && g.visible == 0) continue;
        ctx_->PSSetShaderResources(0, 1, &textures_[size_t(id)]);
        ID3D11Buffer* bufs[2] = {g.vb, culled ? g.visInst : g.inst};
        ctx_->IASetVertexBuffers(0, 2, bufs, strides, offsets);
        drawInst(g.vertices, culled ? g.visible : g.instances, 0, 0);
    }
    ID3D11Buffer* none[2] = {nullptr, nullptr};
    ctx_->IASetVertexBuffers(0, 2, none, strides, offsets);
    ID3D11ShaderResourceView* noTex[1] = {nullptr};
    ctx_->PSSetShaderResources(0, 1, noTex);
    ctx_->IASetInputLayout(layout_);
    ctx_->VSSetShader(vs_, nullptr, 0);
    ctx_->PSSetShader(ps_, nullptr, 0);
}'''),
 # cull once per frame, after the frustum is known
 ('''    const iw8r::Frustum frustum = iw8r::frustumFromViewProj(viewProj);''', '''    const iw8r::Frustum frustum = iw8r::frustumFromViewProj(viewProj);
    {
        static const bool noCull = perfOff("instcull");  // benchmark switch
        instCull_ = !noCull && !in.capture && !in.clean;
        if (instCull_ && !refTexInst_.empty()) {
            // One pixel of the frame: the projection's x scale (viewProj is row-vector; column 0 of the projection part
            // is not separable here, so the half-width tangent comes from the frame size and a 90-degree fallback).
            const float eyeP[3] = {fog_[0], fog_[1], fog_[2]};  // setFog carries the eye
            cullTexInstances(viewProj, eyeP, pixelAngle_);
        }
    }'''),
])
print('ok')
