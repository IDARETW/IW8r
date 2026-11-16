import io

# Side bar revamp: Materials and Models (owner 2026-11-15, the approved plan). Materials: type chips (Ground, Walls,
# Metal, Wood, Glass, Fabric, Decals, Own, Other), the selected material in the strip with a larger picture, its type,
# how often the map uses it and its techset family, and Apply / Select Faces / Replace in Map / Info. Models: category
# chips (Structure, Containers, Furniture, Foliage, Vehicles, ...), the selected model in the strip (picture, size and
# zone, Place / Preview in 3D / Info) instead of the box under the tiles. The Apply / Place button beside the search
# moves into the strips.

ROOT = 'E:/IW8-Radiant/src/'


def rw(p, pairs):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    s = s.replace('\r\n', '\n')
    for a, b in pairs:
        if b in s and a not in s:
            continue
        assert s.count(a) == 1, (p, s.count(a), a[:90])
        s = s.replace(a, b)
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace('\n', nl))


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


P = 'qtapp/asset_browser.cpp'
rw(P, [
('''#include "brush_tools.h"
#include "panel_chrome.h"''', '''#include "brush_tools.h"
#include "map_queries.h"
#include "panel_chrome.h"'''),
# the groups
('''    return kind == AssetBrowser::Kind::Effects ? effects : kind == AssetBrowser::Kind::Sounds ? sounds
           : kind == AssetBrowser::Kind::Images ? images : none;''', '''    // Materials: what the surface is, from the words of its name; the mapper's own first ("own" is set on their rows).
    static const std::vector<Group> materials = {
        {"own", "Own", "The map's own materials", "^$", {}},
        {"ground", "Ground", "Dirt, grass, gravel, rock, asphalt, floors",
         "ground|dirt|grass|gravel|mud|sand|soil|rock|asphalt|road|terrain|moss|snow|floor|pavement|cobble", {}},
        {"walls", "Walls", "Brick, plaster, concrete, tiles, stone", "wall|brick|plaster|concrete|tile|stucco|siding|block|stone", {}},
        {"metal", "Metal", "Metal, steel, rust, corrugated sheets", "metal|steel|iron|rust|alum|corrugated|chrome", {}},
        {"wood", "Wood", "Wood, planks, bark", "wood|plank|plywood|timber|bark", {}},
        {"glass", "Glass", "Glass and windows", "glass|window", {}},
        {"fabric", "Fabric", "Fabric, carpet, leather", "fabric|cloth|carpet|curtain|leather|rug", {}},
        {"decals", "Decals", "Decals, reveal layers, grime, signs", "decal|_dec($|_)|^i/|_rvl|leak|grime|stain|graffiti|poster|sign", {}},
        {"other", "Other", "Everything else (terrain blends, weapons, characters, effects)", nullptr, {}},
    };
    // Models: what the prop is.
    static const std::vector<Group> models = {
        {"structure", "Structure", "Walls, doors, windows, stairs, fences, pipes, frames",
         "wall|door|window|stair|pillar|column|beam|fence|roof|frame|pipe|railing|rail_|scaffold|girder|gate|ladder|bridge|building|floor", {}},
        {"containers", "Containers", "Crates, boxes, barrels, pallets, bins", "crate|box|barrel|container|pallet|bin_|dumpster|case_|sack|bag", {}},
        {"furniture", "Furniture", "Chairs, tables, shelves, beds, lockers",
         "chair|table|desk|sofa|couch|bed_|shelf|cabinet|furniture|bench|stool|dresser|wardrobe|locker|bookcase", {}},
        {"household", "Household", "Small props: kitchen, books, bottles, decor",
         "decor|kitchen|dinnerware|accessory|school|book|cup|plate|bottle|can_|food|toy|clock|vase|towel", {}},
        {"foliage", "Foliage", "Trees, bushes, grass, plants", "foliage|tree|bush|grass|plant|fern|shrub|ivy|flower|hedge|leaf|leaves|vine|weed|cactus|palm", {}},
        {"veh", "Vehicles", "Cars, trucks, helicopters, boats", "^veh|vehicle|car_|_car|truck|heli|boat|bike|tractor|bus_|plane|train", {}},
        {"equipment", "Equipment", "Machinery, military and industrial gear", "equipment|machinery|industrial|military|pump|ac_unit|tank", {}},
        {"lights", "Lights", "Lamps and light fixtures", "light|lamp|lantern|bulb", {}},
        {"electronics", "Electronics", "Screens, computers, cables, speakers",
         "tv_|computer|monitor|phone|radio|electronic|cable|wire|server|speaker|generator|antenna", {}},
        {"debris", "Debris", "Rubble, trash, rocks, tires", "debris|rubble|trash|garbage|junk|litter|rock|stone|brick|plank|tire|cinder", {}},
        {"signs", "Signs", "Signs, posters, flags", "sign|poster|banner|flag|billboard", {}},
        {"other", "Other", "Everything else", nullptr, {}},
    };
    return kind == AssetBrowser::Kind::Effects ? effects : kind == AssetBrowser::Kind::Sounds ? sounds
           : kind == AssetBrowser::Kind::Images ? images : kind == AssetBrowser::Kind::Materials ? materials : models;'''),
# custom materials are "own"
('''    item->setData(true, Qt::UserRole + 1);
    item->setToolTip(name + "\\ncustom material: " + image);''', '''    item->setData(true, Qt::UserRole + 1);
    item->setData(QString("own"), kGroupRole);
    item->setToolTip(name + "\\ncustom material: " + image);'''),
# no Apply / Place beside the search: the strips have them
('''    QPushButton* use = nullptr;    // materials / models: Apply / Place beside the search
    QPushButton* place = nullptr;  // effects / sounds: Place in the details strip
    if (kind == Kind::Materials || kind == Kind::Models) {
        use = new QPushButton(kind == Kind::Materials ? "Apply" : "Place", this);
        use->setToolTip(kind == Kind::Materials ? "Apply to the selection (Shift+M)" : "Place a static misc_model at the view centre");
        searchRow->addWidget(use);
    }''', '''    QPushButton* use = nullptr;    // (kept for the wiring below; the strips have Apply / Place now)
    QPushButton* place = nullptr;  // Apply / Place in the details strip'''),
])

# Models: the strip replaces the box under the tiles; Materials: a strip of its own.
slice_replace(P, '''        // The selected model, compact under the tiles (only while one is selected): a small picture, its name, size and
        // zone, and the 3D preview.''', '''    } else if (kind == Kind::Images) {''', '''        // The selected model in the strip (only while one is selected): a picture, its size and zone, Place, Preview in
        // 3D and Info.
        strip_ = new DetailsStrip(this);
        strip_->setObjectName("modelPreviewBox");
        auto* row = new QHBoxLayout();
        row->setSpacing(6);
        preview_ = new QLabel(strip_);
        preview_->setObjectName("modelPreview");
        preview_->setFixedSize(96, 96);
        preview_->setAlignment(Qt::AlignCenter);
        preview_->setStyleSheet("background: #202226; border: none");
        previewCaption_ = new QLabel("Select a model to preview it", strip_);
        previewCaption_->setWordWrap(true);
        previewCaption_->setStyleSheet(QString("color: %1").arg(themeMix(0.6).name()));
        previewCaption_->setMinimumWidth(0);  // a long unbroken name is elided, never widening the column
        previewCaption_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        previewCaption_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        row->addWidget(preview_, 0, Qt::AlignTop);
        row->addWidget(previewCaption_, 1);
        strip_->body()->addLayout(row);
        place = strip_->addAction(glyph::Add, "Place", "Place a static misc_model at the view centre (or drag the tile into a view)", "modelPlace");
        QPushButton* open3d = strip_->addAction(glyph::Cube, "3D",
                                                "A textured 3D view of the model: orbit, pan and zoom, levels of detail, materials, collision hull",
                                                "modelPreview3d");
        connect(open3d, &QPushButton::clicked, this, [this] {
            const QModelIndex idx = view_->currentIndex();
            if (idx.isValid()) emit previewRequested(idx.data(Qt::DisplayRole).toString());
        });
        QPushButton* info = strip_->addAction(glyph::Info, "Info", "Model Info: levels of detail, materials, bounds", "modelInfo");
        connect(info, &QPushButton::clicked, this, [this] {
            const QModelIndex idx = view_->currentIndex();
            if (idx.isValid()) emit infoRequested(idx.data(Qt::DisplayRole).toString());
        });
        strip_->setVisible(false);
        previewBox_ = strip_;
        lay->addWidget(strip_);
    } else if (kind == Kind::Materials) {
        // The selected material in the strip: a larger picture, its type, how often the map uses it, its techset family;
        // Apply, Select Faces, Replace in Map, Info.
        lay->addWidget(view_, 1);
        strip_ = new DetailsStrip(this);
        auto* row = new QHBoxLayout();
        row->setSpacing(6);
        preview_ = new QLabel(strip_);
        preview_->setObjectName("materialPreview");
        preview_->setFixedSize(112, 112);
        preview_->setAlignment(Qt::AlignCenter);
        preview_->setStyleSheet("background: #202226; border: none");
        row->addWidget(preview_, 0, Qt::AlignTop);
        row->addStretch(1);
        strip_->body()->addLayout(row);
        place = strip_->addAction(glyph::Brush, "Apply", "Apply to the selected brushes or faces (Shift+M; or drag the tile onto a surface)",
                                  "materialApply");
        QPushButton* faces = strip_->addAction(glyph::Target, "Faces", "Select every brush in the map that uses this material", "materialSelectFaces");
        connect(faces, &QPushButton::clicked, this, [this] {
            const QModelIndex idx = view_->currentIndex();
            if (!idx.isValid()) return;
            const QString name = idx.data(Qt::DisplayRole).toString();
            const auto refs = findByMaterial(ctx_->doc.map(), name.toStdString(), false);
            ctx_->doc.deselectAll();
            for (const auto& r : refs) ctx_->doc.select(r, true);
            ctx_->documentEdited(QString("Selected %1 primitive(s) with %2").arg(refs.size()).arg(name));
        });
        QPushButton* replace = strip_->addAction(glyph::Redo, "Replace", "Replace this material everywhere in the map with another", "materialReplace");
        connect(replace, &QPushButton::clicked, this, [this] {
            const QModelIndex idx = view_->currentIndex();
            if (idx.isValid()) emit replaceRequested(idx.data(Qt::DisplayRole).toString());
        });
        QPushButton* info = strip_->addAction(glyph::Info, "Info", "Material Info: images, shader, surface", "materialInfo");
        connect(info, &QPushButton::clicked, this, [this] {
            const QModelIndex idx = view_->currentIndex();
            if (idx.isValid()) emit infoRequested(idx.data(Qt::DisplayRole).toString());
        });
        strip_->setVisible(false);
        lay->addWidget(strip_);
''')

rw(P, [
# the old Models branch's remaining lines are gone with the slice; Materials no longer falls to the plain branch
('''    if (kind_ == Kind::Models || kind_ == Kind::Images)
        connect(view_->selectionModel(), &QItemSelectionModel::currentChanged, this, [this](const QModelIndex& idx) {''',
 '''    if (kind_ == Kind::Materials)
        connect(view_->selectionModel(), &QItemSelectionModel::currentChanged, this, [this] { updateStrip(); });
    if (kind_ == Kind::Models || kind_ == Kind::Images)
        connect(view_->selectionModel(), &QItemSelectionModel::currentChanged, this, [this](const QModelIndex& idx) {'''),
# the strip of a material or a model
('''    } else if (!group.isEmpty()) {
        facts << group;  // images: the size and format come with the preview (setPreview)
    }
    strip_->setTitle(name, facts.join(factSep()));''', '''    } else if (kind_ == Kind::Materials || kind_ == Kind::Models) {
        if (!group.isEmpty()) facts << group;
        const int uses = usageCount(name);
        facts << (uses ? QString("used %1 time%2 in this map").arg(uses).arg(uses == 1 ? "" : "s") : QString("not used in this map"));
        if (kind_ == Kind::Materials && name.contains('/')) facts << "techset family " + name.section('/', 0, 0);
        if (QPushButton* r = strip_->findChild<QPushButton*>("materialReplace")) r->setEnabled(uses > 0);
        if (QPushButton* f = strip_->findChild<QPushButton*>("materialSelectFaces")) f->setEnabled(uses > 0);
        if (kind_ == Kind::Materials && preview_) {
            // A larger picture than the tile's: the colour image again at the strip's size, else the tile's picture.
            QPixmap pm;
            if (const QString file = imageFiles_.value(name); !file.isEmpty())
                if (const QImage img = decodeImageFile(file, 112); !img.isNull()) pm = QPixmap::fromImage(img);
            if (pm.isNull()) pm = idx.data(Qt::DecorationRole).value<QIcon>().pixmap(112, 112);
            preview_->setPixmap(pm);
        }
    } else if (!group.isEmpty()) {
        facts << group;  // images: the size and format come with the preview (setPreview)
    }
    strip_->setTitle(name, facts.join(factSep()));'''),
])
print('ok')
