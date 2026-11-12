#pragma once
// Material Info and Model Info (roadmap 4.16 / 4.18): what the editor knows about one stock asset, as HTML for a
// small read-only window. The text is built from the caches only, so it never blocks on ACTS.
#include <QString>
#include <QStringList>
#include <optional>

#include "material_cache.h"
#include "model_cache.h"
#include "xmodel_info.h"
#include "streamed_images.h"
#include "model_import.h"

class QWidget;

namespace iw8r::qt {

struct MaterialInfoInput {
    QString name;
    std::optional<MaterialCache::Entry> entry;  // nullopt: not fetched yet
    size_t faces = 0, primitives = 0;           // use in the open map
    QString surfaceType;                        // footstep / impact surface the build tags it with ("" = default)
    QString customSource;                       // a custom (imported) material's definition file, "" for stock
    bool toolMaterial = false;
    bool buildsNormal = false;                  // Compile > Stock textures carries the normal + gloss map
};
QString materialInfoHtml(const MaterialInfoInput& in);
// Streamed image sample catalogue (4.25, data/asset_catalog/iw8_streamed_images.tsv), loaded once.
const StreamedImageCatalog& streamedImages();

struct ModelInfoInput {
    QString name;
    std::optional<ModelCache::Entry> entry;     // nullopt: not fetched yet
    std::optional<ImportedModel> mesh;          // LOD0 from the cache, when loaded
    size_t placed = 0;                          // entities in the open map with this model
    QStringList catalogZones;                   // zones the catalog lists it in
    std::optional<XModelInfo> detail;           // LODs, bones and tags (3.18), when the cache kept them
    double cameraDistance = -1;                 // editor camera to the nearest placed copy (inches), -1 when none
};
QString modelInfoHtml(const ModelInfoInput& in);

// A non-modal window with the HTML and Copy / Close buttons; objectName `objectName` (one per kind, reused).
void showAssetInfo(QWidget* parent, const QString& objectName, const QString& title, const QString& html);

}  // namespace iw8r::qt
