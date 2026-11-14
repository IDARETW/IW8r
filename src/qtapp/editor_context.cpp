#include "editor_context.h"

#include <algorithm>
#include <cmath>

#include "brush_ops2.h"
#include "decal_tools.h"
#include "editor_log.h"
#include "game_mode_view.h"
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
    // The region's own outline, for the camera view (the 2D views draw it from `region`).
    std::vector<std::pair<Vec3, Vec3>> lines;
    if (region && region->valid()) {
        const Vec3 a = region->mins, b = region->maxs;
        const Vec3 c[8] = {{a.x, a.y, a.z}, {b.x, a.y, a.z}, {b.x, b.y, a.z}, {a.x, b.y, a.z},
                           {a.x, a.y, b.z}, {b.x, a.y, b.z}, {b.x, b.y, b.z}, {a.x, b.y, b.z}};
        for (int k = 0; k < 4; ++k) {
            lines.push_back({c[k], c[(k + 1) % 4]});
            lines.push_back({c[4 + k], c[4 + (k + 1) % 4]});
            lines.push_back({c[k], c[4 + k]});
        }
    }
    const auto same = [](const std::vector<std::pair<Vec3, Vec3>>& x, const std::vector<std::pair<Vec3, Vec3>>& y) {
        if (x.size() != y.size()) return false;
        for (size_t k = 0; k < x.size(); ++k)
            if (length(x[k].first - y[k].first) > 1e-9 || length(x[k].second - y[k].second) > 1e-9) return false;
        return true;
    };
    if (!same(lines, scene.regionLines)) {
        scene.regionLines = std::move(lines);
        scene.invalidate();
    }
}

void EditorContext::refreshGameMode() {
    std::set<Id> out;
    if (const GameModeView* mode = findGameModeView(scene.gameMode))
        for (const auto& e : doc.map().entities)
            if (!e.isWorldspawn() && !gameModeKeepsEntity(*mode, e.classname(), e.value("script_gameobjectname"))) out.insert(e.id);
    if (out != scene.hiddenByMode) {
        scene.hiddenByMode = std::move(out);
        scene.invalidate();
    }
}

void EditorContext::documentEdited(const QString& status) {
    refreshRegion();
    refreshGameMode();
    scene.sync(doc);
    if (!status.isEmpty()) setStatus(status);
    emit documentChanged();
}

void EditorContext::cameraMoved() { emit cameraChanged(); }

void EditorContext::tickCamera(double dt, bool shiftHeld) {
    const bool resting = length(camVelocity) < 1e-3 && std::fabs(camYawRate) < 1e-3 && std::fabs(camPitchRate) < 1e-3;
    if (heldKeys.isEmpty() && resting) return;
    const double fullSpeed = cam.speed * (shiftHeld ? 3.0 : 1.0);  // units per second
    const double turn = 90.0;                                      // degrees per second
    Vec3 move;
    bool turned = false;
    double yawWant = 0, pitchWant = 0;
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
        if (down(Qt::Key_Left)) yawWant += turn;
        if (down(Qt::Key_Right)) yawWant -= turn;
        if (down(Qt::Key_A)) pitchWant += turn;
        if (down(Qt::Key_Z)) pitchWant -= turn;
    }
    const Vec3 wantVel = length(move) > 0 ? normalize(move) * fullSpeed : Vec3{};
    if (cameraRamp <= 0) {
        camVelocity = wantVel, camYawRate = yawWant, camPitchRate = pitchWant;
    } else {
        // Exponential easing: about 95 % of the way in cameraRamp seconds, starting and stopping alike.
        const double k = 1.0 - std::exp(-3.0 * dt / cameraRamp);
        camVelocity = camVelocity + (wantVel - camVelocity) * k;
        camYawRate += (yawWant - camYawRate) * k;
        camPitchRate += (pitchWant - camPitchRate) * k;
        // Settle exactly at rest (no endless creep).
        if (length(wantVel) == 0 && length(camVelocity) < fullSpeed * 0.01) camVelocity = Vec3{};
        if (yawWant == 0 && std::fabs(camYawRate) < 0.5) camYawRate = 0;
        if (pitchWant == 0 && std::fabs(camPitchRate) < 0.5) camPitchRate = 0;
    }
    const bool moved = length(camVelocity) > 0;
    if (moved) cam.pos += camVelocity * dt;
    if (camYawRate != 0) cam.yaw += camYawRate * dt, turned = true;
    if (camPitchRate != 0) cam.pitch = std::clamp(cam.pitch + camPitchRate * dt, -89.0, 89.0), turned = true;
    if (moved || turned) emit cameraChanged();
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
