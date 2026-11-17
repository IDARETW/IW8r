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
