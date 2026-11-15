// IW8 Radiant Suite - Prefabs dock (roadmap 4.4): the prefab bundles (docs/PREFAB_BUNDLES.md: folders named
// <name>.iw8prefab, listed first with their title, description and tags) and every plain .map prefab under the open
// map's `prefabs` folder and the prefab folders (Preferences), each with a picture. Double-click or Enter stamps one
// (misc_prefab, one undo step) through the caller's insert function; a drag carries its path to the views. A line under
// the list counts what is shown; right-click has Place, Preview, Copy Name and Show in Folder.
#pragma once

#include <QImage>
#include <QStringList>
#include <QWidget>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "prefab_bundle.h"

class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;

namespace iw8r {
struct ImportedModel;
}

namespace iw8r::qt {

class PrefabBrowser : public QWidget {
    Q_OBJECT
public:
    using RootsFn = std::function<QStringList()>;                     // folders to scan, recursively
    using InsertFn = std::function<bool(const QString& file, QString* err)>;
    PrefabBrowser(RootsFn roots, InsertFn insert, QWidget* parent = nullptr);

    void refresh();                  // rescans the folders
    // The prefab's map file for a preview (a bundle: its main contents; "" when there is none).
    QString previewFileAt(int row) const;
    int count() const;               // listed prefabs (bundles first, then plain .map prefabs)
    QString fileAt(int row) const;   // absolute path of a listed prefab: the .map file, or the bundle folder
    void setFilter(const QString& text);  // bundles: title, tags, category, id and description; plain prefabs: path
    int visibleCount() const;
    bool shownAt(int row) const;     // the filter keeps this row
    bool stamp(int row, QString* err = nullptr);  // inserts the prefab at `row`

    // Bundles. A bundle that builds its fallback contents, lacks a build feature, names a stock asset the catalogue
    // does not have, or cannot be placed at all has a badge drawn on its picture; the reason is in its tooltip.
    bool isBundleAt(int row) const;
    int rowOfFile(const QString& file) const;    // -1 when not listed (a bundle: its folder)
    QString titleAt(int row) const;              // the text under the picture
    QString badgeAt(int row) const;              // "" = no badge; else FALLBACK, LIMITED, MISSING or CANNOT PLACE
    QString badgeReasonAt(int row) const;        // why, in one or two sentences
    QString toolTipAt(int row) const;
    bool placeableAt(int row) const;
    bool pictureFromFileAt(int row) const;       // the picture is the bundle's thumb.png (false: drawn from its contents)
    QImage pictureAt(int row);                   // draws it now when it was not drawn yet (tests)
    // The badge and its reason for a bundle, as the dock shows them (also used by the Entity dock's bundle panel).
    static QString bundleBadge(const PrefabBundle& bundle, QString* reason = nullptr);

    // Prefab folders searched everywhere (browser, camera, build): the Preferences folders plus the bundled library
    // data/prefabs (stock-harvested prefabs, the editor's own and the shipped bundles), found beside the editor.
    static QStringList searchRoots();
    static std::vector<std::string> searchRootsStd();
    // The stock asset names of data/asset_catalog (models, materials, always-loaded sounds and effects), read once.
    // Null when the folder is not beside the editor.
    static const PrefabStockCatalog* stockCatalog();
    // What a build of this editor can provide: the default build features and the stock catalogue.
    static PrefabBuildSupport buildSupport();
    // Whether builds carry stock scriptable definitions (doors, cones, cars) and bundles may use them.
    static bool realScriptables();
    // Model bounds for thumbnails (mins xyz, maxs xyz in model space; false when unknown): misc_model prefabs draw each
    // model's rotated footprint instead of a dot. Set once by the main window (stock model cache index).
    using ModelBoundsFn = std::function<bool(const std::string& model, double bounds[6])>;
    static void setModelBounds(ModelBoundsFn fn);
    // Model meshes for thumbnails (owner playtest 2, 2026-11-11: prefabs made of models all looked like boxes): the
    // cached mesh of a model (glTF Y-up, as the model cache gives it), or null when it is not cached yet; the source may
    // start fetching it. A thumbnail drawn with a box for a missing mesh is drawn again by refreshIncomplete().
    using ModelMeshFn = std::function<std::shared_ptr<const iw8r::ImportedModel>(const std::string& model)>;
    static void setModelMesh(ModelMeshFn fn);
    static bool lastThumbnailIncomplete();  // the last thumbnail() call fell back to a box for at least one model
    // Textured pictures (owner request 2026-11-14; TexturedThumbs): the stored picture of a prefab .map file, or a null
    // image with *pending set while it is being made (the row shows a placeholder and is asked again by
    // refreshIncomplete()). When set, the grey drawing of thumbnail() is not used for the list.
    using PictureFn = std::function<QImage(const QString& mapFile, bool* pending)>;
    static void setPictureSource(PictureFn fn);
    void refreshIncomplete();               // redraw the thumbnails that had a box in place of a mesh

    // A three-quarter view of a map's brushes and models, fitted into `size` pixels; a null image when nothing can be
    // drawn. modelRoots: folders with the mapper's own model files (a bundle's models folder), looked up before the
    // stock models.
    static QImage thumbnail(const QString& mapFile, int size = 96, const QStringList& modelRoots = {});

signals:
    void previewRequested(const QString& mapFile);  // context menu > Preview in 3D

protected:
    void showEvent(QShowEvent*) override;

private:
    RootsFn roots_;
    InsertFn insert_;
    QLineEdit* filter_ = nullptr;
    QListWidget* list_ = nullptr;
    QLabel* count_ = nullptr;  // "N prefabs", "N of M shown" or why the list is empty
    bool scanned_ = false;
    class QTimer* thumbTimer_ = nullptr;  // draws thumbnails of the visible rows a few at a time (5,000+ prefabs)
    void loadVisibleThumbnails();
    void loadThumbnail(QListWidgetItem* item);

public:
    int thumbnailsLoaded() const;  // tests
};

}  // namespace iw8r::qt
