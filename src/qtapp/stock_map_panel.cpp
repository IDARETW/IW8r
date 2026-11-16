// IW8 Radiant Suite (Qt shell) - the side bar's Stock Map panel (stock_map_panel.h).
#include "stock_map_panel.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <set>

#include <QAction>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QCompleter>
#include <QCoreApplication>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileInfo>
#include <QFontMetrics>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QListWidget>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QSlider>
#include <QStackedWidget>
#include <QStringListModel>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include <DockManager.h>
#include <DockWidget.h>

#include "asset_browser.h"
#include "brush_ops.h"
#include "flow_layout.h"
#include "main_window.h"
#include "map_text.h"
#include "stock_world.h"
#include "stock_zone_edit.h"

namespace iw8r::qt {

namespace {

QString qs(const std::string& s) { return QString::fromStdString(s); }
constexpr double kDeg = 3.14159265358979323846 / 180.0;
constexpr const char* kSideAction = "side_Stock_Map";  // main_window.cpp: "side_" + the dock's name
constexpr const char* kDockName = "Stock Map";

// The intensity slider is logarithmic: 0.1 to 1000 over its 1000 steps (stock suns are about 8 to 31); the spin box
// beside it takes any value the rebuild takes (0 to 1000000).
constexpr int kIntensitySteps = 1000;
constexpr double kIntensityLow = 0.1, kIntensityDecades = 4.0;
double intensityOfSlider(int pos) { return kIntensityLow * std::pow(10.0, kIntensityDecades * pos / kIntensitySteps); }
int sliderOfIntensity(double v) {
    if (v <= kIntensityLow) return 0;
    return std::clamp(int(std::lround(std::log10(v / kIntensityLow) / kIntensityDecades * kIntensitySteps)), 0, kIntensitySteps);
}
// The colour picker shows display values (gamma 2.2, as File > Stock Map > Relight Stock Map Sun); the key is linear.
int displayOf(double linear) { return int(std::lround(std::pow(std::clamp(linear, 0.0, 1.0), 1.0 / 2.2) * 255.0)); }
double linearOf(int display) { return std::pow(std::clamp(display, 0, 255) / 255.0, 2.2); }
double rounded(double v, int decimals) {
    const double f = std::pow(10.0, decimals);
    return std::round(v * f) / f;
}
std::string fixed(double v, int decimals) { return formatNumber(rounded(v, decimals)); }
double wrapYaw(double y) {
    y = std::fmod(y, 360.0);
    if (y > 180) y -= 360;
    if (y <= -180) y += 360;
    return y;
}
// The values the rebuild and the viewport take (core stockSunJson, MainWindow::applyStockSunPreview): a value they
// leave out is not shown here either.
bool parseColor(const std::string& s, double out[3]) {
    double v[3] = {0, 0, 0};
    char tail = 0;
    if (std::sscanf(s.c_str(), "%lf %lf %lf %c", &v[0], &v[1], &v[2], &tail) != 3) return false;
    for (const double c : v)
        if (!(c >= 0 && c <= 16)) return false;
    std::copy(v, v + 3, out);
    return true;
}
bool parseIntensity(const std::string& s, double& out) {
    char* end = nullptr;
    const double v = std::strtod(s.c_str(), &end);
    if (s.empty() || !end || *end != 0 || !(v >= 0 && v <= 1000000)) return false;
    out = v;
    return true;
}
// A removal box key that is off ("0", "false", "no": core documentWorldRemovals).
bool keyOff(const Entity& e, const char* key) {
    const std::string* v = e.value(key);
    if (!v) return false;
    const QString t = qs(*v).trimmed().toLower();
    return t == "0" || t == "false" || t == "no";
}
AABB entityBounds(const Entity& e) {
    AABB box;
    for (const EntityItem& it : e.items)
        if (const Primitive* p = it.prim()) {
            const AABB b = p->bounds();
            if (b.valid()) box.add(b);
        }
    return box;
}
const Entity* worldOf(const Document& doc) {
    for (const Entity& e : doc.map().entities)
        if (e.isWorldspawn()) return &e;
    return nullptr;
}
QString worldValue(const Document& doc, const char* key) {
    const Entity* w = worldOf(doc);
    const std::string* v = w ? w->value(key) : nullptr;
    return v ? qs(*v) : QString();
}

// iw8r_stock_retexture as written: the "from=to" pairs in order, and any part that is not a pair (kept as it is).
struct Retexture {
    std::vector<std::pair<QString, QString>> pairs;
    QStringList extras;
};
Retexture parseRetexture(const QString& value) {
    Retexture r;
    for (const QString& raw : value.split(';')) {
        const QString part = raw.trimmed();
        if (part.isEmpty()) continue;
        const int eq = part.indexOf('=');
        if (eq < 0) r.extras << part;
        else r.pairs.push_back({part.left(eq).trimmed(), part.mid(eq + 1).trimmed()});
    }
    return r;
}
// A material name the rebuild takes (core stockRetextureEdits: letters, digits and _ / - . ~ & #, under 128).
bool materialName(const QString& n) {
    static const QRegularExpression valid(R"(^[A-Za-z0-9_/.~&#-]{1,127}$)");
    return valid.match(n).hasMatch();
}

QString catalogFile(const QString& rel) {
    QDir d(QCoreApplication::applicationDirPath());
    for (int up = 0; up < 4 && !QFileInfo::exists(d.filePath(rel)); ++up) d.cdUp();
    return d.filePath(rel);
}
// The stock maps File > Open IW8 Stock Map opens (data/asset_catalog/iw8_stock_maps.tsv, the Relight Sky list), those
// whose sky material tw/sky_<id> the material catalogue lists first: that material is what the viewport shows and
// the rebuild carries (core stockSkyEdit).
struct SkyCatalog {
    QStringList withSky, other;
};
const SkyCatalog& skyCatalog() {
    static const SkyCatalog c = [] {
        SkyCatalog out;
        std::set<QString> skies;
        QFile mf(catalogFile("data/asset_catalog/iw8_materials.tsv"));
        if (mf.open(QIODevice::ReadOnly | QIODevice::Text))
            while (!mf.atEnd()) {
                const QByteArray line = mf.readLine();
                if (!line.startsWith("tw/sky_mp_")) continue;
                const int tab = line.indexOf('\t');
                skies.insert(QString::fromUtf8(tab < 0 ? line : line.left(tab)).trimmed().mid(7));  // "mp_<id>"
            }
        QFile f(catalogFile("data/asset_catalog/iw8_stock_maps.tsv"));
        if (f.open(QIODevice::ReadOnly | QIODevice::Text))
            while (!f.atEnd()) {
                const QStringList col = QString::fromUtf8(f.readLine()).trimmed().split('\t');
                if (col.size() < 7 || col[0].startsWith('#') || col[6] != "yes" || !col[0].startsWith("mp_")) continue;
                (skies.count(col[0]) ? out.withSky : out.other) << col[0];
            }
        return out;
    }();
    return c;
}

QLabel* noteLabel(const QString& text = {}) {
    auto* l = new QLabel(text);
    l->setWordWrap(true);
    l->setTextInteractionFlags(Qt::TextSelectableByMouse);
    return l;
}
QPushButton* button(const QString& text, const QString& tip, const QString& name) {
    auto* b = new QPushButton(text);
    b->setToolTip(tip);
    b->setObjectName(name);
    return b;
}
QWidget* flowRow(std::initializer_list<QWidget*> widgets) {
    auto* host = new QWidget;
    auto* flow = new FlowLayout(host, 6, 4);
    flow->setContentsMargins(0, 0, 0, 0);
    for (QWidget* w : widgets) flow->addWidget(w);
    return host;
}

}  // namespace

StockMapPanel::StockMapPanel(MainWindow* window, EditorContext* ctx, QWidget* parent) : QWidget(parent), w_(window), ctx_(ctx) {
    setObjectName("stockMapPanel");
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    pages_ = new QStackedWidget;
    outer->addWidget(pages_);

    // Page 0: no stock map open.
    {
        auto* none = new QWidget;
        auto* l = new QVBoxLayout(none);
        l->addWidget(noteLabel("Open a stock map (File > Open IW8 Stock Map) to change its sun, sky, materials, world "
                               "geometry and props here. Each change shows in the viewport as you make it."));
        auto* open = button("Open IW8 Stock Map...", "File > Open IW8 Stock Map", "stockPanelOpen");
        connect(open, &QPushButton::clicked, this, [this] {
            if (QAction* a = w_->action("OpenStockZone")) a->trigger();
        });
        l->addWidget(open, 0, Qt::AlignLeft);
        l->addStretch(1);
        pages_->addWidget(none);
    }

    // Page 1: the edits, in a column that only scrolls down.
    scroll_ = new QScrollArea;
    scroll_->setObjectName("stockMapScroll");
    scroll_->setWidgetResizable(true);
    scroll_->setFrameShape(QFrame::NoFrame);
    scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* body = new QWidget;
    auto* v = new QVBoxLayout(body);
    v->setContentsMargins(6, 6, 6, 6);
    v->setSpacing(8);
    scroll_->setWidget(body);
    pages_->addWidget(scroll_);
    header_ = noteLabel();
    header_->setTextFormat(Qt::RichText);
    v->addWidget(header_);
    const int labelW = QFontMetrics(font()).horizontalAdvance("Intensity") + 8;
    const auto rowLabel = [labelW](const QString& t) {
        auto* l = new QLabel(t);
        l->setFixedWidth(labelW);
        return l;
    };

    // ---- Sun
    {
        auto* box = new QGroupBox("Sun");
        box->setObjectName("stockSunGroup");
        auto* l = new QVBoxLayout(box);
        auto* crow = new QHBoxLayout;
        colorButton_ = new QToolButton;
        colorButton_->setObjectName("stockSunColorButton");
        colorButton_->setIconSize(QSize(28, 16));
        colorButton_->setToolTip("Pick the sun colour: the viewport follows while you pick; Cancel puts the colour back");
        colorEdit_ = new QLineEdit;
        colorEdit_->setObjectName("stockSunColorEdit");
        colorEdit_->setPlaceholderText("r g b (linear, 0-16)");
        colorEdit_->setToolTip("The sun colour as linear r g b, 0 to 16 (iw8r_sun_color); values over 1 brighten it");
        crow->addWidget(rowLabel("Colour"));
        crow->addWidget(colorButton_);
        crow->addWidget(colorEdit_, 1);
        l->addLayout(crow);
        connect(colorButton_, &QToolButton::clicked, this, [this] { pickColor(); });
        connect(colorEdit_, &QLineEdit::editingFinished, this, [this] {
            double c[3];
            const std::string t = colorEdit_->text().simplified().toStdString();
            if (t.empty()) return;
            if (!parseColor(t, c)) {
                ctx_->setStatus("Stock sun colour: \"" + colorEdit_->text() + "\" is not three numbers 0 to 16 (linear r g b)");
                refreshSun();
                return;
            }
            setSunColor(c[0], c[1], c[2]);
        });

        const auto sliderRow = [&](const QString& label, QSlider*& slider, QDoubleSpinBox*& spin, const QString& name) {
            auto* row = new QHBoxLayout;
            slider = new QSlider(Qt::Horizontal);
            slider->setObjectName(name + "Slider");
            slider->setMinimumWidth(40);
            spin = new QDoubleSpinBox;
            spin->setObjectName(name + "Spin");
            spin->setKeyboardTracking(false);  // a typed value is one change, on Enter or leaving the box
            row->addWidget(rowLabel(label));
            row->addWidget(slider, 1);
            row->addWidget(spin);
            l->addLayout(row);
            // A drag is one undo step: the changes while the slider is held replace each other.
            connect(slider, &QSlider::sliderPressed, this, [this] {
                sliderHeld_ = true;
                endMerge();
            });
            connect(slider, &QSlider::sliderReleased, this, [this] {
                sliderHeld_ = false;
                endMerge();
            });
        };
        sliderRow("Intensity", intensitySlider_, intensitySpin_, "stockSunIntensity");
        intensitySlider_->setRange(0, kIntensitySteps);
        intensitySlider_->setToolTip("Sun intensity, 0.1 to 1000 (logarithmic); stock suns are about 8 (mp_shipment) to 31 (mp_br_tut)");
        intensitySpin_->setRange(0, 1000000);
        intensitySpin_->setDecimals(2);
        intensitySpin_->setSingleStep(0.5);
        intensitySpin_->setMaximumWidth(96);
        intensitySpin_->setToolTip("Sun intensity (iw8r_sun_intensity, 0 to 1000000)");
        connect(intensitySlider_, &QSlider::valueChanged, this, [this](int pos) {
            const double val = rounded(intensityOfSlider(pos), intensityOfSlider(pos) < 10 ? 2 : 1);
            {
                const QSignalBlocker b(intensitySpin_);
                intensitySpin_->setValue(val);
            }
            setSunIntensity(val, "intensity");
        });
        connect(intensitySpin_, &QDoubleSpinBox::valueChanged, this, [this](double val) {
            {
                const QSignalBlocker b(intensitySlider_);
                intensitySlider_->setValue(sliderOfIntensity(val));
            }
            setSunIntensity(val, "intensity");
        });

        sliderRow("Pitch", pitchSlider_, pitchSpin_, "stockSunPitch");
        pitchSlider_->setRange(-900, -10);  // tenths of a degree
        pitchSpin_->setRange(-90, -1);
        pitchSpin_->setDecimals(2);
        pitchSpin_->setSingleStep(1);
        pitchSpin_->setMaximumWidth(80);
        const QString dirTip = "Sun direction \"pitch yaw\" toward the sun (iw8r_sun_direction): pitch -90 (straight above) to -1 "
                               "(on the horizon). A new direction bakes the sun shadow again when the zone is rebuilt";
        pitchSlider_->setToolTip(dirTip);
        pitchSpin_->setToolTip(dirTip);
        sliderRow("Yaw", yawSlider_, yawSpin_, "stockSunYaw");
        yawSlider_->setRange(-1800, 1800);
        yawSpin_->setRange(-180, 180);
        yawSpin_->setWrapping(true);
        yawSpin_->setDecimals(2);
        yawSpin_->setSingleStep(1);
        yawSpin_->setMaximumWidth(80);
        yawSlider_->setToolTip(dirTip);
        yawSpin_->setToolTip(dirTip);
        for (QSlider* s : {pitchSlider_, yawSlider_})
            connect(s, &QSlider::valueChanged, this, [this, s](int pos) {
                QDoubleSpinBox* spin = s == pitchSlider_ ? pitchSpin_ : yawSpin_;
                {
                    const QSignalBlocker b(spin);
                    spin->setValue(pos / 10.0);
                }
                applyDirection("direction");
            });
        for (QDoubleSpinBox* sp : {pitchSpin_, yawSpin_})
            connect(sp, &QDoubleSpinBox::valueChanged, this, [this, sp](double val) {
                QSlider* s = sp == pitchSpin_ ? pitchSlider_ : yawSlider_;
                {
                    const QSignalBlocker b(s);
                    s->setValue(int(std::lround(val * 10)));
                }
                applyDirection("direction");
            });

        gameLighting_ = new QCheckBox("Game Lighting (shows the sun)");
        gameLighting_->setObjectName("stockSunGameLighting");
        gameLighting_->setToolTip("View > Game Lighting: the viewport lit as the built map looks in game, with this sun");
        l->addWidget(gameLighting_);
        // The View menu's command (made after the docks: bindGameLighting finds it on the first refresh).
        connect(gameLighting_, &QCheckBox::toggled, this, [this](bool on) {
            bindGameLighting();
            if (gameAction_ && gameAction_->isChecked() != on) gameAction_->trigger();
            refreshSun();
        });
        sunInfo_ = noteLabel();
        sunInfo_->setObjectName("stockSunInfo");
        l->addWidget(sunInfo_);
        resetSun_ = button("Reset to Stock", "Removes iw8r_sun_color, iw8r_sun_intensity and iw8r_sun_direction: the map's own sun again",
                           "stockSunReset");
        connect(resetSun_, &QPushButton::clicked, this, [this] { resetSun(); });
        l->addWidget(flowRow({resetSun_}));
        v->addWidget(box);
    }

    // ---- Sky
    {
        auto* box = new QGroupBox("Sky");
        auto* l = new QVBoxLayout(box);
        skyCombo_ = new QComboBox;
        skyCombo_->setObjectName("stockSkyCombo");
        skyCombo_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        skyCombo_->setMinimumContentsLength(14);  // narrow: long names are cut in the box, whole in the list
        skyCombo_->setToolTip("Another stock map whose sky this map uses (iw8r_sky_from); the map's baked lighting stays");
        l->addWidget(skyCombo_);
        connect(skyCombo_, &QComboBox::activated, this, [this](int i) { setSkyFrom(skyCombo_->itemData(i).toString()); });
        skyInfo_ = noteLabel();
        l->addWidget(skyInfo_);
        v->addWidget(box);
    }

    // ---- Retexture
    {
        auto* box = new QGroupBox("Retexture");
        auto* l = new QVBoxLayout(box);
        retexTable_ = new QTableWidget(0, 2);
        retexTable_->setObjectName("stockRetextureTable");
        retexTable_->setHorizontalHeaderLabels({"From (stock)", "To"});
        retexTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        retexTable_->horizontalHeader()->setMinimumSectionSize(40);
        retexTable_->verticalHeader()->hide();
        retexTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
        retexTable_->setSelectionMode(QAbstractItemView::SingleSelection);
        retexTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        retexTable_->setTextElideMode(Qt::ElideMiddle);
        retexTable_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        retexTable_->setWordWrap(false);
        retexTable_->setToolTip("The stock materials swapped on the world surfaces (iw8r_stock_retexture); pick a row to change it");
        l->addWidget(retexTable_);
        connect(retexTable_, &QTableWidget::itemSelectionChanged, this, [this] {
            const auto rows = retexTable_->selectionModel()->selectedRows();
            if (!rows.isEmpty()) {
                const int r = rows.front().row();
                if (QTableWidgetItem* f = retexTable_->item(r, 0)) fromEdit_->setText(f->data(Qt::UserRole).toString());
                if (QTableWidgetItem* t = retexTable_->item(r, 1)) toEdit_->setText(t->data(Qt::UserRole).toString());
            }
            updateSelectionButtons();
        });
        auto* frow = new QHBoxLayout;
        fromEdit_ = new QLineEdit;
        fromEdit_->setObjectName("stockRetextureFrom");
        fromEdit_->setPlaceholderText("stock material of this map");
        fromEdit_->setToolTip("The stock material to replace: type part of a name (the list offers this map's world materials)");
        fromModel_ = new QStringListModel(this);
        auto* completer = new QCompleter(fromModel_, this);
        completer->setCaseSensitivity(Qt::CaseInsensitive);
        completer->setFilterMode(Qt::MatchContains);
        completer->setMaxVisibleItems(12);
        fromEdit_->setCompleter(completer);
        frow->addWidget(rowLabel("From"));
        frow->addWidget(fromEdit_, 1);
        l->addLayout(frow);
        auto* trow = new QHBoxLayout;
        toEdit_ = new QLineEdit;
        toEdit_->setObjectName("stockRetextureTo");
        toEdit_->setPlaceholderText("new material");
        toEdit_->setToolTip("The material it becomes (a stock material the rebuild can carry: like for like, a blend for a blend)");
        auto* fromBrowser = new QToolButton;
        fromBrowser->setObjectName("stockRetextureToBrowser");
        fromBrowser->setText("Browser");
        fromBrowser->setToolTip("The material highlighted in the Materials browser (else the current material)");
        trow->addWidget(rowLabel("To"));
        trow->addWidget(toEdit_, 1);
        trow->addWidget(fromBrowser);
        l->addLayout(trow);
        connect(fromBrowser, &QToolButton::clicked, this, [this] {
            const QString m = browserMaterial();
            if (m.isEmpty()) return ctx_->setStatus("Retexture: pick a material in the Materials browser first");
            toEdit_->setText(m);
            updateSelectionButtons();
        });
        for (QLineEdit* e : {fromEdit_, toEdit_}) connect(e, &QLineEdit::textChanged, this, [this] { updateSelectionButtons(); });
        retexSet_ = button("Set Pair", "From becomes To on the world surfaces (one undo step); the viewport shows it at once", "stockRetextureSet");
        retexRemove_ = button("Remove Pair", "The From material stays as stock", "stockRetextureRemove");
        connect(retexSet_, &QPushButton::clicked, this, [this] { setRetexturePair(fromEdit_->text(), toEdit_->text()); });
        connect(retexRemove_, &QPushButton::clicked, this, [this] { removeRetexturePair(fromEdit_->text().trimmed()); });
        connect(toEdit_, &QLineEdit::returnPressed, retexSet_, &QPushButton::click);
        l->addWidget(flowRow({retexSet_, retexRemove_}));
        retexInfo_ = noteLabel();
        l->addWidget(retexInfo_);
        v->addWidget(box);
    }

    // ---- World removal boxes
    {
        auto* box = new QGroupBox("Remove World Geometry");
        auto* l = new QVBoxLayout(box);
        boxList_ = new QListWidget;
        boxList_->setObjectName("stockRemovalList");
        boxList_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        boxList_->setTextElideMode(Qt::ElideRight);
        boxList_->setToolTip("The iw8r_remove_world boxes. Checked: the world triangles inside leave the drawing (key render); "
                             "unchecked: they stay drawn and only the collision goes. Double-click frames a box");
        l->addWidget(boxList_);
        connect(boxList_, &QListWidget::itemChanged, this, [this](QListWidgetItem* it) {
            const Id id = Id(it->data(Qt::UserRole).toULongLong());
            const Entity* e = ctx_->doc.findEntity(id);
            if (!e) return;
            const bool want = it->checkState() == Qt::Checked;
            if (want != !keyOff(*e, "render")) setRemovalBoxRender(id, want);
        });
        connect(boxList_, &QListWidget::itemSelectionChanged, this, [this] { updateSelectionButtons(); });
        connect(boxList_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* it) {
            selectBox(Id(it->data(Qt::UserRole).toULongLong()), true);
        });
        boxAdd_ = button("Add Box Around Selection",
                         "A new iw8r_remove_world box (caulk) around the selection: the world triangles with all three corners "
                         "inside leave the viewport at once; Rebuild Stock Zone takes them out of the drawing and the collision",
                         "stockRemovalAdd");
        boxMake_ = button("Selected Brushes Become Boxes",
                          "The selected world brushes become one iw8r_remove_world entity (each brush one box)", "stockRemovalMake");
        boxSelect_ = button("Select", "Select the box in the views (double-click a row to frame it too)", "stockRemovalSelect");
        boxDelete_ = button("Delete", "Delete the box: the world inside it is drawn again (Undo brings the box back)", "stockRemovalDelete");
        boxCollision_ = button("Keep Collision", "Whether the rebuild takes the collision inside the box out (key collision)",
                               "stockRemovalCollision");
        connect(boxAdd_, &QPushButton::clicked, this, [this] { addRemovalBoxAroundSelection(); });
        connect(boxMake_, &QPushButton::clicked, this, [this] { makeSelectedBrushesBoxes(); });
        connect(boxSelect_, &QPushButton::clicked, this, [this] { selectBox(selectedBox(), false); });
        connect(boxDelete_, &QPushButton::clicked, this, [this] {
            const Id id = selectedBox();
            if (id == kNoId) return;
            ctx_->doc.deselectAll();
            ctx_->doc.select({id, kNoId}, false);
            endMerge();
            if (ctx_->doc.deleteSelection()) ctx_->documentEdited("World removal box deleted: the world inside it is drawn again (Undo brings it back)");
        });
        connect(boxCollision_, &QPushButton::clicked, this, [this] {
            const Id id = selectedBox();
            if (const Entity* e = ctx_->doc.findEntity(id)) setRemovalBoxCollision(id, keyOff(*e, "collision"));
        });
        l->addWidget(flowRow({boxAdd_, boxMake_, boxSelect_, boxDelete_, boxCollision_}));
        boxInfo_ = noteLabel();
        l->addWidget(boxInfo_);
        v->addWidget(box);
    }

    // ---- Hidden props
    {
        auto* box = new QGroupBox("Stock Props");
        auto* l = new QVBoxLayout(box);
        propsInfo_ = noteLabel();
        propsInfo_->setObjectName("stockPropsInfo");
        l->addWidget(propsInfo_);
        propsHide_ = button("Hide In Selection", "File > Stock Map > Hide Stock Props In Selection: the props whose origin is inside the "
                                                 "selection leave the view and the rebuilt map", "stockPropsHide");
        propsShow_ = button("Show All", "File > Stock Map > Show All Stock Props", "stockPropsShow");
        connect(propsHide_, &QPushButton::clicked, this, [this] {
            endMerge();
            w_->hideStockPropsInSelection();
        });
        connect(propsShow_, &QPushButton::clicked, this, [this] {
            endMerge();
            w_->showAllStockProps();
        });
        l->addWidget(flowRow({propsHide_, propsShow_}));
        // One prop clicked in the camera (MainWindow::stockPropPicked).
        pickedPropInfo_ = noteLabel("Click a stock prop in the camera to hide just that one.");
        pickedPropInfo_->setObjectName("stockPickedPropInfo");
        pickedPropInfo_->setTextFormat(Qt::PlainText);
        l->addWidget(pickedPropInfo_);
        pickedPropHide_ = button("Hide Picked Prop", "Adds the picked prop's row to iw8r_hide_smodels (one undo step)", "stockPickedPropHide");
        pickedPropHide_->setEnabled(false);
        connect(pickedPropHide_, &QPushButton::clicked, this, [this] { togglePickedPropHidden(); });
        l->addWidget(flowRow({pickedPropHide_}));
        v->addWidget(box);
    }

    // ---- Into the game
    {
        auto* box = new QGroupBox("Put It In the Game");
        auto* l = new QVBoxLayout(box);
        l->addWidget(noteLabel("Rebuild writes the edited zone in a few seconds and checks it (the Console shows the folder). "
                               "Install puts it in the game's zone folder: close the game first."));
        auto* rebuild = button("Rebuild Stock Zone", "File > Stock Map > Rebuild Stock Zone (fast)", "stockPanelRebuild");
        auto* install = button("Install Into Game...", "File > Stock Map > Install Edited Stock Map Into Game", "stockPanelInstall");
        auto* restore = button("Restore Stock Map...", "File > Stock Map > Restore Stock Map In Game", "stockPanelRestore");
        const auto run = [this](const char* command) {
            return [this, command] {
                endMerge();
                if (QAction* a = w_->action(command)) a->trigger();
            };
        };
        connect(rebuild, &QPushButton::clicked, this, run("RebuildStockZoneFast"));
        connect(install, &QPushButton::clicked, this, run("InstallStockMap"));
        connect(restore, &QPushButton::clicked, this, run("RestoreStockMap"));
        l->addWidget(flowRow({rebuild, install, restore}));
        v->addWidget(box);
    }
    v->addStretch(1);

    connect(ctx_, &EditorContext::documentChanged, this, [this] { scheduleRefresh(); });
    // Twice a second: the stock sun and world arrive from worker threads without a document change (shown: the panel
    // reads them; hidden: only the side bar icon follows the open map, a cheap check).
    poll_.setInterval(500);
    connect(&poll_, &QTimer::timeout, this, [this] {
        if (isVisible()) refresh();
        else updateStockVisibility();
    });
    poll_.start();
    scheduleRefresh();  // after the side bar exists: its icon is hidden until a stock map opens
}

// ------------------------------------------------------------------ state

bool StockMapPanel::stockMapOpen() const { return !w_->stockZone().isEmpty(); }

StockMapPanel::Sun StockMapPanel::stockSun() const {
    Sun s;
    if (stockMapOpen() && w_->stockSunBaseKnown_) {
        s.known = true;
        s.intensity = w_->stockSunBaseIntensity_;
        s.color[0] = w_->stockSunBaseColor_.x, s.color[1] = w_->stockSunBaseColor_.y, s.color[2] = w_->stockSunBaseColor_.z;
        // The zone's sun light direction is toward the sun: (cos p cos y, cos p sin y, -sin p), as iw8r_sun_direction.
        const Vec3 t = normalize(w_->stockSunBaseToward_);
        s.pitch = std::clamp(-std::asin(std::clamp(t.z, -1.0, 1.0)) / kDeg, -90.0, -1.0);
        s.yaw = wrapYaw(std::atan2(t.y, t.x) / kDeg);
        return s;
    }
    // Not read yet (the reference is loading, or off): the worldspawn's own sundirection, the Relight Sun start text.
    if (const Entity* w = worldOf(ctx_->doc))
        if (const std::string* d = w->value("sundirection")) {
            double p = 0, y = 0;
            if (parseStockSunDirection(*d, p, y)) s.pitch = p, s.yaw = wrapYaw(y);
        }
    return s;
}

StockMapPanel::Sun StockMapPanel::shownSun() const {
    Sun s = stockSun();
    const Entity* w = worldOf(ctx_->doc);
    if (!w) return s;
    if (const std::string* c = w->value("iw8r_sun_color")) parseColor(*c, s.color);
    if (const std::string* i = w->value("iw8r_sun_intensity")) parseIntensity(*i, s.intensity);
    if (const std::string* d = w->value("iw8r_sun_direction")) {
        double p = 0, y = 0;
        if (parseStockSunDirection(*d, p, y)) s.pitch = p, s.yaw = wrapYaw(y);
    }
    return s;
}

QStringList StockMapPanel::skyChoices() const {
    const SkyCatalog& c = skyCatalog();
    QStringList out;
    for (const QStringList* l : {&c.withSky, &c.other})
        for (const QString& id : *l)
            if (id != w_->stockZone()) out << id;
    return out;
}

std::vector<std::pair<QString, QString>> StockMapPanel::retexturePairs() const {
    return parseRetexture(worldValue(ctx_->doc, "iw8r_stock_retexture")).pairs;
}

QStringList StockMapPanel::worldMaterials() const {
    std::set<QString> names;
    if (const auto& world = w_->stockWorld_)
        for (const ImportedMesh& m : world->meshes)
            if (!m.material.empty()) names.insert(qs(m.material));
    return QStringList(names.begin(), names.end());
}

QString StockMapPanel::browserMaterial() const {
    if (AssetBrowser* mb = w_->materialBrowser())
        for (QListView* lv : mb->findChildren<QListView*>())  // the material list, not a combo box's or completer's popup
            if (lv->window()->windowType() != Qt::Popup && lv->currentIndex().isValid())
                return lv->currentIndex().data(Qt::DisplayRole).toString();
    return qs(ctx_->currentMaterial);
}

std::vector<Id> StockMapPanel::removalBoxes() const {
    std::vector<Id> out;
    for (const Entity& e : ctx_->doc.map().entities)
        if (isRemoveWorldClass(e.classname())) out.push_back(e.id);
    return out;
}

size_t StockMapPanel::hiddenPropCount() const { return stockEditHiddenProps(ctx_->doc.map()).size(); }

Id StockMapPanel::selectedBox() const {
    const auto items = boxList_->selectedItems();
    return items.isEmpty() ? kNoId : Id(items.front()->data(Qt::UserRole).toULongLong());
}

// ------------------------------------------------------------------ edits

bool StockMapPanel::canMerge(const QString& step, const QString& mergeTag) const {
    if (mergeTag.isEmpty() || mergeTag != mergeTag_) return false;
    const Document& doc = ctx_->doc;
    if (doc.undoPosition() != mergePos_ || doc.undoPosition() == 0 || doc.canRedo() || doc.inGroup()) return false;
    if (doc.history()[doc.undoPosition() - 1].name != step.toStdString()) return false;
    return sliderHeld_ || mergeClock_.elapsed() < 1000;
}

void StockMapPanel::noteMerge(const QString& mergeTag) {
    mergeTag_ = mergeTag;
    mergePos_ = ctx_->doc.undoPosition();
    mergeClock_.restart();
}

bool StockMapPanel::editKeys(Id entity, const QString& step, const std::vector<KeyEdit>& edits, const QString& mergeTag, const QString& status) {
    Document& doc = ctx_->doc;
    if (entity == kNoId || !doc.findEntity(entity)) return false;
    // The previous change of this control is taken back first: the step then goes from the value before the drag
    // (or the run of steps) to this one.
    const bool merge = canMerge(step, mergeTag);
    if (merge) doc.undo();
    const size_t before = doc.undoPosition();
    doc.beginGroup(step.toStdString());
    for (const KeyEdit& e : edits) {
        const Entity* ent = doc.findEntity(entity);
        if (!ent) break;
        const std::string* cur = ent->value(e.key);
        if (e.value) {
            if (!cur || *cur != *e.value) doc.setKeyValue(entity, e.key, *e.value);
        } else if (cur) {
            doc.removeKey(entity, e.key);
        }
    }
    doc.endGroup();
    const bool changed = doc.undoPosition() != before;
    if (changed && !mergeTag.isEmpty()) noteMerge(mergeTag);
    else endMerge();
    if (!changed && !merge) return false;
    ctx_->documentEdited();
    if (!status.isEmpty()) merge ? ctx_->setTransientStatus(status) : ctx_->setStatus(status);
    return changed;
}

bool StockMapPanel::editWorld(const QString& step, const std::vector<KeyEdit>& edits, const QString& mergeTag, const QString& status) {
    const Entity* w = worldOf(ctx_->doc);
    if (!w || !stockMapOpen()) {
        ctx_->setStatus("Stock Map: open a stock map first (File > Open IW8 Stock Map)");
        return false;
    }
    return editKeys(w->id, step, edits, mergeTag, status);
}

void StockMapPanel::setSunColor(double r, double g, double b, const QString& mergeTag) {
    const std::string text = fixed(std::clamp(r, 0.0, 16.0), 4) + " " + fixed(std::clamp(g, 0.0, 16.0), 4) + " " +
                             fixed(std::clamp(b, 0.0, 16.0), 4);
    editWorld("Stock Sun Colour", {{"iw8r_sun_color", text}}, mergeTag,
              "Stock sun colour " + qs(text) + " (linear); Rebuild Stock Zone writes it");
    w_->applyStockSunPreview();  // at once (the world edits follow the document a moment later)
}

void StockMapPanel::setSunIntensity(double v, const QString& mergeTag) {
    const std::string text = fixed(std::clamp(v, 0.0, 1000000.0), 3);
    editWorld("Stock Sun Intensity", {{"iw8r_sun_intensity", text}}, mergeTag,
              "Stock sun intensity " + qs(text) + "; Rebuild Stock Zone writes it");
    w_->applyStockSunPreview();
}

void StockMapPanel::setSunDirection(double pitch, double yaw, const QString& mergeTag) {
    pitch = std::clamp(pitch, -90.0, -1.0);
    yaw = wrapYaw(yaw);
    // The stock direction itself needs no key: a key, even an equal one, makes the rebuild bake the sun shadow again.
    const Sun st = stockSun();
    const bool stock = st.known && std::fabs(pitch - st.pitch) < 1e-3 && std::fabs(wrapYaw(yaw - st.yaw)) < 1e-3;
    const std::string text = fixed(pitch, 3) + " " + fixed(yaw, 3);
    editWorld("Stock Sun Direction", {{"iw8r_sun_direction", stock ? std::nullopt : std::optional<std::string>(text)}}, mergeTag,
              stock ? QString("Stock sun direction: the map's own again")
                    : "Stock sun direction " + qs(text) + "; Rebuild Stock Zone bakes the sun shadow again");
    w_->applyStockSunPreview();
}

void StockMapPanel::applyDirection(const QString& mergeTag) {
    double p = pitchSpin_->value(), y = yawSpin_->value();
    // The boxes show two decimals: a value the box shows unchanged keeps its full precision.
    const Sun cur = shownSun();
    if (std::fabs(p - rounded(cur.pitch, 2)) < 1e-6) p = cur.pitch;
    if (std::fabs(y - rounded(cur.yaw, 2)) < 1e-6) y = cur.yaw;
    setSunDirection(p, y, mergeTag);
}

void StockMapPanel::resetSun() {
    editWorld("Reset Stock Sun", {{"iw8r_sun_color", std::nullopt}, {"iw8r_sun_intensity", std::nullopt}, {"iw8r_sun_direction", std::nullopt}},
              {}, "The stock sun again (colour, intensity and direction of the map)");
    w_->applyStockSunPreview();
}

void StockMapPanel::pickColor() {
    const Sun cur = shownSun();
    QColorDialog dlg(QColor(displayOf(cur.color[0]), displayOf(cur.color[1]), displayOf(cur.color[2])), this);
    dlg.setObjectName("stockSunColorDialog");
    dlg.setWindowTitle("Stock Sun Colour");
    dlg.setOption(QColorDialog::DontUseNativeDialog);  // the native dialog reports no colour until it closes
    endMerge();
    sliderHeld_ = true;  // the whole pick is one undo step
    bool picked = false;
    connect(&dlg, &QColorDialog::currentColorChanged, this, [this, &picked](const QColor& c) {
        if (!c.isValid()) return;
        picked = true;
        setSunColor(linearOf(c.red()), linearOf(c.green()), linearOf(c.blue()), "colour-pick");
    });
    const bool ok = dlg.exec() == QDialog::Accepted;
    if (ok && dlg.selectedColor().isValid()) {
        const QColor c = dlg.selectedColor();
        setSunColor(linearOf(c.red()), linearOf(c.green()), linearOf(c.blue()), "colour-pick");
    }
    sliderHeld_ = false;
    if (!ok && picked && mergeTag_ == "colour-pick" && ctx_->doc.undoPosition() == mergePos_ && !ctx_->doc.canRedo()) {
        ctx_->doc.undo();  // Cancel: the colour before the pick
        ctx_->documentEdited("Stock sun colour: the pick was cancelled, the colour is as before");
        w_->applyStockSunPreview();
    }
    endMerge();
}

void StockMapPanel::setSkyFrom(const QString& mapId) {
    const QString id = mapId.trimmed().toLower();
    endMerge();
    editWorld("Stock Sky", {{"iw8r_sky_from", id.isEmpty() ? std::nullopt : std::optional<std::string>(id.toStdString())}}, {},
              id.isEmpty() ? QString("The map keeps its own sky")
                           : "The sky of " + id + " is shown (fetched once from that map); Rebuild Stock Zone carries it in (the "
                                                  "map's baked lighting stays)");
    refreshSky();
}

void StockMapPanel::writeRetexture(const std::vector<std::pair<QString, QString>>& pairs, const QStringList& extras, const QString& step,
                                   const QString& status) {
    QStringList parts;
    for (const auto& [from, to] : pairs) parts << from + "=" + to;
    parts << extras;
    const QString value = parts.join("; ");
    endMerge();
    editWorld(step, {{"iw8r_stock_retexture", value.isEmpty() ? std::nullopt : std::optional<std::string>(value.toStdString())}}, {}, status);
}

bool StockMapPanel::setRetexturePair(const QString& fromIn, const QString& toIn) {
    const QString from = fromIn.trimmed(), to = toIn.trimmed();
    if (!materialName(from) || !materialName(to)) {
        ctx_->setStatus("Retexture: From and To must be material names (letters, digits and _ / - . ~ & #)");
        return false;
    }
    if (from == to) {
        ctx_->setStatus("Retexture: From and To are the same material");
        return false;
    }
    Retexture r = parseRetexture(worldValue(ctx_->doc, "iw8r_stock_retexture"));
    bool replaced = false;
    for (auto& p : r.pairs)
        if (p.first == from) p.second = to, replaced = true;
    if (!replaced) r.pairs.push_back({from, to});
    writeRetexture(r.pairs, r.extras, "Stock Retexture", {});
    // Only the pairs the rebuild can carry are shown (MainWindow::stockWorldEdits takes the rebuild's check).
    const auto remap = w_->stockWorldEdits().remap;
    const auto it = remap.find(from.toStdString());
    const bool taken = it != remap.end() && it->second == to.toStdString();
    ctx_->setStatus(taken ? "Retexture: " + from + " becomes " + to + " (shown in the viewport a moment later; Rebuild Stock Zone writes it)"
                          : "Retexture: " + from + " = " + to + " is kept in the map, but " + to +
                                " cannot be carried into a stock map (not in the material catalogue, or only an always-loaded zone has it): "
                                "the viewport and the rebuild leave it out");
    return true;
}

bool StockMapPanel::removeRetexturePair(const QString& from) {
    Retexture r = parseRetexture(worldValue(ctx_->doc, "iw8r_stock_retexture"));
    const size_t before = r.pairs.size();
    r.pairs.erase(std::remove_if(r.pairs.begin(), r.pairs.end(), [&](const auto& p) { return p.first == from; }), r.pairs.end());
    if (r.pairs.size() == before) {
        ctx_->setStatus("Retexture: no pair for " + (from.isEmpty() ? QString("(nothing)") : from));
        return false;
    }
    writeRetexture(r.pairs, r.extras, "Stock Retexture", "Retexture: " + from + " is the stock material again");
    return true;
}

Id StockMapPanel::addRemovalBoxAroundSelection() {
    Document& doc = ctx_->doc;
    AABB b = doc.selectionBounds();
    if (!stockMapOpen() || !b.valid()) {
        ctx_->setStatus("Add Box Around Selection: select what the box should enclose first (a brush drawn around the walls, or props "
                        "and entities)");
        return kNoId;
    }
    for (int a = 0; a < 3; ++a)
        if (b.maxs[a] - b.mins[a] < 2) {  // a flat selection still gives a box
            const double mid = (b.maxs[a] + b.mins[a]) * 0.5;
            b.mins[a] = mid - 1, b.maxs[a] = mid + 1;
        }
    const auto prim = makeBrushPrimitive(makeBox(b, " caulk 128 128 0 0 0 0"), doc.eol());
    if (!prim) {
        ctx_->setStatus("Add Box Around Selection: the box could not be made");
        return kNoId;
    }
    endMerge();
    const Id id = doc.addEntity({{"classname", kRemoveWorldClass}}, {*prim}, "Add World Removal Box");
    ctx_->documentEdited(QString("World removal box %1 x %2 x %3 added: the world triangles inside leave the viewport; Rebuild Stock Zone "
                                 "takes them out of the drawing and the collision")
                             .arg(b.maxs.x - b.mins.x, 0, 'f', 0).arg(b.maxs.y - b.mins.y, 0, 'f', 0).arg(b.maxs.z - b.mins.z, 0, 'f', 0));
    return id;
}

Id StockMapPanel::makeSelectedBrushesBoxes() {
    Document& doc = ctx_->doc;
    const Entity* w = worldOf(doc);
    bool worldOnly = w && !doc.selection().empty();
    for (const SelRef& r : doc.selection()) worldOnly = worldOnly && r.entity == w->id && r.item != kNoId;
    if (!stockMapOpen() || !worldOnly) {
        ctx_->setStatus("Selected Brushes Become Boxes: select world brushes only (drawn around the geometry to take out)");
        return kNoId;
    }
    const size_t n = doc.selection().size();
    endMerge();
    const Id id = doc.groupSelection(kRemoveWorldClass);
    if (id != kNoId)
        ctx_->documentEdited(QString("%1 brush(es) became a world removal entity: the world triangles inside leave the viewport; "
                                     "Rebuild Stock Zone takes them out of the drawing and the collision").arg(n));
    return id;
}

bool StockMapPanel::setRemovalBoxRender(Id entity, bool removeDrawing) {
    endMerge();
    return editKeys(entity, "Removal Box Drawing", {{"render", removeDrawing ? std::nullopt : std::optional<std::string>("0")}}, {},
                    removeDrawing ? QString("Removal box: the world triangles inside leave the drawing")
                                  : QString("Removal box: the world inside stays drawn (only its collision goes)"));
}

bool StockMapPanel::setRemovalBoxCollision(Id entity, bool removeCollision) {
    endMerge();
    return editKeys(entity, "Removal Box Collision", {{"collision", removeCollision ? std::nullopt : std::optional<std::string>("0")}}, {},
                    removeCollision ? QString("Removal box: the rebuild takes the collision inside out")
                                    : QString("Removal box: the rebuild keeps the collision inside"));
}

void StockMapPanel::selectBox(Id id, bool frame) {
    const Entity* e = ctx_->doc.findEntity(id);
    if (!e) return;
    ctx_->doc.deselectAll();
    bool any = false;
    for (const EntityItem& it : e->items)
        if (it.isPrimitive()) ctx_->doc.select({id, it.id}, true), any = true;
    if (!any) ctx_->doc.select({id, kNoId}, true);
    ctx_->documentEdited();
    if (frame) ctx_->frameSelection();
}

void StockMapPanel::setPickedMaterial(const QString& material) {
    const QString m = material.trimmed();
    if (m.isEmpty()) return;
    fromEdit_->setText(m);
    for (const auto& [from, to] : retexturePairs())
        if (from == m) toEdit_->setText(to);
    ctx_->setStatus("Retexture From: " + m + " (choose To, then Set Pair)");
    updateSelectionButtons();
}

void StockMapPanel::setPickedProp(const QString& model, uint32_t row) {
    pickedModel_ = model;
    pickedRow_ = row;
    pickedZone_ = w_->stockZone();
    refreshProps();
    const std::vector<uint32_t> hidden = stockEditHiddenProps(ctx_->doc.map());
    const bool isHidden = std::find(hidden.begin(), hidden.end(), row) != hidden.end();
    ctx_->setStatus(QString("Picked stock prop %1 (row %2)%3").arg(model).arg(row)
                        .arg(isHidden ? QString(": hidden; Show Picked Prop in the Stock Map panel brings it back")
                                      : QString(": Hide Picked Prop in the Stock Map panel hides it")));
}

bool StockMapPanel::togglePickedPropHidden() {
    if (!pickedRow_ || pickedZone_ != w_->stockZone() || !stockMapOpen()) {
        ctx_->setStatus("Hide Picked Prop: click a stock prop in the camera first");
        return false;
    }
    std::vector<uint32_t> hidden = stockEditHiddenProps(ctx_->doc.map());
    std::sort(hidden.begin(), hidden.end());
    hidden.erase(std::unique(hidden.begin(), hidden.end()), hidden.end());
    const auto at = std::lower_bound(hidden.begin(), hidden.end(), *pickedRow_);
    const bool show = at != hidden.end() && *at == *pickedRow_;
    if (show) hidden.erase(at);
    else hidden.insert(at, *pickedRow_);
    std::string list;
    for (const uint32_t i : hidden) list += (list.empty() ? "" : " ") + std::to_string(i);
    endMerge();
    const bool changed = editWorld(show ? "Show Stock Prop" : "Hide Stock Prop",
                                   {{"iw8r_hide_smodels", list.empty() ? std::nullopt : std::optional<std::string>(list)}}, {},
                                   QString("Stock prop %1 (row %2) %3").arg(pickedModel_).arg(*pickedRow_)
                                       .arg(show ? QString("is shown again") : QString("is hidden: the rebuilt map leaves it out")));
    if (changed) w_->rebuildReferenceProps();  // the viewport's props without it (as Hide Stock Props In Selection)
    refreshProps();
    return changed;
}

// ------------------------------------------------------------------ showing

void StockMapPanel::showEvent(QShowEvent* e) {
    QWidget::showEvent(e);
    shownSig_.clear();
    scheduleRefresh();
}

void StockMapPanel::scheduleRefresh() {
    if (refreshQueued_) return;
    refreshQueued_ = true;
    // Deferred: openStockZone names the zone only after the load's documentChanged.
    QTimer::singleShot(0, this, [this] { refresh(); });
}

void StockMapPanel::bindGameLighting() {
    if (gameAction_) return;
    gameAction_ = w_->action("ToggleGameLighting");
    if (!gameAction_) return;
    {
        const QSignalBlocker b(gameLighting_);
        gameLighting_->setChecked(gameAction_->isChecked());
    }
    connect(gameAction_, &QAction::toggled, gameLighting_, [this](bool on) {
        {
            const QSignalBlocker b(gameLighting_);
            gameLighting_->setChecked(on);
        }
        if (isVisible()) refreshSun();
    });
}

void StockMapPanel::updateStockVisibility() {
    bindGameLighting();
    const bool open = stockMapOpen();
    if (!sideAction_) sideAction_ = w_->findChild<QAction*>(kSideAction);
    if (sideAction_ && sideAction_->isVisible() != open) sideAction_->setVisible(open);
    if (!dock_)
        if (auto* dm = w_->findChild<ads::CDockManager*>()) dock_ = dm->findDockWidget(kDockName);
    // Another map than a stock map: the panel goes with its icon.
    if (!open && dock_ && !dock_->isClosed()) dock_->toggleView(false);
}

void StockMapPanel::refresh() {
    refreshQueued_ = false;
    updateStockVisibility();
    const bool open = stockMapOpen();
    pages_->setCurrentIndex(open ? 1 : 0);
    if (!open || !isVisible()) return;
    updateSelectionButtons();
    const Document& doc = ctx_->doc;
    const QString sig = QString("%1|%2|%3|%4|%5|%6")
                            .arg(doc.revision())
                            .arg(doc.loadSerial())
                            .arg(w_->stockZone())
                            .arg(w_->stockSunBaseKnown_ ? 1 : 0)
                            .arg(quintptr(w_->stockWorld_.get()))
                            .arg(quintptr(w_->refProps_.get()));
    if (sig == shownSig_) return;
    shownSig_ = sig;
    header_->setText("<b>" + w_->stockZone().toHtmlEscaped() +
                     "</b>: the edits below show in the viewport as you make them; Rebuild Stock Zone (at the end) puts them in the game.");
    refreshSun();
    refreshSky();
    refreshRetexture();
    refreshBoxes();
    refreshProps();
    updateSelectionButtons();
}

void StockMapPanel::refreshSun() {
    const Sun s = shownSun(), st = stockSun();
    {
        QPixmap pm(28, 16);
        pm.fill(QColor(displayOf(s.color[0]), displayOf(s.color[1]), displayOf(s.color[2])));
        QPainter p(&pm);
        p.setPen(palette().color(QPalette::WindowText));
        p.drawRect(pm.rect().adjusted(0, 0, -1, -1));
        p.end();
        colorButton_->setIcon(QIcon(pm));
    }
    if (!colorEdit_->hasFocus())
        colorEdit_->setText(qs(fixed(s.color[0], 4) + " " + fixed(s.color[1], 4) + " " + fixed(s.color[2], 4)));
    // A control being dragged keeps its own place (the value it gives may round differently).
    const auto setSpin = [](QDoubleSpinBox* sp, double val) {
        if (std::fabs(sp->value() - val) < 0.5 * std::pow(10.0, -sp->decimals())) return;
        const QSignalBlocker b(sp);
        sp->setValue(val);
    };
    const auto setSlider = [](QSlider* sl, int pos) {
        if (sl->isSliderDown() || sl->value() == pos) return;
        const QSignalBlocker b(sl);
        sl->setValue(pos);
    };
    setSpin(intensitySpin_, s.intensity);
    // The slider's steps are about 1 % apart and its values are rounded: one step off is the same value.
    if (std::abs(sliderOfIntensity(s.intensity) - intensitySlider_->value()) > 1) setSlider(intensitySlider_, sliderOfIntensity(s.intensity));
    setSpin(pitchSpin_, rounded(s.pitch, 2));
    setSpin(yawSpin_, rounded(s.yaw, 2));
    setSlider(pitchSlider_, int(std::lround(s.pitch * 10)));
    setSlider(yawSlider_, int(std::lround(s.yaw * 10)));

    const Entity* w = worldOf(ctx_->doc);
    QStringList changed;
    bool direction = false;
    if (w) {
        if (w->value("iw8r_sun_color")) changed << "colour";
        if (w->value("iw8r_sun_intensity")) changed << "intensity";
        if (w->value("iw8r_sun_direction")) changed << "direction", direction = true;
    }
    QString info = st.known ? QString("Stock: colour %1 %2 %3, intensity %4, direction %5 %6.")
                                  .arg(st.color[0], 0, 'f', 2).arg(st.color[1], 0, 'f', 2).arg(st.color[2], 0, 'f', 2)
                                  .arg(st.intensity, 0, 'f', 2).arg(st.pitch, 0, 'f', 2).arg(st.yaw, 0, 'f', 2)
                            : QString("The stock sun is not read yet (the map's world is loading, or the reference is off): the "
                                      "values start from the Relight Sun defaults.");
    info += changed.isEmpty() ? QString(" Nothing changed.") : " Changed: " + changed.join(", ") + ".";
    if (direction) info += " A new direction bakes the sun shadow again when the zone is rebuilt (slower).";
    if (gameLighting_ && !gameLighting_->isChecked()) info += " Turn on Game Lighting to see the sun.";
    sunInfo_->setText(info);
    resetSun_->setEnabled(!changed.isEmpty());
}

void StockMapPanel::refreshSky() {
    const QString zone = w_->stockZone();
    const QString current = worldValue(ctx_->doc, "iw8r_sky_from").trimmed().toLower();
    const QSignalBlocker b(skyCombo_);
    if (skyZone_ != zone) {
        skyZone_ = zone;
        skyCombo_->clear();
        skyCombo_->addItem("This map's own sky", QString());
        const SkyCatalog& c = skyCatalog();
        for (const QString& id : c.withSky)
            if (id != zone) skyCombo_->addItem(id, id);
        if (!c.other.isEmpty()) skyCombo_->insertSeparator(skyCombo_->count());
        for (const QString& id : c.other)
            if (id != zone) {
                skyCombo_->addItem(id + " (no sky material listed)", id);
                skyCombo_->setItemData(skyCombo_->count() - 1, "The material catalogue lists no tw/sky_" + id +
                                                                   ": the viewport may show no sky and the rebuild may not find it",
                                       Qt::ToolTipRole);
            }
    }
    int at = skyCombo_->findData(current);
    if (at < 0) {  // a value typed elsewhere (Entity inspector): shown as it is
        skyCombo_->addItem(current + " (typed)", current);
        at = skyCombo_->count() - 1;
    }
    skyCombo_->setCurrentIndex(at);
    skyInfo_->setText(current.isEmpty() ? QString("The map's own sky.")
                                        : "The viewport shows the sky of " + current +
                                              " (fetched once from that map); Rebuild Stock Zone carries it in. The map's baked lighting "
                                              "stays as it was.");
}

void StockMapPanel::refreshRetexture() {
    if (materialsWorld_ != w_->stockWorld_.get()) {
        materialsWorld_ = w_->stockWorld_.get();
        fromModel_->setStringList(worldMaterials());
    }
    const auto pairs = retexturePairs();
    const auto remap = w_->stockWorldEdits().remap;
    QString keep;
    if (const auto rows = retexTable_->selectionModel()->selectedRows(); !rows.isEmpty())
        if (QTableWidgetItem* f = retexTable_->item(rows.front().row(), 0)) keep = f->data(Qt::UserRole).toString();
    const QSignalBlocker b(retexTable_);
    retexTable_->setRowCount(int(pairs.size()));
    size_t taken = 0;
    for (int r = 0; r < int(pairs.size()); ++r) {
        const auto& [from, to] = pairs[size_t(r)];
        const auto it = remap.find(from.toStdString());
        const bool ok = it != remap.end() && it->second == to.toStdString();
        taken += ok;
        const QString tip = ok ? from + " becomes " + to + " (shown in the viewport; Rebuild Stock Zone writes it)"
                               : from + " = " + to + ": left out, " + to +
                                     " cannot be carried into a stock map (not in the material catalogue, or only an always-loaded "
                                     "zone has it)";
        for (int c = 0; c < 2; ++c) {
            auto* item = new QTableWidgetItem(c == 0 ? from : (ok ? to : "(left out) " + to));
            item->setData(Qt::UserRole, c == 0 ? from : to);
            item->setToolTip(tip);
            if (!ok) {
                QFont f = item->font();
                f.setItalic(true);
                item->setFont(f);
            }
            retexTable_->setItem(r, c, item);
        }
        if (from == keep) retexTable_->selectRow(r);
    }
    const int rowH = retexTable_->verticalHeader()->defaultSectionSize();
    retexTable_->setFixedHeight(retexTable_->horizontalHeader()->sizeHint().height() + rowH * std::clamp(int(pairs.size()), 2, 6) + 4);
    retexInfo_->setText(pairs.empty() ? QString("No material is swapped. Type or pick a stock material (From) and the one it becomes (To), "
                                                "then Set Pair. Replace like with like: a terrain blend with a blend.")
                                      : QString("%1 pair(s): %2 shown in the viewport%3.")
                                            .arg(pairs.size())
                                            .arg(taken)
                                            .arg(taken < pairs.size() ? QString(", %1 left out (their To material cannot be carried)")
                                                                            .arg(pairs.size() - taken)
                                                                      : QString()));
}

void StockMapPanel::refreshBoxes() {
    const Id keep = selectedBox();
    const QSignalBlocker b(boxList_);
    boxList_->clear();
    const std::vector<Id> ids = removalBoxes();
    size_t drawn = 0;
    for (size_t i = 0; i < ids.size(); ++i) {
        const Entity* e = ctx_->doc.findEntity(ids[i]);
        if (!e) continue;
        const AABB bb = entityBounds(*e);
        const bool render = !keyOff(*e, "render"), collision = !keyOff(*e, "collision");
        drawn += render;
        QString text = QString("Box %1").arg(i + 1);
        if (bb.valid()) {
            const Vec3 c = bb.center();
            text += QString(": %1 x %2 x %3 at %4 %5 %6")
                        .arg(bb.maxs.x - bb.mins.x, 0, 'f', 0).arg(bb.maxs.y - bb.mins.y, 0, 'f', 0).arg(bb.maxs.z - bb.mins.z, 0, 'f', 0)
                        .arg(c.x, 0, 'f', 0).arg(c.y, 0, 'f', 0).arg(c.z, 0, 'f', 0);
        } else {
            text += ": no brush";
        }
        if (!collision) text += ", collision kept";
        if (!render && !collision) text += " (does nothing)";
        auto* item = new QListWidgetItem(text);
        item->setData(Qt::UserRole, quint64(ids[i]));
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
        item->setCheckState(render ? Qt::Checked : Qt::Unchecked);
        item->setToolTip(text + (render ? "\nChecked: the world triangles inside leave the drawing" : "\nUnchecked: the world inside stays drawn") +
                         (collision ? "; the rebuild takes the collision inside out" : "; the rebuild keeps the collision"));
        boxList_->addItem(item);
        if (ids[i] == keep) item->setSelected(true);
    }
    const int rowH = boxList_->fontMetrics().height() + 8;
    boxList_->setFixedHeight(rowH * std::clamp(int(ids.size()), 2, 6) + 6);
    boxInfo_->setText(ids.empty() ? QString("No world geometry is removed. Draw a brush around the walls or floor to take out, select it "
                                            "and use Add Box Around Selection (or make the brushes the boxes).")
                                  : QString("%1 box(es), %2 taking out the drawing: the viewport leaves out the world triangles with all "
                                            "three corners inside; Rebuild Stock Zone takes them out of the drawing and the collision. Props "
                                            "are not world geometry: hide them below.")
                                        .arg(ids.size())
                                        .arg(drawn));
}

void StockMapPanel::refreshProps() {
    const size_t hidden = hiddenPropCount();
    const auto& props = w_->refProps_;
    QString text = hidden ? QString("%1 stock prop(s) hidden: the viewport and the rebuilt map leave them out.").arg(hidden)
                          : QString("No stock prop is hidden. Draw a brush around props, select it and use Hide In Selection.");
    if (!props) text += " The map's props are still loading.";
    else text += QString(" The map places %1 prop(s).").arg(props->size());
    propsInfo_->setText(text);
    propsShow_->setEnabled(hidden > 0);
    // The prop clicked in the camera (only on the map it was picked on).
    if (pickedRow_ && pickedZone_ != w_->stockZone()) pickedRow_.reset(), pickedModel_.clear();
    if (!pickedRow_) {
        pickedPropInfo_->setText("Click a stock prop in the camera to hide just that one.");
        pickedPropHide_->setText("Hide Picked Prop");
        pickedPropHide_->setEnabled(false);
        return;
    }
    const std::vector<uint32_t> rows = stockEditHiddenProps(ctx_->doc.map());
    const bool isHidden = std::find(rows.begin(), rows.end(), *pickedRow_) != rows.end();
    // Break points after / and _ (a zero-width space) so a long model name wraps in the narrow column.
    pickedPropInfo_->setText(QString("Picked prop: %1 (row %2)%3")
                                 .arg(QString(pickedModel_).replace('/', QString("/") + QChar(0x200B)).replace('_', QString("_") + QChar(0x200B)))
                                 .arg(*pickedRow_)
                                 .arg(isHidden ? QString(", hidden.") : QString(".")));
    pickedPropHide_->setText(isHidden ? "Show Picked Prop" : "Hide Picked Prop");
    pickedPropHide_->setEnabled(true);
}

void StockMapPanel::updateSelectionButtons() {
    const Document& doc = ctx_->doc;
    const bool sel = !doc.selection().empty();
    const Entity* w = worldOf(doc);
    bool worldOnly = w && sel;
    for (const SelRef& r : doc.selection()) worldOnly = worldOnly && r.entity == w->id && r.item != kNoId;
    boxAdd_->setEnabled(sel);
    boxMake_->setEnabled(worldOnly);
    propsHide_->setEnabled(sel);
    const Id box = selectedBox();
    const Entity* be = doc.findEntity(box);
    boxSelect_->setEnabled(be != nullptr);
    boxDelete_->setEnabled(be != nullptr);
    boxCollision_->setEnabled(be != nullptr);
    boxCollision_->setText(be && keyOff(*be, "collision") ? "Remove Collision" : "Keep Collision");
    const QString from = fromEdit_->text().trimmed(), to = toEdit_->text().trimmed();
    retexSet_->setEnabled(!from.isEmpty() && !to.isEmpty());
    bool hasPair = false;
    for (const auto& p : retexturePairs()) hasPair = hasPair || p.first == from;
    retexRemove_->setEnabled(hasPair);
}

}  // namespace iw8r::qt
