import io


def patch(p, a, b, n=1):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


H = 'src/viewport/render.h'
R = 'src/viewport/render.cpp'
patch(H, '''        ID3D11Buffer* visInst = nullptr;
        unsigned visible = 0;''', '''        ID3D11Buffer* visInst = nullptr;
        unsigned visible = 0;
        // The visible placements large on screen (the depth pre-pass draws only these: Aniyah Tac 23.5 -> 16 ms).
        ID3D11Buffer* bigInst = nullptr;
        unsigned big = 0;''')
patch(R, '''    for (auto& g : refTexInst_) safeRelease(g.vb), safeRelease(g.inst);''',
      '''    for (auto& g : refTexInst_) safeRelease(g.vb), safeRelease(g.inst), safeRelease(g.visInst), safeRelease(g.bigInst);''')
patch(R, '''    for (auto& g : refTexInst_) safeRelease(g.vb), safeRelease(g.inst), safeRelease(g.visInst);''',
      '''    for (auto& g : refTexInst_) safeRelease(g.vb), safeRelease(g.inst), safeRelease(g.visInst), safeRelease(g.bigInst);''')
patch(R, '''                    dev_->CreateBuffer(&dd, nullptr, &g.visInst);''', '''                    dev_->CreateBuffer(&dd, nullptr, &g.visInst);
                    dev_->CreateBuffer(&dd, nullptr, &g.bigInst);''')
patch(R, '''    instCulledLast_ = 0;
    std::vector<float> keep;
    for (GpuTexInstanced& g : refTexInst_) {
        if (!g.vb || !g.inst || !g.visInst || g.rows.empty()) continue;
        keep.clear();''', '''    instCulledLast_ = 0;
    std::vector<float> keep, keepBig;
    static const float bigPx = std::getenv("IW8R_PREPASS_PX") ? std::max(0.0f, float(std::atof(std::getenv("IW8R_PREPASS_PX")))) : 24.0f;
    for (GpuTexInstanced& g : refTexInst_) {
        if (!g.vb || !g.inst || !g.visInst || g.rows.empty()) continue;
        keep.clear();
        keepBig.clear();''')
patch(R, '''            if (out) {
                ++instCulledLast_;
                continue;
            }
            keep.insert(keep.end(), r, r + 12);
        }
        g.visible = unsigned(keep.size() / 12);''', '''            if (out) {
                ++instCulledLast_;
                continue;
            }
            keep.insert(keep.end(), r, r + 12);
            {
                const float dx = c[0] - eye[0], dy = c[1] - eye[1], dz = c[2] - eye[2];
                const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
                if (dist <= rad || rad >= dist * pixelAngle * bigPx) keepBig.insert(keepBig.end(), r, r + 12);
            }
        }
        g.big = unsigned(keepBig.size() / 12);
        if (g.big && g.bigInst) {
            D3D11_MAPPED_SUBRESOURCE mb;
            if (SUCCEEDED(ctx_->Map(g.bigInst, 0, D3D11_MAP_WRITE_DISCARD, 0, &mb))) {
                std::memcpy(mb.pData, keepBig.data(), keepBig.size() * sizeof(float));
                ctx_->Unmap(g.bigInst, 0);
            } else {
                g.big = 0;
            }
        }
        g.visible = unsigned(keep.size() / 12);''')
patch(R, '''        const bool culled = instCull_ && g.visInst && g.visible <= g.instances;
        if (culled && g.visible == 0) continue;''', '''        const bool culled = instCull_ && g.visInst && g.visible <= g.instances;
        if (culled && g.visible == 0) continue;
        // The depth pre-pass: only the placements large on screen (the shading pass writes depth for the rest).
        const bool prepassBig = texPsOverride_ && texPsOverride_ == psPrepass_ && culled && g.bigInst;
        if (prepassBig && g.big == 0) continue;''')
patch(R, '''        ID3D11Buffer* bufs[2] = {g.vb, culled ? g.visInst : g.inst};
        ctx_->IASetVertexBuffers(0, 2, bufs, strides, offsets);
        drawInst(g.vertices, culled ? g.visible : g.instances, 0, 0);
    }
    if (instCb && cbLayer_)''', '''        ID3D11Buffer* bufs[2] = {g.vb, prepassBig ? g.bigInst : culled ? g.visInst : g.inst};
        ctx_->IASetVertexBuffers(0, 2, bufs, strides, offsets);
        drawInst(g.vertices, prepassBig ? g.big : culled ? g.visible : g.instances, 0, 0);
    }
    if (instCb && cbLayer_)''')
# the experiment switch goes; the shading pass always writes depth (the pre-pass leaves small props out)
patch(R, '''        static const bool prepassNoProps = std::getenv("IW8R_PREPASS_NO_PROPS") != nullptr;  // experiment
        if (refVisible_ && !refTexInst_.empty() && !prepassNoProps) {''', '''        if (refVisible_ && !refTexInst_.empty()) {''')
patch(R, '''    ID3D11DepthStencilState* texOpaqueDs = prepass && !std::getenv("IW8R_PREPASS_NO_PROPS") ? dsTest_ : dsWrite_;''',
      '''    // After the pre-pass the opaque geometry still writes depth (LESS_EQUAL: the pre-pass depths pass): the pre-pass
    // leaves out the props small on screen.
    ID3D11DepthStencilState* texOpaqueDs = dsWrite_;''')
print('ok')
