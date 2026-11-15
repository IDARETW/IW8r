import io

# Verdansk regression found 2026-11-15 (bench 452-672 ms a frame, 21-24 GB committed; 11-13: 28 ms, 5-7 GB): since
# b649991 every Warzone vehicle spawn struct draws its vehicle model, and the scene holds each item's triangles on its
# own (not instanced). The vehicles' LOD0 are 100k-140k triangles (veh8_*_physics_mp); Verdansk's spawns came to about
# 17 M triangles and 3.5 GB of scene streams. On a document with more than eight vehicle spawns, each spawn now draws
# its vehicle's last LOD (a box until that LOD is in the cache), as the game draws far vehicles; a map with a few
# placed vehicles keeps LOD0.

ROOT = 'E:/IW8-Radiant/'


def patch(p, a, b, count=1):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == count, (p, s.count(a), a[:70])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


H = 'src/qtapp/main_window.h'
patch(H, '''    int stockLodFor(const Entity& e, const std::string& model);''', '''    int stockLodFor(const Entity& e, const std::string& model);
    // A Warzone vehicle spawn on a document with many of them (stockLodFor gives it its vehicle's last LOD).
    bool manyVehicleSpawn(const Entity& e);
    uint64_t vehicleCountRev_ = ~0ull;
    size_t vehicleCount_ = 0;
    std::set<std::string> vehicleLodWanted_;  // vehicle models whose last LOD is being fetched (scene redrawn when in)''')

M = 'src/qtapp/main_window.cpp'
patch(M, '''int MainWindow::stockLodFor(const Entity& e, const std::string& model) {
    const bool collision = ctx_.scene.cameraMode == iw8r::app::CameraMode::Collision;
    if ((!lodPreview_ && !collision) || !modelCacheStock_) return 0;''', '''bool MainWindow::manyVehicleSpawn(const Entity& e) {
    const std::string* type = e.value("vehicletype");
    if (!type || type->empty()) return false;
    if (vehicleCountRev_ != ctx_.doc.revision()) {
        vehicleCountRev_ = ctx_.doc.revision();
        vehicleCount_ = 0;
        for (const auto& x : ctx_.doc.map().entities)
            if (const std::string* t = x.value("vehicletype"); t && !t->empty()) ++vehicleCount_;
    }
    return vehicleCount_ > 8;
}

int MainWindow::stockLodFor(const Entity& e, const std::string& model) {
    const bool collision = ctx_.scene.cameraMode == iw8r::app::CameraMode::Collision;
    // Warzone vehicle spawns by the hundred (Verdansk): each vehicle's last LOD, not its 100k-triangle LOD0.
    const bool vehicles = !lodPreview_ && !collision && modelCacheStock_ && manyVehicleSpawn(e);
    if (vehicles) {
        auto it = lodDetail_.find(model);
        if (it == lodDetail_.end()) {
            std::optional<XModelInfo> d;
            if (const auto ent = modelCacheStock_->lookup(qs(model)); ent && !ent->failed) d = modelCacheStock_->loadDetail(qs(model));
            it = lodDetail_.emplace(model, std::move(d)).first;
        }
        return it->second && it->second->lods.size() >= 2 ? int(it->second->lods.size()) - 1 : 0;
    }
    if ((!lodPreview_ && !collision) || !modelCacheStock_) return 0;''')
patch(M, '''            } else {
                modelCacheStock_->requestLod(qn, want);
            }
        }
        if (auto it = modelCache_.find(*m); it != modelCache_.end()) return it->second;''', '''            } else {
                modelCacheStock_->requestLod(qn, want);
                // A vehicle spawn among many: its box until the low LOD is in (LOD0 is 100k+ triangles each).
                if (!lodPreview_ && manyVehicleSpawn(e)) {
                    vehicleLodWanted_.insert(*m);
                    return nullptr;
                }
            }
        }
        if (auto it = modelCache_.find(*m); it != modelCache_.end()) return it->second;''')
patch(M, '''        // The scene draws LOD0 except in the LOD preview.
        if (lodPreview_) invalidateSceneSoon();''', '''        // The scene draws LOD0 except in the LOD preview and for Warzone vehicle spawns (their last LOD).
        if (lodPreview_ || vehicleLodWanted_.erase(name.toStdString())) invalidateSceneSoon();''')
print('ok')
