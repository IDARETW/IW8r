import io

# Side bar revamp, step 1, part 2: the asset browser's constructor (see sidebar_flush_assets.py).

ROOT = 'E:/IW8-Radiant/src/'


def slice_replace(p, start, end, new):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    s = s.replace('\r\n', '\n')
    if new in s:
        return
    assert s.count(start) == 1 and s.count(end) == 1, (p, s.count(start), s.count(end))
    a = s.index(start)
    b = s.index(end)
    assert a < b
    s = s[:a] + new + s[b:]
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace('\n', nl))


CTOR = r'''AssetBrowser::AssetBrowser(EditorContext* ctx, Kind kind, QWidget* parent) : QWidget(parent), ctx_(ctx), kind_(kind) {
    // Flush (owner 2026-11-15, panel_chrome.h): the header line (name, count, favourites star, the dots menu), the search
    // across the panel, one row of filter chips, the list edge to edge, the selection's details strip at the bottom.
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(4);
    header_ = new PanelHeader(kind == Kind::Materials ? "Materials" : kind == Kind::Models ? "Models" : kind == Kind::Effects ? "Effects"
                              : kind == Kind::Sounds ? "Sounds" : "Images", this);
    favButton_ = header_->addButton(glyph::Star, "Favourites only (right-click an asset > Add to Favourites)", true, "assetFavourites");
    connect(favButton_, &QToolButton::toggled, this, [this](bool on) { setListModeTo(on ? 1 : 0, {}); });
    menu_ = new QMenu(this);
    header_->addMenuButton(menu_, "What the list shows, options, and the tools of this kind of asset", "assetTools");
    lay->addWidget(header_);
    auto* searchRow = new QHBoxLayout();
    searchRow->setContentsMargins(6, 0, 6, 0);
    searchRow->setSpacing(4);
    search_ = new QLineEdit(this);
    search_->setObjectName(kind == Kind::Materials ? "materialSearch" : kind == Kind::Effects ? "effectSearch"
                           : kind == Kind::Sounds ? "soundSearch" : "modelSearch");
    search_->setPlaceholderText(kind == Kind::Images ? "Search images" : kind == Kind::Effects ? "Search effects"
                                : kind == Kind::Sounds ? "Search sounds" : kind == Kind::Materials ? "Search materials"
                                                                                                  : "Search models");
    search_->setToolTip(kind == Kind::Images ? "50k stock images (colour _c, normal _n, sky_, ...)"
                        : kind == Kind::Effects ? "1020 stock effects that load in any map (e.g. fire, smoke, spark)"
                        : kind == Kind::Sounds ? "11k stock sound aliases that load in any map (amb_, emt_ = ambient loops)"
                        : kind == Kind::Materials ? "46k stock IW8 materials (e.g. concrete, brick, plaster)"
                                                  : "Stock IW8 models (e.g. crate, barrel, car)");
    search_->setClearButtonEnabled(true);
    search_->addAction(glyphIcon(0xE721, 14, themeMix(0.55), "?"), QLineEdit::LeadingPosition);  // a magnifier
    searchRow->addWidget(search_, 1);
    QPushButton* use = nullptr;    // materials / models: Apply / Place beside the search
    QPushButton* place = nullptr;  // effects / sounds: Place in the details strip
    if (kind == Kind::Materials || kind == Kind::Models) {
        use = new QPushButton(kind == Kind::Materials ? "Apply" : "Place", this);
        use->setToolTip(kind == Kind::Materials ? "Apply to the selection (Shift+M)" : "Place a static misc_model at the view centre");
        searchRow->addWidget(use);
    }
    lay->addLayout(searchRow);
    // The chips: the panel's groups (one at a time, with counts), then toggles.
    chips_ = new ChipBar(this);
    chips_->setObjectName("assetChips");
    if (!groupsOf(kind).empty()) {
        chips_->addChip("all", "All", -1, "Everything listed");
        for (const Group& g : groupsOf(kind)) chips_->addChip(g.id, g.label, -1, g.tip, g.color);
    }
    if (kind == Kind::Sounds) chips_->addToggle("loops", "Loops", false, "Looping aliases only (ambience, engines, music beds)");
    chips_->addToggle("recent", "Recent", false, "The last 30 placed or applied");
    if (kind == Kind::Materials || kind == Kind::Models)
        chips_->addToggle("inmap", "In this map", false,
                          kind == Kind::Materials ? "Materials on the open map's faces" : "Models placed in the open map");
    connect(chips_, &ChipBar::currentChanged, this, [this](const QString& id) {
        static_cast<NameFilter*>(proxy_)->group = id == "all" ? QString() : id;
        static_cast<NameFilter*>(proxy_)->refresh();
        updateInfo();
    });
    connect(chips_, &ChipBar::toggled, this, [this](const QString& id, bool on) {
        auto* f = static_cast<NameFilter*>(proxy_);
        if (id == "recent") return setListModeTo(on ? 2 : 0, {});
        if (id == "loops") {
            f->loopsOnly = on;
            f->refresh();
            return updateInfo();
        }
        if (id == "inmap") {
            usedOnly_ = on;
            f->useOnly = on;
            refreshUsedFilter();
        }
    });
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
        chips_->addWidget(kit_);
    }
    lay->addWidget(chips_);
    // Only what the count cannot say: that the catalogue is being read, or why the list is empty.
    info_ = new QLabel(this);
    info_->setObjectName("assetInfo");
    info_->setWordWrap(true);
    info_->setContentsMargins(6, 0, 6, 0);
    info_->setStyleSheet(QString("color: %1").arg(themeMix(0.6).name()));
    info_->setVisible(false);
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
    view_->setFrameShape(QFrame::NoFrame);  // edge to edge
    view_->setUniformItemSizes(true);
    view_->setTextElideMode(Qt::ElideMiddle);  // long asset paths keep both ends
    auto* delegate = new AssetTileDelegate(kind == Kind::Materials ? AssetTileDelegate::MaterialTiles
                                           : kind == Kind::Models  ? AssetTileDelegate::ModelTiles
                                           : kind == Kind::Images  ? AssetTileDelegate::ImageTiles
                                           : kind == Kind::Effects ? AssetTileDelegate::EffectRows
                                                                   : AssetTileDelegate::SoundRows,
                                           view_);
    delegate->facts = &facts_;
    delegate->kind = kind;
    delegate_ = delegate;
    view_->setItemDelegate(delegate);
    if (kind == Kind::Effects || kind == Kind::Sounds) {
        view_->setMouseTracking(true);  // the hover shade
        view_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
        view_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    }
    if (kind == Kind::Sounds) delegate->onPlay = [this](const QModelIndex& idx) { playRow(idx); };
    if (kind == Kind::Materials || kind == Kind::Models || kind == Kind::Images) {
        // Tiles: the whole thumbnail fitted in its box, the short name (models: on two lines), the folder below
        // (owner: previews and names were cut off). The full name is the tooltip.
        view_->setViewMode(QListView::IconMode);
        view_->setMovement(QListView::Static);  // icon mode's free movement let drags shuffle tiles
        view_->setFlow(QListView::LeftToRight);
        view_->setWrapping(true);
        view_->setResizeMode(QListView::Adjust);
        view_->setWordWrap(false);
        view_->setSpacing(0);
        view_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
        view_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);  // a narrow column wraps the tiles, never scrolls sideways
        if (kind == Kind::Models) view_->setUniformItemSizes(false);  // 7k rows of a constant size hint: cheap, never stale
        gridFit_ = new TileGridFit(view_, delegate);
        view_->viewport()->installEventFilter(gridFit_);  // columns follow the panel width
    }
    if (kind == Kind::Models) {
        // Owner 2026-11-15: larger model pictures, with a size choice (kept in the settings).
        tileSizeBox_ = new QComboBox(this);
        tileSizeBox_->setObjectName("assetTileSize");
        tileSizeBox_->addItems({"Small", "Medium", "Large"});
        tileSizeBox_->setToolTip("Picture size of the model tiles");
        chips_->addWidget(tileSizeBox_);
        tileSize_ = TileSize(std::clamp(Settings::instance().value(settingsKey("tileSize"), 1).toInt(), 0, 2));
        applyTileSize();
        {
            const QSignalBlocker block(tileSizeBox_);
            tileSizeBox_->setCurrentIndex(int(tileSize_));
        }
        connect(tileSizeBox_, &QComboBox::currentIndexChanged, this, [this](int i) { setTileSize(TileSize(i)); });
        lay->addWidget(view_, 1);
        // The selected model, compact under the tiles (only while one is selected): a small picture, its name, size and
        // zone, and the 3D preview.
        auto* box = new QFrame(this);
        box->setObjectName("modelPreviewBox");
        box->setFrameShape(QFrame::StyledPanel);
        // The picture and the caption side by side, the button under them (its own line: the column stays narrow).
        auto* grid = new QGridLayout(box);
        grid->setContentsMargins(4, 4, 4, 4);
        grid->setHorizontalSpacing(6);
        grid->setVerticalSpacing(4);
        preview_ = new QLabel(box);
        preview_->setObjectName("modelPreview");
        preview_->setFixedSize(96, 96);
        preview_->setAlignment(Qt::AlignCenter);
        preview_->setStyleSheet("background: #202226; border: 1px solid #3a3d44");
        previewCaption_ = new QLabel("Select a model to preview it", box);
        previewCaption_->setWordWrap(true);
        previewCaption_->setStyleSheet("color: gray");
        previewCaption_->setMinimumWidth(0);  // a long unbroken name is elided, never widening the column
        previewCaption_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        previewCaption_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        auto* open3d = new QPushButton("Preview in 3D...", box);
        open3d->setObjectName("modelPreview3d");
        open3d->setToolTip("A textured 3D view of the model: orbit, pan and zoom, levels of detail, materials, collision hull");
        connect(open3d, &QPushButton::clicked, this, [this] {
            const QModelIndex idx = view_->currentIndex();
            if (idx.isValid()) emit previewRequested(idx.data(Qt::DisplayRole).toString());
        });
        grid->addWidget(preview_, 0, 0, Qt::AlignTop);
        grid->addWidget(previewCaption_, 0, 1);
        grid->addWidget(open3d, 1, 0, 1, 2, Qt::AlignLeft);
        grid->setColumnStretch(1, 1);
        box->setVisible(false);
        previewBox_ = box;
        lay->addWidget(box);
    } else if (kind == Kind::Images) {
        // A grid of thumbnails; the selected image large in the strip with its size and format.
        lay->addWidget(view_, 1);
        strip_ = new DetailsStrip(this);
        preview_ = new QLabel(strip_);
        preview_->setObjectName("modelPreview");
        preview_->setMinimumSize(96, 96);
        preview_->setMaximumHeight(220);
        preview_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
        preview_->setAlignment(Qt::AlignCenter);
        preview_->setStyleSheet("background: #202226; border: none");
        // The caption is kept whole here (previewCaption()); the strip shows it as the title and facts.
        previewCaption_ = new QLabel(strip_);
        previewCaption_->setVisible(false);
        strip_->body()->addWidget(preview_);
        QPushButton* copy = strip_->addAction(glyph::Copy, "Copy name", "Copy the image's whole name", "imageCopyName");
        connect(copy, &QPushButton::clicked, this, [this] {
            const QModelIndex idx = view_->currentIndex();
            if (idx.isValid()) QGuiApplication::clipboard()->setText(idx.data(Qt::DisplayRole).toString());
        });
        strip_->setVisible(false);
        lay->addWidget(strip_);
    } else if (kind == Kind::Effects) {
        // The folders of the effects' paths above the list (a splitter between them), the selected effect playing in
        // the strip as a square (owner 2026-11-15) with Place fx_origin.
        folders_ = new QTreeWidget(this);
        folders_->setObjectName("effectFolders");
        folders_->setHeaderHidden(true);
        folders_->setColumnCount(2);
        folders_->setFrameShape(QFrame::NoFrame);
        folders_->setIndentation(12);
        folders_->setUniformRowHeights(true);
        folders_->header()->setStretchLastSection(false);
        folders_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
        folders_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
        folders_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        folders_->setTextElideMode(Qt::ElideMiddle);
        folders_->setToolTip("The folders of the effects' paths: pick one to list the effects under it");
        connect(folders_, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* it) {
            if (it) setFolder(it->data(0, Qt::UserRole).toString());
        });
        auto* split = new QSplitter(Qt::Vertical, this);
        split->setObjectName("effectSplit");
        split->setHandleWidth(4);
        split->addWidget(folders_);
        split->addWidget(view_);
        split->setCollapsible(1, false);
        split->setStretchFactor(0, 0);
        split->setStretchFactor(1, 1);
        split->setSizes({130, 400});
        lay->addWidget(split, 1);
        strip_ = new DetailsStrip(this);
        fxPreview_ = new VfxPreview(strip_);
        fxPreview_->setObjectName("effectPreview");
        fxPreview_->setVisible(false);  // a square that follows the column's width (VfxPreview::heightForWidth)
        fxCaption_ = new QLabel(strip_);
        fxCaption_->setObjectName("effectPreviewCaption");
        fxCaption_->setWordWrap(true);
        fxCaption_->setStyleSheet(QString("color: %1").arg(themeMix(0.6).name()));
        fxCaption_->setMinimumWidth(0);
        fxCaption_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        strip_->body()->addWidget(fxPreview_);
        strip_->body()->addWidget(fxCaption_);
        place = strip_->addAction(glyph::Add, "Place fx_origin", "Place an fx_origin with this effect at the view centre (or drag the row into a view)",
                                  "effectPlace");
        strip_->setVisible(false);
        lay->addWidget(strip_);
    } else if (kind == Kind::Sounds) {
        // A play button on every row; the selected alias in the strip with its waveform, Play / Stop and Place speaker.
        lay->addWidget(view_, 1);
        strip_ = new DetailsStrip(this);
        wave_ = new WaveView(strip_);
        soundCaption_ = new QLabel("Select a sound, then Play", strip_);
        soundCaption_->setObjectName("soundPreviewCaption");
        soundCaption_->setWordWrap(true);
        soundCaption_->setStyleSheet(QString("color: %1").arg(themeMix(0.6).name()));
        soundCaption_->setMinimumWidth(0);
        soundCaption_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        strip_->body()->addWidget(wave_);
        strip_->body()->addWidget(soundCaption_);
        soundPlay_ = strip_->addAction(glyph::Play, "Play",
                                       "Play the selected alias (a random variant, as the game picks one); click again to stop. The first "
                                       "time prepares the zone's sound banks with ACTS (about half a minute, once).",
                                       "soundPlay");
        soundPlay_->setEnabled(false);
        place = strip_->addAction(glyph::Add, "Place speaker",
                                  "Place a sound_emitter with this alias at the view centre (loops keep playing, one-shots repeat)", "soundPlace");
        connect(soundPlay_, &QPushButton::clicked, this, [this] {
            if (soundPlay_->property("playing").toBool()) emit soundStopRequested();
            else playRow(view_->currentIndex());
        });
        strip_->setVisible(false);
        lay->addWidget(strip_);
    } else {
        lay->addWidget(view_, 1);
    }
    model_ = new QStandardItemModel(this);
    auto* filter = new NameFilter(this);
    proxy_ = filter;
    proxy_->setSourceModel(model_);
    proxy_->setFilterCaseSensitivity(Qt::CaseInsensitive);
    view_->setModel(proxy_);
    if (kind == Kind::Effects) {
        // What earlier previews found (loop / one-shot length), kept in the settings.
        const QVariantMap kept = Settings::instance().value(settingsKey("facts")).toMap();
        for (auto it = kept.begin(); it != kept.end(); ++it) facts_.insert(it.key(), it.value().toString());
    }

    connect(search_, &QLineEdit::textChanged, this, [this](const QString& t) { setFilter(t); });
    refillListModes();  // tag list modes (4.22)
    if (kit_) connect(kit_, &QComboBox::currentIndexChanged, this, [this] { setKit(kit_->currentData().toString()); });
'''
slice_replace('qtapp/asset_browser.cpp',
              'AssetBrowser::AssetBrowser(EditorContext* ctx, Kind kind, QWidget* parent) : QWidget(parent), ctx_(ctx), kind_(kind) {\n',
              '    auto choose = [this](const QModelIndex& idx) {\n', CTOR)
print('ok part 2')
