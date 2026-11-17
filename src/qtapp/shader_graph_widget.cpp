#include "shader_graph_widget.h"
#include <QApplication>
#include <QFormLayout>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QTimer>
#include <QToolBar>
#include <QGroupBox>
#include <QScrollArea>
#include <QComboBox>
#include <QClipboard>
#include <QDialog>
#include <QDialogButtonBox>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsView>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMouseEvent>
#include <QContextMenuEvent>
#include <QMessageBox>
#include <QMimeData>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QShortcut>
#include <QSplitter>
#include <QUndoCommand>
#include <QUndoStack>
#include <QUuid>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <algorithm>
#include <set>

namespace iw8r::qt {
namespace {
std::string id() { return "n_" + QUuid::createUuid().toString(QUuid::Id128).toStdString(); }
QString qs(const std::string &s) { return QString::fromStdString(s); }
class Change : public QUndoCommand {
  public:
    Change(std::function<void(const ShaderGraph &)> apply, ShaderGraph before, ShaderGraph after, const QString &label)
        : apply_(std::move(apply)), before_(std::move(before)), after_(std::move(after)) {
        setText(label);
    }
    void undo() override { apply_(before_); }
    void redo() override { apply_(after_); }

  private:
    std::function<void(const ShaderGraph &)> apply_;
    ShaderGraph before_, after_;
};
class GraphView : public QGraphicsView {
  public:
    using QGraphicsView::QGraphicsView;
    std::function<void()> add, remove;
    void wheelEvent(QWheelEvent *e) override {
        const double scaleBy = e->angleDelta().y() > 0 ? 1.15 : 1.0 / 1.15;
        const auto current = transform().m11();
        if (current * scaleBy >= 0.15 && current * scaleBy <= 3.0)
            scale(scaleBy, scaleBy);
        e->accept();
    }
    void mousePressEvent(QMouseEvent *e) override {
        if (e->button() == Qt::MiddleButton) {
            pan_ = true;
            last_ = e->pos();
            setCursor(Qt::ClosedHandCursor);
            e->accept();
            return;
        }
        QGraphicsView::mousePressEvent(e);
    }
    void mouseMoveEvent(QMouseEvent *e) override {
        if (pan_) {
            const auto delta = mapToScene(last_) - mapToScene(e->pos());
            translate(-delta.x(), -delta.y());
            last_ = e->pos();
            return;
        }
        QGraphicsView::mouseMoveEvent(e);
    }
    void mouseReleaseEvent(QMouseEvent *e) override {
        if (e->button() == Qt::MiddleButton) {
            pan_ = false;
            unsetCursor();
            return;
        }
        QGraphicsView::mouseReleaseEvent(e);
    }
    void contextMenuEvent(QContextMenuEvent *e) override {
        QMenu menu;
        auto *a = menu.addAction("Add node...");
        auto *d = menu.addAction("Delete selected");
        const auto selected = menu.exec(e->globalPos());
        if (selected == a && add)
            add();
        if (selected == d && remove)
            remove();
    }

  private:
    bool pan_ = false;
    QPoint last_;
};
class NodeItem : public QGraphicsItem {
  public:
    ShaderNode node;
    std::vector<ShaderPort> inputs;
    bool error = false, output = true;
    bool dragStarted = false;
    std::function<void(const std::string &, const std::string &)> port;
    std::function<void()> edit, beginMove, endMove;
    std::function<void(QPointF)> moving;
    NodeItem(ShaderNode value, std::vector<ShaderPort> ports) : node(std::move(value)), inputs(std::move(ports)) {
        setFlags(ItemIsMovable | ItemIsSelectable | ItemSendsGeometryChanges);
        setData(0, qs(node.id));
        output =
            node.kind != "surface" && node.kind != "vertex_output" && node.kind != "comment" && node.kind != "output";
        setPos(node.x, node.y);
    }
    QRectF boundingRect() const override { return {-7, -3, 224, double(std::max<size_t>(2, inputs.size()) * 23 + 48)}; }
    QPointF inputPos(size_t i) const { return mapToScene(QPointF(0, 44 + 23 * i)); }
    QPointF outputPos() const { return mapToScene(QPointF(210, 44)); }
    void paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *) override {
        p->setRenderHint(QPainter::Antialiasing);
        p->setPen(QPen(error          ? QColor("#ef7373")
                       : isSelected() ? QColor("#75baff")
                                      : QColor("#4b5563"),
                       isSelected() ? 2 : 1));
        p->setBrush(node.kind == "comment" ? QColor("#423d28") : QColor("#272e38"));
        p->drawRoundedRect(QRectF(0, 0, 210, boundingRect().height() - 6), 6, 6);
        p->setPen(QColor("#eef2f6"));
        auto f = p->font();
        f.setBold(true);
        p->setFont(f);
        p->drawText(QRectF(10, 5, 190, 25), Qt::AlignVCenter, qs(node.label.empty() ? node.kind : node.label));
        f.setBold(false);
        p->setFont(f);
        for (size_t i = 0; i < inputs.size(); ++i) {
            const float y = 44 + 23 * float(i);
            p->setPen(Qt::NoPen);
            p->setBrush(QColor("#8bb8a7"));
            p->drawEllipse(QPointF(0, y), 5, 5);
            p->setPen(QColor("#bdc8d6"));
            p->drawText(QRectF(12, y - 10, 160, 20), Qt::AlignVCenter, qs(inputs[i].name + "  " + inputs[i].type));
        }
        if (output) {
            p->setPen(Qt::NoPen);
            p->setBrush(QColor("#89b6e8"));
            p->drawEllipse(QPointF(210, 44), 5, 5);
        }
    }
    void mousePressEvent(QGraphicsSceneMouseEvent *e) override {
        if (e->button() == Qt::LeftButton) {
            if (output && QLineF(e->pos(), QPointF(210, 44)).length() < 12) {
                if (port)
                    port(node.id, {});
                e->accept();
                return;
            }
            for (size_t i = 0; i < inputs.size(); ++i)
                if (QLineF(e->pos(), QPointF(0, 44 + 23 * i)).length() < 12) {
                    if (port)
                        port(node.id, inputs[i].name);
                    e->accept();
                    return;
                }
        }
        dragStarted = true;
        if (beginMove)
            beginMove();
        QGraphicsItem::mousePressEvent(e);
    }
    void mouseReleaseEvent(QGraphicsSceneMouseEvent *e) override {
        QGraphicsItem::mouseReleaseEvent(e);
        if (dragStarted && endMove)
            endMove();
        dragStarted = false;
    }
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent *e) override {
        e->accept();
        if (edit)
            edit();
    }
    QVariant itemChange(GraphicsItemChange change, const QVariant &v) override {
        if (change == ItemPositionHasChanged && moving)
            moving(v.toPointF());
        return QGraphicsItem::itemChange(change, v);
    }
};
} // namespace

ShaderGraphWidget::ShaderGraphWidget(QWidget *parent) : QWidget(parent) {
    setObjectName("techsetGraph");
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    auto *bar = new QToolBar(this);
    bar->setMovable(false);
    bar->setToolButtonStyle(Qt::ToolButtonTextOnly);
    const auto button = [&](const QString &text, auto action) {
        auto *b = bar->addAction(text);
        connect(b, &QAction::triggered, this, action);
    };
    button("Add node...", [this] { chooseNode(); });
    button("Undo", [this] { undo(); });
    button("Redo", [this] { redo(); });
    button("Subgraph", [this] { makeSubgraph(); });
    button("Preview node", [this] {
        if (onPreview)
            onPreview(selected_);
    });
    button("Final output", [this] {
        if (onPreview)
            onPreview({});
    });
    button("Frame graph", [this] { frameGraph(); });
    root->addWidget(bar);
    auto *split = new QSplitter(this);
    scene_ = new QGraphicsScene(this);
    auto *view = new GraphView(scene_, split);
    view_ = view;
    view->setObjectName("techsetGraphCanvas");
    view->setRenderHint(QPainter::Antialiasing);
    view->setDragMode(QGraphicsView::RubberBandDrag);
    view->setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    view->setBackgroundBrush(QColor("#191e26"));
    view->add = [this] { chooseNode(); };
    view->remove = [this] { deleteSelected(); };
    auto *properties = new QWidget(split);
    auto *form = new QVBoxLayout(properties);
    properties->setMaximumWidth(330);
    propertyTitle_ = new QLabel("Select a node", properties);
    propertyTitle_->setStyleSheet("font-weight: 600; padding: 6px 0;");
    form->addWidget(propertyTitle_);
    auto *fields = new QWidget(properties);
    propertyForm_ = new QFormLayout(fields);
    propertyForm_->setContentsMargins(0, 0, 0, 0);
    propertyForm_->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->addWidget(fields);
    auto *advancedToggle = new QCheckBox("Advanced properties", properties);
    form->addWidget(advancedToggle);
    auto *advanced = new QWidget(properties);
    auto *advancedLayout = new QVBoxLayout(advanced);
    advancedLayout->setContentsMargins(0, 0, 0, 0);
    advanced->hide();
    connect(advancedToggle, &QCheckBox::toggled, advanced, &QWidget::setVisible);
    properties_ = new QPlainTextEdit(properties);
    properties_->setObjectName("techsetNodeProperties");
    advancedLayout->addWidget(properties_);
    auto *apply = new QPushButton("Apply properties", properties);
    advancedLayout->addWidget(apply);
    form->addWidget(advanced);
    form->addStretch();
    connect(apply, &QPushButton::clicked, this, [this] {
        try {
            const auto j = TechsetJson::parse(properties_->toPlainText().toStdString());
            if (!j.is_object())
                throw std::runtime_error("Properties must be an object.");
            const auto before = graph_;
            for (auto &n : graph_.nodes)
                if (n.id == selected_)
                    n.properties = j;
            commit(before, "Edit node properties");
        } catch (const std::exception &e) {
            QMessageBox::warning(this, "Node properties", QString::fromUtf8(e.what()));
        }
    });
    split->addWidget(view);
    split->addWidget(properties);
    split->setStretchFactor(0, 4);
    split->setStretchFactor(1, 1);
    split->setSizes({820, 250});
    root->addWidget(split, 1);
    auto *help = new QLabel("Click an output dot, then an input dot to connect. Middle drag pans; wheel zooms. "
                            "Double-click a node to edit it.",
                            this);
    help->setWordWrap(true);
    root->addWidget(help);
    undo_ = new QUndoStack(this);
    undo_->setUndoLimit(256);
    connect(scene_, &QGraphicsScene::selectionChanged, this, [this] { selectionChanged(); });
    const auto shortcut = [&](const QKeySequence &keys, auto fn) {
        auto *k = new QShortcut(keys, view_);
        k->setContext(Qt::WidgetWithChildrenShortcut);
        connect(k, &QShortcut::activated, this, fn);
    };
    shortcut(QKeySequence::Copy, [this] { copy(); });
    shortcut(QKeySequence::Paste, [this] { paste(); });
    shortcut(QKeySequence::Delete, [this] { deleteSelected(); });
    shortcut(QKeySequence::Undo, [this] { undo(); });
    shortcut(QKeySequence::Redo, [this] { redo(); });
    shortcut(QKeySequence(Qt::Key_Tab), [this] { chooseNode(); });
}
void ShaderGraphWidget::setGraph(const ShaderGraph &graph, ShaderStage stage) {
    graph_ = graph;
    stage_ = stage;
    undo_->clear();
    selected_.clear();
    pendingOutput_.clear();
    rebuild();
    QTimer::singleShot(0, this, [this] { frameGraph(); });
}
ShaderGraphWidget::~ShaderGraphWidget() {
    rebuilding_ = true;
    disconnect(scene_, nullptr, this, nullptr);
    scene_->clear();
}
void ShaderGraphWidget::restore(const ShaderGraph &graph) {
    graph_ = graph;
    rebuild();
    if (onChanged)
        onChanged(graph_);
}
void ShaderGraphWidget::commit(const ShaderGraph &before, const QString &label) {
    if (shaderGraphJson(before) == shaderGraphJson(graph_))
        return;
    undo_->push(new Change([this](const ShaderGraph &g) { restore(g); }, before, graph_, label));
}
void ShaderGraphWidget::undo() { undo_->undo(); }
void ShaderGraphWidget::redo() { undo_->redo(); }
void ShaderGraphWidget::rebuild() {
    rebuilding_ = true;
    scene_->clear();
    std::map<std::string, NodeItem *> items;
    for (const auto &n : graph_.nodes) {
        auto *spec = shaderNodeSpec(n.kind);
        std::vector<ShaderPort> inputs = spec ? spec->inputs : std::vector<ShaderPort>();
        if (n.kind == "custom") {
            const auto j = n.properties.value("inputs", TechsetJson::object());
            for (auto i = j.begin(); i != j.end(); ++i)
                inputs.push_back({i.key(), i.value().is_string() ? i.value().get<std::string>() : "float"});
        }
        if (n.kind == "subgraph") {
            auto it = graph_.subgraphs.find(n.properties.value("name", ""));
            if (it != graph_.subgraphs.end())
                for (const auto &x : it->second.nodes)
                    if (x.kind == "input")
                        inputs.push_back({x.properties.value("name", x.id), x.properties.value("type", "float")});
        }
        auto *item = new NodeItem(n, inputs);
        item->error = errors_.count(n.id) > 0;
        scene_->addItem(item);
        items[n.id] = item;
        item->port = [this](const std::string &node, const std::string &input) {
            if (input.empty()) {
                pendingOutput_ = node;
                return;
            }
            if (!pendingOutput_.empty()) {
                const auto from = pendingOutput_;
                QMetaObject::invokeMethod(
                    this, [this, from, node, input] { connectPorts(from, node, input); }, Qt::QueuedConnection);
                pendingOutput_.clear();
            }
        };
        item->edit = [this, key = n.id] {
            QMetaObject::invokeMethod(this, [this, key] { editNode(key); }, Qt::QueuedConnection);
        };
        auto before = std::make_shared<ShaderGraph>();
        item->beginMove = [this, before] { *before = graph_; };
        item->endMove = [this, before] {
            const auto b = *before;
            QMetaObject::invokeMethod(this, [this, b] { commit(b, "Move nodes"); }, Qt::QueuedConnection);
        };
        item->moving = [this, key = n.id](QPointF p) {
            for (auto &x : graph_.nodes)
                if (x.id == key) {
                    x.x = p.x();
                    x.y = p.y();
                }
        };
        if (n.id == selected_)
            item->setSelected(true);
    }
    for (size_t index = 0; index < graph_.links.size(); ++index) {
        const auto &link = graph_.links[index];
        if (!items.count(link.from) || !items.count(link.to))
            continue;
        auto *to = items.at(link.to);
        size_t port = 0;
        while (port < to->inputs.size() && to->inputs[port].name != link.input)
            ++port;
        if (port == to->inputs.size())
            continue;
        const auto a = items.at(link.from)->outputPos(), b = to->inputPos(port);
        QPainterPath path(a);
        const double dx = std::max(60.0, std::abs(b.x() - a.x()) * 0.5);
        path.cubicTo(a + QPointF(dx, 0), b - QPointF(dx, 0), b);
        auto *edge = scene_->addPath(path, QPen(QColor("#8eb8dc"), 2));
        edge->setZValue(-1);
        edge->setFlag(QGraphicsItem::ItemIsSelectable);
        edge->setData(1, int(index));
    }
    scene_->setSceneRect(scene_->itemsBoundingRect().adjusted(-800, -600, 800, 600));
    rebuilding_ = false;
    selectionChanged();
}
void ShaderGraphWidget::showEvent(QShowEvent *event) {
    QWidget::showEvent(event);
    QTimer::singleShot(0, this, [this] { frameGraph(); });
}
void ShaderGraphWidget::frameGraph() {
    if (!isVisible())
        return;
    const auto bounds = scene_->itemsBoundingRect().adjusted(-60, -60, 60, 60);
    if (bounds.isEmpty())
        return;
    view_->fitInView(bounds, Qt::KeepAspectRatio);
    if (view_->transform().m11() > 1.0)
        view_->resetTransform();
    view_->centerOn(bounds.center());
}
void ShaderGraphWidget::chooseNode() {
    QDialog dialog(this);
    dialog.setWindowTitle("Add shader node");
    auto *root = new QVBoxLayout(&dialog);
    auto *search = new QLineEdit(&dialog);
    search->setPlaceholderText("Search nodes...");
    root->addWidget(search);
    auto *list = new QListWidget(&dialog);
    root->addWidget(list);
    for (const auto &s : shaderNodeLibrary()) {
        if (s.pixelOnly && stage_ != ShaderStage::Pixel)
            continue;
        if (s.kind == "vertex_output" && stage_ != ShaderStage::Vertex)
            continue;
        auto *row = new QListWidgetItem(qs(s.category + " / " + s.title), list);
        row->setData(Qt::UserRole, qs(s.kind));
    }
    connect(search, &QLineEdit::textChanged, &dialog, [list](const QString &text) {
        for (int i = 0; i < list->count(); ++i)
            list->item(i)->setHidden(!list->item(i)->text().contains(text, Qt::CaseInsensitive));
    });
    connect(list, &QListWidget::itemDoubleClicked, &dialog, [&dialog] { dialog.accept(); });
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    root->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    dialog.resize(440, 560);
    if (dialog.exec() == QDialog::Accepted && list->currentItem())
        addNode(list->currentItem()->data(Qt::UserRole).toString().toStdString());
}
void ShaderGraphWidget::addNode(const std::string &kind) {
    const auto *spec = shaderNodeSpec(kind);
    if (!spec)
        return;
    const auto before = graph_;
    ShaderNode n;
    n.id = id();
    n.kind = kind;
    n.label = spec->title;
    const auto p = view_->mapToScene(view_->viewport()->rect().center());
    n.x = p.x();
    n.y = p.y();
    if (kind == "constant")
        n.properties["value"] = {1.0, 1.0, 1.0};
    if (kind == "custom")
        n.properties = {{"type", "float3"}, {"expression", "$color * 2.0"}, {"inputs", {{"color", "float3"}}}};
    if (kind == "parameter")
        n.properties = {{"parameter", "Tint"}, {"type", "float4"}};
    if (kind == "texture" || kind == "texture_cube" || kind == "triplanar")
        n.properties["resource"] = "Texture";
    if (kind == "subgraph")
        n.properties["name"] = graph_.subgraphs.empty() ? "subgraph" : graph_.subgraphs.begin()->first;
    selected_ = n.id;
    graph_.nodes.push_back(std::move(n));
    commit(before, "Add node");
}
void ShaderGraphWidget::connectPorts(const std::string &from, const std::string &to, const std::string &input) {
    if (from == to)
        return;
    const auto before = graph_;
    graph_.links.erase(std::remove_if(graph_.links.begin(), graph_.links.end(),
                                      [&](const auto &e) { return e.to == to && e.input == input; }),
                       graph_.links.end());
    graph_.links.push_back({from, "out", to, input});
    commit(before, "Connect nodes");
}
void ShaderGraphWidget::propertyChanged(const std::string &node, const std::string &key, TechsetJson value) {
    QTimer::singleShot(0, this, [this, node, key, value = std::move(value)] {
        const auto before = graph_;
        for (auto &n : graph_.nodes)
            if (n.id == node)
                n.properties[key] = value;
        commit(before, "Change node property");
    });
}
void ShaderGraphWidget::selectionChanged() {
    if (rebuilding_)
        return;
    selected_.clear();
    for (auto *item : scene_->selectedItems())
        if (auto *n = dynamic_cast<NodeItem *>(item)) {
            selected_ = n->node.id;
            break;
        }
    while (propertyForm_->rowCount())
        propertyForm_->removeRow(0);
    propertyTitle_->setText("Select a node");
    properties_->clear();
    for (const auto &n : graph_.nodes)
        if (n.id == selected_) {
            propertyTitle_->setText(qs(n.label.empty() ? n.kind : n.label));
            properties_->setPlainText(qs(n.properties.dump(2)));
            auto values = n.properties;
            if (n.kind == "surface" && !values.contains("unlit"))
                values["unlit"] = false;
            for (auto it = values.begin(); it != values.end(); ++it) {
                const auto key = it.key(), node = n.id;
                const auto value = it.value();
                if (value.is_boolean()) {
                    auto *field = new QCheckBox(this);
                    field->setChecked(value.get<bool>());
                    connect(field, &QCheckBox::toggled, this,
                            [this, node, key](bool v) { propertyChanged(node, key, v); });
                    propertyForm_->addRow(qs(key), field);
                } else if (value.is_number()) {
                    auto *field = new QDoubleSpinBox(this);
                    field->setRange(-1e9, 1e9);
                    field->setDecimals(5);
                    field->setValue(value.get<double>());
                    connect(field, &QDoubleSpinBox::editingFinished, this,
                            [this, field, node, key] { propertyChanged(node, key, field->value()); });
                    propertyForm_->addRow(qs(key), field);
                } else if (value.is_string()) {
                    if (key == "type") {
                        auto *field = new QComboBox(this);
                        field->addItems({"float", "float2", "float3", "float4", "bool"});
                        field->setCurrentText(qs(value.get<std::string>()));
                        connect(field, &QComboBox::currentTextChanged, this,
                                [this, node, key](const QString &v) { propertyChanged(node, key, v.toStdString()); });
                        propertyForm_->addRow(qs(key), field);
                    } else {
                        auto *field = new QLineEdit(qs(value.get<std::string>()), this);
                        connect(field, &QLineEdit::editingFinished, this,
                                [this, field, node, key] { propertyChanged(node, key, field->text().toStdString()); });
                        propertyForm_->addRow(qs(key), field);
                    }
                } else if (value.is_array() && value.size() <= 4 &&
                           std::all_of(value.begin(), value.end(), [](const auto &v) { return v.is_number(); })) {
                    auto *vector = new QWidget(this);
                    auto *row = new QHBoxLayout(vector);
                    row->setContentsMargins(0, 0, 0, 0);
                    row->setSpacing(3);
                    for (size_t i = 0; i < value.size(); ++i) {
                        auto *field = new QDoubleSpinBox(vector);
                        field->setRange(-1e6, 1e6);
                        field->setDecimals(3);
                        field->setValue(value[i].get<double>());
                        field->setButtonSymbols(QAbstractSpinBox::NoButtons);
                        row->addWidget(field);
                        connect(field, &QDoubleSpinBox::editingFinished, this, [this, field, node, key, value, i] {
                            auto v = value;
                            v[i] = field->value();
                            propertyChanged(node, key, std::move(v));
                        });
                    }
                    propertyForm_->addRow(qs(key), vector);
                }
            }
            return;
        }
}

void ShaderGraphWidget::editNode(const std::string &key) {
    auto it = std::find_if(graph_.nodes.begin(), graph_.nodes.end(), [&](const auto &n) { return n.id == key; });
    if (it == graph_.nodes.end())
        return;
    if (it->kind == "subgraph") {
        const auto name = it->properties.value("name", "");
        auto sub = graph_.subgraphs.find(name);
        if (sub == graph_.subgraphs.end())
            return;
        QDialog dialog(this);
        dialog.setWindowTitle("Subgraph: " + qs(name));
        dialog.resize(1100, 720);
        auto *l = new QVBoxLayout(&dialog);
        auto *editor = new ShaderGraphWidget(&dialog);
        editor->setGraph(sub->second, stage_);
        l->addWidget(editor);
        auto *b = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
        l->addWidget(b);
        connect(b, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(b, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        if (dialog.exec() == QDialog::Accepted) {
            const auto before = graph_;
            graph_.subgraphs[name] = editor->graph();
            commit(before, "Edit subgraph");
        }
        return;
    }
    bool ok = false;
    const auto title = QInputDialog::getText(this, "Node label", "Label", QLineEdit::Normal, qs(it->label), &ok);
    if (ok) {
        const auto before = graph_;
        it->label = title.toStdString();
        commit(before, "Rename node");
    }
}
void ShaderGraphWidget::deleteSelected() {
    std::set<std::string> nodes;
    std::set<size_t> edges;
    for (auto *item : scene_->selectedItems()) {
        if (auto *n = dynamic_cast<NodeItem *>(item))
            nodes.insert(n->node.id);
        else if (item->data(1).isValid())
            edges.insert(item->data(1).toUInt());
    }
    const auto before = graph_;
    size_t index = 0;
    graph_.links.erase(
        std::remove_if(graph_.links.begin(), graph_.links.end(),
                       [&](const auto &e) { return edges.count(index++) || nodes.count(e.from) || nodes.count(e.to); }),
        graph_.links.end());
    graph_.nodes.erase(
        std::remove_if(graph_.nodes.begin(), graph_.nodes.end(), [&](const auto &n) { return nodes.count(n.id); }),
        graph_.nodes.end());
    commit(before, "Delete nodes / connections");
}
void ShaderGraphWidget::copy() {
    std::set<std::string> ids;
    for (auto *item : scene_->selectedItems())
        if (auto *n = dynamic_cast<NodeItem *>(item))
            ids.insert(n->node.id);
    ShaderGraph copied;
    copied.subgraphs = graph_.subgraphs;
    for (const auto &n : graph_.nodes)
        if (ids.count(n.id))
            copied.nodes.push_back(n);
    for (const auto &e : graph_.links)
        if (ids.count(e.from) && ids.count(e.to))
            copied.links.push_back(e);
    auto *mime = new QMimeData;
    mime->setData("application/x-iw8r-shader-graph", QByteArray::fromStdString(shaderGraphJson(copied).dump()));
    QApplication::clipboard()->setMimeData(mime);
}
void ShaderGraphWidget::paste() {
    const auto *mime = QApplication::clipboard()->mimeData();
    if (!mime->hasFormat("application/x-iw8r-shader-graph"))
        return;
    ShaderGraph copied;
    std::string error;
    try {
        if (!readShaderGraph(TechsetJson::parse(mime->data("application/x-iw8r-shader-graph").toStdString()), copied,
                             error))
            return;
    } catch (...) {
        return;
    }
    const auto before = graph_;
    std::map<std::string, std::string> ids;
    std::map<std::string, std::string> definitions;
    std::set<std::string> reserved;
    for (const auto &[name, sub] : copied.subgraphs) {
        auto next = name;
        unsigned suffix = 1;
        while (graph_.subgraphs.count(next) || reserved.count(next))
            next = name + "_copy" + std::to_string(suffix++);
        definitions[name] = next;
        reserved.insert(next);
    }
    const auto renameCalls = [](std::vector<ShaderNode> &nodes, const auto &names) {
        for (auto &node : nodes)
            if (node.kind == "subgraph") {
                const auto name = node.properties.value("name", "");
                if (names.count(name))
                    node.properties["name"] = names.at(name);
            }
    };
    renameCalls(copied.nodes, definitions);
    std::function<void(ShaderGraph &, std::map<std::string, std::string>)> renameDefinition;
    renameDefinition = [&](ShaderGraph &sub, std::map<std::string, std::string> names) {
        for (const auto &[local, child] : sub.subgraphs)
            names.erase(local);
        renameCalls(sub.nodes, names);
        for (auto &[local, child] : sub.subgraphs)
            renameDefinition(child, names);
    };
    for (auto &[name, sub] : copied.subgraphs)
        renameDefinition(sub, definitions);
    for (auto n : copied.nodes) {
        ids[n.id] = id();
        n.id = ids[n.id];
        n.x += 40;
        n.y += 40;
        graph_.nodes.push_back(n);
    }
    for (auto e : copied.links)
        if (ids.count(e.from) && ids.count(e.to)) {
            e.from = ids[e.from];
            e.to = ids[e.to];
            graph_.links.push_back(e);
        }
    for (const auto &[name, sub] : copied.subgraphs)
        graph_.subgraphs.emplace(definitions.at(name), sub);
    commit(before, "Paste nodes");
}
void ShaderGraphWidget::makeSubgraph() {
    std::set<std::string> selected;
    for (auto *item : scene_->selectedItems())
        if (auto *n = dynamic_cast<NodeItem *>(item))
            selected.insert(n->node.id);
    if (selected.empty())
        return;
    std::set<std::string> outputs;
    for (const auto &e : graph_.links)
        if (selected.count(e.from) && !selected.count(e.to))
            outputs.insert(e.from);
    for (const auto &n : graph_.nodes)
        if (selected.count(n.id) && (n.kind == "surface" || n.kind == "vertex_output")) {
            QMessageBox::information(this, "Subgraph",
                                     "Select value nodes, leaving the stage output outside the subgraph.");
            return;
        }
    if (outputs.size() != 1) {
        QMessageBox::information(this, "Subgraph",
                                 "The selected nodes must have one result connected to the rest of the graph.");
        return;
    }
    bool ok = false;
    const QString name =
        QInputDialog::getText(this, "Subgraph", "Reusable name", QLineEdit::Normal, "my_function", &ok);
    if (!ok || name.isEmpty() || graph_.subgraphs.count(name.toStdString()))
        return;
    const auto before = graph_;
    ShaderGraph sub;
    ShaderNode call{id(), "subgraph", name.toStdString(), 100, 100, {{"name", name.toStdString()}}};
    std::vector<ShaderLink> retained;
    size_t inputNo = 0;
    for (const auto &n : graph_.nodes)
        if (selected.count(n.id))
            sub.nodes.push_back(n);
    for (const auto &e : graph_.links) {
        const bool from = selected.count(e.from), to = selected.count(e.to);
        if (from && to)
            sub.links.push_back(e);
        else if (from)
            retained.push_back({call.id, "out", e.to, e.input});
        else if (to) {
            const auto inputName = "input_" + std::to_string(++inputNo);
            std::string type = "float";
            for (const auto &n : graph_.nodes)
                if (n.id == e.from) {
                    auto *spec = shaderNodeSpec(n.kind);
                    if (spec && spec->output != "any")
                        type = spec->output;
                    else
                        type = n.properties.value("type", "float");
                }
            const auto nodeId = id();
            sub.nodes.push_back(
                {nodeId, "input", inputName, -200, double(inputNo * 80), {{"name", inputName}, {"type", type}}});
            sub.links.push_back({nodeId, "out", e.to, e.input});
            retained.push_back({e.from, "out", call.id, inputName});
        } else
            retained.push_back(e);
    }
    const auto outputId = id();
    sub.nodes.push_back({outputId, "output", "Result", 600, 100});
    sub.links.push_back({*outputs.begin(), "out", outputId, "value"});
    graph_.nodes.erase(
        std::remove_if(graph_.nodes.begin(), graph_.nodes.end(), [&](const auto &n) { return selected.count(n.id); }),
        graph_.nodes.end());
    graph_.nodes.push_back(call);
    graph_.links = std::move(retained);
    graph_.subgraphs[name.toStdString()] = std::move(sub);
    selected_ = call.id;
    commit(before, "Extract subgraph");
}
void ShaderGraphWidget::focusNode(const QString &id) {
    for (auto *item : scene_->items())
        if (item->data(0).toString() == id) {
            scene_->clearSelection();
            item->setSelected(true);
            view_->centerOn(item);
            return;
        }
}
void ShaderGraphWidget::setDiagnostics(const std::vector<ShaderDiagnostic> &diagnostics) {
    errors_.clear();
    for (const auto &d : diagnostics)
        if (!d.warning && !d.node.empty())
            errors_.insert(d.node);
    for (auto *item : scene_->items())
        if (auto *n = dynamic_cast<NodeItem *>(item)) {
            n->error = errors_.count(n->node.id) > 0;
            n->update();
        }
}
} // namespace iw8r::qt
