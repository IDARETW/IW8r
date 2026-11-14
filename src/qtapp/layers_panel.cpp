#include "layers_panel.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>
#include <map>

#include "layers.h"

namespace iw8r::qt {

namespace {
const char* kNoLayer = "(no layer)";
QString qs(const std::string& s) { return QString::fromStdString(s); }
}  // namespace

LayersPanel::LayersPanel(EditorContext* ctx, QWidget* parent) : QWidget(parent), ctx_(ctx) {
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(4, 4, 4, 4);
    table_ = new QTableWidget(0, 2, this);
    table_->setObjectName("layersTable");
    table_->setHorizontalHeaderLabels({"Layer (checked = visible)", "Primitives"});
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table_->verticalHeader()->hide();
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    lay->addWidget(table_, 1);
    auto* row = new QHBoxLayout();
    auto* sel = new QPushButton("Select", this);
    auto* move = new QPushButton("Move Selection Here", this);
    auto* add = new QPushButton("New Layer...", this);
    row->addWidget(sel);
    row->addWidget(move);
    row->addWidget(add);
    row->addStretch(1);
    lay->addLayout(row);
    info_ = new QLabel(this);
    info_->setStyleSheet("color: gray");
    lay->addWidget(info_);

    auto current = [this]() -> QString {
        const int r = table_->currentRow();
        return r >= 0 ? table_->item(r, 0)->text() : QString();
    };
    connect(sel, &QPushButton::clicked, this, [this, current] {
        const size_t n = selectLayer(current());
        ctx_->documentEdited(QString("%1 primitive(s) selected in %2").arg(n).arg(current()));
    });
    connect(move, &QPushButton::clicked, this, [this, current] {
        if (!moveSelectionToLayer(current())) ctx_->setStatus("Select primitives first");
    });
    connect(add, &QPushButton::clicked, this, [this] {
        bool ok = false;
        const QString name = QInputDialog::getText(this, "New Layer", "Layer name (may contain / for nesting):", QLineEdit::Normal, "", &ok);
        if (ok && !newLayer(name.trimmed())) ctx_->setStatus("Invalid or existing layer name");
    });
    connect(table_, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* it) {
        if (filling_ || it->column() != 0) return;
        setLayerVisible(it->text(), it->checkState() == Qt::Checked);
    });
    connect(table_, &QTableWidget::cellDoubleClicked, this, [this](int r, int) {
        const size_t n = selectLayer(table_->item(r, 0)->text());
        ctx_->documentEdited(QString("%1 primitive(s) selected").arg(n));
    });
    connect(ctx_, &EditorContext::documentChanged, this, &LayersPanel::refresh);
    refresh();
}

QStringList LayersPanel::layerNames() const {
    QStringList out;
    for (int r = 0; r < table_->rowCount(); ++r) out << table_->item(r, 0)->text();
    return out;
}

std::vector<LayersPanel::Row> LayersPanel::rows() const {
    std::vector<Row> out;
    for (int r = 0; r < table_->rowCount(); ++r) {
        const QTableWidgetItem* name = table_->item(r, 0);
        const QTableWidgetItem* count = table_->item(r, 1);
        if (!name) continue;
        out.push_back({name->text(), count ? size_t(count->data(Qt::DisplayRole).toULongLong()) : 0, hiddenLayers_.count(name->text().toStdString()) == 0});
    }
    return out;
}

void LayersPanel::refresh() {
    std::map<std::string, size_t> counts;
    for (const auto& d : declaredLayers(ctx_->doc.map().header)) counts[d.name];
    size_t none = 0;
    for (const auto& e : ctx_->doc.map().entities)
        for (const auto& it : e.items)
            if (const Primitive* p = it.prim()) {
                const std::string l = primitiveLayer(*p);
                if (l.empty()) ++none;
                else ++counts[l];
            }
    filling_ = true;
    table_->setRowCount(int(counts.size()) + 1);
    int r = 0;
    auto put = [&](const QString& name, size_t n) {
        auto* it = new QTableWidgetItem(name);
        it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
        it->setCheckState(hiddenLayers_.count(name.toStdString()) ? Qt::Unchecked : Qt::Checked);
        table_->setItem(r, 0, it);
        auto* c = new QTableWidgetItem;
        c->setData(Qt::DisplayRole, qulonglong(n));
        table_->setItem(r, 1, c);
        ++r;
    };
    put(kNoLayer, none);
    for (const auto& [name, n] : counts) put(qs(name), n);
    filling_ = false;
    info_->setText(QString("%1 declared layer(s); hidden layers are not drawn or pickable").arg(declaredLayers(ctx_->doc.map().header).size()));
    applyVisibility();
}

void LayersPanel::applyVisibility() {
    std::set<Id> ids;
    if (!hiddenLayers_.empty())
        for (const auto& e : ctx_->doc.map().entities)
            for (const auto& it : e.items)
                if (const Primitive* p = it.prim()) {
                    std::string l = primitiveLayer(*p);
                    if (l.empty()) l = kNoLayer;
                    if (hiddenLayers_.count(l)) ids.insert(it.id);
                }
    if (ids != ctx_->scene.hiddenByLayer) {
        ctx_->scene.hiddenByLayer = std::move(ids);
        ctx_->scene.invalidate();
        ctx_->cameraMoved();
    }
}

void LayersPanel::setLayerVisible(const QString& layer, bool visible) {
    if (visible) hiddenLayers_.erase(layer.toStdString());
    else hiddenLayers_.insert(layer.toStdString());
    const bool was = filling_;
    filling_ = true;  // the row follows; its change signal must not come back here
    for (int r = 0; r < table_->rowCount(); ++r)
        if (QTableWidgetItem* it = table_->item(r, 0); it && it->text() == layer) it->setCheckState(visible ? Qt::Checked : Qt::Unchecked);
    filling_ = was;
    applyVisibility();
    ctx_->setStatus(QString("Layer %1 %2").arg(layer, visible ? "shown" : "hidden"));
}

size_t LayersPanel::selectLayer(const QString& layer) {
    const std::string want = layer == kNoLayer ? std::string() : layer.toStdString();
    ctx_->doc.deselectAll();
    size_t n = 0;
    for (const auto& e : ctx_->doc.map().entities)
        for (const auto& it : e.items)
            if (const Primitive* p = it.prim(); p && primitiveLayer(*p) == want) {
                ctx_->doc.select({e.id, it.id}, true);
                ++n;
            }
    return n;
}

bool LayersPanel::moveSelectionToLayer(const QString& layer) {
    const std::string name = layer == kNoLayer ? std::string() : layer.toStdString();
    std::vector<Document::PrimEdit> edits;
    for (const auto& r : ctx_->doc.selection()) {
        const EntityItem* it = r.item ? ctx_->doc.findItem(r.entity, r.item) : nullptr;
        if (!it || !it->prim()) continue;
        Primitive p = *it->prim();
        if (setPrimitiveLayer(p, name, ctx_->doc.eol())) edits.push_back({r.entity, r.item, p});
    }
    if (edits.empty()) return false;
    ctx_->doc.beginGroup("Move To Layer");
    if (!name.empty()) ctx_->doc.setHeader(headerWithLayer(ctx_->doc.map().header, name, ctx_->doc.eol()), "Move To Layer");
    ctx_->doc.editPrimitives(edits, "Move To Layer", false);
    ctx_->doc.endGroup();
    ctx_->documentEdited(QString("%1 primitive(s) moved to %2 (one undo step)").arg(edits.size()).arg(layer));
    return true;
}

bool LayersPanel::newLayer(const QString& name) {
    if (!isValidLayerName(name.toStdString())) return false;
    const std::string h = headerWithLayer(ctx_->doc.map().header, name.toStdString(), ctx_->doc.eol());
    if (!ctx_->doc.setHeader(h, "New Layer")) return false;
    ctx_->documentEdited("Layer " + name + " declared (undoable)");
    return true;
}

}  // namespace iw8r::qt
