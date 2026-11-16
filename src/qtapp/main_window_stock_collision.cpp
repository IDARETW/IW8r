// IW8 Radiant Suite - a stock map's props in the play preview, by the build's model collision rule (core
// model_physics.h): a prop without a stock physics asset (most bushes,
// grass) is walked through, one with a physics asset blocks with that asset's convex pieces (read from the catalogue:
// no model is loaded), and a prop whose physics shape is not read blocks with its render mesh's hull, made once per
// model on a worker thread from the cached GLB.
#include "main_window.h"

#include <QCoreApplication>
#include <QThreadPool>

#include <memory>
#include <mutex>
#include <set>

#include "convex_hull.h"
#include "model_cache.h"
#include "model_physics.h"
#include "stock_zone_edit.h"

namespace iw8r::qt {

namespace {

// Render-mesh pieces (Z-up model space) of stock models, kept for the session. Entries are only added, so a pointer
// handed out stays valid.
struct RenderPieceCache {
    std::mutex mx;
    std::map<std::string, std::shared_ptr<const std::vector<std::vector<Plane>>>> ready;
    std::set<std::string> queued;
};

RenderPieceCache& renderPieceCache() {
    static RenderPieceCache c;
    return c;
}

}  // namespace

StockPropCollisionStats MainWindow::addStockPropCollision(CollisionWorld& world, const Vec3& around, double radius) {
    const std::shared_ptr<const std::vector<StockModelInstance>> props = refProps_;
    if (!props || props->empty()) return {};
    const ModelPhysicsCatalog& catalog = ModelPhysicsCatalog::shared(QCoreApplication::applicationDirPath().toStdString());
    ModelCache* cache = modelCacheStock_;
    const auto render = [cache](const std::string& model) -> const std::vector<std::vector<Plane>>* {
        RenderPieceCache& rc = renderPieceCache();
        {
            std::lock_guard<std::mutex> lock(rc.mx);
            if (const auto it = rc.ready.find(model); it != rc.ready.end()) return it->second.get();
            if (rc.queued.count(model)) return nullptr;
        }
        // The cached GLB (the index answers at once once it is read; before that the prop waits for a later call).
        if (!cache || !cache->indexReady()) return nullptr;
        const auto entry = cache->lookup(QString::fromStdString(model));
        if (!entry || entry->failed || entry->glbPath.isEmpty()) return nullptr;
        {
            std::lock_guard<std::mutex> lock(rc.mx);
            if (!rc.queued.insert(model).second) return nullptr;
        }
        const QString glb = entry->glbPath, name = entry->name;
        const QStringList materials = entry->materials;
        QThreadPool::globalInstance()->start([model, glb, name, materials] {
            auto pieces = std::make_shared<std::vector<std::vector<Plane>>>();
            QString err;
            if (const auto m = ModelCache::loadGlb(glb, materials, name, &err)) {
                // The model cache's file convention is glTF Y-up inches; the placement wants Z-up: (x, y, z) -> (x, -z, y).
                for (std::vector<Plane>& piece : convexPieces(*m, 4, 24)) {
                    for (Plane& pl : piece) pl.n = Vec3{pl.n.x, -pl.n.z, pl.n.y};
                    pieces->push_back(std::move(piece));
                }
            }
            RenderPieceCache& c = renderPieceCache();
            std::lock_guard<std::mutex> lock(c.mx);
            c.ready[model] = std::move(pieces);  // an unreadable model: no pieces (it gives no collision)
        });
        return nullptr;
    };
    // Props the document hides (Hide Stock Props): the rebuilt zone has neither them nor their collision.
    std::set<uint32_t> hidden;
    if (refPropZone_ == stockZone_)
        for (const uint32_t i : stockEditHiddenProps(ctx_.doc.map())) hidden.insert(i);
    const auto skip = [&hidden](const StockModelInstance& p) { return !hidden.empty() && hidden.count(p.index) != 0; };
    return iw8r::addStockPropCollision(*props, world, around, radius, catalog, 24, render, skip);
}

}  // namespace iw8r::qt
