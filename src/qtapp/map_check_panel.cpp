#include "map_check_panel.h"

#include <QAction>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QRegularExpression>
#include <QShowEvent>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <QHash>

#include <algorithm>
#include <optional>

#include "find_dialog.h"
#include "panel_chrome.h"

namespace iw8r::qt {

namespace {

struct Kind {
    const char* id;
    const char* label;
    const char* tip;
};
const Kind kKinds[] = {
    {"fix", "Fixable", "Findings the Map Doctor can fix by itself (Fix, or Fix All in the menu)"},
    {"gameplay", "Gameplay", "Spawns, game modes, killstreaks, callouts"},
    {"entities", "Entities", "Entity keys, links, scripted objects, scriptables, models"},
    {"lighting", "Lighting", "Lights, the sun, the unbaked look"},
    {"leak", "Leak", "A hole to the void"},
    {"build", "Build", "What the IW8 build will do with the map"},
    {"map", "Map", "Brushes and the map file"},
};

QColor severityColor(const QString& sev) {
    if (sev == "fix") return QColor(120, 200, 255);
    if (sev == "warning") return QColor(232, 190, 80);
    if (sev == "note") return themeMix(0.55);
    return QColor(235, 100, 90);
}

}  // namespace

QString MapCheckPanel::kindOf(const QString& line) {
    if (line.startsWith("[fix] ")) return "fix";
    const QString head = line.section(':', 0, 0).toLower();
    for (const char* g : {"gameplay", "spawns", "game modes", "killstreaks", "callouts"})
        if (head.startsWith(g)) return "gameplay";
    for (const char* e : {"entity", "links", "scriptables", "models", "stock scripted"})
        if (head.startsWith(e)) return "entities";
    for (const char* l : {"lighting", "sun", "lights"})
        if (head.startsWith(l)) return "lighting";
    if (head.startsWith("leak")) return "leak";
    if (head.startsWith("iw8 build")) return "build";
    return "map";
}

QString MapCheckPanel::severityOf(const QString& line) {
    if (line.startsWith("[fix] ")) return "fix";
    const QString head = line.section(':', 0, 0).toLower();
    if (head.contains("(warning)")) return "warning";
    if (head.contains("(note)") || head.startsWith("iw8 build")) return "note";
    return "error";
}

MapCheckPanel::MapCheckPanel(EditorContext* ctx, QWidget* parent) : QWidget(parent), ctx_(ctx) {
    setObjectName("mapCheckPanel");
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(4);
    header_ = new PanelHeader("Map Check", this);
    QToolButton* again = header_->addButton(glyph::Refresh, "Check again", false, "mapCheckAgain");
    leak_ = header_->addButton(0xE7BA, "Leak Check: find a hole to the void (the yellow line shows the path)", false, "mapCheckLeak");
    leak_->setVisible(false);  // shown once the tools are set
    menu_ = new QMenu(this);
    menu_->setToolTipsVisible(true);
    header_->addMenuButton(menu_, "The map's check tools", "mapCheckTools");
    lay->addWidget(header_);
    connect(again, &QToolButton::clicked, this, [this] {
        checkedRevision_ = uint64_t(-1);
        recheck();
    });
    chips_ = new ChipBar(this);
    chips_->setObjectName("mapCheckChips");
    chips_->addChip("all", "All", -1, "Every finding");
    for (const Kind& k : kKinds) chips_->addChip(k.id, k.label, 0, k.tip);
    lay->addWidget(chips_);
    connect(chips_, &ChipBar::currentChanged, this, [this] { applyChip(); });
    list_ = new QListWidget(this);
    list_->setObjectName("mapCheckList");
    list_->setFrameShape(QFrame::NoFrame);
    list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list_->setWordWrap(true);
    list_->setTextElideMode(Qt::ElideRight);
    list_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
    list_->setIconSize(QSize(14, 14));
    list_->setSpacing(1);
    lay->addWidget(list_, 1);
    strip_ = new DetailsStrip(this);
    fix_ = strip_->addAction(glyph::Repair, "Fix", "Apply the Map Doctor's fix of this finding (one undo step)", "mapCheckFix");
    go_ = strip_->addAction(glyph::Target, "Go to", "Select what the finding names and move the views to it", "mapCheckGo");
    strip_->setVisible(false);
    lay->addWidget(strip_);
    connect(fix_, &QPushButton::clicked, this, [this] {
        const int row = list_->currentRow();
        if (row < 0 || size_t(row) >= fixes_.size()) return;
        const size_t n = applyDoctorFixes(ctx_->doc, {fixes_[size_t(row)]}, "Map Doctor: fix");
        if (n) ctx_->documentEdited("Map Doctor fixed the finding (one undo step)");
        recheck();
    });
    connect(go_, &QPushButton::clicked, this, [this] { goTo(list_->currentRow()); });
    connect(list_, &QListWidget::currentRowChanged, this, [this] { updateStrip(); });
    connect(list_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* it) { goTo(list_->row(it)); });
    // Checked again a moment after the map changes, while the panel is shown (a check builds a collision world).
    again_ = new QTimer(this);
    again_->setSingleShot(true);
    again_->setInterval(1500);
    connect(again_, &QTimer::timeout, this, [this] {
        if (isVisible()) recheck();
    });
    connect(ctx_, &EditorContext::documentChanged, this, [this] {
        if (isVisible() && ctx_->doc.revision() != checkedRevision_) again_->start();
    });
}

void MapCheckPanel::showEvent(QShowEvent* e) {
    QWidget::showEvent(e);
    if (ctx_->doc.revision() != checkedRevision_) QTimer::singleShot(0, this, [this] { recheck(); });
}

void MapCheckPanel::setTools(const QList<QAction*>& actions) {
    tools_ = actions;
    tools_.removeAll(nullptr);
    menu_->clear();
    QAction* leak = nullptr;
    for (QAction* a : tools_) {
        menu_->addAction(a);
        if (a->objectName() == "LeakCheck") leak = a;
    }
    menu_->addSeparator();
    QAction* all = menu_->addAction("Fix All");
    all->setToolTip("Apply every automatic fix (one undo step)");
    connect(all, &QAction::triggered, this, [this] { fixAll(); });
    leak_->setVisible(leak != nullptr);
    leak_->disconnect(this);
    if (leak) connect(leak_, &QToolButton::clicked, leak, &QAction::trigger);
}

void MapCheckPanel::recheck() {
    checkedRevision_ = ctx_->doc.revision();
    const QString was = list_->currentItem() ? list_->currentItem()->text() : QString();
    lines_ = checkMapLines(ctx_, fixes_, &summary_);
    list_->clear();
    QHash<QString, int> counts;
    for (const QString& l : lines_) {
        const QString sev = severityOf(l), kind = kindOf(l);
        ++counts[kind];
        // The line without its "Kind (warning): " head (the chip and the icon say it), the whole line in the tooltip.
        QString text = l.startsWith("[fix] ") ? l.mid(6) : l.section(':', 1).trimmed();
        if (text.isEmpty()) text = l;
        auto* item = new QListWidgetItem(glyphIcon(sev == "fix" ? glyph::Repair : sev == "note" ? glyph::Info : 0xE7BA, 14, severityColor(sev)), text);
        item->setToolTip(l);
        item->setData(Qt::UserRole, kind);
        item->setData(Qt::UserRole + 1, sev);
        item->setData(Qt::UserRole + 2, l);
        list_->addItem(item);
    }
    for (const Kind& k : kKinds) chips_->setCount(k.id, counts.value(k.id));
    chips_->setCount("all", int(lines_.size()));
    for (int r = 0; r < list_->count(); ++r)
        if (!was.isEmpty() && list_->item(r)->text() == was) list_->setCurrentRow(r);
    applyChip();
    updateStrip();
}

void MapCheckPanel::applyChip() {
    const QString chip = chips_->current();
    int shown = 0;
    for (int r = 0; r < list_->count(); ++r) {
        QListWidgetItem* it = list_->item(r);
        const bool on = chip == "all" || it->data(Qt::UserRole).toString() == chip;
        it->setHidden(!on);
        shown += on;
    }
    const int errors = int(std::count_if(lines_.begin(), lines_.end(), [](const QString& l) { return severityOf(l) == "error"; }));
    const QString all = lines_.isEmpty() ? QString("no problems") : QString("%1 finding%2").arg(lines_.size()).arg(lines_.size() == 1 ? "" : "s");
    header_->setCount(shown == int(lines_.size()) ? all : QString("%1 of %2").arg(shown).arg(lines_.size()),
                      summary_ + (errors ? QString("\n%1 error(s)").arg(errors) : QString()));
}

void MapCheckPanel::setChip(const QString& id) {
    chips_->setCurrent(id);
    applyChip();
}

void MapCheckPanel::selectRow(int row) { list_->setCurrentRow(row); }

int MapCheckPanel::findingCount() const { return int(lines_.size()); }

int MapCheckPanel::visibleCount() const {
    int n = 0;
    for (int r = 0; r < list_->count(); ++r) n += !list_->item(r)->isHidden();
    return n;
}

QString MapCheckPanel::countText() const { return header_->count(); }
QString MapCheckPanel::stripTitle() const { return strip_->titleText(); }
QString MapCheckPanel::stripFacts() const { return strip_->factsText(); }

size_t MapCheckPanel::fixAll() {
    if (fixes_.empty()) return 0;
    const size_t n = applyDoctorFixes(ctx_->doc, fixes_, "Map Doctor: fix all");
    if (n) ctx_->documentEdited(QString("Map Doctor fixed %1 finding(s) (one undo step)").arg(n));
    recheck();
    return n;
}

namespace {

// What a finding names: an entity (by its index in the map) or a position ("at x y z").
struct Target {
    std::optional<size_t> entity;
    bool byId = false;
    std::optional<Vec3> at;
};

Target targetOf(const QString& line) {
    Target t;
    static const QRegularExpression ent("[Ee]ntity (\\d+)"), at(" at (-?[\\d.]+) (-?[\\d.]+) (-?[\\d.]+)");
    if (const auto m = ent.match(line); m.hasMatch()) t.entity = size_t(m.captured(1).toULongLong());
    t.byId = line.startsWith("Links");  // the link check names entities by id, the others by their place in the map
    if (const auto m = at.match(line); m.hasMatch()) t.at = Vec3{m.captured(1).toDouble(), m.captured(2).toDouble(), m.captured(3).toDouble()};
    return t;
}

// An entity named by id becomes its place in the map (none when it is gone).
void resolve(Target& t, const MapFile& m) {
    if (!t.byId || !t.entity) return;
    const Id id = Id(*t.entity);
    t.entity.reset();
    for (size_t i = 0; i < m.entities.size(); ++i)
        if (m.entities[i].id == id) t.entity = i;
    t.byId = false;
}

}  // namespace

void MapCheckPanel::updateStrip() {
    QListWidgetItem* it = list_->currentItem();
    if (!it || it->isHidden()) {
        strip_->setVisible(false);
        return;
    }
    const QString line = it->data(Qt::UserRole + 2).toString(), sev = it->data(Qt::UserRole + 1).toString();
    QString kind;
    for (const Kind& k : kKinds)
        if (it->data(Qt::UserRole).toString() == k.id) kind = k.label;
    strip_->setTitle(kind + factSep() + (sev == "fix" ? QString("can be fixed") : sev), it->text());
    fix_->setVisible(sev == "fix");
    Target t = targetOf(line);
    resolve(t, ctx_->doc.map());
    go_->setVisible((t.entity && *t.entity < ctx_->doc.map().entities.size()) || t.at);
    strip_->setVisible(true);
}

void MapCheckPanel::goTo(int row) {
    QListWidgetItem* it = row >= 0 ? list_->item(row) : nullptr;
    if (!it) return;
    Target t = targetOf(it->data(Qt::UserRole + 2).toString());
    resolve(t, ctx_->doc.map());
    std::optional<Vec3> at = t.at;
    const auto& ents = ctx_->doc.map().entities;
    if (t.entity && *t.entity < ents.size()) {
        const Entity& e = ents[*t.entity];
        ctx_->doc.deselectAll();
        ctx_->doc.select({e.id, kNoId}, false);
        for (const SelRef& r : ctx_->doc.selectionUnit({e.id, kNoId})) ctx_->doc.select(r, true);
        if (const AABB b = ctx_->doc.selectionBounds(); b.valid()) at = (b.mins + b.maxs) * 0.5;
        else if (const std::string* o = e.value("origin")) {
            const QStringList v = QString::fromStdString(*o).split(' ', Qt::SkipEmptyParts);
            if (v.size() == 3) at = Vec3{v[0].toDouble(), v[1].toDouble(), v[2].toDouble()};
        }
    }
    if (at) {
        ctx_->cam.pos = *at + Vec3{0, 0, 60} - ctx_->cam.forward() * 200.0;
        ctx_->gridCenterH = (*at)[ctx_->axisH()];
        ctx_->gridCenterV = (*at)[ctx_->axisV()];
        ctx_->cameraMoved();
    }
    ctx_->documentEdited(t.entity ? QString("Selected entity %1 named by the check").arg(*t.entity) : QString("Moved to the finding"));
}

}  // namespace iw8r::qt
