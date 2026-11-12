#include "editor_context.h"

#include <algorithm>
#include <cmath>

#include "brush_ops2.h"
#include "decal_tools.h"
#include "editor_log.h"
#include "region.h"

namespace iw8r::qt {

void EditorContext::setStatus(const QString& s) {
    if (!s.isEmpty()) EditorLog::instance().add(LogLevel::Info, "status", s);
    emit statusMessage(s);
}

void EditorContext::refreshRegion() {
    std::set<Id> out = region ? itemsOutside(doc.map(), *region) : std::set<Id>{};
    if (out != scene.hiddenByRegion) {
        scene.hiddenByRegion = std::move(out);
        scene.invalidate();
    }
}

void EditorContext::documentEdited(const QString& status) {
    refreshRegion();
    scene.sync(doc);
    if (!status.isEmpty()) setStatus(status);
    emit documentChanged();
}

void EditorContext::cameraMoved() { emit cameraChanged(); }

void EditorContext::tickCamera(double dt, bool shiftHeld) {
    if (heldKeys.isEmpty()) return;
    const double speed = cam.speed * (shiftHeld ? 3.0 : 1.0) * dt;
    const double turn = 90.0 * dt;
    Vec3 move;
    bool turned = false;
    auto down = [&](int k) { return heldKeys.contains(k); };
    if (looking) {
        if (down(Qt::Key_W)) move += cam.forward();
        if (down(Qt::Key_S)) move += -cam.forward();
        if (down(Qt::Key_D)) move += cam.right();
        if (down(Qt::Key_A)) move += -cam.right();
        if (down(Qt::Key_E)) move += Vec3{0, 0, 1};
        if (down(Qt::Key_Q)) move += Vec3{0, 0, -1};
    } else {
        // Stock CoD Radiant camera keys (docs/research/07 section 1a).
        if (down(Qt::Key_Up)) move += cam.forward();
        if (down(Qt::Key_Down)) move += -cam.forward();
        if (down(Qt::Key_Period)) move += cam.right();
        if (down(Qt::Key_Comma)) move += -cam.right();
        if (down(Qt::Key_D)) move += Vec3{0, 0, 1};
        if (down(Qt::Key_C)) move += Vec3{0, 0, -1};
        if (down(Qt::Key_Left)) { cam.yaw += turn; turned = true; }
        if (down(Qt::Key_Right)) { cam.yaw -= turn; turned = true; }
        if (down(Qt::Key_A)) { cam.pitch = std::min(89.0, cam.pitch + turn); turned = true; }
        if (down(Qt::Key_Z)) { cam.pitch = std::max(-89.0, cam.pitch - turn); turned = true; }
    }
    if (length(move) > 0) cam.pos += normalize(move) * speed;
    if (length(move) > 0 || turned) emit cameraChanged();
}

std::optional<Plane> EditorContext::clipPlane() const {
    if (clipPoints.size() < 2) return std::nullopt;
    const Vec3 a = clipPoints[0], b = clipPoints[1];
    Vec3 depth;
    depth[axisD()] = 64.0;
    if (clipPoints.size() == 3) {
        // Third point: the plane through all three. The kept side is the one holding a + depth (the 2-point
        // convention), or a - depth when flipped. Collinear points give a vertical plane, which the 2-point code handles.
        if (auto pl = clipPlaneFrom3Points(a, b, clipPoints[2], clipFlip ? a - depth : a + depth)) return pl;
    }
    auto pl = clipFlip ? planeFromPoints(b, a, a + depth) : planeFromPoints(a, b, a + depth);
    return pl;
}

void EditorContext::frameSelection() {
    const AABB b = doc.selectionBounds();
    if (!b.valid()) return;
    app::frameBounds(cam, b, 2.0);
    gridCenterH = b.center()[axisH()];
    gridCenterV = b.center()[axisV()];
    emit cameraChanged();
}

int EditorContext::decalAt(const Ray& ray, double t, const SelRef& hit) {
    if (currentMaterial.empty()) return 0;
    const Vec3 at = ray.origin + ray.dir * t;
    // Surface normal: the face of the hit brush whose plane passes closest to the point, facing the camera.
    Vec3 n = ray.dir * -1.0;
    if (const EntityItem* it = doc.findItem(hit.entity, hit.item); it && it->prim() && it->prim()->kind == PrimKind::Brush) {
        double best = 1e300;
        for (const Plane& pl : it->prim()->planes()) {
            const double d = std::fabs(dot(pl.n, at) - pl.d);
            if (d < best && dot(pl.n, ray.dir) < 0) best = d, n = pl.n;
        }
    }
    const auto patches = makeConformingDecal(doc.map(), at, n, decalSize, decalSize, decalRotation, 0.5, currentMaterial);
    if (patches.empty() || !placeDecal(doc, patches, true)) return 0;
    scene.invalidate();
    return int(patches.size());
}

int EditorContext::vertexPaintAt(const Vec3& at) {
    std::vector<Document::PrimEdit> edits;
    int changed = 0;
    const double r2 = vertexBrush.radius * vertexBrush.radius;
    for (const Entity& e : doc.map().entities)
        for (const EntityItem& it : e.items) {
            const Primitive* p = it.prim();
            if (!p || p->kind != PrimKind::Patch) continue;
            bool near = false;
            for (const PatchVertex& v : p->verts) {
                const Vec3 d = v.pos - at;
                if (dot(d, d) < r2) {
                    near = true;
                    break;
                }
            }
            if (!near) continue;
            Primitive copy = *p;
            const int n = paintPatchVertices(copy, at, vertexBrush);
            if (n > 0) {
                changed += n;
                edits.push_back({e.id, it.id, std::move(copy)});
            }
        }
    if (edits.empty() || !doc.editPrimitives(edits, "Vertex Paint", false)) return 0;
    return changed;
}

}  // namespace iw8r::qt
