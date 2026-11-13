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


H = 'src/qtapp/main_window.h'
C = 'src/qtapp/main_window.cpp'

patch(H, '''        size_t tris = 0;
        double lodOut = 0;  // foliage: the last LOD distance (not drawn beyond it, as in game); 0 = always
    };''', '''        size_t tris = 0;
        double lodOut = 0;  // foliage: the last LOD distance (not drawn beyond it, as in game); 0 = always
        // Plan 1.13: the model's further LODs that are cached, each drawn from `near` to `far` inches from the camera
        // (the game's LOD distances); LOD0 (tex above) up to the first one's near. far 0 = no limit.
        struct Lod {
            int level = 0;
            double near = 0, far = 0;
            std::vector<std::pair<std::string, std::vector<app::GpuTexVertex>>> tex;
        };
        std::vector<Lod> lods;
        double lod0Far = 0;  // LOD0's far (0 = no limit)
    };
    bool refPropLodsAsked_ = false;  // a LOD fetch for the props was asked since the last rebuild''')

# the build: LOD0 as before, then the cached further LODs; the missing ones are asked for
patch(C, '''                {
                    std::vector<app::GpuVertex>& plain = fresh->plain;
                    const auto zup = [](const Vec3& p) { return Vec3{p.x, -p.z, p.y}; };  // glTF Y-up -> Z-up
                    for (const auto& ms : im->meshes) {''', '''                // Textured triangles of an imported mesh set (LOD0 also fills `plain` with its untextured surfaces).
                const auto toTex = [&](const ImportedModel& src, std::vector<std::pair<std::string, std::vector<app::GpuTexVertex>>>& outTex,
                                       std::vector<app::GpuVertex>* plainOut) {
                    std::vector<app::GpuVertex> ignored;
                    std::vector<app::GpuVertex>& plain = plainOut ? *plainOut : ignored;
                    const auto zup = [](const Vec3& p) { return Vec3{p.x, -p.z, p.y}; };  // glTF Y-up -> Z-up
                    for (const auto& ms : src.meshes) {''')
patch(C, '''                        if (!texTris.empty()) fresh->tex.push_back({ms.material, std::move(texTris)});
                    }
                }
                built = fresh;''', '''                        if (!texTris.empty()) outTex.push_back({ms.material, std::move(texTris)});
                    }
                };
                toTex(*im, fresh->tex, &fresh->plain);
                // Plan 1.13: the cached further LODs, with the game's distances (LOD i up to lods[i].distance); a LOD
                // not cached yet is asked for and its range stays with the LOD before it.
                if (const auto detail = modelCacheStock_->loadDetail(qs(in.model)); detail && detail->lods.size() > 1 && !fresh->tex.empty()) {
                    static const bool noLods = std::getenv("IW8R_NO_PROP_LODS") != nullptr;
                    const auto& L = detail->lods;
                    for (size_t li = 1; li < L.size() && !noLods; ++li) {
                        if (!modelCacheStock_->hasLod(qs(in.model), int(li))) {
                            if (!modelCacheStock_->lodFailed(qs(in.model), int(li))) wantLods_.append({qs(in.model), int(li)});
                            continue;
                        }
                        auto lm = modelCacheStock_->loadModelLod(qs(in.model), int(li));
                        if (!lm) continue;
                        RefPropMesh::Lod lod;
                        lod.level = int(li);
                        lod.near = L[li - 1].distance;
                        toTex(*lm, lod.tex, nullptr);
                        if (!lod.tex.empty() && lod.near > 0) fresh->lods.push_back(std::move(lod));
                    }
                    // Each level runs to the next one's start; the last to the foliage LOD-out (others: no limit).
                    for (size_t k = 0; k < fresh->lods.size(); ++k)
                        fresh->lods[k].far = k + 1 < fresh->lods.size() ? fresh->lods[k + 1].near : fresh->lodOut;
                    fresh->lod0Far = fresh->lods.empty() ? fresh->lodOut : fresh->lods[0].near;
                }
                built = fresh;''')
# the instanced meshes: one per LOD and material, in one LOD group
patch(C, '''            if (built && built->tris > 0 && meshTris + built->tris <= kPropTriangleBudget) {
                meshTris += built->tris;
                for (const auto& [material, tris] : built->tex) {
                    app::InstancedTexMesh tm;
                    tm.tris = tris;
                    tm.lodOut = float(built->lodOut);
                    mm.tex.push_back(int(texInstanced.size()));
                    texInstanced.push_back(std::move(tm));
                    texMaterials.push_back(material);
                }''', '''            if (built && built->tris > 0 && meshTris + built->tris <= kPropTriangleBudget) {
                meshTris += built->tris;
                const int group = built->lods.empty() ? -1 : ++lodGroups;
                for (const auto& [material, tris] : built->tex) {
                    app::InstancedTexMesh tm;
                    tm.tris = tris;
                    tm.lodOut = float(built->lodOut);
                    tm.lodGroup = group;
                    tm.lodNear = 0, tm.lodFar = float(built->lod0Far);
                    mm.tex.push_back(int(texInstanced.size()));
                    texInstanced.push_back(std::move(tm));
                    texMaterials.push_back(material);
                }
                for (const RefPropMesh::Lod& lod : built->lods)
                    for (const auto& [material, tris] : lod.tex) {
                        app::InstancedTexMesh tm;
                        tm.tris = tris;
                        tm.lodOut = float(built->lodOut);
                        tm.lodGroup = group;
                        tm.lodNear = float(lod.near), tm.lodFar = float(lod.far);
                        mm.tex.push_back(int(texInstanced.size()));
                        texInstanced.push_back(std::move(tm));
                        texMaterials.push_back(material);
                    }''')
patch(C, '''    size_t meshTris = 0, meshed = 0;
    refPropLit_ = refPropLitOf_ = 0;''', '''    size_t meshTris = 0, meshed = 0;
    int lodGroups = 0;  // plan 1.13: LOD groups numbered per rebuild
    wantLods_.clear();
    refPropLit_ = refPropLitOf_ = 0;''')
patch(H, '''    bool refPropLodsAsked_ = false;  // a LOD fetch for the props was asked since the last rebuild''',
      '''    QList<QPair<QString, int>> wantLods_;  // rebuildReferenceProps: prop LODs not cached yet (asked for at its end)''')
# ask for them once the pass is done (the existing missing-model request is right after the placement loop)
patch(C, '''    refPropPlaceholders_ = (waitMesh.rows.size() + failMesh.rows.size()) / 12;''', '''    // Plan 1.13: the props' further LODs not cached yet, in batches (lodReady rebuilds the props).
    if (!wantLods_.isEmpty() && modelCacheStock_) modelCacheStock_->requestLods(wantLods_);
    refPropPlaceholders_ = (waitMesh.rows.size() + failMesh.rows.size()) / 12;''')
# a LOD that arrives: that model is built again, the props once for a burst
patch(C, '''    connect(modelCacheStock_, &ModelCache::lodReady, this, [this](const QString&, int) {
        ctx_.scene.invalidate();
        ctx_.cameraMoved();
    });''', '''    connect(modelCacheStock_, &ModelCache::lodReady, this, [this](const QString& name, int) {
        ctx_.scene.invalidate();
        ctx_.cameraMoved();
        // A stock prop's LOD (plan 1.13): its kept mesh is rebuilt with it; the props once, when every LOD fetch is done.
        refPropBuilt_.erase(name.toStdString());
        if (refProps_ && modelCacheStock_->lodsPending() == 0 && !refPropRebuildQueued_) {
            refPropRebuildQueued_ = true;
            QTimer::singleShot(2000, this, [this] {
                refPropRebuildQueued_ = false;
                rebuildReferenceProps();
            });
        }
    });''')
print('ok')
