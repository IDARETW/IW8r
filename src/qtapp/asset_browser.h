// IW8 Radiant Suite (Qt shell) - stock IW8 asset browser (materials and models).
// Names come from data/asset_catalog/iw8_{materials,models}.tsv (tools/build_asset_catalog.py, built from x64-zt
// verifyzone traces of the Replay 1.20 stock zones). Thumbnails come from ACTS exports indexed in
// data/stock_assets/<zone>/index.json (tools/acts_asset_index.py) and are decoded on demand.
#pragma once
#include <functional>
#include <QSet>

#include <QHash>
#include <QPixmap>
#include <QWidget>

#include "editor_context.h"

class QLineEdit;
class QListView;
class QCheckBox;
class QComboBox;
class QLabel;
class QStandardItemModel;
class QSortFilterProxyModel;

namespace iw8r::qt {

// Drag and drop from the browsers into the views (4.24): the mime type and its text, "<kind>\t<name>\t<loop 0|1>" with
// kind materials / models / effects / sounds.
inline const char* kAssetMimeType = "application/x-iw8r-asset";

class AssetBrowser : public QWidget {
    Q_OBJECT
public:
    enum class Kind { Materials, Models, Effects, Images, Sounds };
    AssetBrowser(EditorContext* ctx, Kind kind, QWidget* parent = nullptr);
    int assetCount() const;
    void setFilter(const QString& text);  // also used by the GUI test
    // Favourites and recent picks (4.22), kept per browser kind in the settings.
    enum class ListMode { All, Favourites, Recent };
    void setListMode(ListMode m);
    bool isFavourite(const QString& name) const;
    void setFavourite(const QString& name, bool on);
    QStringList recent() const;
    void noteRecent(const QString& name);
    // Tags (4.22): free words per asset, kept per browser kind in the settings; every tag in use is also a list mode
    // ("#tag") in the list combo.
    QStringList tagsOf(const QString& name) const;
    void setTags(const QString& name, const QStringList& tags);
    QStringList allTags() const;
    void showTag(const QString& tag);  // list mode = that tag (tests)
    // Usage in the open map (4.22): faces for a material, entities for a model / effect / sound.
    int usageCount(const QString& name) const;
    // Kit filter (4.23, data/asset_catalog/iw8_kits.tsv): "" = all kits.
    void setKit(const QString& kit);
    QStringList kits() const;
    int visibleCount() const;
    QString visibleName(int row) const;
    // Colour image of a stock material from the exported stock assets (empty when not exported).
    QString colorImageFile(const QString& material) const { return imageFiles_.value(material); }
    // Any material with a colour image; `usable` can pass over some (the GUI test skips draw-less shadow casters).
    template <class Pred = bool (*)(const QString&)>
    QString anyMaterialWithImage(Pred usable = [](const QString&) { return true; }) const {
        QStringList names = imageFiles_.keys();
        names.sort();  // the same choice on every run, whatever the hash order
        for (const QString& m : names)
            if (usable(m)) return m;
        return {};
    }
    // The mapper's custom materials (name, colour image), listed first with their own thumbnails.
    void setCustomMaterials(const QList<QPair<QString, QString>>& materials);
    // Models: the preview pane beside the list (an image and a caption; a null image shows the caption only).
    void setPreview(const QImage& image, const QString& caption);
    // Thumbnails from the ACTS caches for the visible rows: the source returns the image when cached; otherwise it
    // requests it, sets *pending and returns a null image (the row keeps a placeholder until refreshThumbnails()).
    using ThumbSource = std::function<QImage(const QString& name, bool* pending)>;
    void setThumbnailSource(ThumbSource fn) { thumbSource_ = std::move(fn); refreshThumbnails(); }
    void refreshThumbnails();
    int thumbnailsLoaded() const;  // rows with a real thumbnail (tests)
    // Availability of a row's asset: "" = available or not known yet, else why it cannot be built (the caches tried and
    // failed). Unavailable visible rows are greyed with the reason in their tooltip.
    using StatusSource = std::function<QString(const QString& name)>;
    void setStatusSource(StatusSource fn) { statusSource_ = std::move(fn); refreshThumbnails(); }
    StatusSource statusSource() const { return statusSource_; }  // tests
    int unavailableMarked() const { return int(unavailable_.size()); }  // tests
    int hiddenMissing() const { return hiddenMissing_; }                 // tests
    QString previewCaption() const;
    bool previewHasImage() const;
    void selectRow(int row);  // also used by the GUI test

signals:
    void materialChosen(const QString& name);
    void infoRequested(const QString& name);  // context menu > Info (Material Info / Model Info)
    void replaceRequested(const QString& name);  // context menu > Replace Everywhere With... (the main window asks for the new name)
    void modelChosen(const QString& name);
    void effectChosen(const QString& name);  // Effects browser: place an fx_origin with this stock vfx
    void soundChosen(const QString& name, bool loop);  // Sounds browser: place a sound_emitter with this stock alias
    void imageHighlighted(const QString& name);  // Images browser: the current row changed (preview)
    void modelHighlighted(const QString& name);  // the current model row changed (preview)
    void previewRequested(const QString& name);  // Models: open the 3D preview window

public:
    void reloadCatalog() { load(); }  // after an availability audit (4.12)

private:
    void load();
    void refreshUsedFilter();
    QIcon thumbnail(const QString& name);

    EditorContext* ctx_;
    Kind kind_;
    QLineEdit* search_;
    QCheckBox* usedOnly_;
    QComboBox* listMode_ = nullptr;
    QComboBox* kit_ = nullptr;
    QHash<QString, QStringList> kitNames_;  // kit -> names of this browser's kind
    QString settingsKey(const char* what) const;
    void applyListMode();
    void refillListModes();
    QCheckBox* buildableOnly_ = nullptr;  // hide assets the availability catalog lists as missing
    int hiddenMissing_ = 0;
    QLabel* info_;
    QListView* view_;
    QLabel* preview_ = nullptr;
    QLabel* previewCaption_ = nullptr;
    QStandardItemModel* model_;
    QSortFilterProxyModel* proxy_;
    int hiddenStreamOnly_ = 0;  // models left out (streaming-zone weapons/attachments)
    StatusSource statusSource_;
    QSet<QString> unavailable_;  // rows greyed as not available
    QHash<QString, QString> imageFiles_;  // material -> decoded-able colour image (absolute path)
    QHash<QString, QIcon> icons_;
    ThumbSource thumbSource_;
    QSet<QString> pendingThumbs_;
    QSet<QString> realThumbs_;
    QTimer* thumbs_ = nullptr;
};

}  // namespace iw8r::qt
