import io


def patch(p, a, b, n=1):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


R = 'src/viewport/render.cpp'
patch(R, '''        std::vector<GpuTexVertex> all;
        for (size_t j = 0; j < in.batches->size(); ++j) {
            const auto& b = (*in.batches)[j];
            g.first.push_back(UINT(all.size()));
            g.count.push_back(UINT(b.size()));
            all.insert(all.end(), b.begin(), b.end());
            iw8r::AABB bb;
            for (const GpuTexVertex& v : b) bb.add(iw8r::Vec3{v.x, v.y, v.z});
            g.bounds.push_back(bb);
            g.layer.push_back(in.world && j < in.layers.size() ? in.layers[j] : 0);
        }
        if (!all.empty() && dev_) {
            D3D11_BUFFER_DESC bd{};
            bd.ByteWidth = UINT(all.size() * sizeof(GpuTexVertex));
            bd.Usage = D3D11_USAGE_IMMUTABLE;
            bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
            D3D11_SUBRESOURCE_DATA init{all.data(), 0, 0};
            if (FAILED(dev_->CreateBuffer(&bd, &init, &g.vb))) g.vb = nullptr;
        }''', '''        // Each batch goes straight into one buffer (no CPU copy of the whole group: Verdansk's terrain layer group
        // briefly needed two to three times its size while a concatenated vector grew, std::bad_alloc 2026-11-13).
        size_t total = 0;
        for (size_t j = 0; j < in.batches->size(); ++j) {
            const auto& b = (*in.batches)[j];
            g.first.push_back(UINT(total));
            g.count.push_back(UINT(b.size()));
            total += b.size();
            iw8r::AABB bb;
            for (const GpuTexVertex& v : b) bb.add(iw8r::Vec3{v.x, v.y, v.z});
            g.bounds.push_back(bb);
            g.layer.push_back(in.world && j < in.layers.size() ? in.layers[j] : 0);
        }
        if (total > 0 && dev_ && total * sizeof(GpuTexVertex) < 0xFFFFFFFFull) {
            D3D11_BUFFER_DESC bd{};
            bd.ByteWidth = UINT(total * sizeof(GpuTexVertex));
            bd.Usage = D3D11_USAGE_DEFAULT;
            bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
            if (FAILED(dev_->CreateBuffer(&bd, nullptr, &g.vb))) {
                g.vb = nullptr;
            } else {
                for (size_t j = 0; j < in.batches->size(); ++j) {
                    const auto& b = (*in.batches)[j];
                    if (b.empty()) continue;
                    const D3D11_BOX box{UINT(g.first[j] * sizeof(GpuTexVertex)), 0, 0, UINT((g.first[j] + b.size()) * sizeof(GpuTexVertex)), 1, 1};
                    ctx_->UpdateSubresource(g.vb, 0, &box, b.data(), 0, 0);
                }
            }
        }''')
print('ok')
