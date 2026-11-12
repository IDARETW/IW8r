#include "model_preview.h"

#include <QPainter>
#include <QPolygonF>

#include <algorithm>
#include <cmath>
#include <vector>

namespace iw8r::qt {

QImage renderModelPreview(const ImportedModel& m, int size, double yawDeg, size_t maxTriangles) {
    struct Tri {
        Vec3 a, b, c;
    };
    std::vector<Tri> tris;
    size_t total = 0;
    for (const auto& mesh : m.meshes) total += mesh.idx.size() / 3;
    if (!total) return {};
    const size_t step = std::max<size_t>(1, (total + maxTriangles - 1) / maxTriangles);
    size_t n = 0;
    AABB box;
    const auto zUp = [](const Vec3& p) { return Vec3{p.x, -p.z, p.y}; };
    for (const auto& mesh : m.meshes)
        for (size_t i = 0; i + 2 < mesh.idx.size(); i += 3, ++n) {
            if (n % step) continue;
            if (mesh.idx[i] >= mesh.pos.size() || mesh.idx[i + 1] >= mesh.pos.size() || mesh.idx[i + 2] >= mesh.pos.size()) continue;
            Tri t{zUp(mesh.pos[mesh.idx[i]]), zUp(mesh.pos[mesh.idx[i + 1]]), zUp(mesh.pos[mesh.idx[i + 2]])};
            box.add(t.a), box.add(t.b), box.add(t.c);
            tris.push_back(t);
        }
    if (!box.valid()) return {};

    // View: yaw around Z, then pitch 25 degrees down. Screen x = right, y = up, depth = toward the viewer.
    const double yaw = yawDeg * 3.14159265358979 / 180.0, pitch = 25 * 3.14159265358979 / 180.0;
    const Vec3 c = box.center();
    const auto view = [&](const Vec3& p) {
        const Vec3 d = p - c;
        const double x = d.x * std::cos(yaw) + d.y * std::sin(yaw);
        const double y0 = -d.x * std::sin(yaw) + d.y * std::cos(yaw);
        const double y = d.z * std::cos(pitch) - y0 * std::sin(pitch);
        const double z = d.z * std::sin(pitch) + y0 * std::cos(pitch);  // larger = farther
        return Vec3{x, y, z};
    };
    struct Drawn {
        QPolygonF poly;
        double depth;
        double shade;
    };
    std::vector<Drawn> drawn;
    drawn.reserve(tris.size());
    double ext = 1e-6;
    std::vector<std::array<Vec3, 3>> vs;
    vs.reserve(tris.size());
    for (const Tri& t : tris) {
        vs.push_back({view(t.a), view(t.b), view(t.c)});
        for (const Vec3& v : vs.back()) ext = std::max({ext, std::fabs(v.x), std::fabs(v.y)});
    }
    const double k = (size * 0.45) / ext;
    const Vec3 light = normalize(Vec3{-0.4, 0.5, -0.75});  // from the upper left, toward the model
    for (const auto& v : vs) {
        const Vec3 nrm = normalize(cross(v[1] - v[0], v[2] - v[0]));
        const double lambert = std::fabs(dot(nrm, light));  // two-sided: the source winding does not matter here
        QPolygonF poly;
        for (const Vec3& p : v) poly << QPointF(size / 2.0 + p.x * k, size / 2.0 - p.y * k);
        drawn.push_back({poly, (v[0].z + v[1].z + v[2].z) / 3.0, 0.25 + 0.75 * lambert});
    }
    std::sort(drawn.begin(), drawn.end(), [](const Drawn& a, const Drawn& b) { return a.depth > b.depth; });  // far first

    QImage img(size, size, QImage::Format_ARGB32_Premultiplied);
    img.fill(QColor(32, 34, 38));
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing, false);
    for (const Drawn& d : drawn) {
        const int g = int(std::clamp(d.shade * 210.0, 0.0, 255.0));
        const QColor col(g, int(g * 0.98), int(std::min(255.0, g * 1.04)));
        p.setPen(col);
        p.setBrush(col);
        p.drawPolygon(d.poly);
    }
    return img;
}

}  // namespace iw8r::qt
