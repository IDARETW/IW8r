#include "outliner_panel.h"

#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <map>
#include <set>

#include "editor_context.h"
#include "map_queries.h"

namespace iw8r::qt {

namespace {

QString qs(const std::string& s) { return QString::fromStdString(s); }

// Row label of one entity: the most telling key after the class.
QString labelOf(const Entity& e) {
    for (const char* k : {"targetname", "model", "fx", "soundalias", "script_noteworthy", "_light"}) {
        if (const std::string* v = e.value(k); v && !v->empty()) return qs(*v);
    }
    if (const std::string* o = e.value("origin")) return "at " + qs(*o);
    return QString("entity %1").arg(e.id);
}

}  // namespace

OutlinerPanel::OutlinerPanel(EditorContext* ctx, QWidget* parent) : QWidget(parent), ctx_(ctx) {
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(4, 4, 4, 4);
    filter_ = new QLineEdit(this);
    filter_->setObjectName("outlinerFilter");
    filter_->setPlaceholderText("Filter (class, targetname, model, key value)");
    filter_->setClearButtonEnabled(true);
    lay->addWidget(filter_);
    tree_ = new QTreeWidget(this);
    tree_->setObjectName("outlinerTree");
    tree_->setColumnCount(2);
    tree_->setHeaderLabels({"Entity", "Brushes"});
    tree_->header()->setStretchLastSection(false);
    tree_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    tree_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    tree_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    lay->addWidget(tree_, 1);
    stats_ = new QLabel(this);
    stats_->setWordWrap(true);
    stats_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    lay->addWidget(stats_);

    connect(filter_, &QLineEdit::textChanged, this, [this] { rebuild(); });
    connect(tree_, &QTreeWidget::itemSelectionChanged, this, [this] {
        if (syncing_) return;
        // The tree selection becomes the document selection (whole entities; worldspawn rows are skipped).
        ctx_->doc.deselectAll();
        for (QTreeWidgetItem* it : tree_->selectedItems()) {
            const Id id = Id(it->data(0, Qt::UserRole).toULongLong());
            const Entity* e = id ? ctx_->doc.findEntity(id) : nullptr;
            if (!e || e->isWorldspawn()) continue;
            bool any = false;
            for (const auto& item : e->items)
                if (item.isPrimitive()) {
                    ctx_->doc.select({e->id, item.id}, true);
                    any = true;
                }
            if (!any) ctx_->doc.select({e->id, kNoId}, true);
        }
        syncing_ = true;
        ctx_->documentEdited(QString("%1 selected from the Outliner").arg(tree_->selectedItems().size()));
        syncing_ = false;
    });
    connect(tree_, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* it) {
        if (it->data(0, Qt::UserRole).toULongLong()) ctx_->frameSelection();
    });
    // Rebuilt lazily: many edits in a row (drags) cost one rebuild when the dock is next shown or idle.
    connect(ctx_, &EditorContext::documentChanged, this, [this] {
        if (syncing_) return;
        if (docSignature() == signature_) return syncSelectionFromDoc();
        dirty_ = true;
        QTimer::singleShot(150, this, [this] {
            if (dirty_ && isVisible()) rebuild();
            else syncSelectionFromDoc();
        });
    });
    rebuild();
}

void OutlinerPanel::setFilter(const QString& text) {
    filter_->setText(text);
    rebuild();  // also when the text is unchanged: the panel may be stale while its dock was hidden
}

void OutlinerPanel::showEvent(QShowEvent* e) {
    QWidget::showEvent(e);
    if (dirty_ || docSignature() != signature_) rebuild();
}

quint64 OutlinerPanel::docSignature() const {
    quint64 h = 1469598103934665603ull;
    auto mix = [&h](quint64 v) { h = (h ^ v) * 1099511628211ull; };
    for (const Entity& e : ctx_->doc.map().entities) {
        mix(e.id);
        for (const auto& item : e.items) mix(item.rev * 31 + item.id);
    }
    return h;
}

void OutlinerPanel::rebuild() {
    dirty_ = false;
    signature_ = docSignature();
    const QString needle = filter_->text().trimmed().toLower();
    syncing_ = true;
    tree_->clear();
    std::map<QString, QTreeWidgetItem*> groups;
    size_t brushes = 0, patches = 0, entities = 0;
    std::set<std::string> models;
    for (const Entity& e : ctx_->doc.map().entities) {
        size_t nb = 0;
        for (const auto& item : e.items)
            if (const Primitive* p = item.prim()) {
                if (p->kind == PrimKind::Patch) ++patches;
                else ++brushes, ++nb;
            }
        if (const std::string* m = e.value("model"); m && !m->empty() && (*m)[0] != '*') models.insert(*m);
        if (e.isWorldspawn()) continue;
        ++entities;
        const QString cls = qs(e.classname());
        const QString label = labelOf(e);
        if (!needle.isEmpty()) {
            bool hit = cls.toLower().contains(needle) || label.toLower().contains(needle);
            for (const auto& item : e.items)
                if (const KeyValueLine* kv = item.kv(); !hit && kv) hit = qs(kv->value).toLower().contains(needle);
            if (!hit) continue;
        }
        QTreeWidgetItem*& g = groups[cls];
        if (!g) {
            g = new QTreeWidgetItem(tree_, {cls, QString()});
            g->setFlags(g->flags() & ~Qt::ItemIsSelectable);
        }
        auto* row = new QTreeWidgetItem(g, {label, nb ? QString::number(nb) : QString()});
        row->setData(0, Qt::UserRole, qulonglong(e.id));
        row->setToolTip(0, cls + " " + label);
    }
    for (auto& [cls, g] : groups) g->setText(0, QString("%1  (%2)").arg(cls).arg(g->childCount()));
    tree_->sortItems(0, Qt::AscendingOrder);
    if (!needle.isEmpty()) tree_->expandAll();
    syncing_ = false;
    syncSelectionFromDoc();
    const auto mats = materialUsage(ctx_->doc.map());
    stats_->setText(QString("%1 entities in %2 classes, %3 brushes, %4 patches, %5 materials, %6 models")
                        .arg(entities)
                        .arg(groups.size())
                        .arg(brushes)
                        .arg(patches)
                        .arg(mats.size())
                        .arg(models.size()));
}

void OutlinerPanel::syncSelectionFromDoc() {
    std::set<Id> selected;
    for (const SelRef& r : ctx_->doc.selection()) selected.insert(r.entity);
    syncing_ = true;
    for (int g = 0; g < tree_->topLevelItemCount(); ++g) {
        QTreeWidgetItem* grp = tree_->topLevelItem(g);
        for (int i = 0; i < grp->childCount(); ++i) {
            QTreeWidgetItem* row = grp->child(i);
            row->setSelected(selected.count(Id(row->data(0, Qt::UserRole).toULongLong())) != 0);
        }
    }
    syncing_ = false;
}

int OutlinerPanel::classCount() const { return tree_->topLevelItemCount(); }

int OutlinerPanel::entityCount() const {
    int n = 0;
    for (int g = 0; g < tree_->topLevelItemCount(); ++g) n += tree_->topLevelItem(g)->childCount();
    return n;
}

QString OutlinerPanel::statsText() const { return stats_->text(); }

int OutlinerPanel::selectMatching(const QString& text) {
    tree_->clearSelection();
    int n = 0;
    for (int g = 0; g < tree_->topLevelItemCount(); ++g) {
        QTreeWidgetItem* grp = tree_->topLevelItem(g);
        for (int i = 0; i < grp->childCount(); ++i)
            if (grp->child(i)->text(0).contains(text, Qt::CaseInsensitive)) {
                grp->child(i)->setSelected(true);
                ++n;
            }
    }
    return n;
}

}  // namespace iw8r::qt
