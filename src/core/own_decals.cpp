#include "own_decals.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "brush_ops.h"
#include "compile_iw8.h"
#include "model_import.h"
#include "stock_decal_volumes.h"
#include "stock_smodels.h"

namespace iw8r {

namespace {

std::string lowered(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool three(const std::string* v, double out[3]) {
    if (!v) return false;
    return std::sscanf(v->c_str(), "%lf %lf %lf", &out[0], &out[1], &out[2]) == 3 && std::isfinite(out[0] + out[1] + out[2]);
}

double ranged(const Entity& e, const char* key, double def, double lo, double hi) {
    const std::string* v = e.value(key);
    if (!v || v->empty()) return def;
    char* end = nullptr;
    const double d = std::strtod(v->c_str(), &end);
    return end != v->c_str() && std::isfinite(d) && d >= lo && d <= hi ? d : def;
}

std::array<double, 2> pair(const Entity& e, const char* key, double a, double b, bool positive) {
    const std::string* v = e.value(key);
    double x = 0, y = 0;
    if (!v || v->empty() || std::sscanf(v->c_str(), "%lf %lf", &x, &y) != 2 || !std::isfinite(x + y) || (positive && !(x > 0 && y > 0)))
        return {a, b};
    return {x, y};
}

}  // namespace

bool ownDecalBox(const Entity& e, OwnDecalBox& out) {
    if (lowered(e.classname()) != "iw8r_decal") return false;
    const std::string* mv = e.value("material");
    if (!mv || mv->empty()) return false;
    double size[3] = {8, 64, 64};
    if (const std::string* sv = e.value("size"); sv && !three(sv, size)) return false;
    if (!(size[0] > 0 && size[1] > 0 && size[2] > 0)) return false;
    double ang[3] = {0, 0, 0};
    if (!three(e.value("angles"), ang)) ang[0] = ang[1] = ang[2] = 0;
    // The build's own conversion (compile_iw8.cpp anglesToAxis: forward, left, up), the same rotation as the stock one.
    out.axis = anglesToAxis(Vec3{ang[0], ang[1], ang[2]});
    out.center = e.origin().value_or(Vec3{});
    out.halfSize = Vec3{size[0] / 2, size[1] / 2, size[2] / 2};
    out.material = *mv;
    out.opacity = ranged(e, "opacity", 1, 0, 1);
    const double rot = ranged(e, "uv_rotate", 0, -360, 360) * 3.14159265358979323846 / 180.0;
    const auto scale = pair(e, "uv_scale", 1, 1, true), shift = pair(e, "uv_shift", 0, 0, false);
    const double c = std::cos(rot), s = std::sin(rot);
    out.uvMatrix = {c * scale[0], -s * scale[1], s * scale[0], c * scale[1]};
    out.uvOffset = {shift[0], shift[1]};
    return true;
}

std::vector<OwnDecal> projectOwnDecals(const MapFile& map, std::vector<Id>* entityIds, double lift) {
    std::vector<OwnDecal> out;
    if (entityIds) entityIds->clear();
    std::vector<OwnDecalBox> boxes;
    std::vector<Id> ids;
    for (const Entity& e : map.entities) {
        OwnDecalBox b;
        if (ownDecalBox(e, b)) boxes.push_back(b), ids.push_back(e.id);
    }
    if (boxes.empty()) return out;
    // The receivers: drawn brush faces of the world, func_groups and brush models, as one mesh each brush.
    std::vector<ImportedMesh> meshes;
    for (const Entity& e : map.entities) {
        const std::string cls = lowered(e.classname());
        if (!(e.isWorldspawn() || cls == "func_group" || cls == "script_brushmodel")) continue;
        for (const auto& it : e.items) {
            const Primitive* p = it.prim();
            if (!p || p->kind != PrimKind::Brush) continue;
            const ConvexBrush cb = brushFromPrimitive(*p);
            if (!isValidBrush(cb)) continue;
            const std::vector<Winding> wins = brushWindings(cb);
            ImportedMesh m;
            for (size_t f = 0; f < wins.size() && f < p->faces.size(); ++f) {
                const Winding& w = wins[f];
                if (w.size() < 3 || isIw8ToolMaterial(p->faces[f].material())) continue;
                const Vec3 n = cb[f].plane.n;
                const uint32_t first = uint32_t(m.pos.size());
                for (const Vec3& v : w) {
                    m.pos.push_back(v);
                    m.normal.push_back(n);
                    m.uv.push_back({0, 0});
                }
                for (size_t k = 1; k + 1 < w.size(); ++k) {
                    // Counter-clockwise from outside, as the projector expects: the winding's own order.
                    m.idx.push_back(first);
                    m.idx.push_back(first + uint32_t(k));
                    m.idx.push_back(first + uint32_t(k + 1));
                }
            }
            if (!m.idx.empty()) meshes.push_back(std::move(m));
        }
    }
    StockDecalReceivers receivers;
    for (const ImportedMesh& m : meshes) receivers.addMesh(m);
    receivers.build();
    StockDecalProjectOptions opt;
    opt.offsetAlongNormal = lift;
    for (size_t i = 0; i < boxes.size(); ++i) {
        const OwnDecalBox& b = boxes[i];
        StockDecalVolume vol;
        vol.center = b.center;
        vol.axis = b.axis;
        vol.halfSize = b.halfSize;
        vol.material = b.material;
        vol.uvMatrix = b.uvMatrix;
        vol.uvOffset = b.uvOffset;
        StockDecalBatch batch;
        projectDecalVolume(receivers, vol, batch, opt);
        OwnDecal d;
        d.material = b.material;
        d.opacity = b.opacity;
        for (size_t t = 0; t + 2 < batch.idx.size(); t += 3) {
            OwnDecalPiece piece;
            for (int k = 0; k < 3; ++k) {
                const StockDecalVertex& v = batch.verts[batch.idx[t + size_t(k)]];
                piece.pos[size_t(k)] = v.pos;
                // The material UV (the projector applies the volume's UV matrix and offset; the game wraps it).
                piece.uv[size_t(k)] = {v.mu, v.mv};
                piece.fade[k] = v.fade;
                piece.normal = v.normal;
            }
            d.pieces.push_back(piece);
        }
        out.push_back(std::move(d));
        if (entityIds) entityIds->push_back(ids[i]);
    }
    return out;
}

}  // namespace iw8r
