#include "settings.h"
#include "asset_browser.h"
#include <QVariantMap>
#include <QSignalBlocker>
#include <QInputDialog>

#include <QComboBox>
#include <QDrag>
#include <QMimeData>
#include <QGuiApplication>
#include <QClipboard>
#include <QMenu>
#include <QColor>

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
#include <set>

#include "brush_tools.h"
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
        return tiles_ ? QSize(kTileW, kTileH) : QStyledItemDelegate::sizeHint(opt, idx);
    }
    static constexpr int kTileW = 152, kTileH = 112, kIcon = 72;
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

}  // namespace

AssetBrowser::AssetBrowser(EditorContext* ctx, Kind kind, QWidget* parent) : QWidget(parent), ctx_(ctx), kind_(kind) {
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(4, 4, 4, 4);
    auto* top = new QHBoxLayout();
    search_ = new QLineEdit(this);
    search_->setObjectName(kind == Kind::Materials ? "materialSearch" : kind == Kind::Effects ? "effectSearch"
                           : kind == Kind::Sounds ? "soundSearch" : "modelSearch");
    search_->setPlaceholderText(kind == Kind::Images ? "Search 50k stock images (colour _c, normal _n, sky_, ...)"
                                : kind == Kind::Effects ? "Search 1020 stock effects that load in any map (e.g. fire, smoke, spark)"
                                : kind == Kind::Sounds ? "Search 11k stock sound aliases that load in any map (amb_, emt_ = ambient loops)"
                                : kind == Kind::Materials ? "Search 46k stock IW8 materials (e.g. concrete, brick, plaster)"
                                                        : "Search stock IW8 models (e.g. crate, barrel, car)");
    search_->setClearButtonEnabled(true);
    top->addWidget(search_, 1);
    usedOnly_ = new QCheckBox("In this map", this);
    usedOnly_->setVisible(kind == Kind::Materials || kind == Kind::Models);
    top->addWidget(usedOnly_);
    listMode_ = new QComboBox(this);
    listMode_->setObjectName("assetListMode");
    listMode_->addItems({"All", "Favourites", "Recent"});
    listMode_->setToolTip("Favourites: right-click an asset > Add to Favourites. Recent: the last 30 placed or applied. "
                          "#tag: assets you tagged (right-click > Tags...).");
    top->addWidget(listMode_);
    if (kind == Kind::Materials || kind == Kind::Models) {
        // Kits: stock families from tools/build_asset_kits.py (excluded_* kits are left out of the list).
        QFile kf(repoData().filePath("asset_catalog/iw8_kits.tsv"));
        const QString want = kind == Kind::Models ? "model" : "material";
        if (kf.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream in(&kf);
            while (!in.atEnd()) {
                const QStringList c = in.readLine().split('\t');
                if (c.size() < 3 || c[0].startsWith('#') || c[0] == "kit" || c[1] != want || c[0].startsWith("excluded")) continue;
                kitNames_[c[0]] << c[2];
            }
        }
        if (!kitNames_.isEmpty()) {
            kit_ = new QComboBox(this);
            kit_->setObjectName("assetKit");
            QStringList names = kitNames_.keys();
            names.sort();
            kit_->addItem("All kits");
            for (const QString& k : names) kit_->addItem(QString(k).replace('_', ' '), k);
            kit_->setToolTip("Stock families (containers, foliage, furniture, ...) from data/asset_catalog/iw8_kits.tsv");
            top->addWidget(kit_);
        }
    }
    // The availability catalog (data/asset_catalog/iw8_<kind>_status.tsv, tools: --asset-audit): assets that cannot be
    // exported from Replay are hidden unless this is unticked; materials without a colour image are tinted.
    buildableOnly_ = new QCheckBox("Buildable only", this);
    buildableOnly_->setChecked(true);
    buildableOnly_->setVisible(kind == Kind::Materials || kind == Kind::Models || kind == Kind::Effects || kind == Kind::Images);
    buildableOnly_->setToolTip("Hide stock assets the availability audit could not export from Replay");
    top->addWidget(buildableOnly_);
    connect(buildableOnly_, &QCheckBox::toggled, this, [this] { load(); });
    auto* use = new QPushButton(kind == Kind::Materials ? "Apply" : "Place", this);
    use->setToolTip(kind == Kind::Materials ? "Apply to the selection (Shift+M)"
                    : kind == Kind::Effects ? "Place an fx_origin with this effect at the view centre"
                    : kind == Kind::Sounds ? "Place a sound_emitter with this alias at the view centre"
                                            : "Place a static misc_model at the view centre");
    top->addWidget(use);
    use->setVisible(kind != Kind::Images);  // images are previewed only
    lay->addLayout(top);
    info_ = new QLabel(this);
    info_->setStyleSheet("color: gray");
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
        view_->setResizeMode(QListView::Adjust);
        view_->setWordWrap(false);
        view_->setSpacing(0);
        view_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    }
    if (kind == Kind::Models || kind == Kind::Images) {
        // List and preview side by side.
        auto* row = new QHBoxLayout();
        row->addWidget(view_, 1);
        auto* side = new QVBoxLayout();
        preview_ = new QLabel(this);
        preview_->setObjectName("modelPreview");
        preview_->setFixedSize(256, 256);
        preview_->setAlignment(Qt::AlignCenter);
        preview_->setStyleSheet("background: #202226; border: 1px solid #3a3d44");
        previewCaption_ = new QLabel("Select a model to preview it", this);
        previewCaption_->setWordWrap(true);
        previewCaption_->setFixedWidth(256);
        previewCaption_->setStyleSheet("color: gray");
        side->addWidget(preview_);
        side->addWidget(previewCaption_);
        side->addStretch(1);
        row->addLayout(side);
        lay->addLayout(row, 1);
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
    connect(view_, &QListView::doubleClicked, this, choose);
    if (kind_ != Kind::Images) {
        view_->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(view_, &QListView::customContextMenuRequested, this, [this, choose](const QPoint& at) {
            const QModelIndex idx = view_->indexAt(at);
            if (!idx.isValid()) return;
            const QString name = idx.data(Qt::DisplayRole).toString();
            QMenu menu(this);
            menu.addAction(kind_ == Kind::Materials ? "Apply to Selection" : "Place", this, [choose, idx] { choose(idx); });
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
        if (filter->useOnly) refreshUsedFilter();
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

void AssetBrowser::load() {
    hiddenStreamOnly_ = 0;
    hiddenMissing_ = 0;
    // Availability catalog: name -> (status, reason).
    QHash<QString, QPair<QString, QString>> status;
    if (kind_ == Kind::Materials || kind_ == Kind::Models || kind_ == Kind::Effects || kind_ == Kind::Images) {
        // Every status file of the kind (an audit can run as two halves: iw8_<kind>_status.tsv and ..._status_b.tsv).
        const QDir cat(repoData().filePath("asset_catalog"));
        const QString pattern = kind_ == Kind::Materials ? "iw8_materials_status*.tsv"
                                : kind_ == Kind::Effects ? "iw8_effects_status*.tsv"
                                : kind_ == Kind::Images  ? "iw8_images_status*.tsv"
                                                         : "iw8_models_status*.tsv";
        for (const QString& fn : cat.entryList({pattern}, QDir::Files)) {
        QFile sf(cat.filePath(fn));
        if (sf.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream in(&sf);
            while (!in.atEnd()) {
                const QString l = in.readLine();
                if (l.startsWith('#')) continue;
                const QStringList f = l.split('\t');
                if (f.size() >= 2 && f[1] != "ok") status.insert(f[0], {f[1], f.value(2)});
            }
        }
        }
    }
    const QDir data = repoData();
    const QString file = data.filePath(kind_ == Kind::Materials ? "asset_catalog/iw8_materials.tsv"
                                       : kind_ == Kind::Effects ? "asset_catalog/iw8_common_vfx.tsv"
                                       : kind_ == Kind::Images  ? "asset_catalog/iw8_images.tsv"
                                       : kind_ == Kind::Sounds  ? "asset_catalog/iw8_common_sounds.tsv"
                                                                : "asset_catalog/iw8_models.tsv");
    QFile f(file);
    QList<QStandardItem*> rows;
    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&f);
        in.readLine();  // header (names themselves may start with '#')
        while (!in.atEnd()) {
            const QString line = in.readLine();
            const int tab = line.indexOf('\t');
            if (tab <= 0) continue;
            auto* item = new QStandardItem(line.left(tab));
            item->setEditable(false);
            if (kind_ == Kind::Sounds) {
                // alias<TAB>loop<TAB>distMax<TAB>zones
                const QStringList f = line.split('\t');
                if (line.startsWith('#') || f.size() < 4) {
                    delete item;
                    continue;
                }
                const bool loop = f[1] == "1";
                item->setData(loop, Qt::UserRole + 2);
                item->setToolTip(QString("%1\n%2, audible to %3 units\nzones: %4").arg(f[0], loop ? "loop" : "one-shot", f[2], f[3]));
                if (loop) item->setForeground(QColor(120, 200, 255));
                rows.append(item);
                continue;
            }
            if (kind_ == Kind::Models) {
                // Weapons, attachments and cosmetics that only the streaming zone lists (8.9k of 15.6k) are not map
                // props and ACTS cannot export them from there: left out so every listed model can be placed and built.
                bool streamOnly = true;
                for (const QString& z : line.mid(tab + 1).split(',', Qt::SkipEmptyParts))
                    streamOnly &= z.trimmed() == "global_stream_mp" || z.trimmed() == "techsets_global_stream_mp";
                if (streamOnly) {
                    ++hiddenStreamOnly_;
                    delete item;
                    continue;
                }
            }
            if (const auto st = status.constFind(line.left(tab)); st != status.constEnd()) {
                if (st->first == "missing" && buildableOnly_ && buildableOnly_->isChecked()) {
                    ++hiddenMissing_;
                    delete item;
                    continue;
                }
                item->setForeground(st->first == "grey" ? QColor(200, 160, 90) : QColor(130, 130, 130));
                item->setToolTip(line.left(tab) + "\nzones: " + line.mid(tab + 1).left(300) +
                                 (st->first == "grey" ? "\nBUILDS GREY: no colour image in Replay (" : "\nNOT AVAILABLE in a build (") +
                                 st->second.left(200) + ")");
                rows.append(item);
                continue;
            }
            item->setToolTip(line.left(tab) + "\nzones: " + line.mid(tab + 1).left(300));
            rows.append(item);
        }
    }
    model_->clear();
    for (auto* r : rows) model_->appendRow(r);
    // Colour images from ACTS exports (data/stock_assets/<zone>/index.json).
    if (kind_ == Kind::Materials) {
        const QDir assets(data.filePath("stock_assets"));
        for (const QString& zone : assets.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            QFile idx(assets.filePath(zone + "/index.json"));
            if (!idx.open(QIODevice::ReadOnly)) continue;
            const QJsonArray mats = QJsonDocument::fromJson(idx.readAll()).object().value("materials").toArray();
            for (const auto& v : mats) {
                const QJsonObject m = v.toObject();
                const QString img = m.value("color_image_file").toString();
                if (!img.isEmpty()) imageFiles_.insert(m.value("name").toString(), assets.filePath(zone + "/" + img));
            }
        }
    }
    info_->setText(QString("%1 stock %2 (Replay 1.20, %3)%4")
                       .arg(rows.size())
                       .arg(kind_ == Kind::Materials ? "materials" : kind_ == Kind::Effects ? "effects" : kind_ == Kind::Images ? "images"
                            : kind_ == Kind::Sounds ? "sound aliases (blue = loop)" : "models")
                       .arg(QFileInfo(file).fileName())
                       .arg(kind_ == Kind::Materials ? QString(", %1 with thumbnails").arg(imageFiles_.size())
                            : kind_ == Kind::Models && hiddenStreamOnly_
                                ? QString("; %1 weapon/attachment models of the streaming zone not listed").arg(hiddenStreamOnly_)
                                : QString())
                       + (hiddenMissing_ ? QString("; %1 not buildable hidden").arg(hiddenMissing_) : QString()));
}

void AssetBrowser::setCustomMaterials(const QList<QPair<QString, QString>>& materials) {
    // Drop the previous custom rows (flagged with UserRole + 1), then insert the new ones at the top.
    for (int r = model_->rowCount() - 1; r >= 0; --r)
        if (model_->item(r)->data(Qt::UserRole + 1).toBool()) model_->removeRow(r);
    for (int i = materials.size() - 1; i >= 0; --i) {
        auto* item = new QStandardItem(materials[i].first);
        item->setEditable(false);
        item->setData(true, Qt::UserRole + 1);
        item->setToolTip(materials[i].first + "\ncustom material: " + materials[i].second);
        model_->insertRow(0, item);
        imageFiles_.insert(materials[i].first, materials[i].second);
        icons_.remove(materials[i].first);
    }
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

void AssetBrowser::setFilter(const QString& text) { proxy_->setFilterFixedString(text); }

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
    previewCaption_->setText(caption);
}

QString AssetBrowser::previewCaption() const { return previewCaption_ ? previewCaption_->text() : QString(); }

bool AssetBrowser::previewHasImage() const { return preview_ && !preview_->pixmap().isNull(); }

void AssetBrowser::selectRow(int row) { view_->setCurrentIndex(proxy_->index(row, 0)); }

}  // namespace iw8r::qt
