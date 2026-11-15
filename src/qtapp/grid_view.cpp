#include "game_mode_view.h"
#include "grid_view.h"
#include "spawn_analysis.h"
#include "light_budget.h"

#include "brush_tools.h"
#include "patch_ops2.h"

#include "asset_browser.h"
#include <QDropEvent>
#include <QDragEnterEvent>
#include <QMimeData>
#include <QAction>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace iw8r::qt {

namespace {
const char* kAxisNames[3] = {"XY Top", "XZ Front", "YZ Side"};

QColor fromAbgr(uint32_t c, int alpha = 255) { return QColor(int(c & 0xff), int((c >> 8) & 0xff), int((c >> 16) & 0xff), alpha); }

bool isCameraKey(int k) {
    switch (k) {
        case Qt::Key_Up: case Qt::Key_Down: case Qt::Key_Left: case Qt::Key_Right:
        case Qt::Key_Comma: case Qt::Key_Period: case Qt::Key_A: case Qt::Key_Z: case Qt::Key_D: case Qt::Key_C:
            return true;
        default:
            return false;
    }
}

// Minor grid line spacing as drawn: the grid size, doubled while the lines would be under 6 px apart.
double minorGridStep(double grid, double scale) {
    double minor = grid;
    if (!(grid > 0) || !(scale > 0)) return minor;
    while (minor * scale < 6.0) minor *= 2.0;
    return minor;
}

constexpr double kWheelStep = 1.25;  // zoom factor of one wheel notch and of one Zoom In / Zoom Out
}  // namespace

GridView::GridView(EditorContext* ctx, QWidget* parent) : QWidget(parent), ctx_(ctx) {
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setMinimumSize(64, 64);
    setAttribute(Qt::WA_OpaquePaintEvent);
    connect(ctx_, &EditorContext::documentChanged, this, qOverload<>(&QWidget::update));
    connect(ctx_, &EditorContext::cameraChanged, this, &GridView::cameraChangedRepaint);
}

QRect GridView::cameraMarkerRect() const {
    const AxisScope axisScope(const_cast<GridView*>(this));
    const int H = ctx_->axisH(), V = ctx_->axisV();
    const QPointF c = worldToWidget(ctx_->cam.pos[H], ctx_->cam.pos[V]);
    return QRect(int(c.x()) - 30, int(c.y()) - 30, 61, 61);  // the dot (4) and the heading line (24), with the pen
}

void GridView::cameraChangedRepaint() {
    const AxisScope axisScope(this);
    const double now[5] = {ctx_->cam.pos.x, ctx_->cam.pos.y, ctx_->cam.pos.z, ctx_->cam.yaw, ctx_->cam.pitch};
    bool moved = false;
    for (int k = 0; k < 5; ++k) moved |= now[k] != lastCam_[k];
    // Center On Selection and Next View move this view's pan, zoom or axis along with the camera: then the whole
    // view is new, not just the marker.
    const double view[4] = {double(ctx_->gridAxis), ctx_->gridCenterH, ctx_->gridCenterV, ctx_->gridScale};
    bool viewChanged = false;
    for (int k = 0; k < 4; ++k) viewChanged |= view[k] != lastView_[k];
    const QRect marker = cameraMarkerRect();
    // Only the marker moved: the rest of the view is as it was. The light-overlap heat map follows the camera height,
    // so it repaints whole.
    if (moved && !viewChanged && !lastCameraMarker_.isNull() && !(ctx_->heatMap == 1 && now[2] != lastCam_[2]))
        update(QRegion(lastCameraMarker_) + QRegion(marker));
    else update();
    for (int k = 0; k < 5; ++k) lastCam_[k] = now[k];
    for (int k = 0; k < 4; ++k) lastView_[k] = view[k];
    lastCameraMarker_ = marker;
}

void GridView::resizeEvent(QResizeEvent* e) {
    // The main view's size, for fits made from the context (Center Camera On Selection).
    if (fixedAxis_ < 0) ctx_->gridViewWidth = width(), ctx_->gridViewHeight = height();
    QWidget::resizeEvent(e);
}

double GridView::drawnGridStep() const {
    // Outside a handler a fixed-axis view's zoom is its own, not the context's.
    const double scale = fixedAxis_ >= 0 && scopeDepth_ == 0 ? ownScale_ : ctx_->gridScale;
    return minorGridStep(ctx_->doc.grid(), scale);
}

void GridView::zoomIn() {
    const AxisScope axisScope(this);
    ctx_->gridScale = EditorContext::clampGridScale(ctx_->gridScale * kWheelStep);
    update();
}

void GridView::zoomOut() {
    const AxisScope axisScope(this);
    ctx_->gridScale = EditorContext::clampGridScale(ctx_->gridScale / kWheelStep);
    update();
}

void GridView::fitAll() {
    const AxisScope axisScope(this);
    ctx_->scene.sync(ctx_->doc);
    AABB b;
    for (const app::PrimGeom* g : ctx_->scene.items())
        if (g->bounds.valid()) b.add(g->bounds);
    if (!b.valid()) {
        ctx_->setStatus("Nothing in the map to fit");
        return;
    }
    ctx_->fitGridTo(b, width(), height());
    update();
}

QPointF GridView::worldToWidget(double h, double v) const {
    return {width() * 0.5 + (h - ctx_->gridCenterH) * ctx_->gridScale, height() * 0.5 - (v - ctx_->gridCenterV) * ctx_->gridScale};
}
double GridView::widgetToWorldH(double x) const { return ctx_->gridCenterH + (x - width() * 0.5) / ctx_->gridScale; }
double GridView::widgetToWorldV(double y) const { return ctx_->gridCenterV - (y - height() * 0.5) / ctx_->gridScale; }

const GridView::HeatMap& GridView::heatMap() {
    const int mode = ctx_->heatMap;
    // The light overlap depends on the sampling height: the camera's, rounded to 64 units.
    const int64_t zKey = mode == 1 ? int64_t(std::floor(ctx_->cam.pos.z / 64.0)) : 0;
    const uint64_t key = (ctx_->doc.revision() * 1000003ull) ^ (uint64_t(mode) << 56) ^ uint64_t(zKey * 7919);
    if (key == heatKey_ && heat_.mode == mode) return heat_;
    heatKey_ = key;
    heat_ = HeatMap{};
    heat_.mode = mode;
    if (mode == 0) return heat_;
    AABB b;
    for (const auto* g : ctx_->scene.items())
        if (g->bounds.valid()) b.add(g->bounds);
    if (!b.valid()) return heat_;
    const double pad = 256;
    heat_.minX = b.mins.x - pad, heat_.minY = b.mins.y - pad, heat_.maxX = b.maxs.x + pad, heat_.maxY = b.maxs.y + pad;
    const double cell = std::max(32.0, std::max(heat_.maxX - heat_.minX, heat_.maxY - heat_.minY) / 96.0);
    heat_.cellsX = std::clamp(int(std::ceil((heat_.maxX - heat_.minX) / cell)), 1, 256);
    heat_.cellsY = std::clamp(int(std::ceil((heat_.maxY - heat_.minY) / cell)), 1, 256);
    AABB area;
    area.add(Vec3{heat_.minX, heat_.minY, b.mins.z});
    area.add(Vec3{heat_.maxX, heat_.maxY, b.maxs.z});
    if (mode == 1) {
        const std::vector<int> v = lightOverlap(collectLights(ctx_->doc.map()), area, heat_.cellsX, heat_.cellsY, ctx_->cam.pos.z);
        heat_.values.assign(v.begin(), v.end());
    } else {
        heat_.values = spawnDensity(collectSpawns(ctx_->doc.map()), area, heat_.cellsX, heat_.cellsY, 512.0);
    }
    for (double v : heat_.values) heat_.maxValue = std::max(heat_.maxValue, v);
    if (heat_.values.size() != size_t(heat_.cellsX) * size_t(heat_.cellsY)) heat_.cellsX = heat_.cellsY = 0;
    return heat_;
}

void GridView::setFixedAxis(int axis) {
    fixedAxis_ = axis;
    if (axis >= 0) {
        ownCenterH_ = ctx_->gridCenterH, ownCenterV_ = ctx_->gridCenterV, ownScale_ = ctx_->gridScale;
    }
    update();
}

GridView::AxisScope::AxisScope(GridView* view) : v(view) {
    if (v->fixedAxis_ < 0 || v->scopeDepth_++ > 0) return;
    EditorContext* c = v->ctx_;
    v->saved_ = {c->gridAxis, c->gridCenterH, c->gridCenterV, c->gridScale};
    c->gridAxis = v->fixedAxis_;
    c->gridCenterH = v->ownCenterH_, c->gridCenterV = v->ownCenterV_, c->gridScale = v->ownScale_;
}

GridView::AxisScope::~AxisScope() {
    if (v->fixedAxis_ < 0 || --v->scopeDepth_ > 0) return;
    EditorContext* c = v->ctx_;
    v->ownCenterH_ = c->gridCenterH, v->ownCenterV_ = c->gridCenterV, v->ownScale_ = c->gridScale;
    c->gridAxis = v->saved_.axis;
    c->gridCenterH = v->saved_.h, c->gridCenterV = v->saved_.v, c->gridScale = v->saved_.scale;
}

void GridView::paintEvent(QPaintEvent*) {
    const AxisScope axisScope(this);
    ctx_->scene.sync(ctx_->doc);
    QPainter p(this);
    p.fillRect(rect(), QColor(30, 32, 36));
    const int H = ctx_->axisH(), V = ctx_->axisV();
    const double scale = ctx_->gridScale;
    const double h0 = widgetToWorldH(0), h1 = widgetToWorldH(width()), v0 = widgetToWorldV(height()), v1 = widgetToWorldV(0);
    const double minor = minorGridStep(ctx_->doc.grid(), scale);
    const double major = std::max(64.0, minor * 8.0);
    auto lines = [&](double step, const QColor& c) {
        if (step * scale < 3.0) return;
        p.setPen(QPen(c, 0));
        for (double h = std::floor(h0 / step) * step; h <= h1; h += step) p.drawLine(worldToWidget(h, v0), worldToWidget(h, v1));
        for (double v = std::floor(v0 / step) * step; v <= v1; v += step) p.drawLine(worldToWidget(h0, v), worldToWidget(h1, v));
    };
    // Stock map reference backdrop (3.19), under the grid lines.
    for (const ReferencePlan& rp : ctx_->referencePlans)
        if (rp.h == H && rp.v == V && !rp.image.isNull()) {
            p.setOpacity(0.55);
            p.drawImage(QRectF(worldToWidget(rp.minH, rp.maxV), worldToWidget(rp.maxH, rp.minV)), rp.image);
            p.setOpacity(1.0);
        }
    // Heat map (5.15 light overlap, 6.18 spawn density): top view only, under the grid lines.
    if (ctx_->heatMap != 0 && ctx_->gridAxis == 0) {
        const HeatMap& hm = heatMap();
        if (hm.cellsX > 0 && hm.maxValue > 0) {
            const double cw = (hm.maxX - hm.minX) / hm.cellsX, ch = (hm.maxY - hm.minY) / hm.cellsY;
            p.setPen(Qt::NoPen);
            for (int y = 0; y < hm.cellsY; ++y)
                for (int x = 0; x < hm.cellsX; ++x) {
                    const double v = hm.values[size_t(y) * size_t(hm.cellsX) + size_t(x)];
                    if (v <= 0) continue;
                    QColor c;
                    if (hm.mode == 1) {
                        // Lights reaching the cell: green 1-2, yellow 3-5, orange 6-8, red over the budget (8).
                        c = v > lightbudget::kMaxOverlap ? QColor(230, 40, 40) : v > 5 ? QColor(240, 140, 30) : v > 2 ? QColor(230, 220, 40) : QColor(60, 200, 80);
                    } else {
                        const double t = std::clamp(v / hm.maxValue, 0.0, 1.0);  // blue (sparse) -> red (crowded)
                        c = QColor::fromHsvF(0.66 * (1.0 - t), 0.85, 0.95);
                    }
                    c.setAlphaF(hm.mode == 1 ? 0.33 : 0.25 + 0.35 * std::clamp(v / hm.maxValue, 0.0, 1.0));
                    p.fillRect(QRectF(worldToWidget(hm.minX + x * cw, hm.minY + (y + 1) * ch), worldToWidget(hm.minX + (x + 1) * cw, hm.minY + y * ch)), c);
                }
        }
    }
    lines(minor, QColor(48, 51, 57));
    lines(major, QColor(72, 76, 86));
    p.setPen(QPen(QColor(110, 60, 60), 0));
    p.drawLine(worldToWidget(0, v0), worldToWidget(0, v1));
    p.setPen(QPen(QColor(60, 110, 60), 0));
    p.drawLine(worldToWidget(h0, 0), worldToWidget(h1, 0));
    p.setPen(QColor(150, 150, 160));
    const QFontMetrics fm = p.fontMetrics();
    for (double h = std::floor(h0 / major) * major; h <= h1; h += major)
        p.drawText(QPointF(worldToWidget(h, 0).x() + 2, fm.ascent() + 2), QString::fromStdString(formatNumber(h)));
    for (double v = std::floor(v0 / major) * major; v <= v1; v += major)
        p.drawText(QPointF(2, worldToWidget(0, v).y() - 2), QString::fromStdString(formatNumber(v)));

    // Warzone maps hold tens of thousands of entities (Verdansk: 47,545 plus 23,446 loot points): no antialiasing
    // then, and an entity a few pixels across is a dot (a filled, outlined, antialiased rectangle each made a zoomed-out
    // paint take seconds).
    const bool manyItems = ctx_->scene.items().size() > 20000;
    p.setRenderHint(QPainter::Antialiasing, !manyItems);
    for (int pass = 0; pass < 2; ++pass) {
        for (const app::PrimGeom* g : ctx_->scene.items()) {
            const bool sel = ctx_->doc.isSelected(g->ref);
            if (sel != (pass == 1)) continue;
            if (g->bounds.maxs[H] < h0 || g->bounds.mins[H] > h1 || g->bounds.maxs[V] < v0 || g->bounds.mins[V] > v1) continue;
            QColor col;
            if (sel) col = QColor(255, 60, 60);
            else if (g->kind == app::PrimGeom::PointEntity || g->brushEntity) col = fromAbgr(g->color);
            else if (g->kind == app::PrimGeom::Patch) col = QColor(190, 140, 230);
            else col = QColor(200, 200, 205);
            p.setPen(QPen(col, sel ? 1.5 : 1.0));
            if (g->prefab) {
                // misc_prefab: its contents, inside a dashed bounding box.
                const QRectF r(worldToWidget(g->bounds.mins[H], g->bounds.maxs[V]), worldToWidget(g->bounds.maxs[H], g->bounds.mins[V]));
                p.setBrush(Qt::NoBrush);
                p.setPen(QPen(col, 1.0, Qt::DashLine));
                p.drawRect(r);
                p.setPen(QPen(col, sel ? 1.5 : 1.0));
            } else if (g->kind == app::PrimGeom::PointEntity) {
                const QRectF r(worldToWidget(g->bounds.mins[H], g->bounds.maxs[V]), worldToWidget(g->bounds.maxs[H], g->bounds.mins[V]));
                if (!sel && std::fabs(r.width()) < 3.0 && std::fabs(r.height()) < 3.0) {
                    p.drawPoint(r.center());
                    continue;
                }
                p.fillRect(r, QColor(col.red(), col.green(), col.blue(), 96));
                p.drawRect(r);
                if (g->hasArrow && ctx_->showAngles)
                    p.drawLine(worldToWidget(g->arrowFrom[H], g->arrowFrom[V]), worldToWidget(g->arrowTo[H], g->arrowTo[V]));
                continue;
            }
            p.setBrush(Qt::NoBrush);
            if (!g->edges.empty()) {
                for (const auto& [a, b] : g->edges) p.drawLine(worldToWidget(a[H], a[V]), worldToWidget(b[H], b[V]));
                continue;
            }
            // Models (thousands of triangles each; a stock map holds about a thousand) are their bounding box here unless
            // selected: drawing every triangle made each camera step repaint millions of polygons (5 fps on mp_rust).
            if (g->mesh && !sel) {
                p.drawRect(QRectF(worldToWidget(g->bounds.mins[H], g->bounds.maxs[V]), worldToWidget(g->bounds.maxs[H], g->bounds.mins[V])));
                continue;
            }
            for (size_t pi = 0; pi < g->polys.size(); ++pi) {
                if (!sel && g->meshPoly(pi)) continue;  // a prefab's models: its box (dashed, above) stands for them
                const auto& w = g->polys[pi];
                QPolygonF poly;
                poly.reserve(int(w.size()));
                for (const auto& v : w) poly << worldToWidget(v[H], v[V]);
                p.drawPolygon(poly);
            }
        }
    }
    // Entity display (2.17): names next to point entities and models, crosses at model origins.
    if ((ctx_->showEntityNames || ctx_->showModelOrigins) && ctx_->gridScale > 0.15) {
        QFont small = p.font();
        small.setPointSizeF(std::max(6.0, small.pointSizeF() - 1));
        p.setFont(small);
        for (const app::PrimGeom* g : ctx_->scene.items()) {
            if (g->kind != app::PrimGeom::PointEntity) continue;
            if (g->bounds.maxs[H] < h0 || g->bounds.mins[H] > h1 || g->bounds.maxs[V] < v0 || g->bounds.mins[V] > v1) continue;
            const Entity* e = ctx_->doc.findEntity(g->ref.entity);
            if (!e) continue;
            const std::string* model = e->value("model");
            if (ctx_->showModelOrigins && model && !model->empty()) {
                if (const auto o = e->origin()) {
                    const QPointF c = worldToWidget((*o)[H], (*o)[V]);
                    p.setPen(QPen(QColor(255, 220, 90), 1.0));
                    p.drawLine(c + QPointF(-5, 0), c + QPointF(5, 0));
                    p.drawLine(c + QPointF(0, -5), c + QPointF(0, 5));
                }
            }
            if (ctx_->showEntityNames) {
                QString name;
                if (const std::string* tn = e->value("targetname"); tn && !tn->empty()) name = QString::fromStdString(*tn);
                else if (model && !model->empty()) name = QString::fromStdString(*model).section('/', -1);
                else name = QString::fromStdString(e->classname());
                p.setPen(QColor(210, 210, 215));
                p.drawText(worldToWidget(g->bounds.maxs[H], g->bounds.maxs[V]) + QPointF(3, -2), name);
            }
        }
    }
    if (ctx_->showSpawns) {
        p.setPen(QPen(QColor(64, 255, 64, 200), 1.0));
        const GameModeView* mode = findGameModeView(ctx_->scene.gameMode);
        for (const auto& s : ctx_->scene.spawns)
            if (!mode || gameModeKeepsSpawn(*mode, s.name)) p.drawRect(QRectF(worldToWidget(s.origin[H] - 16, s.origin[V] + (V == 2 ? 72 : 16)), worldToWidget(s.origin[H] + 16, s.origin[V] - (V == 2 ? 0 : 16))));
    }
    // Vertex mode handles: brush corners and patch control points of the selection (dragged ones follow the mouse).
    if (ctx_->vertexMode) {
        const Vec3 d = drag_ == Drag::Vertex ? vertexDelta() : Vec3{};
        auto dragged = [&](const VertexHandle& h) {
            for (const auto& v : vertexDrag_)
                if (v.entity == h.entity && v.item == h.item && v.patch == h.patch && v.index == h.index && length(v.pos - h.pos) < 1e-6) return true;
            return false;
        };
        p.setPen(QPen(QColor(20, 20, 20), 1.0));
        for (const VertexHandle& h : selectionVertexHandles(*ctx_)) {
            const bool on = drag_ == Drag::Vertex && dragged(h);
            const Vec3 at = on ? h.pos + d : h.pos;
            const QPointF c = worldToWidget(at[H], at[V]);
            p.setBrush(on ? QColor(255, 230, 60) : h.locked ? QColor(220, 60, 60) : (h.patch ? QColor(230, 120, 255) : QColor(60, 220, 255)));
            p.drawRect(QRectF(c.x() - 3, c.y() - 3, 6, 6));
        }
        p.setBrush(Qt::NoBrush);
    }
    // Edge mode handles: midpoints of the edges of the selected brushes (the dragged one follows the mouse).
    if (ctx_->edgeMode) {
        const Vec3 d = drag_ == Drag::Edge ? vertexDelta() : Vec3{};
        p.setPen(QPen(QColor(20, 20, 20), 1.0));
        for (const EdgeHandle& h : selectionEdgeHandles()) {
            const bool on = drag_ == Drag::Edge && edgeDrag_ && edgeDrag_->entity == h.entity && edgeDrag_->item == h.item &&
                            length(edgeDrag_->edge.a - h.edge.a) < 1e-6 && length(edgeDrag_->edge.b - h.edge.b) < 1e-6;
            const Vec3 at = on ? h.mid + d : h.mid;
            p.setBrush(on ? QColor(255, 230, 60) : QColor(120, 255, 160));
            p.drawEllipse(worldToWidget(at[H], at[V]), 4, 4);
        }
        p.setBrush(Qt::NoBrush);
    }
    // Entity connections (target -> targetname).
    p.setPen(QPen(QColor(255, 208, 48, 200), 1.0, Qt::DashLine));
    for (const auto& [a, b] : ctx_->scene.connections()) p.drawLine(worldToWidget(a[H], a[V]), worldToWidget(b[H], b[V]));
    // Leak pointfile: from the leaking entity out to the void.
    if (ctx_->scene.pointfile.size() > 1) {
        p.setPen(QPen(QColor(255, 210, 32), 2.0));
        for (size_t i = 0; i + 1 < ctx_->scene.pointfile.size(); ++i) {
            const Vec3& a = ctx_->scene.pointfile[i];
            const Vec3& b = ctx_->scene.pointfile[i + 1];
            p.drawLine(worldToWidget(a[H], a[V]), worldToWidget(b[H], b[V]));
        }
    }
    // The active Region: its box, dashed amber (what lies outside it is hidden).
    if (ctx_->region && ctx_->region->valid()) {
        p.setPen(QPen(QColor(255, 180, 48), 1.5, Qt::DashDotLine));
        p.setBrush(Qt::NoBrush);
        p.drawRect(QRectF(worldToWidget(ctx_->region->mins[H], ctx_->region->maxs[V]), worldToWidget(ctx_->region->maxs[H], ctx_->region->mins[V])));
    }
    // Tool previews: new brush rectangle, resized brushes, clipper points and line (a press still within the drag
    // threshold shows none: it may be a click).
    if (drag_ == Drag::Create && dragLive_) {
        const AABB b = createBox();
        p.setPen(QPen(QColor(255, 220, 80), 1.5, Qt::DashLine));
        p.setBrush(QColor(255, 220, 80, 40));
        p.drawRect(QRectF(worldToWidget(b.mins[H], b.maxs[V]), worldToWidget(b.maxs[H], b.mins[V])));
    } else if (drag_ == Drag::Resize && dragLive_) {
        p.setPen(QPen(QColor(255, 220, 80), 1.5));
        p.setBrush(Qt::NoBrush);
        for (const auto& b : resizePreview())
            for (const auto& w : brushWindings(b)) {
                QPolygonF poly;
                for (const auto& v : w) poly << worldToWidget(v[H], v[V]);
                p.drawPolygon(poly);
            }
    } else if (drag_ == Drag::Edge && edgeDrag_) {
        // The brush as it would be after the edge move; nothing is drawn while the move is rejected.
        if (auto moved = moveBrushEdge(edgeDrag_->brush, edgeDrag_->edge, vertexDelta())) {
            p.setPen(QPen(QColor(255, 220, 80), 1.5));
            p.setBrush(Qt::NoBrush);
            for (const auto& w : brushWindings(*moved)) {
                QPolygonF poly;
                for (const auto& v : w) poly << worldToWidget(v[H], v[V]);
                p.drawPolygon(poly);
            }
        }
    }
    if (ctx_->clipMode) {
        p.setBrush(QColor(255, 120, 40));
        p.setPen(QPen(QColor(255, 120, 40), 2.0));
        for (const auto& cp : ctx_->clipPoints) p.drawEllipse(worldToWidget(cp[H], cp[V]), 5, 5);
        if (ctx_->clipPoints.size() >= 2) {
            const QPointF a = worldToWidget(ctx_->clipPoints[0][H], ctx_->clipPoints[0][V]);
            const QPointF b = worldToWidget(ctx_->clipPoints[1][H], ctx_->clipPoints[1][V]);
            const QPointF dir = b - a;
            p.drawLine(a - dir * 50, b + dir * 50);
            if (ctx_->clipPoints.size() == 3) {
                // Third point: dotted guides show how it tilts the plane away from the a-b line.
                const QPointF c = worldToWidget(ctx_->clipPoints[2][H], ctx_->clipPoints[2][V]);
                p.setPen(QPen(QColor(255, 120, 40), 1.0, Qt::DotLine));
                p.drawLine(a, c);
                p.drawLine(b, c);
                p.setPen(QPen(QColor(255, 120, 40), 2.0));
            }
            if (auto plane = ctx_->clipPlane()) {
                // Preview the kept part of each selected brush.
                p.setPen(QPen(QColor(255, 120, 40), 1.5, Qt::DashLine));
                p.setBrush(QColor(255, 120, 40, 50));
                for (const auto& sb : selectedBrushes(*ctx_))
                    if (auto kept = clipBrush(sb.brush, *plane, faceSuffix(*ctx_)))
                        for (const auto& w : brushWindings(*kept)) {
                            QPolygonF poly;
                            for (const auto& v : w) poly << worldToWidget(v[H], v[V]);
                            p.drawPolygon(poly);
                        }
            }
        }
        p.setPen(QColor(255, 160, 80));
        p.drawText(QPointF(40, 20), "CLIPPER (X to exit)");
    }
    // Terrain brush: the radius it raises or lowers (4 grid units) around the cursor.
    if (ctx_->terrainBrush && ctx_->gridAxis == 0 && rect().contains(hover_.toPoint())) {
        p.setPen(QPen(QColor(255, 200, 80), 1.0, Qt::DashLine));
        p.setBrush(Qt::NoBrush);
        const double r = ctx_->terrainRadius * ctx_->doc.grid() * ctx_->gridScale;
        p.drawEllipse(hover_, r, r);
    }
    // Measure ruler: the last measured segment with its length and per-axis deltas.
    if (ctx_->measureValid) {
        const QPointF a = worldToWidget(ctx_->measureA[H], ctx_->measureA[V]), b = worldToWidget(ctx_->measureB[H], ctx_->measureB[V]);
        p.setPen(QPen(QColor(255, 230, 90), 1.5));
        p.drawLine(a, b);
        p.drawEllipse(a, 3, 3);
        p.drawEllipse(b, 3, 3);
        const Vec3 d = ctx_->measureB - ctx_->measureA;
        p.drawText((a + b) / 2 + QPointF(6, -6), QString("%1  (%2 x %3)")
                                                     .arg(QString::fromStdString(formatNumber(length(d))))
                                                     .arg(QString::fromStdString(formatNumber(std::fabs(d[H]))))
                                                     .arg(QString::fromStdString(formatNumber(std::fabs(d[V])))));
    }
    // Camera marker.
    const QPointF c = worldToWidget(ctx_->cam.pos[H], ctx_->cam.pos[V]);
    const Vec3 f = ctx_->cam.forward();
    const double len = std::sqrt(f[H] * f[H] + f[V] * f[V]);
    p.setPen(QPen(QColor(80, 160, 255), 2.0));
    p.setBrush(QColor(80, 160, 255));
    p.drawEllipse(c, 4, 4);
    if (len > 1e-6) p.drawLine(c, c + QPointF(f[H] / len * 24, -f[V] / len * 24));
    p.setPen(QColor(200, 200, 200));
    QString label = QString("%1   grid %2").arg(kAxisNames[ctx_->gridAxis], QString::fromStdString(formatNumber(ctx_->doc.grid())));
    // Zoomed out, the lines drawn are a multiple of the grid (they would crowd otherwise): say so, or the label reads
    // as the spacing of the lines on screen.
    if (minor != ctx_->doc.grid()) label += QString(" (lines every %1)").arg(QString::fromStdString(formatNumber(minor)));
    if (!ctx_->snap) label += " (snap off)";
    p.drawText(QPointF(40, height() - 6), label);
}

double GridView::snapped(double v) const { return ctx_->snap ? snapToGrid(v, ctx_->doc.grid()) : v; }

std::vector<GridView::EdgeHandle> GridView::selectionEdgeHandles() const {
    std::vector<EdgeHandle> out;
    for (const SelectedBrush& sb : selectedBrushes(*ctx_))
        for (const BrushEdge& e : brushEdges(sb.brush)) out.push_back({sb.entity, sb.item, sb.brush, e, (e.a + e.b) * 0.5});
    return out;
}

void GridView::terrainStamp(double h, double v) {
    const double g = ctx_->doc.grid();
    const double radius = ctx_->terrainRadius * g;
    const Vec3 center{h, v, 0};
    const double strength = ctx_->terrainStrength;
    using Mode = EditorContext::TerrainBrushMode;
    bool changed = false;
    switch (ctx_->terrainBrushMode) {
        case Mode::Raise: {
            const double amount = terrainSign_ * g;  // one grid unit per step
            changed = editSelectedPatches(*ctx_, [&](const PatchGrid& pg) { return std::optional<PatchGrid>(raiseLower(pg, center, radius, amount)); }, "Terrain Brush");
            break;
        }
        case Mode::Smooth:
            changed = editSelectedPatches(*ctx_, [&](const PatchGrid& pg) { return std::optional<PatchGrid>(smoothAround(pg, center, radius, strength)); }, "Terrain Brush");
            break;
        case Mode::Flatten:
            changed = editSelectedPatches(*ctx_, [&](const PatchGrid& pg) { return std::optional<PatchGrid>(flattenAround(pg, center, radius, terrainFlatZ_, strength)); }, "Terrain Brush");
            break;
        case Mode::Noise: {
            // Each step gets its own seed, so a stroke adds varied bumps instead of repeating one pattern.
            const uint32_t seed = terrainSeed_++;
            const double amplitude = strength * g;  // up to half a grid unit per step at the default strength
            changed = editSelectedPatches(*ctx_, [&](const PatchGrid& pg) { return std::optional<PatchGrid>(noiseAround(pg, center, radius, amplitude, seed)); }, "Terrain Brush");
            break;
        }
    }
    terrainChanged_ |= changed;
    stampH_ = h;
    stampV_ = v;
}

namespace {

// z of the selected patch control point nearest (h, v) in the top view; 0 when no patch is selected.
double nearestPatchControlZ(EditorContext& ctx, double h, double v) {
    double bestZ = 0, bestD = std::numeric_limits<double>::infinity();
    for (const VertexHandle& vh : selectionVertexHandles(ctx)) {
        if (!vh.patch) continue;
        const double d = std::hypot(vh.pos.x - h, vh.pos.y - v);
        if (d < bestD) {
            bestD = d;
            bestZ = vh.pos.z;
        }
    }
    return bestZ;
}

}  // namespace

AABB GridView::createBox() const {
    const int H = ctx_->axisH(), V = ctx_->axisV(), D = ctx_->axisD();
    AABB box;
    Vec3 a, b;
    a[H] = snapped(dragStartH_);
    a[V] = snapped(dragStartV_);
    b[H] = snapped(dragNowH_);
    b[V] = snapped(dragNowV_);
    // Depth range: the selection's (Radiant uses the last brush), else 0..64 above the grid plane.
    const AABB sel = ctx_->doc.selectionBounds();
    a[D] = sel.valid() ? sel.mins[D] : 0;
    b[D] = sel.valid() ? sel.maxs[D] : 64;
    if (b[D] - a[D] < ctx_->doc.grid()) b[D] = a[D] + std::max(64.0, ctx_->doc.grid());
    box.add(a);
    box.add(b);
    return box;
}

std::vector<ConvexBrush> GridView::resizePreview() const {
    const int H = ctx_->axisH(), V = ctx_->axisV();
    Vec3 d;
    d[H] = snapped(dragNowH_ - dragStartH_);
    d[V] = snapped(dragNowV_ - dragStartV_);
    std::vector<ConvexBrush> out;
    for (const auto& t : resize_) {
        ConvexBrush b = t.original;
        bool ok = true;
        for (size_t f : t.faces) {
            const double dist = dot(b[f].plane.n, d);
            if (std::fabs(dist) < 1e-9) continue;
            auto moved = moveFace(b, f, dist);
            if (!moved) { ok = false; break; }
            b = *moved;
        }
        out.push_back(ok ? b : t.original);
    }
    return out;
}

const GridView::SelectionCache& GridView::selectionCache() {
    if (selCache_.revision == ctx_->doc.revision()) return selCache_;
    selCache_ = SelectionCache{};
    selCache_.revision = ctx_->doc.revision();
    selCache_.bounds = ctx_->doc.selectionBounds();
    for (const auto& r : ctx_->doc.selection()) {
        const EntityItem* it = r.item ? ctx_->doc.findItem(r.entity, r.item) : nullptr;
        const Primitive* p = it ? it->prim() : nullptr;
        if (!p || p->kind != PrimKind::Brush) continue;
        ResizeTarget t{r.entity, r.item, brushFromPrimitive(*p), {}, p};
        selCache_.centres.push_back(brushBounds(t.original).center());
        selCache_.brushes.push_back(std::move(t));
    }
    return selCache_;
}

std::optional<GridView::EdgeHandle> GridView::edgeHandleAt(const QPointF& pos) const {
    // Edge handles within 6 px; of those, the one nearest the viewer (top view: highest; front and side: lowest).
    const int H = ctx_->axisH(), V = ctx_->axisV(), D = ctx_->axisD();
    std::optional<EdgeHandle> best;
    for (const EdgeHandle& h : selectionEdgeHandles()) {
        const QPointF hp = worldToWidget(h.mid[H], h.mid[V]);
        if (std::fabs(hp.x() - pos.x()) > 6 || std::fabs(hp.y() - pos.y()) > 6) continue;
        if (!best || (D == 2 ? h.mid[D] > best->mid[D] : h.mid[D] < best->mid[D])) best = h;
    }
    return best;
}

std::vector<VertexHandle> GridView::vertexHandlesAt(const QPointF& pos) const {
    // Every handle within 5 px of the point, at any depth (Radiant picks overlapping vertices together).
    const int H = ctx_->axisH(), V = ctx_->axisV();
    std::vector<VertexHandle> out;
    for (const VertexHandle& h : selectionVertexHandles(*ctx_)) {
        const QPointF hp = worldToWidget(h.pos[H], h.pos[V]);
        if (std::fabs(hp.x() - pos.x()) <= 5 && std::fabs(hp.y() - pos.y()) <= 5) out.push_back(h);
    }
    return out;
}

bool GridView::overSelection(double wh, double wv) {
    const int H = ctx_->axisH(), V = ctx_->axisV();
    const AABB& b = selectionCache().bounds;
    const double tol = 4.0 / ctx_->gridScale;
    return b.valid() && wh >= b.mins[H] - tol && wh <= b.maxs[H] + tol && wv >= b.mins[V] - tol && wv <= b.maxs[V] + tol;
}

std::vector<size_t> GridView::facingFaces(const ConvexBrush& b, const Vec3& centre, double wh, double wv) const {
    const int H = ctx_->axisH(), V = ctx_->axisV(), D = ctx_->axisD();
    Vec3 click = centre;
    click[H] = wh;
    click[V] = wv;
    std::vector<size_t> faces;
    for (size_t f = 0; f < b.size(); ++f) {
        const Plane& pl = b[f].plane;
        if (std::fabs(pl.n[D]) > 1e-6) continue;  // faces seen edge-on in this view only
        if (dot(pl.n, click) - pl.d > 0) faces.push_back(f);
    }
    return faces;
}

std::vector<GridView::ResizeTarget> GridView::resizeTargetsAt(double wh, double wv) {
    // Outside the selection: the faces of selected brushes that face the point (Radiant face drag).
    const SelectionCache& c = selectionCache();
    std::vector<ResizeTarget> out;
    for (size_t i = 0; i < c.brushes.size(); ++i) {
        std::vector<size_t> faces = facingFaces(c.brushes[i].original, c.centres[i], wh, wv);
        if (faces.empty()) continue;
        ResizeTarget t = c.brushes[i];
        t.faces = std::move(faces);
        out.push_back(std::move(t));
    }
    return out;
}

void GridView::updateHoverCursor(const QPointF& pos, Qt::KeyboardModifiers mods) {
    // What a left press here would do (the same tests and order as mousePressEvent): move the selection (SizeAll), drag
    // a vertex or edge handle (SizeAll), resize faces (an arrow along the faces' direction); anything else (create,
    // select with Shift, clipper, measure, terrain) keeps the normal cursor.
    Qt::CursorShape shape = Qt::ArrowCursor;
    const bool otherTool = ctx_->measureMode || ctx_->clipMode || (ctx_->terrainBrush && selectedPatchCount(*ctx_) > 0);
    if (!otherTool && !(mods & Qt::ShiftModifier) && !ctx_->doc.selection().empty()) {
        const double wh = widgetToWorldH(pos.x()), wv = widgetToWorldV(pos.y());
        if ((ctx_->edgeMode && edgeHandleAt(pos)) || (ctx_->vertexMode && !vertexHandlesAt(pos).empty()) || overSelection(wh, wv)) {
            shape = Qt::SizeAllCursor;
        } else {
            // The faces move along their normals: their sum, on screen (y down), picks the arrow.
            const int H = ctx_->axisH(), V = ctx_->axisV();
            const SelectionCache& c = selectionCache();
            double dx = 0, dy = 0;
            bool any = false;
            for (size_t i = 0; i < c.brushes.size(); ++i)
                for (size_t f : facingFaces(c.brushes[i].original, c.centres[i], wh, wv)) {
                    dx += c.brushes[i].original[f].plane.n[H];
                    dy -= c.brushes[i].original[f].plane.n[V];
                    any = true;
                }
            const double ax = std::fabs(dx), ay = std::fabs(dy);
            if (!any) shape = Qt::ArrowCursor;
            else if (ax < 1e-9 && ay < 1e-9) shape = Qt::SizeAllCursor;  // opposite faces of several brushes
            else if (ax > 2 * ay) shape = Qt::SizeHorCursor;
            else if (ay > 2 * ax) shape = Qt::SizeVerCursor;
            else shape = dx * dy > 0 ? Qt::SizeFDiagCursor : Qt::SizeBDiagCursor;
        }
    }
    if (cursor().shape() != shape) setCursor(shape);
}

void GridView::startPendingDrag() {
    dragLive_ = true;
    if (drag_ == Drag::Move) ctx_->doc.beginDrag();
}

void GridView::cancelDrag(const QString& status) {
    const Drag was = drag_;
    drag_ = Drag::None;
    dragLive_ = true;
    resize_.clear();
    edgeDrag_.reset();
    vertexDrag_.clear();
    if (was == Drag::None) return;
    if (was == Drag::Pan) {
        update();
        return;
    }
    if (was == Drag::Move && ctx_->doc.dragging()) ctx_->doc.endDrag(false);  // the previewed move goes back
    if (was == Drag::Terrain) {
        ctx_->doc.endGroup();
        // The stroke so far is one undo step: take it back.
        if (terrainChanged_) ctx_->doc.undo();
        terrainChanged_ = false;
    }
    if (was == Drag::Measure) ctx_->measureValid = false;
    ctx_->documentEdited(status);
}

bool GridView::event(QEvent* e) {
    // Escape is the window's Deselect shortcut (an application shortcut, so it would win): during a drag it reaches
    // keyPressEvent instead and cancels the drag.
    if (e->type() == QEvent::ShortcutOverride && editDragActive() && static_cast<QKeyEvent*>(e)->key() == Qt::Key_Escape) {
        e->accept();
        return true;
    }
    return QWidget::event(e);
}

void GridView::focusOutEvent(QFocusEvent* e) {
    // Focus taken mid-drag (a dialog, another window): the release may never come here, so the drag goes back now.
    if (drag_ != Drag::None) {
        const AxisScope axisScope(this);
        cancelDrag(drag_ == Drag::Pan ? QString() : QStringLiteral("Drag cancelled: the 2D view lost focus"));
    }
    QWidget::focusOutEvent(e);
}

void GridView::leaveEvent(QEvent* e) {
    // The cursor left: the position readout and the terrain brush outline no longer apply.
    hover_ = QPointF(-1e6, -1e6);
    emit mouseLeft();
    emit positionText(QString());
    if (ctx_->terrainBrush) update();
    QWidget::leaveEvent(e);
}

void GridView::mousePressEvent(QMouseEvent* e) {
    const AxisScope axisScope(this);
    setFocus(Qt::MouseFocusReason);
    if (drag_ != Drag::None) {
        // A second button during a drag is ignored: the drag ends on its own button's release.
        if (!(e->button() & dragButtons()) && (e->buttons() & dragButtons())) return;
        // The drag's own button came down again (or is up): its release was lost, so that drag goes back first.
        cancelDrag(QString());
    }
    dragLive_ = true;
    const QPointF pos = e->position();
    lastPos_ = pressPos_ = pos;
    const double wh = widgetToWorldH(pos.x()), wv = widgetToWorldV(pos.y());
    dragStartH_ = dragNowH_ = wh;
    dragStartV_ = dragNowV_ = wv;
    if (e->button() == Qt::RightButton || e->button() == Qt::MiddleButton) {
        drag_ = Drag::Pan;
        return;
    }
    if (e->button() != Qt::LeftButton) return;
    const int H = ctx_->axisH(), V = ctx_->axisV(), D = ctx_->axisD();
    if (ctx_->measureMode) {
        Vec3 a;
        a[H] = snapped(wh);
        a[V] = snapped(wv);
        ctx_->measureA = ctx_->measureB = a;
        ctx_->measureValid = true;
        drag_ = Drag::Measure;
        update();
        return;
    }
    if (ctx_->clipMode) {
        // Clipper: two points, then an optional third that tilts the plane (one grid step deeper along the depth axis).
        // Later clicks move the point nearest in the view plane and keep its depth.
        Vec3 p;
        p[H] = snapped(wh);
        p[V] = snapped(wv);
        const AABB sel = ctx_->doc.selectionBounds();
        const double depth0 = sel.valid() ? sel.center()[D] : 0;
        auto& cp = ctx_->clipPoints;
        if (cp.size() < 3) {
            p[D] = cp.size() == 2 ? depth0 + ctx_->doc.grid() : depth0;
            cp.push_back(p);
        } else {
            size_t nearer = 0;
            double best = 1e300;
            for (size_t i = 0; i < 3; ++i) {
                const double dist = std::hypot(cp[i][H] - p[H], cp[i][V] - p[V]);
                if (dist < best) {
                    best = dist;
                    nearer = i;
                }
            }
            p[D] = cp[nearer][D];
            cp[nearer] = p;
        }
        ctx_->documentEdited(cp.size() < 2    ? QStringLiteral("Clipper: place the second point")
                             : cp.size() == 2 ? QStringLiteral("Clipper: click a third point to tilt the plane, or Enter to clip")
                                              : QStringLiteral("Clipper: Enter clips, Shift+Enter splits, Ctrl+Enter flips the kept side"));
        return;
    }
    if (ctx_->terrainBrush && selectedPatchCount(*ctx_) > 0) {
        if (ctx_->gridAxis != 0) {
            ctx_->setStatus("Terrain brush works in the XY top view (Ctrl+Tab)");
            return;
        }
        // One undo step for the whole stroke; it ends on release (Esc takes it back).
        terrainSign_ = (e->modifiers() & Qt::ShiftModifier) ? -1.0 : 1.0;
        terrainFlatZ_ = nearestPatchControlZ(*ctx_, wh, wv);  // Flatten target: the point nearest the stroke start
        ctx_->doc.beginGroup("Terrain Brush");
        terrainChanged_ = false;
        drag_ = Drag::Terrain;
        terrainStamp(wh, wv);
        return;
    }
    if (ctx_->edgeMode && !(e->modifiers() & Qt::ShiftModifier)) {
        if (auto best = edgeHandleAt(pos)) {
            edgeDrag_ = best;
            drag_ = Drag::Edge;
            ctx_->setStatus("Edge mode: drag the midpoint handle; the brush must stay convex");
            return;
        }
    }
    if (ctx_->vertexMode && !(e->modifiers() & Qt::ShiftModifier)) {
        vertexDrag_ = vertexHandlesAt(pos);
        if (!vertexDrag_.empty()) {
            drag_ = Drag::Vertex;
            ctx_->setStatus(QString("Vertex mode: dragging %1 vertex handle(s)").arg(vertexDrag_.size()));
            return;
        }
    }
    if (e->modifiers() & Qt::ShiftModifier) {
        Ray r;
        r.origin[H] = wh;
        r.origin[V] = wv;
        r.origin[D] = (D == 2) ? 1e6 : -1e6;  // top view looks down; front/side look along +axis
        r.dir[D] = (D == 2) ? -1 : 1;
        ctx_->scene.sync(ctx_->doc);
        if (auto hit = ctx_->scene.pick(r)) {
            ctx_->doc.toggle(*hit);
            ctx_->documentEdited();
        }
        return;
    }
    // Create, move and resize wait until the mouse has moved kDragThreshold px (startPendingDrag): a click on the
    // selection must not nudge it, and a click in empty space must not leave a sliver brush.
    if (ctx_->doc.selection().empty()) {
        drag_ = Drag::Create;
        dragLive_ = false;
        return;
    }
    if (overSelection(wh, wv)) {
        drag_ = Drag::Move;  // beginDrag once the drag goes live
        dragLive_ = false;
        return;
    }
    // Outside the selection: resize the faces of selected brushes that face the click (Radiant face drag).
    resize_ = resizeTargetsAt(wh, wv);
    drag_ = resize_.empty() ? Drag::None : Drag::Resize;
    dragLive_ = resize_.empty();
}

void GridView::mouseMoveEvent(QMouseEvent* e) {
    const AxisScope axisScope(this);
    const QPointF pos = e->position();
    const int H = ctx_->axisH(), V = ctx_->axisV();
    const double wh = widgetToWorldH(pos.x()), wv = widgetToWorldV(pos.y());
    Vec3 w;
    w[H] = snapped(wh);
    w[V] = snapped(wv);
    emit mouseWorld(w.x, w.y, w.z);
    emit positionText(QString("%1 %2   %3 %4")
                          .arg(QChar("xyz"[H])).arg(QString::fromStdString(formatNumber(w[H])))
                          .arg(QChar("xyz"[V])).arg(QString::fromStdString(formatNumber(w[V]))));
    hover_ = pos;
    // A drag whose button is no longer down lost its release (another window took the mouse): put it back.
    if (drag_ != Drag::None && !(e->buttons() & dragButtons()))
        cancelDrag(drag_ == Drag::Pan ? QString() : QStringLiteral("Drag cancelled: the mouse button was released outside the view"));
    if (drag_ == Drag::None) {
        updateHoverCursor(pos, e->modifiers());
        return;
    }
    if (!dragLive_) {
        if ((pos - pressPos_).manhattanLength() < kDragThreshold) return;
        startPendingDrag();
    }
    dragNowH_ = wh;
    dragNowV_ = wv;
    // Progress messages below are transient (setTransientStatus): one per mouse move would flood the Console log.
    switch (drag_) {
        case Drag::Measure: {
            ctx_->measureB[H] = snapped(wh);
            ctx_->measureB[V] = snapped(wv);
            const Vec3 d = ctx_->measureB - ctx_->measureA;
            ctx_->setTransientStatus(QString("Measure %1 units (%2 %3, %4 %5)")
                                .arg(QString::fromStdString(formatNumber(length(d))))
                                .arg(QChar("XYZ"[H])).arg(QString::fromStdString(formatNumber(d[H])))
                                .arg(QChar("XYZ"[V])).arg(QString::fromStdString(formatNumber(d[V]))));
            update();
            break;
        }
        case Drag::Pan: {
            const QPointF d = pos - lastPos_;
            ctx_->gridCenterH -= d.x() / ctx_->gridScale;
            ctx_->gridCenterV += d.y() / ctx_->gridScale;
            lastPos_ = pos;
            update();
            break;
        }
        case Drag::Move: {
            Vec3 d;
            d[H] = snapped(wh - dragStartH_);
            d[V] = snapped(wv - dragStartV_);
            ctx_->doc.updateDrag(d);
            ctx_->documentEdited();
            ctx_->setTransientStatus(QString("Move %1 %2").arg(QString::fromStdString(formatNumber(d[H])), QString::fromStdString(formatNumber(d[V]))));
            break;
        }
        case Drag::Create: {
            const AABB b = createBox();
            ctx_->setTransientStatus(QString("New brush %1 x %2 x %3")
                                .arg(QString::fromStdString(formatNumber(b.maxs.x - b.mins.x)),
                                     QString::fromStdString(formatNumber(b.maxs.y - b.mins.y)),
                                     QString::fromStdString(formatNumber(b.maxs.z - b.mins.z))));
            update();
            break;
        }
        case Drag::Resize:
            update();
            break;
        case Drag::Vertex: {
            const Vec3 d = vertexDelta();
            ctx_->setTransientStatus(QString("Move vertices %1 %2").arg(QString::fromStdString(formatNumber(d[H])), QString::fromStdString(formatNumber(d[V]))));
            update();
            break;
        }
        case Drag::Edge: {
            const Vec3 d = vertexDelta();
            if (edgeDrag_ && moveBrushEdge(edgeDrag_->brush, edgeDrag_->edge, d))
                ctx_->setTransientStatus(QString("Move edge %1 %2").arg(QString::fromStdString(formatNumber(d[H])), QString::fromStdString(formatNumber(d[V]))));
            else
                ctx_->setTransientStatus("That edge move would make the brush non-convex or flip a face");
            update();
            break;
        }
        case Drag::Terrain:
            if (std::hypot(wh - stampH_, wv - stampV_) >= ctx_->doc.grid()) terrainStamp(wh, wv);  // one step per grid unit of travel
            update();
            break;
        case Drag::None:
            break;
    }
}

void GridView::mouseReleaseEvent(QMouseEvent* e) {
    const AxisScope axisScope(this);
    // Only the drag's own button ends it (a second button pressed during the drag was ignored).
    if (drag_ != Drag::None && !(e->button() & dragButtons())) return;
    // A press that never moved past the threshold is a click: nothing moves, no brush, no resize. (A release far from
    // the press without a move event in between still counts as a drag.)
    if (!dragLive_ && (e->position() - pressPos_).manhattanLength() >= kDragThreshold) startPendingDrag();
    const Drag was = dragLive_ ? drag_ : Drag::None;
    drag_ = Drag::None;
    dragLive_ = true;
    if (was == Drag::None) resize_.clear();
    if (was != Drag::Pan && was != Drag::None) {
        // The drag ends where the button is released, even without a final move event.
        dragNowH_ = widgetToWorldH(e->position().x());
        dragNowV_ = widgetToWorldV(e->position().y());
        if (was == Drag::Move) {
            Vec3 d;
            d[ctx_->axisH()] = snapped(dragNowH_ - dragStartH_);
            d[ctx_->axisV()] = snapped(dragNowV_ - dragStartV_);
            ctx_->doc.updateDrag(d);
        }
    }
    if (was == Drag::Pan && e->button() == Qt::RightButton && (e->position() - pressPos_).manhattanLength() < 4) {
        showContextMenu(e->globalPosition().toPoint(), widgetToWorldH(e->position().x()), widgetToWorldV(e->position().y()));
        return;
    }
    if (e->button() != Qt::LeftButton) return;
    if (was == Drag::Move) {
        const bool committed = ctx_->doc.endDrag(true);
        ctx_->documentEdited(committed ? QStringLiteral("Moved (undoable)") : QString());
    } else if (was == Drag::Create) {
        const AABB b = createBox();
        const int H = ctx_->axisH(), V = ctx_->axisV();
        // At least one grid unit across on both dragged axes; with snap off (sizes are free) at least one unit.
        const double least = ctx_->snap ? ctx_->doc.grid() : 1.0;
        if (b.maxs[H] - b.mins[H] < least - 1e-6 || b.maxs[V] - b.mins[V] < least - 1e-6) {
            ctx_->setStatus(QString("No brush created: drag at least %1 unit%2 on both axes")
                                .arg(QString::fromStdString(formatNumber(least)), least == 1.0 ? "" : "s"));
            update();
        } else if (createBoxBrush(*ctx_, b)) {
            ctx_->documentEdited("Created brush with " + QString::fromStdString(ctx_->currentMaterial));
        } else {
            update();
        }
    } else if (was == Drag::Vertex) {
        size_t rejected = 0, lockedSkipped = 0;
        const bool moved = moveVertexHandles(*ctx_, vertexDrag_, vertexDelta(), &rejected, &lockedSkipped);
        vertexDrag_.clear();
        if (moved) ctx_->documentEdited(rejected ? QString("Moved vertices; %1 brush(es) would not stay convex and were left unchanged").arg(rejected)
                                                 : QStringLiteral("Moved vertices (undoable)"));
        else {
            if (rejected) ctx_->setStatus("That vertex move would make the brush non-convex; nothing changed");
            else if (lockedSkipped) ctx_->setStatus("Those patch vertices are locked (Patch > Unlock Vertices)");
            update();
        }
    } else if (was == Drag::Resize) {
        const auto preview = resizePreview();
        std::vector<Document::PrimEdit> edits;
        for (size_t i = 0; i < resize_.size(); ++i) {
            if (brushVolume(preview[i]) == brushVolume(resize_[i].original)) continue;
            if (auto prim = rebuildBrush(*resize_[i].source, preview[i], ctx_->doc.eol())) edits.push_back({resize_[i].entity, resize_[i].item, *prim});
        }
        resize_.clear();
        if (ctx_->doc.editPrimitives(edits, "Resize")) ctx_->documentEdited("Resized (undoable)");
        else update();
    } else if (was == Drag::Edge && edgeDrag_) {
        const EdgeHandle h = *edgeDrag_;
        edgeDrag_.reset();
        const Vec3 d = vertexDelta();
        std::optional<ConvexBrush> moved;
        if (length(d) >= 1e-9) moved = moveBrushEdge(h.brush, h.edge, d);
        if (!moved) {
            ctx_->setStatus("That edge move would make the brush non-convex or flip a face; nothing changed");
            update();
        } else {
            const EntityItem* it = ctx_->doc.findItem(h.entity, h.item);
            const Primitive* src = it ? it->prim() : nullptr;
            std::optional<Primitive> prim;
            if (src) prim = rebuildBrush(*src, *moved, ctx_->doc.eol());
            if (prim && ctx_->doc.editPrimitives({{h.entity, h.item, *prim}}, "Move Edge")) ctx_->documentEdited("Moved edge (undoable)");
            else update();
        }
    } else if (was == Drag::Terrain) {
        ctx_->doc.endGroup();
        terrainChanged_ = false;
        ctx_->documentEdited("Terrain brush stroke (undoable)");
    }
}

Vec3 GridView::vertexDelta() const {
    Vec3 d;
    d[ctx_->axisH()] = snapped(dragNowH_ - dragStartH_);
    d[ctx_->axisV()] = snapped(dragNowV_ - dragStartV_);
    return d;
}

void GridView::showContextMenu(const QPoint& globalPos, double wh, double wv) {
    const AxisScope axisScope(this);
    const int H = ctx_->axisH(), V = ctx_->axisV(), D = ctx_->axisD();
    Vec3 at;
    at[H] = snapped(wh);
    at[V] = snapped(wv);
    const AABB sel = ctx_->doc.selectionBounds();
    at[D] = sel.valid() ? sel.mins[D] : 0;
    QMenu menu(this);
    QMenu* create = menu.addMenu("Create entity here");
    auto addPoint = [&](QMenu* m, const QString& cls) {
        m->addAction(cls, this, [this, cls, at] {
            std::vector<std::pair<std::string, std::string>> kv = {{"classname", cls.toStdString()},
                                                                    {"origin", formatNumber(at.x) + " " + formatNumber(at.y) + " " + formatNumber(at.z)}};
            if (cls.contains("spawn") || cls == "info_player_start") kv.push_back({"angles", "0 0 0"});
            ctx_->doc.addEntity(kv, {}, "Create " + cls.toStdString());
            ctx_->documentEdited("Created " + cls);
        });
    };
    for (const char* common : {"mp_tdm_spawn", "mp_tdm_spawn_allies_start", "mp_tdm_spawn_axis_start", "mp_dm_spawn", "info_player_start",
                               "mp_global_intermission", "script_model", "script_origin", "script_struct", "reflection_probe"})
        addPoint(create, common);
    QMenu* all = create->addMenu("All stock IW8 classes");
    for (const QString& c : stockClassNames()) addPoint(all, c);
    const auto brushes = selectedBrushes(*ctx_);
    if (!brushes.empty()) {
        QMenu* make = menu.addMenu("Make brush entity from selection");
        for (const QString& cls : brushEntityClasses())
            make->addAction(cls, this, [this, cls] {
                if (ctx_->doc.groupSelection(cls.toStdString()) != kNoId) ctx_->documentEdited("Made " + cls);
            });
        menu.addAction("Ungroup to world", this, [this] { if (ctx_->doc.ungroupSelection()) ctx_->documentEdited("Ungrouped"); });
        menu.addSeparator();
        menu.addAction("CSG Hollow", this, [this] {
            if (hollowSelection(*ctx_)) ctx_->documentEdited("Hollowed");
            else ctx_->setStatus("CSG Hollow: select one or more brushes first");
        });
        menu.addAction("CSG Subtract", this, [this] {
            if (subtractSelection(*ctx_)) ctx_->documentEdited("Subtracted");
            else ctx_->setStatus("CSG Subtract: select the brushes to cut with, touching the brushes to cut");
        });
        menu.addAction("Apply material " + QString::fromStdString(ctx_->currentMaterial), this, [this] {
            if (applyMaterialToSelection(*ctx_, ctx_->currentMaterial)) ctx_->documentEdited("Material applied");
        });
    }
    menu.exec(globalPos);
}
void GridView::dragEnterEvent(QDragEnterEvent* e) {
    if (e->mimeData()->hasFormat(kAssetMimeType)) e->acceptProposedAction();
}
void GridView::dragMoveEvent(QDragMoveEvent* e) {
    const AxisScope axisScope(this);
    if (e->mimeData()->hasFormat(kAssetMimeType)) e->acceptProposedAction();
}
void GridView::dropEvent(QDropEvent* e) {
    const AxisScope axisScope(this);
    const QStringList f = QString::fromUtf8(e->mimeData()->data(kAssetMimeType)).split('\t');
    if (f.size() < 2) return;
    e->acceptProposedAction();
    emit assetDropped(f[0], f[1], f.value(2) == "1", widgetToWorldH(e->position().x()), widgetToWorldV(e->position().y()));
}

void GridView::wheelEvent(QWheelEvent* e) {
    const AxisScope axisScope(this);
    // Zoom towards the cursor (iw3xo grid_zoom_to_cursor default).
    const QPointF pos = e->position();
    const double wh = widgetToWorldH(pos.x()), wv = widgetToWorldV(pos.y());
    const double before = ctx_->gridScale;
    ctx_->gridScale = EditorContext::clampGridScale(ctx_->gridScale * std::pow(kWheelStep, e->angleDelta().y() / 120.0));
    ctx_->gridCenterH = wh - (wh - ctx_->gridCenterH) * before / ctx_->gridScale;
    ctx_->gridCenterV = wv - (wv - ctx_->gridCenterV) * before / ctx_->gridScale;
    update();
}

void GridView::keyPressEvent(QKeyEvent* e) {
    const AxisScope axisScope(this);
    if (!e->isAutoRepeat() && isCameraKey(e->key()) && !(e->modifiers() & (Qt::ControlModifier | Qt::AltModifier))) {
        ctx_->heldKeys.insert(e->key());
        return;
    }
    if (e->key() == Qt::Key_E && !e->isAutoRepeat() && !(e->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::ShiftModifier))) {
        // Edge mode. Handled here rather than as a window shortcut, so E still flies the camera up while looking.
        if (QAction* a = window()->findChild<QAction*>("ToggleEdgeMode")) a->trigger();
        return;
    }
    if (e->key() == Qt::Key_Escape && editDragActive()) {
        // Reached through the ShortcutOverride in event(): the drag goes back (a terrain stroke is undone).
        cancelDrag(QStringLiteral("Drag cancelled"));
        return;
    }
    QWidget::keyPressEvent(e);
}

void GridView::keyReleaseEvent(QKeyEvent* e) {
    const AxisScope axisScope(this);
    if (!e->isAutoRepeat()) ctx_->heldKeys.remove(e->key());
    QWidget::keyReleaseEvent(e);
}

}  // namespace iw8r::qt
