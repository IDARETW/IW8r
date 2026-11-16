import io

# Side bar revamp: IW8 Spawns, and the Tools side panel's tools moved into the panels (owner 2026-11-15: "we don't
# need a separate tools menu, what we need to do with that is implement any appropriate tools into already-existing
# menus or into new menus, and we can keep the tools tab at the top of the app").
#
# IW8 Spawns: the map's own spawn entities and a stock map's compiled spawnList, by mode (chips with counts: TDM, FFA,
# Domination, S&D, ...), one row per spawn with its team's colour, position and yaw; a click selects it and moves the
# views there; the strip has Add One Here (the same kind at the camera) and Select All of the mode; the header adds a
# spawn of the chosen mode at the camera; the dots menu has Generate Spawns and Game Modes. Undo History's dots menu:
# Record Macro and Run Command Script.

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


rw('qtapp/panels.h', [
(r'''    QString stripTitle() const;  // the step the strip shows (tests)
    void setFilter(const QString& text);''', r'''    QString stripTitle() const;  // the step the strip shows (tests)
    void setFilter(const QString& text);
    // Record Macro and Run Command Script (the Tools menu's automation), in the dots menu.
    void setTools(const QList<QAction*>& actions);
    QList<QAction*> tools() const { return tools_; }'''),
(r'''    QPushButton* selectTouched_ = nullptr;
    bool filling_ = false;''', r'''    QPushButton* selectTouched_ = nullptr;
    QMenu* menu_ = nullptr;
    QList<QAction*> tools_;
    bool filling_ = false;'''),
(r'''class SpawnsPanel : public QWidget {
    Q_OBJECT
public:
    explicit SpawnsPanel(EditorContext* ctx, QWidget* parent = nullptr);
public slots:
    void refresh();
private:
    EditorContext* ctx_;
    QLabel* info_;
    QTableWidget* table_;
    quint64 shown_ = 0;     // signature of the spawn records the table shows (0 = not filled yet)
    bool filling_ = false;  // the table is being rebuilt: current-cell changes do not move the camera
};''', r'''class SpawnsPanel : public QWidget {
    Q_OBJECT
public:
    explicit SpawnsPanel(EditorContext* ctx, QWidget* parent = nullptr);
    // Generate Spawns and Game Modes (the Tools menu's gameplay commands), in the dots menu.
    void setTools(const QList<QAction*>& actions);
    QList<QAction*> tools() const { return tools_; }
    // The spawns listed (the map's spawn entities, then a stock map's compiled spawnList), by mode.
    int spawnCount() const { return int(rows_.size()); }
    int visibleCount() const;
    QStringList modes() const;  // the chips' modes, most spawns first ("tdm", "dm", "dom", "sd", ...)
    void setMode(const QString& mode);  // "" = all
    void selectRow(int row);            // a listed row (the current filter's)
    QString countText() const;
    QString stripTitle() const;
    QString stripFacts() const;
    Id addHere();                       // the Add button: a spawn of the selected kind (or the mode's) at the camera
    static bool isSpawnClass(const std::string& classname);
    static QString modeOf(const QString& classname);  // mp_tdm_spawn_axis_start -> "tdm"; mp_global_intermission -> "intermission"
    static QString modeLabel(const QString& mode);    // "tdm" -> "TDM", "dm" -> "FFA", "sd" -> "S&D"
public slots:
    void refresh();
private:
    struct Row {
        QString cls, mode;
        Vec3 origin;
        double yaw = 0;
        Id entity = kNoId;  // the map's own spawn; kNoId = a stock compiled record
        uint32_t index = 0;
    };
    void fill();
    void updateStrip();
    void frame(int row);
    EditorContext* ctx_;
    class PanelHeader* header_ = nullptr;
    class ChipBar* chips_ = nullptr;
    QListWidget* list_ = nullptr;
    class DetailsStrip* strip_ = nullptr;
    QLabel* info_ = nullptr;
    QMenu* menu_ = nullptr;
    QPushButton* addLike_ = nullptr;
    QPushButton* selectMode_ = nullptr;
    QList<QAction*> tools_;
    std::vector<Row> rows_;
    QString mode_;          // the chip ("" = all)
    quint64 shown_ = 0;     // signature of the spawns the list shows (0 = not filled yet)
    bool filling_ = false;  // the list is being rebuilt: current-row changes do not move the camera
};'''),
(r'''class QAction;''', r'''class QAction;
class QMenu;'''),
])

# History: the dots menu
rw('qtapp/panels.cpp', [
(r'''    redo_ = header_->addButton(glyph::Redo, "Redo (Ctrl+Y)", false, "historyRedo");
    lay->addWidget(header_);''', r'''    redo_ = header_->addButton(glyph::Redo, "Redo (Ctrl+Y)", false, "historyRedo");
    menu_ = new QMenu(this);
    menu_->setToolTipsVisible(true);
    header_->addMenuButton(menu_, "Record and run edits as command scripts", "historyTools");
    lay->addWidget(header_);'''),
(r'''void HistoryPanel::setFilter(const QString& text) { search_->setText(text); }''', r'''void HistoryPanel::setFilter(const QString& text) { search_->setText(text); }

void HistoryPanel::setTools(const QList<QAction*>& actions) {
    tools_ = actions;
    tools_.removeAll(nullptr);
    menu_->clear();
    for (QAction* a : tools_) menu_->addAction(a);
}'''),
(r'''#include "panels.h"
#include "lightmap_fields.h"''', r'''#include "panels.h"
#include "lightmap_fields.h"
#include "panel_chrome.h"

#include <QHash>
#include <QMenu>
#include <QPainter>
#include <QStyledItemDelegate>'''),
])

SPAWNS = r'''// ------------------------------------------------------------------ IW8 spawns

namespace {

QColor teamColor(const QString& cls) {
    if (cls.contains("allies")) return QColor(90, 150, 255);
    if (cls.contains("axis")) return QColor(235, 90, 80);
    if (cls.contains("attacker")) return QColor(240, 160, 60);
    if (cls.contains("defender")) return QColor(80, 200, 180);
    if (cls == "mp_global_intermission") return QColor(200, 200, 120);
    return themeMix(0.6);
}

QString teamOf(const QString& cls) {
    if (cls.contains("allies")) return "allies";
    if (cls.contains("axis")) return "axis";
    if (cls.contains("attacker")) return "attackers";
    if (cls.contains("defender")) return "defenders";
    return "any team";
}

// The spawn class a mode adds by default.
QString defaultSpawnClass(const QString& mode) {
    if (mode.isEmpty() || mode == "tdm") return "mp_tdm_spawn";
    if (mode == "intermission") return "mp_global_intermission";
    if (mode == "sd") return "mp_sd_spawn_attacker";
    return "mp_" + mode + "_spawn";
}

QString shortNumber(double v) {
    if (qAbs(v) < 0.05) v = 0.0;  // no "-0.0"
    return QString::number(v, 'f', 0);
}

// Rows: a dot in the team's colour, the kind on the first line, the position and yaw under it; "start" / "stock" pills.
class SpawnDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QSize sizeHint(const QStyleOptionViewItem& opt, const QModelIndex&) const override { return {100, 2 * QFontMetrics(opt.font).height() + 8}; }
    void paint(QPainter* p, const QStyleOptionViewItem& opt, const QModelIndex& idx) const override {
        p->save();
        const QRect r = opt.rect;
        const bool sel = opt.state & QStyle::State_Selected;
        if (sel) p->fillRect(r, opt.palette.highlight());
        else if (opt.state & QStyle::State_MouseOver) p->fillRect(r, themeMix(0.07));
        const QColor text = sel ? opt.palette.highlightedText().color() : opt.palette.text().color();
        const QColor dim = sel ? text : themeMix(0.55);
        const QFontMetrics fm(opt.font);
        QFont small = opt.font;
        small.setPointSizeF(std::max(6.5, small.pointSizeF() - 1.5));
        const QFontMetrics sfm(small);
        p->setRenderHint(QPainter::Antialiasing);
        p->setPen(Qt::NoPen);
        p->setBrush(idx.data(Qt::UserRole + 1).value<QColor>());
        p->drawEllipse(QRectF(r.x() + 8, r.y() + 3 + (fm.height() - 9) / 2.0, 9, 9));
        const int x = r.x() + 24;
        int right = r.right() - 6;
        p->setFont(small);
        const QStringList pills = idx.data(Qt::UserRole + 3).toStringList();
        for (int i = int(pills.size()) - 1; i >= 0; --i) {
            const int w = sfm.horizontalAdvance(pills[i]) + 10;
            const QRectF pr(right - w, r.y() + 3 + (fm.height() - sfm.height() - 2) / 2.0, w, sfm.height() + 2);
            p->setPen(dim);
            p->setBrush(Qt::NoBrush);
            p->drawRoundedRect(pr, pr.height() / 2, pr.height() / 2);
            p->drawText(pr, Qt::AlignCenter, pills[i]);
            right -= w + 4;
        }
        p->setFont(opt.font);
        p->setPen(text);
        p->drawText(QRect(x, r.y() + 3, right - x, fm.height()), Qt::AlignLeft | Qt::AlignVCenter,
                    fm.elidedText(idx.data(Qt::DisplayRole).toString(), Qt::ElideRight, right - x));
        p->setFont(small);
        p->setPen(dim);
        p->drawText(QRect(x, r.y() + 3 + fm.height(), r.right() - 6 - x, sfm.height() + 2), Qt::AlignLeft | Qt::AlignVCenter,
                    idx.data(Qt::UserRole + 2).toString());
        p->restore();
    }
};

}  // namespace

bool SpawnsPanel::isSpawnClass(const std::string& c) {
    return c == "mp_global_intermission" || (c.rfind("mp_", 0) == 0 && c.find("spawn") != std::string::npos);
}

QString SpawnsPanel::modeOf(const QString& cls) {
    if (cls == "mp_global_intermission") return "intermission";
    const QStringList p = cls.split('_');
    return p.size() >= 2 ? p[1] : QString();
}

QString SpawnsPanel::modeLabel(const QString& mode) {
    static const QHash<QString, QString> names = {
        {"tdm", "TDM"}, {"dm", "FFA"}, {"dom", "Domination"}, {"sd", "S&D"}, {"ctf", "CTF"}, {"koth", "Hardpoint"}, {"hq", "HQ"},
        {"gun", "Gun Game"}, {"arena", "Gunfight"}, {"cyber", "Cyber Attack"}, {"grind", "Grind"}, {"infect", "Infected"},
        {"intermission", "Intermission"}, {"br", "Warzone"}, {"conf", "Kill Confirmed"}, {"siege", "Reinforce"}};
    return names.value(mode, mode.toUpper());
}

SpawnsPanel::SpawnsPanel(EditorContext* ctx, QWidget* parent) : QWidget(parent), ctx_(ctx) {
    setObjectName("spawnsPanel");
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(4);
    header_ = new PanelHeader("IW8 Spawns", this);
    QToolButton* add = header_->addButton(glyph::Add, "Add a spawn of the chosen mode at the camera (TDM when all are shown)", false, "spawnAdd");
    menu_ = new QMenu(this);
    menu_->setToolTipsVisible(true);
    header_->addMenuButton(menu_, "Generate spawns, game modes", "spawnTools");
    lay->addWidget(header_);
    connect(add, &QToolButton::clicked, this, [this] {
        // The chosen mode's spawn, whatever row is selected.
        const QString cls = defaultSpawnClass(mode_);
        const Vec3 at = ctx_->cam.pos - Vec3{0, 0, 60};
        const Id id = ctx_->doc.addEntity({{"classname", cls.toStdString()},
                                           {"origin", shortNumber(at.x).toStdString() + " " + shortNumber(at.y).toStdString() + " " +
                                                          shortNumber(at.z).toStdString()},
                                           {"angles", "0 " + shortNumber(ctx_->cam.yaw).toStdString() + " 0"}},
                                          {}, "Add " + cls.toStdString());
        if (id != kNoId) ctx_->documentEdited("Added " + cls + " at the camera (Drop to Floor puts it on the ground)");
    });
    chips_ = new ChipBar(this);
    chips_->setObjectName("spawnChips");
    chips_->addChip("", "All", -1, "Every spawn");
    lay->addWidget(chips_);
    connect(chips_, &ChipBar::currentChanged, this, [this](const QString& id) { setMode(id); });
    info_ = new QLabel(this);
    info_->setWordWrap(true);
    info_->setContentsMargins(6, 0, 6, 0);
    info_->setStyleSheet(QString("color: %1").arg(themeMix(0.6).name()));
    lay->addWidget(info_);
    list_ = new QListWidget(this);
    list_->setObjectName("spawnList");
    list_->setFrameShape(QFrame::NoFrame);
    list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list_->setUniformItemSizes(true);
    list_->setMouseTracking(true);
    list_->setItemDelegate(new SpawnDelegate(list_));
    list_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
    lay->addWidget(list_, 1);
    strip_ = new DetailsStrip(this);
    addLike_ = strip_->addAction(glyph::Add, "Add one here", "Another spawn of this kind at the camera, facing its way", "spawnAddLike");
    selectMode_ = strip_->addAction(glyph::Target, "Select all", "Select every spawn of this mode in the map", "spawnSelectMode");
    strip_->setVisible(false);
    lay->addWidget(strip_);
    connect(addLike_, &QPushButton::clicked, this, [this] { addHere(); });
    connect(selectMode_, &QPushButton::clicked, this, [this] {
        const int row = list_->currentItem() ? list_->currentItem()->data(Qt::UserRole + 4).toInt() : -1;
        if (row < 0 || size_t(row) >= rows_.size()) return;
        const QString mode = rows_[size_t(row)].mode;
        ctx_->doc.deselectAll();
        int n = 0;
        for (const Row& r : rows_)
            if (r.mode == mode && r.entity != kNoId) {
                ctx_->doc.select({r.entity, kNoId}, true);
                ++n;
            }
        ctx_->documentEdited(QString("Selected %1 %2 spawn(s)").arg(n).arg(modeLabel(mode)));
    });
    // A click, the arrow keys and Enter all go to the spawn (and select it when it is the map's own).
    connect(list_, &QListWidget::currentRowChanged, this, [this](int row) {
        if (filling_) return;
        updateStrip();
        frame(row);
    });
    connect(list_, &QListWidget::itemActivated, this, [this](QListWidgetItem* it) { frame(list_->row(it)); });
    connect(ctx_, &EditorContext::documentChanged, this, &SpawnsPanel::refresh);
    refresh();
}

void SpawnsPanel::setTools(const QList<QAction*>& actions) {
    tools_ = actions;
    tools_.removeAll(nullptr);
    menu_->clear();
    for (QAction* a : tools_) menu_->addAction(a);
}

Id SpawnsPanel::addHere() {
    QListWidgetItem* it = list_->currentItem();
    const int row = it ? it->data(Qt::UserRole + 4).toInt() : -1;
    const QString cls = row >= 0 && size_t(row) < rows_.size() ? rows_[size_t(row)].cls : defaultSpawnClass(mode_);
    const Vec3 at = ctx_->cam.pos - Vec3{0, 0, 60};
    const Id id = ctx_->doc.addEntity({{"classname", cls.toStdString()},
                                       {"origin", shortNumber(at.x).toStdString() + " " + shortNumber(at.y).toStdString() + " " +
                                                      shortNumber(at.z).toStdString()},
                                       {"angles", "0 " + shortNumber(ctx_->cam.yaw).toStdString() + " 0"}},
                                      {}, "Add " + cls.toStdString());
    if (id != kNoId) ctx_->documentEdited("Added " + cls + " at the camera (Drop to Floor puts it on the ground)");
    return id;
}

void SpawnsPanel::frame(int listRow) {
    QListWidgetItem* it = listRow >= 0 ? list_->item(listRow) : nullptr;
    const int row = it ? it->data(Qt::UserRole + 4).toInt() : -1;
    if (row < 0 || size_t(row) >= rows_.size()) return;
    const Row& s = rows_[size_t(row)];
    ctx_->cam.pos = s.origin + Vec3{0, 0, 60} - ctx_->cam.forward() * 160.0;
    ctx_->gridCenterH = s.origin[ctx_->axisH()];
    ctx_->gridCenterV = s.origin[ctx_->axisV()];
    ctx_->cameraMoved();
    if (s.entity != kNoId && !(ctx_->doc.selection().size() == 1 && ctx_->doc.isSelected({s.entity, kNoId}))) {
        ctx_->doc.deselectAll();
        ctx_->doc.select({s.entity, kNoId}, true);
        ctx_->documentEdited();
    }
}

namespace {
// What the spawns list shows: another map with as many spawns still rebuilds it.
quint64 mixSpawns(quint64 h, const QString& cls, const Vec3& o, double yaw, Id id) {
    auto mix = [&h](quint64 v) { h = (h ^ v) * 1099511628211ull; };
    auto mixDouble = [&mix](double d) {
        quint64 bits = 0;
        std::memcpy(&bits, &d, sizeof bits);
        mix(bits);
    };
    for (const QChar c : cls) mix(c.unicode());
    mixDouble(o.x);
    mixDouble(o.y);
    mixDouble(o.z);
    mixDouble(yaw);
    mix(id);
    return h;
}

Vec3 parseVec(const std::string* s) {
    Vec3 v{0, 0, 0};
    if (!s) return v;
    const QStringList p = QString::fromStdString(*s).split(' ', Qt::SkipEmptyParts);
    for (int i = 0; i < 3 && i < int(p.size()); ++i) v[i] = p[i].toDouble();
    return v;
}
}  // namespace

void SpawnsPanel::refresh() {
    std::vector<Row> rows;
    quint64 sig = 1469598103934665603ull;
    for (const Entity& e : ctx_->doc.map().entities) {
        const std::string cls = e.classname();
        if (!isSpawnClass(cls)) continue;
        Row r;
        r.cls = QString::fromStdString(cls);
        r.mode = modeOf(r.cls);
        r.origin = parseVec(e.value("origin"));
        if (const std::string* a = e.value("angles")) r.yaw = parseVec(a).y;
        else if (const std::string* a1 = e.value("angle")) r.yaw = QString::fromStdString(*a1).toDouble();
        r.entity = e.id;
        sig = mixSpawns(sig, r.cls, r.origin, r.yaw, r.entity);
        rows.push_back(r);
    }
    for (const auto& s : ctx_->scene.spawns) {
        Row r;
        r.cls = qs(s.name);
        r.mode = modeOf(r.cls);
        r.origin = s.origin;
        r.yaw = s.angles.y;
        r.index = s.index;
        sig = mixSpawns(sig, r.cls, r.origin, r.yaw, 0);
        rows.push_back(r);
    }
    sig ^= rows.size();
    if (sig == shown_ && shown_) return;
    shown_ = sig;
    rows_ = std::move(rows);
    // The chips: every mode the map has, most spawns first.
    QHash<QString, int> counts;
    for (const Row& r : rows_) ++counts[r.mode];
    QStringList modes = counts.keys();
    std::sort(modes.begin(), modes.end(), [&counts](const QString& a, const QString& b) {
        return counts[a] != counts[b] ? counts[a] > counts[b] : a < b;
    });
    if (modes != this->modes()) {
        chips_->clear();
        chips_->addChip("", "All", int(rows_.size()), "Every spawn");
        for (const QString& m : modes) chips_->addChip(m, modeLabel(m), counts[m], modeLabel(m) + " spawns");
        if (!modes.contains(mode_)) mode_.clear();
        chips_->setCurrent(mode_);
    } else {
        chips_->setCount("", int(rows_.size()));
        for (const QString& m : modes) chips_->setCount(m, counts[m]);
    }
    fill();
}

QStringList SpawnsPanel::modes() const {
    QStringList out = chips_->chipIds();
    out.removeAll(QString());
    return out;
}

void SpawnsPanel::setMode(const QString& mode) {
    mode_ = mode;
    chips_->setCurrent(mode);
    fill();
}

void SpawnsPanel::fill() {
    filling_ = true;  // rebuilding the rows moves the current row: the camera stays where it is
    list_->clear();
    int stock = 0;
    for (size_t i = 0; i < rows_.size(); ++i) {
        const Row& r = rows_[i];
        stock += r.entity == kNoId;
        if (!mode_.isEmpty() && r.mode != mode_) continue;
        // The kind without its mode prefix ("allies start"), the mode first when all are shown.
        QString rest = r.cls.mid(QString("mp_%1_").arg(r.mode).size());
        rest.replace("spawn", "").replace('_', ' ');
        rest = rest.simplified();
        const QString name = (mode_.isEmpty() ? modeLabel(r.mode) + " " : QString()) + (rest.isEmpty() ? QString("spawn") : rest);
        auto* item = new QListWidgetItem(name.trimmed());
        item->setToolTip(r.cls + (r.entity == kNoId ? QString("\nstock compiled spawnList record %1 (read-only)").arg(r.index) : QString()));
        item->setData(Qt::UserRole + 1, teamColor(r.cls));
        item->setData(Qt::UserRole + 2, QString("%1 %2 %3").arg(shortNumber(r.origin.x), shortNumber(r.origin.y), shortNumber(r.origin.z)) + factSep() +
                                             QString("yaw %1").arg(shortNumber(r.yaw)));
        QStringList pills;
        if (r.cls.contains("start")) pills << "start";
        if (r.entity == kNoId) pills << "stock";
        item->setData(Qt::UserRole + 3, pills);
        item->setData(Qt::UserRole + 4, int(i));
        list_->addItem(item);
    }
    filling_ = false;
    const int shown = list_->count(), total = int(rows_.size());
    header_->setCount(total == 0 ? QString("none") : shown == total ? QString("%1 spawn%2").arg(total).arg(total == 1 ? "" : "s")
                                                                    : QString("%1 of %2").arg(shown).arg(total),
                      stock ? QString("%1 of them from the stock map's compiled spawnList (read-only)").arg(stock) : QString());
    info_->setText(total == 0 ? QString("No spawns yet: + adds one at the camera, and the menu's Generate Spawns makes a starting set over "
                                        "the walkable floors. A stock map's compiled spawnList is listed when it is open.")
                              : QString());
    info_->setVisible(total == 0);
    updateStrip();
}

int SpawnsPanel::visibleCount() const { return list_->count(); }

void SpawnsPanel::selectRow(int row) { list_->setCurrentRow(row); }

QString SpawnsPanel::countText() const { return header_->count(); }
QString SpawnsPanel::stripTitle() const { return strip_->titleText(); }
QString SpawnsPanel::stripFacts() const { return strip_->factsText(); }

void SpawnsPanel::updateStrip() {
    QListWidgetItem* it = list_->currentItem();
    const int row = it ? it->data(Qt::UserRole + 4).toInt() : -1;
    if (row < 0 || size_t(row) >= rows_.size()) {
        strip_->setVisible(false);
        return;
    }
    const Row& r = rows_[size_t(row)];
    strip_->setTitle(r.cls, QStringList{modeLabel(r.mode), teamOf(r.cls),
                                        QString("%1 %2 %3").arg(shortNumber(r.origin.x), shortNumber(r.origin.y), shortNumber(r.origin.z)),
                                        QString("yaw %1").arg(shortNumber(r.yaw)), r.entity == kNoId ? QString("stock, read-only") : QString("the map's own")}
                                .join(factSep()));
    selectMode_->setText("Select all " + modeLabel(r.mode));
    int own = 0;
    for (const Row& o : rows_) own += o.mode == r.mode && o.entity != kNoId;
    selectMode_->setEnabled(own > 0);
    strip_->setVisible(true);
}

'''
slice_replace('qtapp/panels.cpp', '// ------------------------------------------------------------------ IW8 spawns\n',
              '// ------------------------------------------------------------------ hotkeys\n', SPAWNS)
print('ok')
