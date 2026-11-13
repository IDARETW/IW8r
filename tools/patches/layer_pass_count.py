import io


def patch(p, a, b, n=1):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:60])
    s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


patch('src/viewport/render.h', '''    std::array<size_t, 3> referenceTriangleSplit() const {''', '''    size_t layerPassDraws_ = 0, layerPassTris_ = 0, layerPassBatches_ = 0;
    std::array<size_t, 3> layerPassStats() const { return {layerPassBatches_, layerPassDraws_, layerPassTris_}; }
    std::array<size_t, 3> referenceTriangleSplit() const {''')
patch('src/viewport/render.cpp', '''            for (size_t pi = 0; pi < passOrder.size(); ++pi) {
                const size_t k = passOrder[pi];''', '''            layerPassDraws_ = layerPassTris_ = 0;
            layerPassBatches_ = passOrder.size();
            for (size_t pi = 0; pi < passOrder.size(); ++pi) {
                const size_t k = passOrder[pi];''')
patch('src/viewport/render.cpp', '''                if (refCulling_ && k < refTexChunks_.size() && !refTexChunks_[k].empty())
                    refCulledLast_ += forVisibleRuns(frustum, refTexChunks_[k], [&](uint32_t first, uint32_t n) { drawN(n * 3, refTexFirst_[k] + first * 3); });
                else
                    drawN(refTexCount_[k], refTexFirst_[k]);
            }
            const float zero[16] = {};''', '''                if (refCulling_ && k < refTexChunks_.size() && !refTexChunks_[k].empty())
                    refCulledLast_ += forVisibleRuns(frustum, refTexChunks_[k], [&](uint32_t first, uint32_t n) {
                        drawN(n * 3, refTexFirst_[k] + first * 3);
                        ++layerPassDraws_, layerPassTris_ += n;
                    });
                else
                    drawN(refTexCount_[k], refTexFirst_[k]), ++layerPassDraws_, layerPassTris_ += refTexCount_[k] / 3;
            }
            const float zero[16] = {};''')
patch('src/qtapp/camera_view.h', '''    std::array<size_t, 3> referenceTriangleSplit() const { return renderer_.referenceTriangleSplit(); }''',
      '''    std::array<size_t, 3> referenceTriangleSplit() const { return renderer_.referenceTriangleSplit(); }
    std::array<size_t, 3> layerPassStats() const { return renderer_.layerPassStats(); }''')
patch('src/qtapp/main.cpp', '''                report << QString("reference_triangles base %1 layers %2 decals %3").arg(ts[0]).arg(ts[1]).arg(ts[2]);''',
      '''                report << QString("reference_triangles base %1 layers %2 decals %3").arg(ts[0]).arg(ts[1]).arg(ts[2]);
                const auto lp = cam->layerPassStats();
                report << QString("layer_pass batches %1 draws %2 triangles %3").arg(lp[0]).arg(lp[1]).arg(lp[2]);''')
print('ok')
