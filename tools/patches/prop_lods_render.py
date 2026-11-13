import io


def patch(p, a, b, n=1):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    if b and s.count(b) >= 1:
        return
    assert s.count(a) == n, (p, s.count(a), a[:80])
    s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


RH = 'src/viewport/render.h'
R = 'src/viewport/render.cpp'

patch(RH, '''    // none for every instance.
    std::vector<float> light;
};''', '''    // none for every instance.
    std::vector<float> light;
    // Stock prop LODs (plan 1.13): the meshes of one model's LODs share lodGroup (>= 0) and hold the same rows and
    // light in the same order; each draws the placements whose camera distance is in [lodNear, lodFar) (lodFar 0 = no
    // limit). -1: not part of a LOD group (every visible placement).
    int lodGroup = -1;
    float lodNear = 0, lodFar = 0;
};''')
patch(RH, '''        float lodOut = 0;  // InstancedTexMesh::lodOut
        int normal = -1;            // setReferenceInstNormal''', '''        float lodOut = 0;  // InstancedTexMesh::lodOut
        int lodGroup = -1;          // InstancedTexMesh::lodGroup / lodNear / lodFar
        float lodNear = 0, lodFar = 0;
        int normal = -1;            // setReferenceInstNormal''')
patch(RH, '''    void cullTexInstances(const float viewProj[16], const float eye[3], float pixelAngle);
    bool instCull_ = true;''', '''    void cullTexInstances(const float viewProj[16], const float eye[3], float pixelAngle);
    std::vector<std::vector<size_t>> texLodGroups_;  // refTexInst_ indices per LOD group (setReferenceTexInstances)
    bool instCull_ = true;''')

# upload: the LOD fields and the groups
patch(R, '''                    g.rows = m.rows;
                    g.lodOut = m.lodOut;''', '''                    g.rows = m.rows;
                    g.lodOut = m.lodOut;
                    g.lodGroup = m.lodGroup;
                    g.lodNear = m.lodNear, g.lodFar = m.lodFar;''')
patch(R, '''        refTexInst_.push_back(g);
    }
    return true;
}''', '''        refTexInst_.push_back(g);
    }
    // The LOD groups: meshes of one model with the same placements (the culling decides once per placement).
    texLodGroups_.clear();
    std::unordered_map<int, size_t> groupAt;
    for (size_t i = 0; i < refTexInst_.size(); ++i) {
        const GpuTexInstanced& g = refTexInst_[i];
        if (g.lodGroup < 0 || !g.vb || !g.inst) continue;
        const auto [it, fresh] = groupAt.emplace(g.lodGroup, texLodGroups_.size());
        if (fresh) texLodGroups_.emplace_back();
        texLodGroups_[it->second].push_back(i);
    }
    // A group whose meshes disagree on the placements cannot share one decision: its meshes are culled on their own.
    for (auto& grp : texLodGroups_) {
        bool same = true;
        for (size_t k = 1; k < grp.size() && same; ++k) same = refTexInst_[grp[k]].rows.size() == refTexInst_[grp[0]].rows.size();
        if (!same) {
            for (size_t i : grp) refTexInst_[i].lodGroup = -1, refTexInst_[i].lodNear = 0, refTexInst_[i].lodFar = 0;
            grp.clear();
        }
    }
    return true;
}''')

# the cull: one decision per placement, shared by a LOD group
patch(R, '''void SceneRenderer::cullTexInstances(const float viewProj[16], const float eye[3], float pixelAngle) {
    const iw8r::Frustum fr = iw8r::frustumFromViewProj(viewProj);
    instCulledLast_ = 0;
    std::vector<float> keep, keepBig;
    static const float bigPx = std::getenv("IW8R_PREPASS_PX") ? std::max(0.0f, float(std::atof(std::getenv("IW8R_PREPASS_PX")))) : 24.0f;
    for (GpuTexInstanced& g : refTexInst_) {
        if (!g.vb || !g.inst || !g.visInst || g.rows.empty()) continue;''', '''void SceneRenderer::cullTexInstances(const float viewProj[16], const float eye[3], float pixelAngle) {
    const iw8r::Frustum fr = iw8r::frustumFromViewProj(viewProj);
    instCulledLast_ = 0;
    std::vector<float> keep, keepBig;
    static const float bigPx = std::getenv("IW8R_PREPASS_PX") ? std::max(0.0f, float(std::atof(std::getenv("IW8R_PREPASS_PX")))) : 24.0f;
    static const float kPx = std::getenv("IW8R_INST_CULL_PX") ? std::max(0.5f, float(std::atof(std::getenv("IW8R_INST_CULL_PX")))) : 4.0f;
    static const bool noLodOut = std::getenv("IW8R_NO_FOLIAGE_LOD_OUT") != nullptr;
    const auto upload = [&](GpuTexInstanced& g) {
        g.big = unsigned(keepBig.size() / 24);
        if (g.big && g.bigInst) {
            D3D11_MAPPED_SUBRESOURCE mb;
            if (SUCCEEDED(ctx_->Map(g.bigInst, 0, D3D11_MAP_WRITE_DISCARD, 0, &mb))) {
                std::memcpy(mb.pData, keepBig.data(), keepBig.size() * sizeof(float));
                ctx_->Unmap(g.bigInst, 0);
            } else {
                g.big = 0;
            }
        }
        g.visible = unsigned(keep.size() / 24);
        if (g.visible) {
            D3D11_MAPPED_SUBRESOURCE ms;
            if (SUCCEEDED(ctx_->Map(g.visInst, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) {
                std::memcpy(ms.pData, keep.data(), keep.size() * sizeof(float));
                ctx_->Unmap(g.visInst, 0);
            } else {
                g.visible = g.instances;  // fall back to every placement
            }
        }
    };
    // LOD groups (plan 1.13): the placement is tested once against the group's bounding sphere (all its LODs), then
    // goes to the mesh whose distance window holds it.
    struct PlaceEval {
        float dist, rad;
        bool out;
    };
    std::vector<PlaceEval> ev;
    for (const std::vector<size_t>& grp : texLodGroups_) {
        if (grp.empty()) continue;
        const GpuTexInstanced& lead = refTexInst_[grp[0]];
        float radius = lead.radius;
        for (size_t mi : grp) {
            const GpuTexInstanced& m = refTexInst_[mi];
            const float d[3] = {m.centre[0] - lead.centre[0], m.centre[1] - lead.centre[1], m.centre[2] - lead.centre[2]};
            radius = std::max(radius, std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]) + m.radius);
        }
        const size_t n = lead.rows.size() / 12;
        ev.resize(n);
        for (size_t i = 0; i < n; ++i) {
            const float* r = &lead.rows[i * 12];
            const float c[3] = {r[0] * lead.centre[0] + r[1] * lead.centre[1] + r[2] * lead.centre[2] + r[3],
                                r[4] * lead.centre[0] + r[5] * lead.centre[1] + r[6] * lead.centre[2] + r[7],
                                r[8] * lead.centre[0] + r[9] * lead.centre[1] + r[10] * lead.centre[2] + r[11]};
            const float sc = std::sqrt(r[0] * r[0] + r[4] * r[4] + r[8] * r[8]);
            PlaceEval& e = ev[i];
            e.rad = radius * std::max(sc, 1e-6f);
            e.out = false;
            for (const auto& pl : fr.planes)
                if (pl.n.x * c[0] + pl.n.y * c[1] + pl.n.z * c[2] - pl.d > e.rad) {
                    e.out = true;
                    break;
                }
            const float dx = c[0] - eye[0], dy = c[1] - eye[1], dz = c[2] - eye[2];
            e.dist = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (!e.out) e.out = e.dist > e.rad && e.rad < e.dist * pixelAngle * kPx;
            if (e.out) ++instCulledLast_;
        }
        for (size_t mi : grp) {
            GpuTexInstanced& g = refTexInst_[mi];
            if (!g.visInst) continue;
            keep.clear();
            keepBig.clear();
            const float far = g.lodFar > 0 && !(noLodOut && g.lodOut > 0) ? g.lodFar : 1e30f;
            for (size_t i = 0; i < n; ++i) {
                const PlaceEval& e = ev[i];
                if (e.out || e.dist < g.lodNear || e.dist >= far) continue;
                const float* r = &g.rows[i * 12];
                keep.insert(keep.end(), r, r + 12);
                keep.insert(keep.end(), &g.light[i * 12], &g.light[i * 12] + 12);
                if (e.dist <= e.rad || e.rad >= e.dist * pixelAngle * bigPx)
                    keepBig.insert(keepBig.end(), r, r + 12), keepBig.insert(keepBig.end(), &g.light[i * 12], &g.light[i * 12] + 12);
            }
            upload(g);
        }
    }
    for (GpuTexInstanced& g : refTexInst_) {
        if (g.lodGroup >= 0) continue;  // above
        if (!g.vb || !g.inst || !g.visInst || g.rows.empty()) continue;''')
patch(R, '''            if (!out) {
                const float dx = c[0] - eye[0], dy = c[1] - eye[1], dz = c[2] - eye[2];
                const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
                static const float k = std::getenv("IW8R_INST_CULL_PX") ? std::max(0.5f, float(std::atof(std::getenv("IW8R_INST_CULL_PX")))) : 4.0f;  // 4: Verdansk 37 -> 15.5 ms, 106 pixels differ
                out = dist > rad && rad < dist * pixelAngle * k;  // radius under k pixels: the game drops or LODs these
                // Stock foliage past its last LOD distance: the game does not draw it (research 100 section 3.3).
                static const bool noLodOut = std::getenv("IW8R_NO_FOLIAGE_LOD_OUT") != nullptr;
                if (!noLodOut && g.lodOut > 0 && dist > g.lodOut) out = true;
            }''', '''            if (!out) {
                const float dx = c[0] - eye[0], dy = c[1] - eye[1], dz = c[2] - eye[2];
                const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
                out = dist > rad && rad < dist * pixelAngle * kPx;  // radius under k pixels (4: Verdansk 37 -> 15.5 ms)
                // Stock foliage past its last LOD distance: the game does not draw it (research 100 section 3.3).
                if (!noLodOut && g.lodOut > 0 && dist > g.lodOut) out = true;
            }''')
patch(R, '''        g.big = unsigned(keepBig.size() / 24);
        if (g.big && g.bigInst) {
            D3D11_MAPPED_SUBRESOURCE mb;
            if (SUCCEEDED(ctx_->Map(g.bigInst, 0, D3D11_MAP_WRITE_DISCARD, 0, &mb))) {
                std::memcpy(mb.pData, keepBig.data(), keepBig.size() * sizeof(float));
                ctx_->Unmap(g.bigInst, 0);
            } else {
                g.big = 0;
            }
        }
        g.visible = unsigned(keep.size() / 24);
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
}''', '''        upload(g);
    }
}''')
# draw: a group's further LODs only through the culled lists; the shadow casts LOD0 only
patch(R, '''        const bool culled = instCull_ && g.visInst && g.visible <= g.instances;
        if (culled && g.visible == 0) continue;''', '''        const bool culled = instCull_ && g.visInst && g.visible <= g.instances;
        if (culled && g.visible == 0) continue;
        if (!culled && g.lodGroup >= 0 && g.lodNear > 0) continue;  // unculled: a LOD group draws its LOD0 only''')
patch(R, '''    for (const auto& g : refTexInst_) {
        if (!g.vb || !g.inst) continue;
        if (psShadowClip_) {''', '''    for (const auto& g : refTexInst_) {
        if (!g.vb || !g.inst) continue;
        if (g.lodGroup >= 0 && g.lodNear > 0) continue;  // the sun shadow of a LOD group: its LOD0
        if (psShadowClip_) {''')
print('ok')
