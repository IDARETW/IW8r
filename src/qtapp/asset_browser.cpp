#include "settings.h"
#include "asset_browser.h"

#include <QSplitter>

#include "flow_layout.h"
#include <QVariantMap>
#include <QSignalBlocker>
#include <QInputDialog>

#include <QComboBox>
#include <QDesktopServices>
#include <QDrag>
#include <QKeyEvent>
#include <QMimeData>
#include <QGuiApplication>
#include <QClipboard>
#include <QMenu>
#include <QColor>
#include <QPointer>
#include <QUrl>

#include <QCheckBox>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QHelpEvent>
#include <QListView>
#include <QPainter>
#include <QStyledItemDelegate>
#include <QToolTip>
#include <QPushButton>
#include <QScrollBar>
#include <QSortFilterProxyModel>
#include <QStandardItemModel>
#include <QTextStream>
#include <QTimer>
#include <QVBoxLayout>
#include <memory>
#include <set>
#include <thread>

#include "brush_tools.h"
#include "worker_guard.h"
#include "texture_decode.h"

namespace iw8r::qt {

namespace {

// Asset list items: tiles (materials: thumbnail fitted in its box, short name, folder in grey) or rows (icon + name,
// elided in the middle). The tooltip is the full name.
class AssetTileDelegate : public QStyledItemDelegate {
public:
    AssetTileDelegate(bool tiles, QObject* parent) : QStyledItemDelegate(parent), tiles_(tiles) {}
    void paint(QPainter* p, const QStyleOptionViewItem& opt, const QModelIndex& idx) const override {
        if (!tiles_) {
            QStyleOptionViewItem o = opt;
            o.textElideMode = Qt::ElideMiddle;
            QStyledItemDelegate::paint(p, o, idx);
            return;
        }
        p->save();
        const QRect r = opt.rect.adjusted(2, 2, -2, -2);
        if (opt.state & QStyle::State_Selected) p->fillRect(r, opt.palette.highlight());
        else if (opt.state & QStyle::State_MouseOver) p->fillRect(r, opt.palette.alternateBase());
        const QSize ic(kIcon, kIcon);  // fixed box: a thumbnail arriving later does not move the text
        const QRect iconBox(r.x() + (r.width() - ic.width()) / 2, r.y() + 3, ic.width(), ic.height());
        const QIcon icon = idx.data(Qt::DecorationRole).value<QIcon>();
        if (!icon.isNull()) icon.paint(p, iconBox, Qt::AlignCenter);  // scaled to fit, aspect kept
        const QString name = idx.data(Qt::DisplayRole).toString();
        const QString leaf = name.section('/', -1), folder = name.contains('/') ? name.section('/', 0, -2) : QString();
        const QFontMetrics fm(opt.font);
        const QRect line1(r.x() + 3, iconBox.bottom() + 3, r.width() - 6, fm.height());
        p->setPen(opt.state & QStyle::State_Selected ? opt.palette.highlightedText().color() : opt.palette.text().color());
        p->drawText(line1, Qt::AlignHCenter | Qt::AlignVCenter, fm.elidedText(leaf, Qt::ElideMiddle, line1.width()));
        if (!folder.isEmpty()) {
            QFont small = opt.font;
            small.setPointSizeF(std::max(6.0, small.pointSizeF() - 1));
            p->setFont(small);
            p->setPen(QColor(140, 145, 155));
            const QRect line2(line1.x(), line1.bottom() + 1, line1.width(), QFontMetrics(small).height());
            p->drawText(line2, Qt::AlignHCenter | Qt::AlignVCenter, QFontMetrics(small).elidedText(folder, Qt::ElideMiddle, line2.width()));
        }
        p->restore();
    }
    // Every tile is the same size (the grid cell), whatever its name or whether its thumbnail has loaded: a varying
    // size hint made the view re-flow (tiles per row changing) while scrolling.
    QSize sizeHint(const QStyleOptionViewItem& opt, const QModelIndex& idx) const override {
        if (tiles_) return QSize(kTileW, kTileH);
        // List rows no wider than the view: a long name is elided in the middle instead of scrolling the list sideways.
        QSize s = QStyledItemDelegate::sizeHint(opt, idx);
        if (const auto* v = qobject_cast<const QAbstractItemView*>(opt.widget)) s.setWidth(std::min(s.width(), std::max(60, v->viewport()->width() - 4)));
        return s;
    }
    // Narrow column: 124 px tiles (3 per row at about 400 px, 2 at 300); the grid widens each row to fill the view.
    static constexpr int kTileW = 124, kTileH = 104, kIcon = 64;
    bool helpEvent(QHelpEvent* e, QAbstractItemView* view, const QStyleOptionViewItem& opt, const QModelIndex& idx) override {
        if (e && e->type() == QEvent::ToolTip && idx.isValid()) {
            const QString tip = idx.data(Qt::ToolTipRole).toString();
            QToolTip::showText(e->globalPos(), tip.isEmpty() ? idx.data(Qt::DisplayRole).toString() : tip, view);
            return true;
        }
        return QStyledItemDelegate::helpEvent(e, view, opt, idx);
    }

private:
    bool tiles_;
};

// Materials: as many tile columns as fit the view at kTileW, each column widened to fill the row, so the grid never
// leaves an empty strip on the right and the columns follow the panel width.
class TileGridFit : public QObject {
public:
    explicit TileGridFit(QListView* view) : QObject(view), view_(view) {}

protected:
    bool eventFilter(QObject* obj, QEvent* e) override {
        if (e->type() == QEvent::Resize) {
            // A few pixels of slack: cells adding up to the full viewport width wrapped the last one (the list keeps a
            // small margin), so three columns showed as two.
            const int w = view_->viewport()->width() - 6;
            if (w > 0) {
                const int cols = std::max(1, w / AssetTileDelegate::kTileW);
                view_->setGridSize(QSize(w / cols, AssetTileDelegate::kTileH + 4));
            }
        }
        return QObject::eventFilter(obj, e);
    }

private:
    QListView* view_;
};

QDir repoData() {
    QDir d(QCoreApplication::applicationDirPath());
    for (int up = 0; up < 3; ++up, d.cdUp())
        if (QFileInfo::exists(d.filePath("data"))) return QDir(d.filePath("data"));
    return QDir(QCoreApplication::applicationDirPath());
}

// A filter proxy that can also restrict to a set of names (materials used in the open map).
class NameFilter : public QSortFilterProxyModel {
public:
    using QSortFilterProxyModel::QSortFilterProxyModel;
    std::set<QString> only;
    bool useOnly = false;
    std::set<QString> list;  // favourites or recent picks
    bool useList = false;
    std::set<QString> kit;   // names of the chosen kit
    bool useKit = false;
    void refresh() { invalidateFilter(); }

protected:
    bool filterAcceptsRow(int row, const QModelIndex& parent) const override {
        const QString name = sourceModel()->index(row, 0, parent).data(Qt::DisplayRole).toString();
        if (useOnly && !only.count(name)) return false;
        if (useList && !list.count(name)) return false;
        if (useKit && !kit.count(name)) return false;
        return QSortFilterProxyModel::filterAcceptsRow(row, parent);
    }
};

// The map's content as the "In this map" filter sees it (entity ids, item ids and revisions, the file): selection
// clicks also signal documentChanged but leave it unchanged.
quint64 contentSignature(const Document& doc) {
    quint64 h = 1469598103934665603ull;
    auto mix = [&h](quint64 v) { h = (h ^ v) * 1099511628211ull; };
    for (unsigned char c : doc.path()) mix(c);
    for (const Entity& e : doc.map().entities) {
        mix(e.id);
        for (const auto& item : e.items) mix(item.rev * 31 + item.id);
    }
    return h;
}

// focusListOnDown: Down in the search box hands the keyboard to the list.
class DownIntoList : public QObject {
public:
    DownIntoList(QAbstractItemView* list, QObject* parent) : QObject(parent), list_(list) {}

protected:
    bool eventFilter(QObject* watched, QEvent* e) override {
        if (e->type() == QEvent::KeyPress && list_) {
            const auto* ke = static_cast<QKeyEvent*>(e);
            if (ke->key() == Qt::Key_Down && (ke->modifiers() & ~Qt::KeypadModifier) == Qt::NoModifier) {
                list_->setFocus(Qt::TabFocusReason);
                if (!list_->currentIndex().isValid()) {
                    // Home in the list picks its first shown row the way the list itself does (hidden rows skipped).
                    QKeyEvent home(QEvent::KeyPress, Qt::Key_Home, Qt::NoModifier);
                    QCoreApplication::sendEvent(list_, &home);
                }
                return true;
            }
        }
        return QObject::eventFilter(watched, e);
    }

private:
    QPointer<QAbstractItemView> list_;
};

}  // namespace

void focusListOnDown(QLineEdit* search, QAbstractItemView* list) {
    if (search && list) search->installEventFilter(new DownIntoList(list, search));
}

AssetBrowser::AssetBrowser(EditorContext* ctx, Kind kind, QWidget* parent) : QWidget(parent), ctx_(ctx), kind_(kind) {
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(4, 4, 4, 4);
    lay->setSpacing(4);
    // Row 1: the search and the Apply / Place button; row 2: the options, wrapping to more lines in a narrow column.
    auto* top = new QHBoxLayout();
    auto* optionsBox = new QWidget(this);
    optionsBox->setObjectName("assetOptions");
    auto* options = new FlowLayout(optionsBox);
    search_ = new QLineEdit(this);
    search_->setObjectName(kind == Kind::Materials ? "materialSearch" : kind == Kind::Effects ? "effectSearch"
                           : kind == Kind::Sounds ? "soundSearch" : "modelSearch");
    // Short placeholders (the column is narrow); the longer hint is the tooltip.
    search_->setPlaceholderText(kind == Kind::Images ? "Search images" : kind == Kind::Effects ? "Search effects"
                                : kind == Kind::Sounds ? "Search sounds" : kind == Kind::Materials ? "Search materials"
                                                                                                  : "Search models");
    search_->setToolTip(kind == Kind::Images ? "50k stock images (colour _c, normal _n, sky_, ...)"
                        : kind == Kind::Effects ? "1020 stock effects that load in any map (e.g. fire, smoke, spark)"
                        : kind == Kind::Sounds ? "11k stock sound aliases that load in any map (amb_, emt_ = ambient loops)"
                        : kind == Kind::Materials ? "46k stock IW8 materials (e.g. concrete, brick, plaster)"
                                                  : "Stock IW8 models (e.g. crate, barrel, car)");
    search_->setClearButtonEnabled(true);
    top->addWidget(search_, 1);
    usedOnly_ = new QCheckBox("In this map", this);
    usedOnly_->setVisible(kind == Kind::Materials || kind == Kind::Models);
    options->addWidget(usedOnly_);
    listMode_ = new QComboBox(this);
    listMode_->setObjectName("assetListMode");
    listMode_->addItems({"All", "Favourites", "Recent"});
    // Tag entries (#tag) can be long: elided in the box instead of widening the options row.
    listMode_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    listMode_->setMinimumContentsLength(9);
    listMode_->setToolTip("Favourites: right-click an asset > Add to Favourites. Recent: the last 30 placed or applied. "
                          "#tag: assets you tagged (right-click > Tags...).");
    options->addWidget(listMode_);
    if (kind == Kind::Materials || kind == Kind::Models) {
        // Kits: stock families from tools/build_asset_kits.py (excluded_* kits are left out of the list), read with the
        // catalogue on the worker thread; the box is shown once applyCatalog() has filled it.
        kit_ = new QComboBox(this);
        kit_->setObjectName("assetKit");
        kit_->addItem("All kits");
        kit_->setToolTip("Stock families (containers, foliage, furniture, ...) from data/asset_catalog/iw8_kits.tsv");
        kit_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        kit_->setMinimumContentsLength(12);  // long kit names are elided, not widening the panel
        kit_->setVisible(false);
        options->addWidget(kit_);
    }
    // The availability catalog (data/asset_catalog/iw8_<kind>_status.tsv, tools: --asset-audit): assets that cannot be
    // exported from Replay are hidden unless this is unticked; materials without a colour image are tinted.
    buildableOnly_ = new QCheckBox("Buildable only", this);
    buildableOnly_->setChecked(true);
    buildableOnly_->setVisible(kind == Kind::Materials || kind == Kind::Models || kind == Kind::Effects || kind == Kind::Images);
    buildableOnly_->setToolTip("Hide stock assets the availability audit could not export from Replay");
    options->addWidget(buildableOnly_);
    connect(buildableOnly_, &QCheckBox::toggled, this, [this] { load(); });
    auto* use = new QPushButton(kind == Kind::Materials ? "Apply" : "Place", this);
    use->setToolTip(kind == Kind::Materials ? "Apply to the selection (Shift+M)"
                    : kind == Kind::Effects ? "Place an fx_origin with this effect at the view centre"
                    : kind == Kind::Sounds ? "Place a sound_emitter with this alias at the view centre"
                                            : "Place a static misc_model at the view centre");
    top->addWidget(use);
    use->setVisible(kind != Kind::Images);  // images are previewed only
    lay->addLayout(top);
    lay->addWidget(optionsBox);
    info_ = new QLabel(this);
    info_->setStyleSheet("color: gray");
    info_->setWordWrap(true);  // the summary wraps instead of widening the panel
    lay->addWidget(info_);
    // A list whose drags carry the asset (kind, name) so the camera and grid views can take the drop.
    class DragList : public QListView {
    public:
        DragList(QWidget* parent, const char* kind) : QListView(parent), kind_(kind) {}

    protected:
        void startDrag(Qt::DropActions) override {
            const QModelIndex idx = currentIndex();
            if (!idx.isValid()) return;
            auto* mime = new QMimeData;
            mime->setData(kAssetMimeType, (QString(kind_) + "\t" + idx.data(Qt::DisplayRole).toString() + "\t" +
                                           (idx.data(Qt::UserRole + 2).toBool() ? "1" : "0")).toUtf8());
            auto* drag = new QDrag(this);
            drag->setMimeData(mime);
            if (const QIcon icon = idx.data(Qt::DecorationRole).value<QIcon>(); !icon.isNull()) drag->setPixmap(icon.pixmap(64, 64));
            drag->exec(Qt::CopyAction);
        }

    private:
        const char* kind_;
    };
    const char* dragKind = kind_ == Kind::Materials ? "materials" : kind_ == Kind::Models ? "models"
                           : kind_ == Kind::Effects ? "effects" : kind_ == Kind::Sounds ? "sounds" : nullptr;
    view_ = dragKind ? new DragList(this, dragKind) : new QListView(this);
    if (dragKind) view_->setDragEnabled(true);
    view_->setUniformItemSizes(true);
    view_->setTextElideMode(Qt::ElideMiddle);  // long asset paths keep both ends
    view_->setItemDelegate(new AssetTileDelegate(kind == Kind::Materials, view_));
    if (kind == Kind::Models || kind == Kind::Images) view_->setIconSize(QSize(40, 40));
    if (kind == Kind::Materials) {
        // Tiles: the whole thumbnail fitted in its box, the short name, the folder below (owner: previews and names were
        // cut off). The full name is the tooltip.
        view_->setViewMode(QListView::IconMode);
        view_->setMovement(QListView::Static);  // icon mode's free movement let drags shuffle tiles
        view_->setFlow(QListView::LeftToRight);
        view_->setWrapping(true);
        view_->setIconSize(QSize(AssetTileDelegate::kIcon, AssetTileDelegate::kIcon));
        view_->setGridSize(QSize(AssetTileDelegate::kTileW + 4, AssetTileDelegate::kTileH + 4));
        view_->viewport()->installEventFilter(new TileGridFit(view_));  // columns follow the panel width
        view_->setResizeMode(QListView::Adjust);
        view_->setWordWrap(false);
        view_->setSpacing(0);
        view_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    }
    if (kind == Kind::Models || kind == Kind::Images) {
        // The list above, the preview under it (side by side took 256 px of the narrow column); a splitter between them.
        auto* split = new QSplitter(Qt::Vertical, this);
        split->setObjectName("assetPreviewSplit");
        split->setChildrenCollapsible(false);
        split->addWidget(view_);
        auto* sideBox = new QWidget(split);
        auto* side = new QVBoxLayout(sideBox);
        side->setContentsMargins(0, 0, 0, 0);
        preview_ = new QLabel(this);
        preview_->setObjectName("modelPreview");
        preview_->setMinimumSize(128, 128);
        preview_->setMaximumHeight(256);
        preview_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        preview_->setAlignment(Qt::AlignCenter);
        preview_->setStyleSheet("background: #202226; border: 1px solid #3a3d44");
        previewCaption_ = new QLabel("Select a model to preview it", this);
        previewCaption_->setWordWrap(true);
        previewCaption_->setStyleSheet("color: gray");
        // A long unbroken name clips instead of widening the column.
        previewCaption_->setMinimumWidth(0);
        previewCaption_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        side->addWidget(preview_);
        side->addWidget(previewCaption_);
        if (kind == Kind::Models) {
            // Narrow column: the caption and the 3D button share one row (the button no longer takes a full line of its
            // own), and the empty preview box is shorter than the Images one.
            preview_->setMaximumHeight(200);
            side->removeWidget(previewCaption_);
            auto* captionRow = new QHBoxLayout();
            captionRow->setContentsMargins(0, 0, 0, 0);
            auto* open3d = new QPushButton("Preview in 3D...", this);
            open3d->setObjectName("modelPreview3d");
            open3d->setToolTip("A textured 3D view of the model: orbit, pan and zoom, levels of detail, materials, collision hull");
            connect(open3d, &QPushButton::clicked, this, [this] {
                const QModelIndex idx = view_->currentIndex();
                if (idx.isValid()) emit previewRequested(idx.data(Qt::DisplayRole).toString());
            });
            captionRow->addWidget(previewCaption_, 1);
            captionRow->addWidget(open3d, 0, Qt::AlignTop);
            side->addLayout(captionRow);
        }
        split->addWidget(sideBox);
        split->setStretchFactor(0, 3);
        split->setStretchFactor(1, 2);
        if (kind == Kind::Models) split->setStretchFactor(1, 1);  // the list gets the spare height, not blank preview space
        lay->addWidget(split, 1);
        if (kind == Kind::Images) {
            // Images only: the caption says "image", and its text may not widen the narrow column (a long unbroken name did).
            previewCaption_->setText("Select an image to preview it");
            previewCaption_->setMinimumWidth(0);
            previewCaption_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        }
    } else {
        lay->addWidget(view_, 1);
    }
    model_ = new QStandardItemModel(this);
    auto* filter = new NameFilter(this);
    proxy_ = filter;
    proxy_->setSourceModel(model_);
    proxy_->setFilterCaseSensitivity(Qt::CaseInsensitive);
    view_->setModel(proxy_);

    connect(search_, &QLineEdit::textChanged, this, [this](const QString& t) { setFilter(t); });
    connect(listMode_, &QComboBox::currentIndexChanged, this, [this] { applyListMode(); });
    refillListModes();  // tag list modes (4.22)
    if (kit_) connect(kit_, &QComboBox::currentIndexChanged, this, [this] { setKit(kit_->currentData().toString()); });
    connect(usedOnly_, &QCheckBox::toggled, this, [this, filter](bool on) {
        filter->useOnly = on;
        refreshUsedFilter();
    });
    auto choose = [this](const QModelIndex& idx) {
        if (!idx.isValid()) return;
        const QString name = idx.data(Qt::DisplayRole).toString();
        noteRecent(name);
        if (kind_ == Kind::Materials) emit materialChosen(name);
        else if (kind_ == Kind::Effects) emit effectChosen(name);
        else if (kind_ == Kind::Sounds) emit soundChosen(name, idx.data(Qt::UserRole + 2).toBool());
        else emit modelChosen(name);
    };
    // Double-click or Enter (activated covers both, so a double-click places once).
    connect(view_, &QListView::activated, this, choose);
    focusListOnDown(search_, view_);
    {
        // Images are previewed only: their menu has no Apply / Place, Info or usage entries.
        view_->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(view_, &QListView::customContextMenuRequested, this, [this, choose](const QPoint& at) {
            const QModelIndex idx = view_->indexAt(at);
            if (!idx.isValid()) return;
            const QString name = idx.data(Qt::DisplayRole).toString();
            QMenu menu(this);
            if (kind_ != Kind::Images)
                menu.addAction(kind_ == Kind::Materials ? "Apply to Selection" : "Place", this, [choose, idx] { choose(idx); });
            if (kind_ == Kind::Models) menu.addAction("Preview in 3D...", this, [this, name] { emit previewRequested(name); });
            if (kind_ == Kind::Materials || kind_ == Kind::Models || kind_ == Kind::Effects)
                menu.addAction(kind_ == Kind::Materials ? "Material Info..." : kind_ == Kind::Models ? "Model Info..." : "Effect Info...", this,
                               [this, name] { emit infoRequested(name); });
            const bool fav = isFavourite(name);
            menu.addAction(fav ? "Remove from Favourites" : "Add to Favourites", this, [this, name, fav] { setFavourite(name, !fav); });
            menu.addAction(QString("Tags... (%1)").arg(tagsOf(name).isEmpty() ? QString("none") : tagsOf(name).join(", ")), this, [this, name] {
                bool ok = false;
                const QString t = QInputDialog::getText(this, "Tags", "Tags for " + name + " (comma separated):", QLineEdit::Normal,
                                                        tagsOf(name).join(", "), &ok);
                if (ok) setTags(name, t.split(',', Qt::SkipEmptyParts));
            });
            if (kind_ != Kind::Images) {
                const int uses = usageCount(name);
                QAction* u = menu.addAction(QString("Used %1 time(s) in this map").arg(uses));
                u->setEnabled(false);
                if (uses > 0 && (kind_ == Kind::Materials || kind_ == Kind::Models))
                    menu.addAction("Replace Everywhere With...", this, [this, name] { emit replaceRequested(name); });
            }
            menu.addAction("Copy Name", this, [name] { QGuiApplication::clipboard()->setText(name); });
            // The only file a row knows: a material's colour image (exported stock assets, or a custom material's).
            if (const QString file = imageFiles_.value(name); !file.isEmpty() && QFileInfo::exists(file))
                menu.addAction("Show Image in Folder", this,
                               [file] { QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(file).absolutePath())); });
            menu.exec(view_->viewport()->mapToGlobal(at));
        });
    }
    if (kind_ == Kind::Models || kind_ == Kind::Images)
        connect(view_->selectionModel(), &QItemSelectionModel::currentChanged, this, [this](const QModelIndex& idx) {
            if (!idx.isValid()) return;
            if (kind_ == Kind::Images) emit imageHighlighted(idx.data(Qt::DisplayRole).toString());
            else emit modelHighlighted(idx.data(Qt::DisplayRole).toString());
        });
    connect(use, &QPushButton::clicked, this, [this, choose] { choose(view_->currentIndex()); });
    connect(ctx_, &EditorContext::documentChanged, this, [this, filter] {
        // Collected again only when the map's content changed: selection clicks signal a change too, and walking every
        // face of a large map and refiltering 46k rows for a click is wasted work.
        if (filter->useOnly && contentSignature(ctx_->doc) != usedSignature_) refreshUsedFilter();
    });
    // Thumbnails for the visible rows, decoded lazily after scrolling or filtering settles.
    auto* thumbs = new QTimer(this);
    thumbs_ = thumbs;
    thumbs->setSingleShot(true);
    thumbs->setInterval(120);
    connect(thumbs, &QTimer::timeout, this, [this] {
        if (kind_ == Kind::Effects || kind_ == Kind::Sounds) return;  // effects and sounds have no image
        const QRect vis = view_->viewport()->rect();
        int budget = 48;  // rows decoded per pass (rendering a model or decoding a DDS costs a few ms)
        // Start at the row under the top-left of the viewport and stop below its bottom: the list has up to 46k
        // rows, and asking every one for its rectangle on each pass (each scroll step, each cache answer) is what
        // made scrolling stutter. Rows are laid out top to bottom in both view modes.
        int r = 0;
        for (const QPoint probe : {QPoint(4, 4), QPoint(vis.width() / 2, 4), QPoint(4, vis.height() / 2)})
            if (const QModelIndex at = view_->indexAt(probe); at.isValid()) {
                r = std::max(0, at.row() - 64);  // a few rows back: the probe may sit below a partly visible row
                break;
            }
        for (; r < proxy_->rowCount() && budget > 0; ++r) {
            const QModelIndex idx = proxy_->index(r, 0);
            const QRect rect = view_->visualRect(idx);
            if (rect.top() > vis.bottom()) break;
            if (!rect.intersects(vis)) continue;
            QStandardItem* item = model_->itemFromIndex(proxy_->mapToSource(idx));
            if (!item || (!item->icon().isNull() && !pendingThumbs_.contains(item->text()))) continue;
            item->setIcon(thumbnail(item->text()));
            if (statusSource_) {
                const QString why = statusSource_(item->text());
                if (!why.isEmpty() && !unavailable_.contains(item->text())) {
                    unavailable_.insert(item->text());
                    item->setForeground(QColor(130, 130, 130));
                    item->setToolTip(item->toolTip() + "\nNOT AVAILABLE in a build: " + why.left(300));
                }
            }
            // A row that is still waiting for its cache costs a lookup only: every visible row is asked for in
            // the first pass, so the caches get one large request instead of 48 names at a time.
            if (!pendingThumbs_.contains(item->text())) --budget;
        }
        if (budget <= 0) thumbs_->start();  // more visible rows are waiting: continue after the event loop has run
    });
    connect(view_->verticalScrollBar(), &QScrollBar::valueChanged, thumbs, qOverload<>(&QTimer::start));
    connect(proxy_, &QSortFilterProxyModel::layoutChanged, thumbs, qOverload<>(&QTimer::start));
    connect(proxy_, &QSortFilterProxyModel::modelReset, thumbs, qOverload<>(&QTimer::start));
    load();
    thumbs->start();
}

// The catalogue as the worker thread read it: plain values, made into list rows on the GUI thread (applyCatalog).
struct AssetBrowser::Catalog {
    struct Row {
        QString name, tip;
        int tone = 0;  // 0 normal, 1 builds grey (no colour image), 2 not available in a build, 3 a looping sound
        bool loop = false;
    };
    std::vector<Row> rows;
    int hiddenStreamOnly = 0, hiddenMissing = 0;
    QString fileName;
    QHash<QString, QStringList> kits;
    QHash<QString, QString> imageFiles;
};

// Worker thread: reads files only (no widget, no QCoreApplication call: `dataDir` comes from the GUI thread).
std::shared_ptr<AssetBrowser::Catalog> AssetBrowser::readCatalog(Kind kind, bool buildableOnly, const QString& dataDir, bool wantKits) {
    auto c = std::make_shared<Catalog>();
    const QDir data(dataDir);
    // Availability catalog: name -> (status, reason).
    QHash<QString, QPair<QString, QString>> status;
    if (kind == Kind::Materials || kind == Kind::Models || kind == Kind::Effects || kind == Kind::Images) {
        // Every status file of the kind (an audit can run as two halves: iw8_<kind>_status.tsv and ..._status_b.tsv).
        const QDir cat(data.filePath("asset_catalog"));
        const QString pattern = kind == Kind::Materials ? "iw8_materials_status*.tsv"
                                : kind == Kind::Effects ? "iw8_effects_status*.tsv"
                                : kind == Kind::Images  ? "iw8_images_status*.tsv"
                                                        : "iw8_models_status*.tsv";
        for (const QString& fn : cat.entryList({pattern}, QDir::Files)) {
            QFile sf(cat.filePath(fn));
            if (!sf.open(QIODevice::ReadOnly | QIODevice::Text)) continue;
            QTextStream in(&sf);
            while (!in.atEnd()) {
                const QString l = in.readLine();
                if (l.startsWith('#')) continue;
                const QStringList f = l.split('\t');
                if (f.size() >= 2 && f[1] != "ok") status.insert(f[0], {f[1], f.value(2)});
            }
        }
    }
    const QString file = data.filePath(kind == Kind::Materials ? "asset_catalog/iw8_materials.tsv"
                                       : kind == Kind::Effects ? "asset_catalog/iw8_common_vfx.tsv"
                                       : kind == Kind::Images  ? "asset_catalog/iw8_images.tsv"
                                       : kind == Kind::Sounds  ? "asset_catalog/iw8_common_sounds.tsv"
                                                               : "asset_catalog/iw8_models.tsv");
    c->fileName = QFileInfo(file).fileName();
    QFile f(file);
    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&f);
        in.readLine();  // header (names themselves may start with '#')
        while (!in.atEnd()) {
            const QString line = in.readLine();
            const int tab = line.indexOf('\t');
            if (tab <= 0) continue;
            Catalog::Row row;
            row.name = line.left(tab);
            if (kind == Kind::Sounds) {
                // alias<TAB>loop<TAB>distMax<TAB>zones
                const QStringList s = line.split('\t');
                if (line.startsWith('#') || s.size() < 4) continue;
                row.loop = s[1] == "1";
                row.tip = QString("%1\n%2, audible to %3 units\nzones: %4").arg(s[0], row.loop ? "loop" : "one-shot", s[2], s[3]);
                row.tone = row.loop ? 3 : 0;
                c->rows.push_back(std::move(row));
                continue;
            }
            if (kind == Kind::Models) {
                // Weapons, attachments and cosmetics that only the streaming zone lists (8.9k of 15.6k) are not map
                // props and ACTS cannot export them from there: left out so every listed model can be placed and built.
                bool streamOnly = true;
                for (const QString& z : line.mid(tab + 1).split(',', Qt::SkipEmptyParts))
                    streamOnly &= z.trimmed() == "global_stream_mp" || z.trimmed() == "techsets_global_stream_mp";
                if (streamOnly) {
                    ++c->hiddenStreamOnly;
                    continue;
                }
            }
            if (const auto st = status.constFind(row.name); st != status.constEnd()) {
                if (st->first == "missing" && buildableOnly) {
                    ++c->hiddenMissing;
                    continue;
                }
                row.tone = st->first == "grey" ? 1 : 2;
                row.tip = row.name + "\nzones: " + line.mid(tab + 1).left(300) +
                          (st->first == "grey" ? "\nBUILDS GREY: no colour image in Replay (" : "\nNOT AVAILABLE in a build (") +
                          st->second.left(200) + ")";
                c->rows.push_back(std::move(row));
                continue;
            }
            row.tip = row.name + "\nzones: " + line.mid(tab + 1).left(300);
            c->rows.push_back(std::move(row));
        }
    }
    // Colour images from ACTS exports (data/stock_assets/<zone>/index.json).
    if (kind == Kind::Materials) {
        const QDir assets(data.filePath("stock_assets"));
        for (const QString& zone : assets.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            QFile idx(assets.filePath(zone + "/index.json"));
            if (!idx.open(QIODevice::ReadOnly)) continue;
            const QJsonArray mats = QJsonDocument::fromJson(idx.readAll()).object().value("materials").toArray();
            for (const auto& v : mats) {
                const QJsonObject m = v.toObject();
                const QString img = m.value("color_image_file").toString();
                if (!img.isEmpty()) c->imageFiles.insert(m.value("name").toString(), assets.filePath(zone + "/" + img));
            }
        }
    }
    if (wantKits) {
        QFile kf(data.filePath("asset_catalog/iw8_kits.tsv"));
        const QString want = kind == Kind::Models ? "model" : "material";
        if (kf.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream in(&kf);
            while (!in.atEnd()) {
                const QStringList k = in.readLine().split('\t');
                if (k.size() < 3 || k[0].startsWith('#') || k[0] == "kit" || k[1] != want || k[0].startsWith("excluded")) continue;
                c->kits[k[0]] << k[2];
            }
        }
    }
    return c;
}

void AssetBrowser::load() {
    const int gen = ++loadGen_;
    const bool buildable = buildableOnly_ && buildableOnly_->isChecked();
    const QString dataDir = repoData().absolutePath();
    const bool wantKits = kit_ && kitNames_.isEmpty();
    const Kind kind = kind_;
    if (model_->rowCount() == 0) info_->setText("Reading the catalogue...");
    QPointer<AssetBrowser> self(this);
    std::thread(guardedWorker(__FILE__, __LINE__, [self, gen, kind, buildable, dataDir, wantKits] {
        std::shared_ptr<const Catalog> cat = readCatalog(kind, buildable, dataDir, wantKits);
        QMetaObject::invokeMethod(qApp, [self, gen, cat] {
            if (self && gen == self->loadGen_) self->applyCatalog(*cat);  // a newer load() replaces this one
        }, Qt::QueuedConnection);
    })).detach();
}

QStandardItem* AssetBrowser::customItem(const QString& name, const QString& image) const {
    auto* item = new QStandardItem(name);
    item->setEditable(false);
    item->setData(true, Qt::UserRole + 1);
    item->setToolTip(name + "\ncustom material: " + image);
    return item;
}

void AssetBrowser::applyCatalog(const Catalog& c) {
    loadedGen_ = loadGen_;
    hiddenStreamOnly_ = c.hiddenStreamOnly;
    hiddenMissing_ = c.hiddenMissing;
    QList<QStandardItem*> items;
    items.reserve(qsizetype(customMaterials_.size()) + qsizetype(c.rows.size()));
    for (const auto& [name, image] : customMaterials_) items.append(customItem(name, image));  // the custom ones first
    for (const Catalog::Row& r : c.rows) {
        auto* item = new QStandardItem(r.name);
        item->setEditable(false);
        item->setToolTip(r.tip);
        if (kind_ == Kind::Sounds) item->setData(r.loop, Qt::UserRole + 2);
        if (r.tone == 1) item->setForeground(QColor(200, 160, 90));
        else if (r.tone == 2) item->setForeground(QColor(130, 130, 130));
        else if (r.tone == 3) item->setForeground(QColor(120, 200, 255));
        items.append(item);
    }
    model_->clear();
    model_->invisibleRootItem()->appendRows(items);  // one insert (a row at a time made the proxy filter each)
    for (auto it = c.imageFiles.constBegin(); it != c.imageFiles.constEnd(); ++it) imageFiles_.insert(it.key(), it.value());
    if (kit_ && kitNames_.isEmpty() && !c.kits.isEmpty()) {
        kitNames_ = c.kits;
        QStringList names = kitNames_.keys();
        names.sort();
        const QSignalBlocker block(kit_);
        for (const QString& k : names) kit_->addItem(QString(k).replace('_', ' '), k);
        kit_->setVisible(true);
    }
    if (kind_ == Kind::Effects) {
        // Narrow column: the summary drops the source file name (the search tooltip says what the list is), so it stays on one line.
        catalogInfo_ = QString("%1 stock effects (Replay 1.20)").arg(c.rows.size())
                       + (hiddenMissing_ ? QString("; %1 not buildable hidden").arg(hiddenMissing_) : QString());
    } else {
        catalogInfo_ = (QString("%1 stock %2 (Replay 1.20, %3)%4")
                           .arg(c.rows.size())
                           .arg(kind_ == Kind::Materials ? "materials" : kind_ == Kind::Images ? "images"
                                : kind_ == Kind::Sounds ? "sound aliases (blue = loop)" : "models")
                           .arg(c.fileName)
                           .arg(kind_ == Kind::Materials ? QString(", %1 with thumbnails").arg(imageFiles_.size())
                                : kind_ == Kind::Models && hiddenStreamOnly_
                                    ? QString("; %1 weapon/attachment models of the streaming zone not listed").arg(hiddenStreamOnly_)
                                    : QString())
                           + (hiddenMissing_ ? QString("; %1 not buildable hidden").arg(hiddenMissing_) : QString()));
    }
    updateInfo();
    emit catalogReady();
}

void AssetBrowser::setCustomMaterials(const QList<QPair<QString, QString>>& materials) {
    customMaterials_ = materials;  // kept for a catalogue (re)load that lands later
    // Drop the previous custom rows (flagged with UserRole + 1), then insert the new ones at the top.
    for (int r = model_->rowCount() - 1; r >= 0; --r)
        if (model_->item(r)->data(Qt::UserRole + 1).toBool()) model_->removeRow(r);
    for (int i = materials.size() - 1; i >= 0; --i) {
        model_->insertRow(0, customItem(materials[i].first, materials[i].second));
        imageFiles_.insert(materials[i].first, materials[i].second);
        icons_.remove(materials[i].first);
    }
    updateInfo();
}

QIcon AssetBrowser::thumbnail(const QString& name) {
    if (auto it = icons_.find(name); it != icons_.end()) return *it;
    QIcon icon;
    const QString file = imageFiles_.value(name);
    if (file.isEmpty() && thumbSource_) {
        bool pending = false;
        const QImage img = thumbSource_(name, &pending);
        if (!img.isNull()) {
            pendingThumbs_.remove(name);
            realThumbs_.insert(name);
            icon = QIcon(QPixmap::fromImage(img.scaled(64, 64, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
            icons_.insert(name, icon);
            return icon;
        }
        if (pending) pendingThumbs_.insert(name);
        else pendingThumbs_.remove(name);
    }
    if (!file.isEmpty() && !file.endsWith(".dds", Qt::CaseInsensitive)) {
        QImage q(file);  // custom PNG / TGA / JPG
        if (!q.isNull()) icon = QIcon(QPixmap::fromImage(q.scaled(64, 64, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
    } else if (!file.isEmpty()) {
        // The mip level a 64-texel icon needs, not the whole image (texture_decode.h).
        app::Rgba8Image small;
        if (app::loadDdsPreview(file.toStdString(), 64, small)) {
            const QImage q(small.pixels.data(), small.width, small.height, small.width * 4, QImage::Format_RGBA8888);
            icon = QIcon(QPixmap::fromImage(q.copy()));
        }
    }
    if (!icon.isNull()) realThumbs_.insert(name);
    if (icon.isNull()) {
        // Placeholder: the material's hashed viewport colour, so the swatch matches the 3D view.
        const uint32_t c = app::colorForName(name.toStdString());
        QPixmap px(64, 64);
        px.fill(QColor(int(c & 0xff), int((c >> 8) & 0xff), int((c >> 16) & 0xff)));
        icon = QIcon(px);
    }
    if (!pendingThumbs_.contains(name)) icons_.insert(name, icon);  // pending: asked again on refreshThumbnails()
    return icon;
}

void AssetBrowser::refreshThumbnails() {
    for (const QString& n : pendingThumbs_) icons_.remove(n);
    if (thumbs_) thumbs_->start();
}

int AssetBrowser::thumbnailsLoaded() const { return int(realThumbs_.size()); }

void AssetBrowser::refreshUsedFilter() {
    auto* filter = static_cast<NameFilter*>(proxy_);
    usedSignature_ = contentSignature(ctx_->doc);
    filter->only.clear();
    for (const auto& e : ctx_->doc.map().entities) {
        if (kind_ == Kind::Models) {
            if (const std::string* m = e.value("model"); m && !m->empty()) filter->only.insert(QString::fromStdString(*m));
            continue;
        }
        for (const auto& it : e.items)
            if (const Primitive* p = it.prim()) {
                if (p->kind == PrimKind::Patch) filter->only.insert(QString::fromStdString(p->patchMaterial));
                for (const auto& face : p->faces) filter->only.insert(QString::fromStdString(face.material()));
            }
    }
    filter->refresh();
    updateInfo();
}

void AssetBrowser::updateInfo() {
    const int shown = proxy_->rowCount(), total = model_->rowCount();
    if (shown == total) {
        info_->setText(catalogInfo_);
        return;
    }
    // What narrows the list, so an empty list says why it is empty.
    QStringList by;
    if (!filterText_.isEmpty()) by << "\"" + filterText_ + "\"";
    if (listMode_ && listMode_->currentIndex() != 0) by << listMode_->currentText();
    if (kit_ && kit_->currentIndex() > 0) by << "kit " + kit_->currentText();
    if (usedOnly_->isChecked()) by << "in this map";
    const QString why = by.isEmpty() ? QString() : " (" + by.join(", ") + ")";
    info_->setText(shown == 0 ? QString("No matches for %1 among %2").arg(by.isEmpty() ? QString("the filters") : by.join(", ")).arg(total)
                              : QString("%1 of %2 shown%3").arg(shown).arg(total).arg(why));
}

int AssetBrowser::usageCount(const QString& qname) const {
    const std::string name = qname.toStdString();
    int n = 0;
    for (const auto& e : ctx_->doc.map().entities) {
        if (kind_ == Kind::Materials) {
            for (const auto& it : e.items)
                if (const Primitive* p = it.prim()) {
                    if (p->kind == PrimKind::Patch && p->patchMaterial == name) ++n;
                    for (const auto& face : p->faces) n += face.material() == name;
                }
            continue;
        }
        const char* key = kind_ == Kind::Models ? "model" : kind_ == Kind::Effects ? "fx" : "soundalias";
        if (const std::string* v = e.value(key); v && *v == name) ++n;
    }
    return n;
}

QStringList AssetBrowser::tagsOf(const QString& name) const {
    return Settings::instance().value(settingsKey("tags")).toMap().value(name).toStringList();
}

void AssetBrowser::setTags(const QString& name, const QStringList& tags) {
    QVariantMap all = Settings::instance().value(settingsKey("tags")).toMap();
    QStringList clean;
    for (QString t : tags) {
        t = t.trimmed().toLower();
        if (!t.isEmpty() && !clean.contains(t)) clean << t;
    }
    if (clean.isEmpty()) all.remove(name);
    else all.insert(name, clean);
    Settings::instance().setValue(settingsKey("tags"), all);
    refillListModes();
}

QStringList AssetBrowser::allTags() const {
    QStringList out;
    const QVariantMap all = Settings::instance().value(settingsKey("tags")).toMap();
    for (auto it = all.begin(); it != all.end(); ++it)
        for (const QString& t : it.value().toStringList())
            if (!out.contains(t)) out << t;
    out.sort();
    return out;
}

void AssetBrowser::refillListModes() {
    if (!listMode_) return;
    const QString cur = listMode_->currentText();
    QSignalBlocker block(listMode_);
    while (listMode_->count() > 3) listMode_->removeItem(3);
    for (const QString& t : allTags()) listMode_->addItem("#" + t);
    const int i = listMode_->findText(cur);
    listMode_->setCurrentIndex(i >= 0 ? i : 0);
    applyListMode();
}

void AssetBrowser::showTag(const QString& tag) {
    refillListModes();
    const int i = listMode_ ? listMode_->findText("#" + tag.toLower()) : -1;
    if (i >= 0) listMode_->setCurrentIndex(i);
    applyListMode();
}

void AssetBrowser::setFilter(const QString& text) {
    filterText_ = text;
    proxy_->setFilterFixedString(text);
    updateInfo();
}

QString AssetBrowser::settingsKey(const char* what) const {
    const char* k = kind_ == Kind::Materials ? "materials" : kind_ == Kind::Models ? "models" : kind_ == Kind::Effects ? "effects"
                    : kind_ == Kind::Sounds ? "sounds" : "images";
    return QString("assets/%1/%2").arg(k, what);
}

bool AssetBrowser::isFavourite(const QString& name) const {
    return Settings::instance().value(settingsKey("favourites")).toStringList().contains(name);
}

void AssetBrowser::setFavourite(const QString& name, bool on) {
    QStringList f = Settings::instance().value(settingsKey("favourites")).toStringList();
    f.removeAll(name);
    if (on) f.prepend(name);
    Settings::instance().setValue(settingsKey("favourites"), f);
    applyListMode();
}

QStringList AssetBrowser::recent() const { return Settings::instance().value(settingsKey("recent")).toStringList(); }

void AssetBrowser::noteRecent(const QString& name) {
    QStringList r = recent();
    r.removeAll(name);
    r.prepend(name);
    while (r.size() > 30) r.removeLast();
    Settings::instance().setValue(settingsKey("recent"), r);
    if (listMode_ && listMode_->currentIndex() == 2) applyListMode();
}

QStringList AssetBrowser::kits() const {
    QStringList k = kitNames_.keys();
    k.sort();
    return k;
}

void AssetBrowser::setKit(const QString& kit) {
    auto* filter = static_cast<NameFilter*>(proxy_);
    filter->useKit = !kit.isEmpty() && kitNames_.contains(kit);
    filter->kit.clear();
    if (filter->useKit)
        for (const QString& n : kitNames_.value(kit)) filter->kit.insert(n);
    if (kit_ && kit_->currentData().toString() != kit) {
        const int i = kit_->findData(kit);
        kit_->blockSignals(true);
        kit_->setCurrentIndex(i < 0 ? 0 : i);
        kit_->blockSignals(false);
    }
    filter->refresh();
    updateInfo();
}

void AssetBrowser::setListMode(ListMode m) { listMode_->setCurrentIndex(int(m)); applyListMode(); }

void AssetBrowser::applyListMode() {
    auto* filter = static_cast<NameFilter*>(proxy_);
    const int mode = listMode_ ? listMode_->currentIndex() : 0;
    filter->useList = mode != 0;
    filter->list.clear();
    QStringList names = mode == 1 ? Settings::instance().value(settingsKey("favourites")).toStringList() : mode == 2 ? recent() : QStringList();
    if (mode >= 3) {  // a tag
        const QString tag = listMode_->currentText().mid(1);
        const QVariantMap all = Settings::instance().value(settingsKey("tags")).toMap();
        for (auto it = all.begin(); it != all.end(); ++it)
            if (it.value().toStringList().contains(tag)) names << it.key();
    }
    filter->list.insert(names.begin(), names.end());
    filter->refresh();
    updateInfo();
}
int AssetBrowser::assetCount() const { return model_->rowCount(); }
int AssetBrowser::visibleCount() const { return proxy_->rowCount(); }
QString AssetBrowser::visibleName(int row) const { return proxy_->index(row, 0).data(Qt::DisplayRole).toString(); }

void AssetBrowser::setPreview(const QImage& image, const QString& caption) {
    if (!preview_) return;
    // Fitted into the preview box (keeping its shape) instead of shown at full size and cut off.
    QPixmap pm = image.isNull() ? QPixmap() : QPixmap::fromImage(image);
    if (!pm.isNull() && (pm.width() > preview_->width() - 4 || pm.height() > preview_->height() - 4))
        pm = pm.scaled(preview_->size() - QSize(4, 4), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    preview_->setPixmap(pm);
    if (kind_ == Kind::Images) {
        // Images only: the image name (the first line) is elided in the middle to the column width; the full caption is the
        // tooltip. The text itself stays whole, so previewCaption() still returns it.
        const QStringList lines = caption.split('\n');
        const int width = std::max(120, previewCaption_->width() - 8);
        previewCaption_->setText(lines.isEmpty() ? QString()
                                 : QFontMetrics(previewCaption_->font()).elidedText(lines.first(), Qt::ElideMiddle, width) + caption.mid(lines.first().size()));
        previewCaption_->setToolTip(caption);
        return;
    }
    previewCaption_->setText(caption);
}

QString AssetBrowser::previewCaption() const { return previewCaption_ ? previewCaption_->text() : QString(); }

bool AssetBrowser::previewHasImage() const { return preview_ && !preview_->pixmap().isNull(); }

void AssetBrowser::selectRow(int row) { view_->setCurrentIndex(proxy_->index(row, 0)); }

}  // namespace iw8r::qt
