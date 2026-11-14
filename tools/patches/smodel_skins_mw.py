import io


def patch(p, a, b, n=1):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


MH = 'src/qtapp/main_window.h'
patch(MH, '''        std::vector<std::pair<std::string, std::shared_ptr<const app::PropMeshData>>> tex;
        size_t tris = 0;''', '''        std::vector<std::pair<std::string, std::shared_ptr<const app::PropMeshData>>> tex;
        // Per tex entry, its model surface index (XModelLod::surfIndex + the mesh's place), -1 = unknown: the map's
        // material skin (StockModelInstance::surfaceMaterials) replaces the material by it.
        std::vector<int> texSurf;
        size_t tris = 0;''')
patch(MH, '''            std::vector<std::pair<std::string, std::shared_ptr<const app::PropMeshData>>> tex;
        };''', '''            std::vector<std::pair<std::string, std::shared_ptr<const app::PropMeshData>>> tex;
            std::vector<int> texSurf;
        };''')

M = 'src/qtapp/main_window.cpp'
patch(M, '''                const auto toTex = [&](const ImportedModel& src,
                                       std::vector<std::pair<std::string, std::shared_ptr<const app::PropMeshData>>>& outTex,
                                       std::vector<app::GpuVertex>* plainOut) {''', '''                // surfBase: the LOD's first surface in the model (XModelLod::surfIndex), -1 when its meshes do not match its
                // surfaces one to one (then no material skin applies).
                const auto toTex = [&](const ImportedModel& src,
                                       std::vector<std::pair<std::string, std::shared_ptr<const app::PropMeshData>>>& outTex,
                                       std::vector<app::GpuVertex>* plainOut, std::vector<int>& outSurf, int surfBase) {''')
patch(M, '''                        outTex.push_back({ms.material, std::move(pm)});
                    }
                };
                toTex(*im, fresh->tex, &fresh->plain);''', '''                        outTex.push_back({ms.material, std::move(pm)});
                        outSurf.push_back(surfBase >= 0 ? surfBase + int(&ms - src.meshes.data()) : -1);
                    }
                };
                const auto modelDetail = modelCacheStock_->loadDetail(qs(in.model));
                const auto surfBaseOf = [&](size_t level, const ImportedModel& src) {
                    if (!modelDetail || level >= modelDetail->lods.size()) return level == 0 && !modelDetail ? -1 : -1;
                    const XModelLod& l = modelDetail->lods[level];
                    return l.surfCount == int(src.meshes.size()) ? l.surfIndex : -1;
                };
                toTex(*im, fresh->tex, &fresh->plain, fresh->texSurf, surfBaseOf(0, *im));''')
patch(M, '''                        toTex(*lm, lod.tex, nullptr);''', '''                        toTex(*lm, lod.tex, nullptr, lod.texSurf, surfBaseOf(li, *lm));''')
patch(M, '''            if (built && built->tris > 0 && meshTris + built->tris <= kPropTriangleBudget) {
                meshTris += built->tris;
                const int group = built->lods.empty() ? -1 : ++lodGroups;
                for (const auto& [material, tris] : built->tex) {
                    app::InstancedTexMesh tm;''', '''            if (built && built->tris > 0 && meshTris + built->tris <= kPropTriangleBudget) {
                meshTris += built->tris;
                const int group = built->lods.empty() ? -1 : ++lodGroups;
                // The map's material for a surface (its skin), else the model's own.
                const auto skinned = [&](const std::string& own, const std::vector<int>& surfs, size_t k) -> std::string {
                    if (!in.surfaceMaterials || k >= surfs.size() || surfs[k] < 0 || size_t(surfs[k]) >= in.surfaceMaterials->size()) return own;
                    const std::string& m = (*in.surfaceMaterials)[size_t(surfs[k])];
                    return m.empty() ? own : m;
                };
                for (size_t k = 0; k < built->tex.size(); ++k) {
                    const auto& [ownMaterial, tris] = built->tex[k];
                    const std::string material = skinned(ownMaterial, built->texSurf, k);
                    app::InstancedTexMesh tm;''')
patch(M, '''                for (const RefPropMesh::Lod& lod : built->lods)
                    for (const auto& [material, tris] : lod.tex) {
                        app::InstancedTexMesh tm;''', '''                for (const RefPropMesh::Lod& lod : built->lods)
                    for (size_t k = 0; k < lod.tex.size(); ++k) {
                        const auto& [ownMaterial, tris] = lod.tex[k];
                        const std::string material = skinned(ownMaterial, lod.texSurf, k);
                        app::InstancedTexMesh tm;''')
patch(M, '''            it = meshIndex.emplace(in.model, std::move(mm)).first;''', '''            it = meshIndex.emplace(meshKey, std::move(mm)).first;''')
patch(M, '''        auto it = meshIndex.find(in.model);
        if (it == meshIndex.end()) {''', '''        // One mesh set per model and material skin (placements of one model can use different skins).
        const std::string meshKey = in.surfaceMaterials ? in.model + '\\x1f' + std::to_string(reinterpret_cast<uintptr_t>(in.surfaceMaterials.get()))
                                                        : in.model;
        auto it = meshIndex.find(meshKey);
        if (it == meshIndex.end()) {''')
print('ok')
