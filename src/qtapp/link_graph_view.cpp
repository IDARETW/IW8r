#include "link_graph_view.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace iw8r::qt {

namespace {
constexpr double kBoxW = 170, kBoxH = 38;
}

LinkGraphView::LinkGraphView(EditorContext* ctx, QWidget* parent) : QWidget(parent), ctx_(ctx) {
    setMinimumSize(200, 120);
    setMouseTracking(false);
    connect(ctx_, &EditorContext::documentChanged, this, [this] {
        if (isVisible()) rebuild();
        update();
    });
    rebuild();
}

void LinkGraphView::rebuild() {
    if (builtRevision_ == ctx_->doc.revision()) return;
    builtRevision_ = ctx_->doc.revision();
    const MapFile& map = ctx_->doc.map();
    graph_ = buildLinkGraph(map);
    layout_ = layoutLinkGraph(graph_);
    nodeIndex_.clear();
    for (size_t i = 0; i < layout_.nodes.size(); ++i) nodeIndex_[layout_.nodes[i].entity] = i;
    issues_.clear();
    for (const LinkIssue& is : checkLinks(map, graph_)) issues_.insert(is.entity);
    label_.clear();
    for (const auto& n : layout_.nodes)
        if (const Entity* e = ctx_->doc.findEntity(n.entity)) {
            QString l = QString::fromStdString(e->classname());
            if (const std::string* tn = e->value("targetname"); tn && !tn->empty()) l += "\n" + QString::fromStdString(*tn);
            else if (const std::string* ln = e->value("script_linkname"); ln && !ln->empty()) l += "\n" + QString::fromStdString(*ln);
            label_[n.entity] = l;
        }
}

QPointF LinkGraphView::toWidget(double x, double y) const { return QPointF(x * zoom_ + pan_.x(), y * zoom_ + pan_.y()); }

QPointF LinkGraphView::nodeCenter(Id entity) const {
    const auto it = nodeIndex_.find(entity);
    if (it == nodeIndex_.end()) return QPointF(-1e9, -1e9);
    const auto& n = layout_.nodes[it->second];
    return toWidget(n.x, n.y);
}

Id LinkGraphView::nodeAt(const QPointF& p) const {
    for (const auto& n : layout_.nodes) {
        const QPointF c = toWidget(n.x, n.y);
        if (std::fabs(p.x() - c.x()) <= kBoxW * zoom_ / 2 && std::fabs(p.y() - c.y()) <= kBoxH * zoom_ / 2) return n.entity;
    }
    return kNoId;
}

void LinkGraphView::paintEvent(QPaintEvent*) {
    rebuild();
    QPainter p(this);
    p.fillRect(rect(), QColor(32, 34, 40));
    p.setRenderHint(QPainter::Antialiasing, true);
    if (layout_.nodes.empty()) {
        p.setPen(QColor(150, 155, 165));
        p.drawText(rect().adjusted(12, 12, -12, -12), Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap,
                   "No links in this map.\nEntities connect through target / targetname (target2..8) and script_linkto / "
                   "script_linkname; they show here as a node graph.");
        return;
    }
    // Edges first: from the right side of the source box to the left side of the target box.
    for (const LinkEdge& e : graph_.edges) {
        const auto a = nodeIndex_.find(e.from), b = nodeIndex_.find(e.to);
        if (a == nodeIndex_.end() || b == nodeIndex_.end()) continue;
        const auto& na = layout_.nodes[a->second];
        const auto& nb = layout_.nodes[b->second];
        const bool linkto = e.key == "script_linkto";
        p.setPen(QPen(linkto ? QColor(120, 200, 255) : QColor(230, 190, 90), 1.6));
        QPointF from = toWidget(na.x + kBoxW / 2, na.y), to = toWidget(nb.x - kBoxW / 2, nb.y);
        if (e.from == e.to) {  // self-target: a small loop above the box
            const QPointF c = toWidget(na.x, na.y - kBoxH / 2);
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(c + QPointF(0, -8 * zoom_), 10 * zoom_, 8 * zoom_);
            continue;
        }
        if (nb.layer <= na.layer) to = toWidget(nb.x + kBoxW / 2, nb.y), from = toWidget(na.x + kBoxW / 2, na.y);  // back edge
        QPainterPath path(from);
        const double dx = std::max(40.0 * zoom_, std::fabs(to.x() - from.x()) * 0.5);
        path.cubicTo(from + QPointF(dx, 0), to - QPointF(nb.layer <= na.layer ? -dx : dx, 0), to);
        p.setBrush(Qt::NoBrush);
        p.drawPath(path);
        // Arrow head at the target end.
        const QPointF dir = path.pointAtPercent(1.0) - path.pointAtPercent(0.97);
        const double len = std::hypot(dir.x(), dir.y());
        if (len > 1e-6) {
            const QPointF u = dir / len, n(-u.y(), u.x());
            const double s = 7 * std::max(0.6, zoom_);
            QPolygonF head{to, to - u * s + n * s * 0.5, to - u * s - n * s * 0.5};
            p.setBrush(p.pen().color());
            p.drawPolygon(head);
        }
    }
    // Nodes.
    QFont f = font();
    f.setPointSizeF(std::clamp(8.5 * zoom_, 5.0, 14.0));
    p.setFont(f);
    for (const auto& n : layout_.nodes) {
        const QPointF c = toWidget(n.x, n.y);
        const QRectF box(c.x() - kBoxW * zoom_ / 2, c.y() - kBoxH * zoom_ / 2, kBoxW * zoom_, kBoxH * zoom_);
        if (!box.intersects(QRectF(rect()))) continue;
        const bool sel = ctx_->doc.isSelected(SelRef{n.entity, kNoId});
        const bool bad = issues_.count(n.entity) > 0;
        p.setPen(QPen(sel ? QColor(255, 230, 80) : bad ? QColor(240, 80, 70) : QColor(110, 120, 135), sel ? 2.2 : 1.2));
        p.setBrush(QColor(52, 56, 66));
        p.drawRoundedRect(box, 5, 5);
        p.setPen(bad ? QColor(255, 150, 140) : QColor(225, 228, 235));
        const auto it = label_.find(n.entity);
        p.drawText(box.adjusted(6, 2, -6, -2), Qt::AlignCenter | Qt::TextWordWrap, it != label_.end() ? it->second : QString::number(n.entity));
    }
    p.setPen(QColor(140, 145, 155));
    p.setFont(font());
    p.drawText(QRectF(8, height() - 22, width() - 16, 18), Qt::AlignLeft | Qt::AlignVCenter,
               QString("%1 entities, %2 links%3   (orange target, blue script_linkto; wheel zooms, right-drag pans)")
                   .arg(layout_.nodes.size()).arg(graph_.edges.size())
                   .arg(issues_.empty() ? QString() : QString(", %1 with problems (red)").arg(issues_.size())));
}

void LinkGraphView::mousePressEvent(QMouseEvent* e) {
    lastMouse_ = e->pos();
    if (e->button() == Qt::RightButton || e->button() == Qt::MiddleButton) {
        panning_ = true;
        return;
    }
    if (e->button() != Qt::LeftButton) return;
    const Id hit = nodeAt(e->position());
    if (hit == kNoId) {
        if (!(e->modifiers() & Qt::ControlModifier)) {
            panning_ = true;  // left-drag on empty space pans as well
        }
        return;
    }
    const bool additive = e->modifiers() & Qt::ControlModifier;
    if (!additive) ctx_->doc.deselectAll();
    ctx_->doc.select(SelRef{hit, kNoId}, true);
    ctx_->documentEdited();
    update();
}

void LinkGraphView::mouseMoveEvent(QMouseEvent* e) {
    if (!panning_) return;
    pan_ += QPointF(e->pos() - lastMouse_);
    lastMouse_ = e->pos();
    update();
}

void LinkGraphView::mouseReleaseEvent(QMouseEvent*) { panning_ = false; }

void LinkGraphView::mouseDoubleClickEvent(QMouseEvent* e) {
    const Id hit = nodeAt(e->position());
    if (hit == kNoId) return;
    ctx_->doc.deselectAll();
    ctx_->doc.select(SelRef{hit, kNoId}, true);
    ctx_->documentEdited();
    ctx_->frameSelection();
}

void LinkGraphView::wheelEvent(QWheelEvent* e) {
    const double k = e->angleDelta().y() > 0 ? 1.15 : 1.0 / 1.15;
    const double nz = std::clamp(zoom_ * k, 0.2, 3.0);
    const QPointF m = e->position();
    pan_ = m - (m - pan_) * (nz / zoom_);
    zoom_ = nz;
    update();
}

}  // namespace iw8r::qt
