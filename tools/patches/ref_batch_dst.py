import io
p = 'src/viewport/scene.cpp'
s = io.open(p, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'


def rep(a, b):
    global s
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == 1, a[:70]
    s = s.replace(a, b)


rep('''        auto [it, fresh] = byMaterial.emplace(m.material, out.size());
        if (fresh) out.push_back({m.material, {}});
        auto& dst = out[it->second].tris;''', '''        auto [it, fresh] = byMaterial.emplace(m.material, out.size());
        if (fresh) out.push_back({m.material, {}});
        const size_t baseBatch = it->second;  // an index: adding the layer batches below may move `out`''')
rep('''        std::vector<std::vector<GpuTexVertex>*> layerDst;
        if (layered)
            for (int L = 1; L < layers; ++L) {
                auto [lit, lfresh] = byMaterial.emplace(m.material + '\\x01' + std::to_string(L), out.size());
                if (lfresh) {
                    out.push_back({m.material, {}});
                    out.back().layer = L;
                }
                layerDst.push_back(&out[lit->second].tris);
            }''', '''        std::vector<size_t> layerBatch;
        if (layered)
            for (int L = 1; L < layers; ++L) {
                auto [lit, lfresh] = byMaterial.emplace(m.material + '\\x01' + std::to_string(L), out.size());
                if (lfresh) {
                    out.push_back({m.material, {}});
                    out.back().layer = L;
                }
                layerBatch.push_back(lit->second);
            }
        // References only now that `out` holds every batch of this mesh (a push_back above can move its vectors).
        auto& dst = out[baseBatch].tris;
        std::vector<std::vector<GpuTexVertex>*> layerDst;
        for (size_t b : layerBatch) layerDst.push_back(&out[b].tris);''')
io.open(p, 'w', encoding='utf-8', newline='').write(s)
print('ok')
