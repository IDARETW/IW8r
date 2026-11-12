#include "reference_plan.h"

#include <QPainter>
#include <QPolygonF>
#include <algorithm>
#include <cmath>
#include <numeric>

namespace iw8r::qt {

std::vector<ReferencePlan> buildReferencePlans(const std::vector<app::GpuVertex>& tris, int maxPixels) {
    std::vector<ReferencePlan> out;
    const size_t n = tris.size() / 3;
    if (n == 0) return out;
    float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
    for (const auto& v : tris) {
        const float p[3] = {v.x, v.y, v.z};
        for (int k = 0; k < 3; ++k) lo[k] = std::min(lo[k], p[k]), hi[k] = std::max(hi[k], p[k]);
    }
    // (h, v, depth axis, depth sign): top looks down -Z, front looks along +Y (XZ), side along -X (YZ).
    const int axes[3][3] = {{0, 1, 2}, {0, 2, 1}, {1, 2, 0}};
    const double sign[3] = {1, -1, 1};  // larger value = nearer the viewer
    for (int a = 0; a < 3; ++a) {
        const int H = axes[a][0], V = axes[a][1], D = axes[a][2];
        ReferencePlan plan;
        plan.h = H, plan.v = V;
        plan.minH = lo[H], plan.maxH = hi[H], plan.minV = lo[V], plan.maxV = hi[V];
        const double spanH = std::max(1.0, plan.maxH - plan.minH), spanV = std::max(1.0, plan.maxV - plan.minV);
        const double s = maxPixels / std::max(spanH, spanV);
        const int w = std::max(1, int(std::ceil(spanH * s))), hgt = std::max(1, int(std::ceil(spanV * s)));
        plan.image = QImage(w, hgt, QImage::Format_ARGB32_Premultiplied);
        plan.image.fill(Qt::transparent);
        // Far triangles first, so nearer ones cover them.
        std::vector<float> depth(n);
        for (size_t t = 0; t < n; ++t) {
            const auto& A = tris[3 * t];
            const auto& B = tris[3 * t + 1];
            const auto& C = tris[3 * t + 2];
            const float pa[3] = {A.x, A.y, A.z}, pb[3] = {B.x, B.y, B.z}, pc[3] = {C.x, C.y, C.z};
            depth[t] = float(sign[a] * (pa[D] + pb[D] + pc[D]) / 3.0);
        }
        std::vector<uint32_t> order(n);
        std::iota(order.begin(), order.end(), 0u);
        std::sort(order.begin(), order.end(), [&](uint32_t x, uint32_t y) { return depth[x] < depth[y]; });
        const double dLo = a == 0 ? lo[D] : -hi[D], dHi = a == 0 ? hi[D] : -lo[D];
        const double dSpan = std::max(1.0, sign[a] > 0 ? hi[D] - lo[D] : dHi - dLo);
        {
        QPainter p(&plan.image);
        p.setPen(Qt::NoPen);
        for (const uint32_t t : order) {
            QPolygonF poly;
            for (int k = 0; k < 3; ++k) {
                const auto& v = tris[3 * t + k];
                const float pv[3] = {v.x, v.y, v.z};
                poly << QPointF((pv[H] - plan.minH) * s, (plan.maxV - pv[V]) * s);
            }
            // Brightness from depth: near surfaces light, far ones dark; slightly blue-grey to tell it from the map.
            const double f = std::clamp((depth[t] - (sign[a] > 0 ? lo[D] : dLo)) / dSpan, 0.0, 1.0);
            const int g = int(60 + 120 * f);
            p.setBrush(QColor(g, g, std::min(255, g + 20)));
            p.drawPolygon(poly);
        }
        }  // painter done before the image moves
        out.push_back(std::move(plan));
    }
    return out;
}

}  // namespace iw8r::qt
