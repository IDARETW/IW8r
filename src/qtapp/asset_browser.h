// IW8 Radiant (Qt shell) - stock IW8 asset browser (materials, models, effects, images, sounds). Double-click or
// Enter applies / places the asset; right-click has Favourites, Tags, Copy Name (and Show Image in Folder for materials
// with an exported colour image).
// Names come from data/asset_catalog/iw8_{materials,models}.tsv (tools/build_asset_catalog.py, built from x64-zt
// verifyzone traces of the Replay 1.20 stock zones). Thumbnails are made once and kept in packs (thumb_store.h); the
// stored ones are decoded on worker threads for the rows on screen and the pages around them (ThumbDecoder), so a
// panel opens and scrolls with its pictures; only rows with nothing stored go to the thumbnail source (ACTS exports,
// the textured model pictures), which stores what it makes. Models are tiles in three sizes with a compact preview of
// the selected one; Effects play the selected effect (VfxPreview); Sounds play the selected alias (sound_preview.h).
#pragma once

#include <memory>
#include <functional>
#include <QSet>

#include <QHash>
#include <QPixmap>
#include <QWidget>

#include "editor_context.h"
#include "thumb_store.h"

class QAbstractItemView;
class QLineEdit;
class QListView;
class QCheckBox;
class QComboBox;
class QLabel;
class QStandardItem;
class QStandardItemModel;
class QStyledItemDelegate;
class QSortFilterProxyModel;
class QScrollBar;
class QShowEvent;
class QTimer;
class QPushButton;
class QToolButton;
class QMenu;
class QTreeWidget;
namespace iw8r {
struct VfxSimDef;
}

namespace iw8r::qt {

// Drag and drop from the browsers into the views (4.24): the mime type and its text, "<kind>\t<name>\t<loop 0|1>" with
// kind materials / models / effects / sounds.
inline const char* kAssetMimeType = "application/x-iw8r-asset";

// Down in a dock's search box moves the keyboard focus into its list, onto the first row when no row is current
// (asset, prefab and outliner docks).
void focusListOnDown(QLineEdit* search, QAbstractItemView* list);

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
    // Tests: every row forgets its picture (made again from the packs or the source when it is needed), and how many of
    // the first `count` listed rows show their real picture.
    void forgetThumbnails();
    int picturedRows(int count) const;
    VisibleThumbs visibleThumbState() const;  // the rows on screen now (thumb_bench.h, tests)
    QScrollBar* scrollBar() const;
    // Stored pictures read on worker threads (ThumbDecoder, thumb_store.h): `peek` must be thread-safe and only read
    // (the thumbnail packs); it returns the stored picture of a name or a null image. Rows it has none for go to the
    // thumbnail source above (which may decode, render or request an export on the GUI thread).
    using ThumbPeek = std::function<QImage(const QString& name)>;
    void setThumbnailPeek(ThumbPeek fn);
    // Models: the tile picture size (owner 2026-11-15: model thumbnails were too small); kept in the settings.
    enum class TileSize { Small, Medium, Large };
    void setTileSize(TileSize s);
    TileSize tileSize() const { return tileSize_; }
    int iconSide() const;          // the side the pictures are decoded at
    int tileWidth() const;         // the current grid cell (tests)
    bool previewShown() const;     // models: the selected model's preview box is showing
    // Effects: the selected effect plays under the list (VfxPreview, the simplified particle simulation); a null def
    // shows the caption only (exporting, not exportable).
    void setEffectPreview(const VfxSimDef* def, const QString& caption);
    class VfxPreview* effectPreview() const { return fxPreview_; }
    // Sounds: the line under the list (what plays, what is being prepared, or why nothing plays) and the Play / Stop state.
    void setSoundStatus(const QString& text, bool playing);
    QString soundStatus() const;
    // The Tools menu commands of this kind of asset, at the end of the panel's dots menu.
    void setToolActions(const QList<QAction*>& actions);
    QList<QAction*> toolActions() const { return toolActions_; }
    // Short facts at the right of a row and in the details strip ("loop", "1.5 s", "0.4-1.2 s · x3"), by name; `keep`
    // stores them in the settings (effects: known once previewed).
    void setRowFact(const QString& name, const QString& fact, bool keep = false);
    void setRowFacts(const QHash<QString, QString>& facts);
    QString rowFact(const QString& name) const { return facts_.value(name); }
    // Sounds: the waveform of what plays (peak per bucket, 0-1), its length; the strip draws it with a playhead.
    void setSoundWave(const QVector<float>& peaks, double seconds, bool loop);
    // The filter chips: the exclusive chip ("all" or a group id) and the chip ids (tests).
    void setChip(const QString& id);
    QString chip() const;
    QStringList chipIds() const;
    // The category of a name in this panel's chips ("" for Materials / Models), the same rule the chips filter by.
    QString groupOf(const QString& name) const;
    // Effects: the folder the list is narrowed to ("" = all), as the folder tree selects it.
    void setFolder(const QString& folder);
    QString folder() const { return folder_; }
    // The header line's count ("37 of 1,020") and the details strip's title / facts (tests).
    QString countText() const;
    QString stripTitle() const;
    QString stripFacts() const;
    bool stripShown() const;
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
    void catalogReady();  // the catalogue read on the worker thread is in the list
    void effectHighlighted(const QString& name);  // Effects: the current row changed (side bar preview)
    void soundPlayRequested(const QString& name);  // Sounds: Play (or Enter on the Play button)
    void soundStopRequested();
    void firstShown();  // the panel was shown for the first time (sounds: the bank index is read then)

public:
    void reloadCatalog() { load(); }  // after an availability audit (4.12)
    // The menu cache was (re)built: its pills, facts and pictures are read again.
    void reloadPack();
    // The catalogue is read on a worker thread (seconds of files at start-up): true once the list holds the latest read.
    bool catalogLoaded() const { return loadedGen_ == loadGen_; }

protected:
    void showEvent(QShowEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;

private:
    void load();  // starts reading the catalogue on a worker thread; applyCatalog() fills the list
    struct Catalog;
    static std::shared_ptr<Catalog> readCatalog(Kind kind, bool buildableOnly, const QString& dataDir, bool wantKits);
    void applyCatalog(const Catalog& c);
    QStandardItem* customItem(const QString& name, const QString& image) const;
    int loadGen_ = 0, loadedGen_ = -1;  // the latest load() started and the one in the list
    QList<QPair<QString, QString>> customMaterials_;  // the last setCustomMaterials list
    void refreshUsedFilter();
    // The line above the list: the catalogue summary when everything is listed, else "N of M shown" or "No matches".
    void updateInfo();
    QIcon thumbnail(const QString& name);
    QIcon placeholderIcon(const QString& name) const;
    QString filterText_;     // the search text the list is filtered by
    QString catalogInfo_;    // the summary load() wrote
    quint64 usedSignature_ = 0;  // the map content "In this map" was collected from (selection changes skip it)

    EditorContext* ctx_;
    Kind kind_;
    QLineEdit* search_;
    bool usedOnly_ = false;          // "In this map" chip
    int listMode_ = 0;               // 0 all, 1 favourites, 2 recent, 3 the tag listTag_
    QString listTag_;
    QComboBox* kit_ = nullptr;
    QHash<QString, QStringList> kitNames_;  // kit -> names of this browser's kind
    QString settingsKey(const char* what) const;
    void applyListMode();
    void refillListModes();
    void setListModeTo(int mode, const QString& tag);
    void rebuildMenu();               // the dots menu: show modes, tags, options, the kind's tools
    void updateStrip();               // the details strip of the current row
    void fillFolders();               // effects: the folder tree from the names
    void playRow(const QModelIndex& idx);
    void fitEffectSquare();           // effects: the preview square's largest side from the visible height
    bool buildableOnly_ = true;       // hide assets the availability catalog lists as missing
    int hiddenMissing_ = 0;
    class PanelHeader* header_ = nullptr;
    class ChipBar* chips_ = nullptr;
    class DetailsStrip* strip_ = nullptr;
    QToolButton* favButton_ = nullptr;
    QMenu* menu_ = nullptr;
    QList<QAction*> toolActions_;
    QHash<QString, QString> facts_;   // row facts (setRowFact)
    QTreeWidget* folders_ = nullptr;  // effects: folders by path
    QString folder_;
    QWidget* wave_ = nullptr;         // sounds: WaveView
    QString playingName_;             // sounds: the alias asked to play (its row shows Stop)
    bool shownOnce_ = false;
    QString packKind_;                             // its menu pack (menu_pack.h): materials, models, ...
    std::shared_ptr<const class MenuPack> pack() const;  // the pack now (null: not built, or being built)
    QHash<QString, QString> packFacts_;            // its facts, for the details strip
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
    QTimer* thumbs_ = nullptr;  // the slow pass (thumbnail source), after scrolling settles
    QTimer* fast_ = nullptr;    // the stored pictures of the rows on screen (decoder), right away
    std::unique_ptr<ThumbDecoder> decoder_;
    QSet<QString> peekMissed_;  // nothing stored: the thumbnail source makes or requests it
    QHash<QString, QString> fileKeys_;     // decoder key "file:<path>" -> row name (images named by the catalogue)
    QHash<QString, QStandardItem*> items_;  // row name -> its item
    std::vector<QString> iconOrder_;        // names given an icon, oldest first (memory is bounded)
    int emptyPasses_ = 0;                  // passes that found the view not laid out yet
    QStyledItemDelegate* delegate_ = nullptr;  // AssetTileDelegate
    QObject* gridFit_ = nullptr;                // TileGridFit (tiles)
    QComboBox* tileSizeBox_ = nullptr;
    QWidget* previewBox_ = nullptr;             // models: the compact preview of the selected model
    class VfxPreview* fxPreview_ = nullptr;     // effects: the selected effect
    QLabel* fxCaption_ = nullptr;
    QPushButton* soundPlay_ = nullptr;          // sounds: play / stop the selected alias
    QLabel* soundCaption_ = nullptr;
    TileSize tileSize_ = TileSize::Medium;
    void applyTileSize();
    std::pair<int, int> visibleRange() const;  // proxy rows on screen [first, last] (last < first: none)
    void thumbPass();
    void slowPass();
    void applyDecoded(std::vector<ThumbDecoder::Result> results);
    void setRowIcon(QStandardItem* item, const QString& name, const QIcon& icon);
    void markStatus(QStandardItem* item);
    void evictIcons(int keepFrom, int keepTo);
    QString decoderKey(const QString& name);
};

}  // namespace iw8r::qt
