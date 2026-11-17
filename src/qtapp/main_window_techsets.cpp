#include "main_window.h"
#include "techset_workspace.h"
#include "asset_preview_window.h"
#include "asset_browser.h"
#include "camera_view.h"
#include "material_cache.h"
#include "model_cache.h"
#include "stock_sky.h"
#include "techset_export.h"
#include <QFileInfo>
#include <QDir>
#include <QTimer>

namespace iw8r::qt {
TechsetWorkspace *MainWindow::makeTechsetWorkspace() {
    TechsetWorkspace::Hooks hooks;
    hooks.device = dev_;
    hooks.mainView = camera_;
    hooks.makePreview = [this] { return makeAssetPreviewWindow(); };
    hooks.refreshPreview = [this](AssetPreviewWindow *preview) { preview->refreshHooks(camera_, ctx_.scene); };
    hooks.previewSelection = [this](AssetPreviewWindow *preview, QString *error) {
        std::lock_guard lock(gpuMutex());
        preview->refreshHooks(camera_, ctx_.scene);
        if (stockSel_ && !stockSel_->model.isEmpty() && ctx_.doc.selection().empty()) {
            preview->showModel(stockSel_->model);
            return true;
        }
        auto model = std::make_shared<ImportedModel>();
        std::map<std::string, size_t> groups;
        const double scale = std::max(1e-6, ctx_.scene.modelScale);
        const auto fileAxis = [](Vec3 p) { return Vec3{p.x, p.z, -p.y}; };
        const auto triangle = [&](const std::string &material, const std::array<Vec3, 3> &p,
                                  const std::array<Vec3, 3> &normal, const std::array<std::array<double, 2>, 3> &uv) {
            auto [it, inserted] = groups.emplace(material, model->meshes.size());
            if (inserted) {
                model->meshes.emplace_back();
                model->meshes.back().material = material;
            }
            auto &mesh = model->meshes[it->second];
            for (size_t i = 0; i < 3; ++i) {
                mesh.idx.push_back(uint32_t(mesh.pos.size()));
                mesh.pos.push_back(fileAxis(p[i]) * (1.0 / scale));
                mesh.normal.push_back(fileAxis(normal[i]));
                mesh.uv.push_back(uv[i]);
                model->bounds.add(mesh.pos.back());
            }
        };
        ctx_.scene.sync(ctx_.doc);
        for (const auto *g : ctx_.scene.items()) {
            if (!ctx_.doc.isSelected(g->ref) || g->volume)
                continue;
            for (size_t face = 0; face < g->polys.size(); ++face) {
                if (g->markerPoly(face))
                    continue;
                const auto &polygon = g->polys[face];
                const auto material = face < g->polyMaterial.size() ? g->polyMaterial[face] : "iw8r/techset_preview";
                const Vec3 normal = face < g->faceNormals.size() ? g->faceNormals[face] : Vec3{0, 0, 1};
                const auto uvAt = [&](size_t i) -> std::array<double, 2> {
                    if (face < g->polyUV.size() && i < g->polyUV[face].size())
                        return {g->polyUV[face][i][0], g->polyUV[face][i][1]};
                    return {0, 0};
                };
                for (size_t i = 1; i + 1 < polygon.size(); ++i)
                    triangle(material, {polygon[0], polygon[i], polygon[i + 1]}, {normal, normal, normal},
                             {uvAt(0), uvAt(i), uvAt(i + 1)});
            }
        }
        if (model->meshes.empty() && stockSel_ && stockWorld_ && stockSel_->mesh >= 0 &&
            size_t(stockSel_->mesh) < stockWorld_->meshes.size()) {
            const size_t index = size_t(stockSel_->mesh), picked = size_t(std::max(0, stockSel_->tri)) / 3;
            const auto &mesh = stockWorld_->meshes[index];
            if (index < stockWorld_->triSurface.size() && picked < stockWorld_->triSurface[index].size()) {
                const auto &surfaces = stockWorld_->triSurface[index];
                for (size_t at = 0; at + 2 < mesh.idx.size(); at += 3) {
                    if (at / 3 >= surfaces.size() || surfaces[at / 3] != surfaces[picked])
                        continue;
                    std::array<Vec3, 3> positions, normals;
                    std::array<std::array<double, 2>, 3> uv{};
                    bool valid = true;
                    for (size_t k = 0; k < 3; ++k) {
                        const auto v = mesh.idx[at + k];
                        if (v >= mesh.pos.size()) {
                            valid = false;
                            break;
                        }
                        positions[k] = mesh.pos[v];
                        normals[k] = v < mesh.normal.size() ? mesh.normal[v] : Vec3{0, 0, 1};
                        if (v < mesh.uv.size())
                            uv[k] = mesh.uv[v];
                    }
                    if (valid)
                        triangle(mesh.material, positions, normals, uv);
                }
            }
        }
        if (model->meshes.empty()) {
            if (error)
                *error = "Select drawable map geometry or pick a stock surface/model first.";
            return false;
        }
        preview->showGeometry(std::move(model));
        return true;
    };
    hooks.mapFolder = [this] {
        return ctx_.doc.path().empty() ? QString() : QFileInfo(QString::fromStdString(ctx_.doc.path())).absolutePath();
    };
    hooks.selectedTargets = [this](const std::string &material) {
        auto targets = camera_->selectedPreviewTargets(material);
        if (!targets.targets.empty() || !stockSel_)
            return targets;
        const auto &picked = *stockSel_;
        if (!picked.model.isEmpty())
            targets.targets.push_back({app::PreviewFamily::StockModel, 0, 0, material, int64_t(picked.propIndex)});
        else if (stockWorld_ && picked.mesh >= 0 && picked.tri >= 0 &&
                 size_t(picked.mesh) < stockWorld_->triSurface.size()) {
            const auto &surfaces = stockWorld_->triSurface[size_t(picked.mesh)];
            const size_t tri = size_t(picked.tri) / 3;
            if (tri < surfaces.size())
                targets.targets.push_back(
                    {app::PreviewFamily::StockWorld, 0, 0, picked.material.toStdString(), -1, surfaces[tri] + 1});
        }
        return targets;
    };
    hooks.importMaterial = [this](const QString &material, QObject *owner,
                                  std::function<void(const QString &, const QString &)> done) {
        if (!matCache_) {
            done({}, "Material cache is unavailable.");
            return;
        }
        const auto entry = matCache_->lookup(material);
        if (!entry || entry->techset.isEmpty()) {
            matCache_->request({material});
            done({}, "The material is being read. Open it again when its images and technique set are available.");
            return;
        }
        QStringList zones;
        if (!entry->zone.isEmpty())
            zones << entry->zone;
        if (!stockZone_.isEmpty())
            zones << "techsets_" + stockZone_ << stockZone_;
        requestTechsetExport([this](const QString &key) { return toolPath(key); }, entry->techset, owner,
                             std::move(done), zones);
    };
    hooks.requestImage = [this](const QString &name, QObject *owner,
                                std::function<void(const QString &, const QString &)> done) {
        if (!imageCache_) {
            done({}, "Image cache is unavailable.");
            return;
        }
        const auto file = imageCache_->cachedDds(name);
        if (!file.isEmpty()) {
            done(file, {});
            return;
        }
        auto connection = std::make_shared<QMetaObject::Connection>();
        *connection =
            connect(imageCache_, &StockSkyCache::skyReady, owner,
                    [this, name, done = std::move(done), connection](const QString &ready, const QString &file) {
                        if (ready != name)
                            return;
                        disconnect(*connection);
                        done(file, file.isEmpty() ? imageCache_->lastError(name) : QString());
                    });
        imageCache_->request(name);
    };
    auto *workspace = new TechsetWorkspace(std::move(hooks), this);
    QTimer::singleShot(0, workspace, [this, workspace] {
        if (modelCacheStock_) {
            connect(modelCacheStock_, &ModelCache::modelReady, workspace, [workspace](const QString &) {
                if (auto *preview = workspace->previewWindow())
                    preview->refreshAssetData();
            });
            connect(modelCacheStock_, &ModelCache::lodReady, workspace, [workspace](const QString &, int) {
                if (auto *preview = workspace->previewWindow())
                    preview->refreshAssetData();
            });
        }
        if (matCache_)
            connect(matCache_, &MaterialCache::materialReady, workspace, [workspace](const QString &) {
                if (auto *preview = workspace->previewWindow())
                    preview->materialsChanged();
            });
    });

    connect(materials_, &AssetBrowser::techsetRequested, workspace, &TechsetWorkspace::openMaterial);
    connect(models_, &AssetBrowser::techsetRequested, workspace,
            [workspace](const QString &name) { workspace->previewAsset(name, false); });
    connect(effects_, &AssetBrowser::techsetRequested, workspace, &TechsetWorkspace::previewEffect);
    connect(images_, &AssetBrowser::techsetRequested, workspace, &TechsetWorkspace::openImage);
    return workspace;
}
} // namespace iw8r::qt
