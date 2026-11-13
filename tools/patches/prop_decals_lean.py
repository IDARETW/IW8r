import io


def patch(p, a, b, n=1):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


M = 'src/qtapp/main_window.cpp'
patch(M, '''    const StockDecalData& d = *stockDecals_;
    // Only props near a volume become receivers.
    std::vector<AABB> boxes;
    for (const auto& v : d.set.volumes) boxes.push_back(stockDecalVolumeBounds(v));
    std::vector<ImportedMesh> meshes;  // world space; must outlive the receivers
    for (const StockModelInstance& in : *refProps_) {
        const auto it = refPropBuilt_.find(in.model);
        if (it == refPropBuilt_.end() || !it->second) continue;
        const auto place = [&](const app::GpuTexVertex& v) {
            const Vec3 p{v.x, v.y, v.z};
            return in.origin + (in.axis[0] * p.x + in.axis[1] * p.y + in.axis[2] * p.z) * in.scale;
        };
        AABB pb;
        for (const auto& [mat, tris] : it->second->tex)
            for (const auto& v : *tris) pb.add(place(v));
        if (!pb.valid()) continue;
        bool near = false;
        for (const AABB& b : boxes)
            if (!(b.mins.x > pb.maxs.x || b.maxs.x < pb.mins.x || b.mins.y > pb.maxs.y || b.maxs.y < pb.mins.y ||
                  b.mins.z > pb.maxs.z || b.maxs.z < pb.mins.z)) {
                near = true;
                break;
            }
        if (!near) continue;
        ImportedMesh m;
        for (const auto& [mat, trisPtr] : it->second->tex) {
            const std::vector<app::GpuTexVertex>& tris = *trisPtr;
            // Cut-out and blended materials (foliage, cards, glass) receive no decal volume (research 99: their
            // techsets have no decal loop).
            if (const auto e = matCache_->lookup(qs(mat)); e && !e->failed &&
                (!e->maskDds.isEmpty() || materialBlendFromTechset(e->techset.toStdString()) != MaterialBlend::Opaque))
                continue;
            if (qs(mat).contains("foliage", Qt::CaseInsensitive)) continue;
            for (size_t i = 0; i + 2 < tris.size(); i += 3) {
                const Vec3 a = place(tris[i]), b = place(tris[i + 1]), c = place(tris[i + 2]);
                Vec3 n = cross(b - a, c - a);
                if (length(n) < 1e-9) continue;
                n = normalize(n);
                for (const Vec3& p : {a, b, c}) {
                    m.idx.push_back(uint32_t(m.pos.size()));
                    m.pos.push_back(p);
                    m.normal.push_back(n);
                    m.uv.push_back({0, 0});
                }
            }
            }
        if (!m.idx.empty()) meshes.push_back(std::move(m));
    }
    if (meshes.empty()) return;
    StockDecalReceivers receivers;
    for (const ImportedMesh& m : meshes) receivers.addMesh(m);
    receivers.build();
    std::vector<app::ReferenceTexBatch> out;
    int order = 100000;  // over the world's decals of the same order
    // Only the volumes that found no world surface (stencils on crates, paint on barriers: research 99 found every
    // empty Speedball volume beside a prop): ground decals (sand, dirt) on every prop inside their box cost Hideout
    // 790k triangles and 38 ms a frame.
    StockDecalVolumeSet propSet = d.set;
    propSet.volumes.clear();
    std::vector<size_t> back;
    for (size_t i = 0; i < d.set.volumes.size(); ++i)
        if (i >= d.onWorld.size() || !d.onWorld[i]) propSet.volumes.push_back(d.set.volumes[i]), back.push_back(i);
    std::vector<StockDecalBatch> projected = projectDecalVolumes(receivers, propSet);''', '''    const StockDecalData& d = *stockDecals_;
    // Only the volumes that found no world surface (stencils on crates, paint on barriers: research 99 found every
    // empty Speedball volume beside a prop): ground decals (sand, dirt) on every prop inside their box cost Hideout
    // 790k triangles and 38 ms a frame.
    StockDecalVolumeSet propSet = d.set;
    propSet.volumes.clear();
    std::vector<size_t> back;
    for (size_t i = 0; i < d.set.volumes.size(); ++i)
        if (i >= d.onWorld.size() || !d.onWorld[i]) propSet.volumes.push_back(d.set.volumes[i]), back.push_back(i);
    if (propSet.volumes.empty()) return;
    // Receivers: only the triangles of props that touch one of those volumes' boxes (all props near any volume, in
    // world space at about 200 bytes a triangle, took 8 GB on Verdansk, 2026-11-13).
    std::vector<AABB> boxes;
    for (const auto& v : propSet.volumes) boxes.push_back(stockDecalVolumeBounds(v));
    const auto overlaps = [](const AABB& b, const AABB& p) {
        return !(b.mins.x > p.maxs.x || b.maxs.x < p.mins.x || b.mins.y > p.maxs.y || b.maxs.y < p.mins.y || b.mins.z > p.maxs.z ||
                 b.maxs.z < p.mins.z);
    };
    // Cut-out and blended materials (foliage, cards, glass) receive no decal volume (research 99: their techsets have
    // no decal loop); decided once per material.
    std::map<std::string, bool> receives;
    const auto materialReceives = [&](const std::string& mat) {
        const auto [it, fresh] = receives.emplace(mat, true);
        if (fresh) {
            if (const auto e = matCache_->lookup(qs(mat)); e && !e->failed &&
                (!e->maskDds.isEmpty() || materialBlendFromTechset(e->techset.toStdString()) != MaterialBlend::Opaque))
                it->second = false;
            if (qs(mat).contains("foliage", Qt::CaseInsensitive)) it->second = false;
        }
        return it->second;
    };
    std::vector<ImportedMesh> meshes;  // world space; must outlive the receivers
    std::vector<const AABB*> nearBoxes;
    for (const StockModelInstance& in : *refProps_) {
        const auto it = refPropBuilt_.find(in.model);
        if (it == refPropBuilt_.end() || !it->second) continue;
        const auto place = [&](const app::GpuTexVertex& v) {
            const Vec3 p{v.x, v.y, v.z};
            return in.origin + (in.axis[0] * p.x + in.axis[1] * p.y + in.axis[2] * p.z) * in.scale;
        };
        AABB pb;
        for (const auto& [mat, tris] : it->second->tex)
            for (const auto& v : *tris) pb.add(place(v));
        if (!pb.valid()) continue;
        nearBoxes.clear();
        for (const AABB& b : boxes)
            if (overlaps(b, pb)) nearBoxes.push_back(&b);
        if (nearBoxes.empty()) continue;
        ImportedMesh m;
        for (const auto& [mat, trisPtr] : it->second->tex) {
            const std::vector<app::GpuTexVertex>& tris = *trisPtr;
            if (!materialReceives(mat)) continue;
            for (size_t i = 0; i + 2 < tris.size(); i += 3) {
                const Vec3 a = place(tris[i]), b = place(tris[i + 1]), c = place(tris[i + 2]);
                AABB tb;
                tb.add(a), tb.add(b), tb.add(c);
                bool touches = false;
                for (const AABB* nb : nearBoxes)
                    if (overlaps(*nb, tb)) {
                        touches = true;
                        break;
                    }
                if (!touches) continue;
                Vec3 n = cross(b - a, c - a);
                if (length(n) < 1e-9) continue;
                n = normalize(n);
                for (const Vec3& p : {a, b, c}) {
                    m.idx.push_back(uint32_t(m.pos.size()));
                    m.pos.push_back(p);
                    m.normal.push_back(n);
                    m.uv.push_back({0, 0});
                }
            }
        }
        if (!m.idx.empty()) meshes.push_back(std::move(m));
    }
    if (meshes.empty()) return;
    StockDecalReceivers receivers;
    for (const ImportedMesh& m : meshes) receivers.addMesh(m);
    receivers.build();
    std::vector<app::ReferenceTexBatch> out;
    int order = 100000;  // over the world's decals of the same order
    std::vector<StockDecalBatch> projected = projectDecalVolumes(receivers, propSet);''')
print('ok')
