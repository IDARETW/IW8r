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
 ('''    size_t refTexExtraFrom_ = ~size_t(0);    // the first batch in vbRefTexExtra_''',
  '''    size_t refTexExtraFrom_ = ~size_t(0);    // the first batch in vbRefTexExtra_
    std::vector<iw8r::AABB> refTexExtraBounds_;  // per appended batch: drawn only in view and near (prop decals)'''),
])
patch('src/viewport/render.cpp', [
 ('''        refTexChunks_.emplace_back();  // small: drawn whole''', '''        refTexChunks_.emplace_back();  // small: drawn whole
        iw8r::AABB bb;
        for (const GpuTexVertex& v : b) bb.add(iw8r::Vec3{v.x, v.y, v.z});
        refTexExtraBounds_.push_back(bb);'''),
 ('''    safeRelease(vbRefTexExtra_);
    refTexExtraFrom_ = refTexIds_.size();''', '''    safeRelease(vbRefTexExtra_);
    refTexExtraFrom_ = refTexIds_.size();
    refTexExtraBounds_.clear();'''),
 ('''                if (noPropDecals && k >= refTexExtraFrom_) continue;''', '''                if (k >= refTexExtraFrom_) {
                    // Prop decals: small detail; drawn in view and within 4,000 units (Hideout: 790k triangles, 38 ms).
                    if (noPropDecals) continue;
                    const size_t e = k - refTexExtraFrom_;
                    if (e < refTexExtraBounds_.size() && refTexExtraBounds_[e].valid()) {
                        const iw8r::AABB& bb = refTexExtraBounds_[e];
                        const iw8r::Vec3 c = bb.center();
                        const double dx = c.x - fog_[0], dy = c.y - fog_[1], dz = c.z - fog_[2];
                        const double r = iw8r::length(bb.maxs - bb.mins) * 0.5;
                        if (std::sqrt(dx * dx + dy * dy + dz * dz) - r > 4000.0) continue;
                        bool out = false;
                        for (const auto& pl : frustum.planes)
                            if (pl.n.x * c.x + pl.n.y * c.y + pl.n.z * c.z - pl.d > r) {
                                out = true;
                                break;
                            }
                        if (out) continue;
                    }
                }'''),
 ('''bool SceneRenderer::setReferenceTextured(const std::vector<std::vector<GpuTexVertex>>& batches, const std::vector<int>* layers) {
    safeRelease(vbRefTexExtra_);
    refTexExtraFrom_ = ~size_t(0);''', '''bool SceneRenderer::setReferenceTextured(const std::vector<std::vector<GpuTexVertex>>& batches, const std::vector<int>* layers) {
    safeRelease(vbRefTexExtra_);
    refTexExtraFrom_ = ~size_t(0);
    refTexExtraBounds_.clear();'''),
])
print('ok')
