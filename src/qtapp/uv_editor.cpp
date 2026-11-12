#include "uv_editor.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <map>
#include <set>

#include "brush_ops.h"
#include "patch_ops.h"
#include "editor_context.h"
#include "uv_unwrap.h"

namespace iw8r::qt {

namespace {
QString qs(const std::string& s) { return QString::fromStdString(s); }
constexpr int kHandle = 4;  // handle half size, pixels
}  // namespace

UvEditor::UvEditor(EditorContext* ctx, ImageFor imageFor, QWidget* parent)
    : QDialog(parent), ctx_(ctx), imageFor_(std::move(imageFor)) {
    setObjectName("uvEditor");
    setWindowTitle("UV Editor");
    resize(820, 720);
    setMouseTracking(true);
    auto* lay = new QVBoxLayout(this);
    lay->addStretch(1);
    info_ = new QLabel(this);
    info_->setWordWrap(true);
    lay->addWidget(info_);
    auto* row = new QHBoxLayout;
    auto* reload = new QPushButton("Take Selection", this);
    auto* viewBtn = new QPushButton("Texture Space View", this);
    auto* unwrapBtn = new QPushButton("Make Texture Continuous (unwrap)", this);
    unwrapBtn->setToolTip("Rewrites the faces' mapping so the texture runs across shared edges as a net. Changes how the "
                          "brush looks (Apply to keep, Reset to drop).");
    auto* fit = new QPushButton("Fit View", this);
    auto* reset = new QPushButton("Reset", this);
    auto* applyBtn = new QPushButton("Apply", this);
    applyBtn->setDefault(true);
    row->addWidget(reload);
    row->addWidget(viewBtn);
    row->addWidget(unwrapBtn);
    row->addWidget(fit);
    row->addStretch(1);
    row->addWidget(reset);
    row->addWidget(applyBtn);
    lay->addLayout(row);
    connect(reload, &QPushButton::clicked, this, [this] { loadSelection(); });
    connect(unwrapBtn, &QPushButton::clicked, this, [this] { unwrap(); });
    connect(viewBtn, &QPushButton::clicked, this, [this, viewBtn] {
        setLayoutView(!layout_);
        viewBtn->setText(layout_ ? "Texture Space View" : "Mesh Layout View");
    });
    connect(fit, &QPushButton::clicked, this, [this] {
        fitView();
        update();
    });
    connect(reset, &QPushButton::clicked, this, [this] {
        for (Face& f : faces_) f.base = f.tp;
        for (Patch& p : patches_) p.base = p.st0;
        setPending(0, 0, 1, 0);
    });
    connect(applyBtn, &QPushButton::clicked, this, [this] {
        const int n = faceCount();
        if (apply()) ctx_->documentEdited("UV edit applied to " + QString::number(n) + " face(s) (one undo step)");
    });
    loadSelection();
}

int UvEditor::loadSelection() {
    faces_.clear();
    const auto addFace = [&](Id entity, Id item, size_t face) {
        const EntityItem* it = ctx_->doc.findItem(entity, item);
        const Primitive* p = it ? it->prim() : nullptr;
        if (!p || p->kind != PrimKind::Brush || face >= p->faces.size()) return;
        const auto pl = facePlane(*p, face);
        const auto wins = faceWindingsOf(*p);
        if (!pl || face >= wins.size() || wins[face].size() < 3) return;
        const TexParams tp = texParamsOfFace(*p, face).value_or(TexParams{});
        faces_.push_back({entity, item, face, pl->n, wins[face], tp, tp, qs(p->faces[face].material())});
    };
    patches_.clear();
    for (const SelRef& r : ctx_->doc.selection()) {
        const EntityItem* it = r.item != kNoId ? ctx_->doc.findItem(r.entity, r.item) : nullptr;
        const Primitive* p = it ? it->prim() : nullptr;
        if (!p || p->kind != PrimKind::Patch) continue;
        if (const auto g = patchGrid(*p))
            patches_.push_back({r.entity, r.item, g->rows, g->cols, g->pos, g->st, g->st, qs(p->patchMaterial)});
    }
    for (const FaceRef& f : ctx_->doc.faceSelection()) addFace(f.entity, f.item, f.face);
    if (faces_.empty())
        for (const SelRef& r : ctx_->doc.selection()) {
            const EntityItem* it = r.item != kNoId ? ctx_->doc.findItem(r.entity, r.item) : nullptr;
            if (const Primitive* p = it ? it->prim() : nullptr)
                for (size_t i = 0; i < p->faces.size(); ++i) addFace(r.entity, r.item, i);
        }
    pivot_ = Vec3{};
    size_t n = 0;
    for (const Face& f : faces_)
        for (const Vec3& v : f.winding) pivot_ += v, ++n;
    if (n) pivot_ = pivot_ * (1.0 / double(n));
    du_ = dv_ = rot_ = 0;
    scale_ = 1;
    const QString mat = !faces_.empty() ? faces_.front().material : !patches_.empty() ? patches_.front().material : QString();
    texture_ = (!mat.isEmpty() && imageFor_) ? imageFor_(mat) : QImage();
    computeLayout();
    fitView();
    refreshInfo();
    update();
    return int(faces_.size());
}

void UvEditor::computeLayout() {
    // Each brush unfolded from its first loaded face; brushes placed left to right with a gap.
    std::vector<std::pair<Id, Id>> order;
    for (const Face& f : faces_)
        if (std::find(order.begin(), order.end(), std::make_pair(f.entity, f.item)) == order.end()) order.push_back({f.entity, f.item});
    double cursor = 0;
    for (const auto& key : order) {
        const EntityItem* it = ctx_->doc.findItem(key.first, key.second);
        const Primitive* p = it ? it->prim() : nullptr;
        size_t root = 0;
        for (const Face& f : faces_)
            if (f.entity == key.first && f.item == key.second) {
                root = f.face;
                break;
            }
        const auto net = p ? unfoldBrush(*p, root) : std::map<size_t, std::vector<Uv>>{};
        double lo = 1e30, hi = -1e30;
        for (const auto& [fi, pts] : net)
            for (const auto& q : pts) lo = std::min(lo, q[0]), hi = std::max(hi, q[0]);
        if (lo > hi) lo = hi = 0;
        const double shift = cursor - lo;
        for (Face& f : faces_) {
            if (f.entity != key.first || f.item != key.second) continue;
            f.net.clear();
            if (auto n = net.find(f.face); n != net.end() && n->second.size() == f.winding.size())
                for (const auto& q : n->second) f.net.push_back({q[0] + shift, q[1]});
        }
        cursor += (hi - lo) + 32;
    }
}

void UvEditor::setLayoutView(bool on) {
    layout_ = on;
    fitView();
    refreshInfo();
    update();
}

QPointF UvEditor::netToScreen(double x, double y) const {
    const QPointF c(width() / 2.0, (height() - 110) / 2.0);
    return c + QPointF((x - netCentre_.x()) * netScale_, -(y - netCentre_.y()) * netScale_);
}

bool UvEditor::faceAffine(int face, double m[6]) const {
    const Face& f = faces_[size_t(face)];
    if (f.net.size() != f.winding.size() || f.winding.size() < 3) return false;
    const auto uv = faceUvs(face);
    for (size_t a = 0; a + 2 < uv.size() + 2 && a < uv.size(); ++a) {
        const size_t b = (a + 1) % uv.size(), c = (a + 2) % uv.size();
        const double du1 = uv[b][0] - uv[a][0], dv1 = uv[b][1] - uv[a][1], du2 = uv[c][0] - uv[a][0], dv2 = uv[c][1] - uv[a][1];
        const double det = du1 * dv2 - du2 * dv1;
        if (std::fabs(det) < 1e-12) continue;
        for (int k = 0; k < 2; ++k) {
            const double p1 = f.net[b][size_t(k)] - f.net[a][size_t(k)], p2 = f.net[c][size_t(k)] - f.net[a][size_t(k)];
            const double A = (p1 * dv2 - p2 * dv1) / det, B = (p2 * du1 - p1 * du2) / det;
            m[k * 3 + 0] = A, m[k * 3 + 1] = B, m[k * 3 + 2] = f.net[a][size_t(k)] - A * uv[a][0] - B * uv[a][1];
        }
        return true;
    }
    return false;
}

bool UvEditor::dragLayoutVertex(int face, int vertex, double dx, double dy) {
    double m[6];
    if (face < 0 || face >= faceCount() || !faceAffine(face, m)) return false;
    const double det = m[0] * m[4] - m[1] * m[3];
    if (std::fabs(det) < 1e-12) return false;
    // Inverse of the linear part: net delta -> UV delta.
    const double du = (m[4] * dx - m[1] * dy) / det, dv = (-m[3] * dx + m[0] * dy) / det;
    const auto uv = faceUvs(face);
    if (vertex < 0 || vertex >= int(uv.size())) return false;
    return moveVertex(face, vertex, uv[size_t(vertex)][0] + du, uv[size_t(vertex)][1] + dv);
}

void UvEditor::fitView() {
    if (layout_) {
        double lo[2] = {1e30, 1e30}, hi[2] = {-1e30, -1e30};
        for (const Face& f : faces_)
            for (const auto& q : f.net)
                for (int k = 0; k < 2; ++k) lo[k] = std::min(lo[k], q[size_t(k)]), hi[k] = std::max(hi[k], q[size_t(k)]);
        if (lo[0] <= hi[0]) {
            netCentre_ = QPointF((lo[0] + hi[0]) / 2, (lo[1] + hi[1]) / 2);
            const double sx = (hi[0] - lo[0]) / std::max(1, width() - 40), sy = (hi[1] - lo[1]) / std::max(1, height() - 150);
            netScale_ = 1.0 / std::max({sx, sy, 1e-6});
            netScale_ = std::clamp(netScale_ * 0.9, 0.01, 200.0);
        }
    }
    if (faces_.empty() && patches_.empty()) return;
    double lo[2] = {1e30, 1e30}, hi[2] = {-1e30, -1e30};
    for (int i = 0; i < faceCount(); ++i)
        for (const auto& uv : faceUvs(i))
            for (int k = 0; k < 2; ++k) lo[k] = std::min(lo[k], uv[size_t(k)]), hi[k] = std::max(hi[k], uv[size_t(k)]);
    for (int i = 0; i < patchCount(); ++i)
        for (const auto& uv : patchUvs(i))
            for (int k = 0; k < 2; ++k) lo[k] = std::min(lo[k], uv[size_t(k)]), hi[k] = std::max(hi[k], uv[size_t(k)]);
    centre_ = QPointF((lo[0] + hi[0]) / 2, (lo[1] + hi[1]) / 2);
    const double span = std::max({hi[0] - lo[0], hi[1] - lo[1], 0.25});
    view_ = std::clamp(0.8 * std::min(width(), height() - 90) / span, 4.0, 4000.0);
}

TexParams UvEditor::edited(const Face& f) const {
    TexParams tp = f.base;
    if (du_ == 0 && dv_ == 0 && scale_ == 1 && rot_ == 0) return tp;
    // UV of the pivot before the change: scaling and rotating keep it, then the drag offset moves it.
    const auto before = faceUV(f.normal, f.base, pivot_);
    tp.width = f.base.width * scale_;
    tp.height = f.base.height * scale_;
    tp.rot = std::fmod(f.base.rot + rot_, 360.0);
    TexParams zero = tp;
    zero.xoff = zero.yoff = 0;
    const auto raw = faceUV(f.normal, zero, pivot_);
    // Moving the texture right moves every face point left in texture space.
    tp.xoff = (before[0] - du_ - raw[0]) * tp.width;
    tp.yoff = (before[1] - dv_ - raw[1]) * tp.height;
    return tp;
}

void UvEditor::bakeGlobal() {
    for (Face& f : faces_) f.base = edited(f);
    for (int i = 0; i < patchCount(); ++i) patches_[size_t(i)].base = patchUvs(i);
    du_ = dv_ = rot_ = 0;
    scale_ = 1;
}

std::vector<std::array<double, 2>> UvEditor::patchUvs(int patch) const {
    std::vector<std::array<double, 2>> out;
    if (patch < 0 || patch >= patchCount()) return out;
    const Patch& p = patches_[size_t(patch)];
    if (du_ == 0 && dv_ == 0 && scale_ == 1 && rot_ == 0) return p.base;
    // The global move / scale / rotate, about the patch's UV centre (scaling the texture up shrinks the UVs).
    double cu = 0, cv = 0;
    for (const auto& s : p.base) cu += s[0] / double(p.base.size()), cv += s[1] / double(p.base.size());
    const double r = -rot_ * 3.14159265358979 / 180.0, c = std::cos(r), sn = std::sin(r);
    for (const auto& s : p.base) {
        const double x = (s[0] - cu) / scale_, y = (s[1] - cv) / scale_;
        out.push_back({cu + x * c - y * sn - du_, cv + x * sn + y * c - dv_});
    }
    return out;
}

bool UvEditor::movePatchVertex(int patch, int ctrl, double u, double v) {
    if (patch < 0 || patch >= patchCount() || ctrl < 0 || ctrl >= int(patches_[size_t(patch)].base.size())) return false;
    bakeGlobal();
    // Control points at the same spot (a closed or welded patch) move together.
    Patch& p = patches_[size_t(patch)];
    const auto at = p.base[size_t(ctrl)];
    const Vec3 world = p.pos[size_t(ctrl)];
    for (size_t k = 0; k < p.base.size(); ++k)
        if (length(p.pos[k] - world) < 0.01 && std::hypot(p.base[k][0] - at[0], p.base[k][1] - at[1]) < 1e-6) p.base[k] = {u, v};
    refreshInfo();
    update();
    return true;
}

std::vector<std::array<double, 2>> UvEditor::faceUvs(int face) const {
    std::vector<std::array<double, 2>> out;
    if (face < 0 || face >= faceCount()) return out;
    const Face& f = faces_[size_t(face)];
    const TexParams tp = edited(f);
    for (const Vec3& v : f.winding) out.push_back(faceUV(f.normal, tp, v));
    return out;
}

void UvEditor::setPending(double du, double dv, double scale, double rotDeg) {
    du_ = du, dv_ = dv, scale_ = std::clamp(scale, 0.01, 100.0), rot_ = rotDeg;
    refreshInfo();
    update();
}

int UvEditor::unwrap() {
    bakeGlobal();
    // Per brush: the root is its first loaded face (the face the mapper picked, or face 0 of a whole brush).
    std::map<std::pair<Id, Id>, size_t> rootOf;
    for (const Face& f : faces_) rootOf.emplace(std::make_pair(f.entity, f.item), f.face);
    int laid = 0;
    for (const auto& [key, root] : rootOf) {
        const EntityItem* it = ctx_->doc.findItem(key.first, key.second);
        const Primitive* p = it ? it->prim() : nullptr;
        if (!p) continue;
        // Unwrap from the working params of the root (its current look is kept).
        Primitive work = *p;
        for (const Face& f : faces_)
            if (f.entity == key.first && f.item == key.second) applyTexParams(work, f.face, f.base);
        const auto tps = unwrapBrush(work, root);
        for (Face& f : faces_)
            if (f.entity == key.first && f.item == key.second)
                if (auto t = tps.find(f.face); t != tps.end()) {
                    f.base = t->second;
                    ++laid;
                }
    }
    fitView();
    refreshInfo();
    update();
    return laid;
}

std::vector<std::pair<int, int>> UvEditor::coincident(int face, int vertex) const {
    std::vector<std::pair<int, int>> out;
    if (face < 0 || face >= faceCount() || vertex < 0 || vertex >= int(faces_[size_t(face)].winding.size())) return out;
    const Vec3 world = faces_[size_t(face)].winding[size_t(vertex)];
    const auto start = faceUvs(face)[size_t(vertex)];
    for (int i = 0; i < faceCount(); ++i) {
        const auto uvs = faceUvs(i);
        for (size_t k = 0; k < uvs.size(); ++k)
            if (length(faces_[size_t(i)].winding[k] - world) < 0.01 && std::hypot(uvs[k][0] - start[0], uvs[k][1] - start[1]) < 1e-4)
                out.push_back({i, int(k)});
    }
    return out;
}

void UvEditor::moveGroup(const std::vector<std::pair<int, int>>& group, double u, double v) {
    bakeGlobal();
    std::map<int, std::vector<std::array<double, 2>>> targets;
    for (const auto& [fi, vi] : group) {
        if (!targets.count(fi)) targets[fi] = faceUvs(fi);
        targets[fi][size_t(vi)] = {u, v};
    }
    for (auto& [fi, t] : targets) {
        Face& f = faces_[size_t(fi)];
        f.base = fitTexParams(f.normal, f.winding, t, f.base);
    }
    refreshInfo();
    update();
}

bool UvEditor::moveVertex(int face, int vertex, double u, double v) {
    const auto group = coincident(face, vertex);
    if (group.empty()) return false;
    moveGroup(group, u, v);
    return true;
}

bool UvEditor::apply() {
    // One primitive edit per brush with every changed face of it.
    std::map<std::pair<Id, Id>, Primitive> prims;
    for (const Face& f : faces_) {
        const TexParams tp = edited(f);
        if (tp.width == f.tp.width && tp.height == f.tp.height && tp.xoff == f.tp.xoff && tp.yoff == f.tp.yoff && tp.rot == f.tp.rot)
            continue;
        auto key = std::make_pair(f.entity, f.item);
        if (!prims.count(key)) {
            const EntityItem* it = ctx_->doc.findItem(f.entity, f.item);
            if (!it || !it->prim()) continue;
            prims[key] = *it->prim();
        }
        applyTexParams(prims[key], f.face, tp);
    }
    for (int i = 0; i < patchCount(); ++i) {
        const Patch& pt = patches_[size_t(i)];
        const auto now = patchUvs(i);
        if (now == pt.st0) continue;
        const EntityItem* it = ctx_->doc.findItem(pt.entity, pt.item);
        if (!it || !it->prim()) continue;
        Primitive p = *it->prim();
        if (setPatchSt(p, now)) prims[std::make_pair(pt.entity, pt.item)] = std::move(p);
    }
    std::vector<Document::PrimEdit> edits;
    for (auto& [key, p] : prims) edits.push_back({key.first, key.second, p});
    if (edits.empty() || !ctx_->doc.editPrimitives(edits, "UV Edit", false)) return false;
    loadSelection();  // the applied state is the new start
    return true;
}

QString UvEditor::densityText() const {
    if (faces_.empty() && patches_.empty()) return "Select brush faces (Ctrl+Shift+click), brushes or patches, then Take Selection.";
    if (faces_.empty())
        return QString("%1 patch(es): every control point has its own UV; drag its handle. Drag empty space: move all, "
                       "wheel: scale, Ctrl+drag: rotate, Shift+wheel: zoom, middle-drag: pan.").arg(patches_.size());
    const TexParams tp = edited(faces_.front());
    const int px = texture_.isNull() ? 0 : texture_.width();
    QString s = QString("%1 face(s), material %2. One texture repeat covers %3 x %4 units")
                    .arg(faces_.size()).arg(faces_.front().material).arg(std::fabs(tp.width), 0, 'f', 1).arg(std::fabs(tp.height), 0, 'f', 1);
    if (px > 0) s += QString(" (%1 px wide: %2 px per unit)").arg(px).arg(px / std::max(1e-6, std::fabs(tp.width)), 0, 'f', 2);
    return s + ". Drag a vertex handle to move its UV (shared corners move together; a brush face keeps one projection, so "
               "the face re-fits to it). Drag empty space: move all, wheel: scale, Ctrl+drag: rotate, Shift+wheel: zoom, "
               "middle-drag: pan. Unwrap lays brushes out as nets.";
}

void UvEditor::refreshInfo() {
    if (info_) info_->setText(densityText());
}

QPointF UvEditor::toScreen(double u, double v) const {
    const QPointF c(width() / 2.0, (height() - 110) / 2.0);
    return c + QPointF((u - centre_.x()) * view_, (v - centre_.y()) * view_);
}

std::array<double, 2> UvEditor::toUv(const QPointF& p) const {
    const QPointF c(width() / 2.0, (height() - 110) / 2.0);
    return {centre_.x() + (p.x() - c.x()) / view_, centre_.y() + (p.y() - c.y()) / view_};
}

void UvEditor::paintEvent(QPaintEvent*) {
    QPainter g(this);
    g.fillRect(rect(), QColor(32, 34, 38));
    const QRect canvas(0, 0, width(), height() - 110);
    g.setClipRect(canvas);
    if (layout_ && !faces_.empty()) {
        // Mesh layout: each face at its unfolded place, its own texture mapped onto it (UV -> net affine).
        g.setRenderHint(QPainter::Antialiasing);
        g.setRenderHint(QPainter::SmoothPixmapTransform);
        for (int fi = 0; fi < faceCount(); ++fi) {
            const Face& f = faces_[size_t(fi)];
            if (f.net.size() != f.winding.size()) continue;
            QPolygonF poly;
            for (const auto& q : f.net) poly << netToScreen(q[0], q[1]);
            double m[6];
            const auto uvs = faceUvs(fi);
            if (faceAffine(fi, m)) {
                double ulo = 1e30, uhi = -1e30, vlo = 1e30, vhi = -1e30;
                for (const auto& t : uvs) ulo = std::min(ulo, t[0]), uhi = std::max(uhi, t[0]), vlo = std::min(vlo, t[1]), vhi = std::max(vhi, t[1]);
                g.save();
                QPainterPath clip;
                clip.addPolygon(poly);
                g.setClipPath(clip, Qt::IntersectClip);
                // net -> screen: x' = c.x + (x - nc.x) s, y' = c.y - (y - nc.y) s
                const QPointF c(width() / 2.0, (height() - 110) / 2.0);
                const double s = netScale_;
                const QTransform uvToNet(m[0], m[3], m[1], m[4], m[2], m[5]);
                const QTransform netToScr(s, 0, 0, -s, c.x() - netCentre_.x() * s, c.y() + netCentre_.y() * s);
                g.setTransform(uvToNet * netToScr);
                const int i0 = int(std::floor(ulo)), i1 = int(std::ceil(uhi)), j0 = int(std::floor(vlo)), j1 = int(std::ceil(vhi));
                if ((i1 - i0) * (j1 - j0) < 4000)
                    for (int i = i0; i < i1; ++i)
                        for (int j = j0; j < j1; ++j) {
                            const QRectF r(i, j, 1, 1);
                            if (!texture_.isNull()) g.drawImage(r, texture_);
                            else g.fillRect(r, ((i + j) & 1) ? QColor(70, 70, 76) : QColor(90, 90, 98));
                        }
                g.restore();
            }
            g.setPen(QPen(QColor(255, 220, 60), 1.4));
            g.setBrush(Qt::NoBrush);
            g.drawPolygon(poly);
        }
        g.setPen(QPen(QColor(20, 20, 20), 1));
        for (int fi = 0; fi < faceCount(); ++fi)
            for (size_t k = 0; k < faces_[size_t(fi)].net.size(); ++k) {
                const QPointF p = netToScreen(faces_[size_t(fi)].net[k][0], faces_[size_t(fi)].net[k][1]);
                const bool grabbed = std::find(grabbed_.begin(), grabbed_.end(), std::make_pair(fi, int(k))) != grabbed_.end();
                g.setBrush(grabbed ? QColor(255, 90, 60) : QColor(255, 255, 255));
                g.drawRect(QRectF(p.x() - kHandle, p.y() - kHandle, 2 * kHandle, 2 * kHandle));
            }
        return;
    }
    // Tiled texture (or a checker) in texture space: tile (i, j) covers u in [i, i+1), v in [j, j+1).
    const QPointF tl = toScreen(0, 0);
    const int i0 = int(std::floor(centre_.x() - canvas.width() / (2 * view_))) - 1, i1 = i0 + int(canvas.width() / view_) + 3;
    const int j0 = int(std::floor(centre_.y() - canvas.height() / (2 * view_))) - 1, j1 = j0 + int(canvas.height() / view_) + 3;
    if ((i1 - i0) * (j1 - j0) < 40000)
        for (int i = i0; i <= i1; ++i)
            for (int j = j0; j <= j1; ++j) {
                const QRectF r(tl.x() + i * view_, tl.y() + j * view_, view_, view_);
                if (!texture_.isNull()) g.drawImage(r, texture_);
                else g.fillRect(r, ((i + j) & 1) ? QColor(70, 70, 76) : QColor(90, 90, 98));
            }
    g.fillRect(canvas, QColor(0, 0, 0, 70));  // dim the texture so the layout reads on top
    g.setPen(QPen(QColor(255, 255, 255, 50), 1));
    if (i1 - i0 < 400)
        for (int i = i0; i <= i1; ++i) g.drawLine(QPointF(tl.x() + i * view_, 0), QPointF(tl.x() + i * view_, canvas.height()));
    if (j1 - j0 < 400)
        for (int j = j0; j <= j1; ++j) g.drawLine(QPointF(0, tl.y() + j * view_), QPointF(canvas.width(), tl.y() + j * view_));
    g.setRenderHint(QPainter::Antialiasing);
    // Faces: the map's outline faint grey; the edited face filled lightly and outlined yellow, with vertex handles.
    for (int fi = 0; fi < faceCount(); ++fi) {
        const Face& f = faces_[size_t(fi)];
        QPainterPath orig, now;
        const auto uvs = faceUvs(fi);
        for (size_t k = 0; k < f.winding.size(); ++k) {
            const auto o = faceUV(f.normal, f.tp, f.winding[k]);
            const QPointF po = toScreen(o[0], o[1]), pn = toScreen(uvs[k][0], uvs[k][1]);
            if (k == 0) orig.moveTo(po), now.moveTo(pn);
            else orig.lineTo(po), now.lineTo(pn);
        }
        orig.closeSubpath();
        now.closeSubpath();
        g.setPen(QPen(QColor(200, 200, 200, 90), 1, Qt::DashLine));
        g.setBrush(Qt::NoBrush);
        g.drawPath(orig);
        g.setPen(QPen(QColor(255, 220, 60), 1.6));
        g.setBrush(QColor(255, 220, 60, 28));
        g.drawPath(now);
    }
    for (int pi = 0; pi < patchCount(); ++pi) {
        const Patch& p = patches_[size_t(pi)];
        const auto uvs = patchUvs(pi);
        g.setPen(QPen(QColor(120, 220, 255), 1.4));
        for (int r = 0; r < p.rows; ++r)
            for (int c = 0; c < p.cols; ++c) {
                const size_t k = size_t(r) * size_t(p.cols) + size_t(c);
                const QPointF a = toScreen(uvs[k][0], uvs[k][1]);
                if (c + 1 < p.cols) g.drawLine(a, toScreen(uvs[k + 1][0], uvs[k + 1][1]));
                if (r + 1 < p.rows) g.drawLine(a, toScreen(uvs[k + size_t(p.cols)][0], uvs[k + size_t(p.cols)][1]));
            }
        for (size_t k = 0; k < uvs.size(); ++k) {
            const QPointF q = toScreen(uvs[k][0], uvs[k][1]);
            const bool grabbed = std::find(grabbedPatch_.begin(), grabbedPatch_.end(), std::make_pair(pi, int(k))) != grabbedPatch_.end();
            g.setPen(QPen(QColor(20, 20, 20), 1));
            g.setBrush(grabbed ? QColor(255, 90, 60) : QColor(120, 220, 255));
            g.drawEllipse(q, kHandle, kHandle);
        }
    }
    g.setBrush(QColor(255, 255, 255));
    for (int fi = 0; fi < faceCount(); ++fi) {
        const auto uvs = faceUvs(fi);
        for (size_t k = 0; k < uvs.size(); ++k) {
            const auto& uv = uvs[k];
            const QPointF p = toScreen(uv[0], uv[1]);
            const bool grabbed = std::find(grabbed_.begin(), grabbed_.end(), std::make_pair(fi, int(k))) != grabbed_.end();
            g.setPen(QPen(QColor(20, 20, 20), 1));
            g.setBrush(grabbed ? QColor(255, 90, 60) : QColor(255, 255, 255));
            g.drawRect(QRectF(p.x() - kHandle, p.y() - kHandle, 2 * kHandle, 2 * kHandle));
        }
    }
}

void UvEditor::mousePressEvent(QMouseEvent* e) {
    lastMouse_ = pressPos_ = e->pos();
    grabbed_.clear();
    panning_ = e->button() == Qt::MiddleButton;
    if (e->button() != Qt::LeftButton) return;
    if (layout_) {
        double best = 1e30;
        int bf = -1, bv = -1;
        for (int fi = 0; fi < faceCount(); ++fi)
            for (size_t k = 0; k < faces_[size_t(fi)].net.size(); ++k) {
                const QPointF p = netToScreen(faces_[size_t(fi)].net[k][0], faces_[size_t(fi)].net[k][1]);
                const double d = std::hypot(p.x() - e->position().x(), p.y() - e->position().y());
                if (d <= kHandle + 3 && d < best) best = d, bf = fi, bv = int(k);
            }
        if (bf >= 0) {
            bakeGlobal();
            grabbed_ = {{bf, bv}};
            grabUv_ = faceUvs(bf)[size_t(bv)];
            grabMouse_ = e->position();
            update();
            return;
        }
        dragging_ = true;
        rotating_ = e->modifiers() & Qt::ControlModifier;
        return;
    }
    // A vertex handle under the cursor: grab it (and every handle at the same spot).
    double best = 1e30;
    std::array<double, 2> at{};
    for (int fi = 0; fi < faceCount(); ++fi) {
        const auto uvs = faceUvs(fi);
        for (size_t k = 0; k < uvs.size(); ++k) {
            const QPointF p = toScreen(uvs[k][0], uvs[k][1]);
            const double d = std::hypot(p.x() - e->position().x(), p.y() - e->position().y());
            if (d <= kHandle + 3 && d < best) best = d, at = uvs[k], grabbed_ = {{fi, int(k)}};
        }
    }
    grabbedPatch_.clear();
    for (int pi = 0; pi < patchCount(); ++pi) {
        const auto uvs = patchUvs(pi);
        for (size_t k = 0; k < uvs.size(); ++k) {
            const QPointF p = toScreen(uvs[k][0], uvs[k][1]);
            const double d = std::hypot(p.x() - e->position().x(), p.y() - e->position().y());
            if (d <= kHandle + 3 && d < best) best = d, grabbed_.clear(), grabbedPatch_ = {{pi, int(k)}};
        }
    }
    if (!grabbedPatch_.empty()) {
        bakeGlobal();
        update();
        return;
    }
    if (!grabbed_.empty()) {
        bakeGlobal();
        grabbed_ = coincident(grabbed_.front().first, grabbed_.front().second);
        grabStart_ = at;
        update();
        return;
    }
    dragging_ = true;
    rotating_ = e->modifiers() & Qt::ControlModifier;
}

void UvEditor::mouseMoveEvent(QMouseEvent* e) {
    const QPoint d = e->pos() - lastMouse_;
    lastMouse_ = e->pos();
    if (layout_ && !grabbed_.empty() && (e->buttons() & Qt::LeftButton)) {
        // Move the grabbed vertex across its face's texture by the mouse delta (net units), from the drag start.
        const auto [fi, vi] = grabbed_.front();
        double m[6];
        if (faceAffine(fi, m)) {
            const double det = m[0] * m[4] - m[1] * m[3];
            const double dx = (e->position().x() - grabMouse_.x()) / netScale_, dy = -(e->position().y() - grabMouse_.y()) / netScale_;
            if (std::fabs(det) > 1e-12) {
                const double du = (m[4] * dx - m[1] * dy) / det, dv = (-m[3] * dx + m[0] * dy) / det;
                moveVertex(fi, vi, grabUv_[0] + du, grabUv_[1] + dv);
                grabUv_ = faceUvs(fi)[size_t(vi)];
                grabMouse_ = e->position();
            }
        }
        return;
    }
    if (layout_ && panning_) {
        netCentre_ -= QPointF(d.x() / netScale_, -d.y() / netScale_);
        update();
        return;
    }
    if (!grabbedPatch_.empty() && (e->buttons() & Qt::LeftButton)) {
        const auto uv = toUv(e->position());
        movePatchVertex(grabbedPatch_.front().first, grabbedPatch_.front().second, uv[0], uv[1]);
        return;
    }
    if (!grabbed_.empty() && (e->buttons() & Qt::LeftButton)) {
        const auto uv = toUv(e->position());
        moveGroup(grabbed_, uv[0], uv[1]);
        return;
    }
    if (panning_) {
        centre_ -= QPointF(d.x() / view_, d.y() / view_);
        update();
        return;
    }
    if (!dragging_) return;
    if (rotating_) setPending(du_, dv_, scale_, rot_ + d.x() * 0.5);
    else setPending(du_ - d.x() / view_, dv_ - d.y() / view_, scale_, rot_);
}

void UvEditor::mouseReleaseEvent(QMouseEvent*) {
    dragging_ = rotating_ = panning_ = false;
    grabbed_.clear();
    grabbedPatch_.clear();
    update();
}

void UvEditor::wheelEvent(QWheelEvent* e) {
    const double f = e->angleDelta().y() > 0 ? 1.0 / 1.1 : 1.1;
    if (e->modifiers() & Qt::ShiftModifier) {
        if (layout_) netScale_ = std::clamp(netScale_ / f, 0.01, 200.0);
        else view_ = std::clamp(view_ / f, 4.0, 4000.0);  // Shift+wheel zooms the view
        update();
    } else {
        setPending(du_, dv_, scale_ * f, rot_);
    }
}

}  // namespace iw8r::qt
