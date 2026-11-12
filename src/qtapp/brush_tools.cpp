#include "brush_tools.h"

#include "tex_projection.h"

#include <map>
#include <set>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>
#include <cmath>
#include <set>

namespace iw8r::qt {

std::vector<SelectedBrush> selectedBrushes(EditorContext& ctx) {
    std::vector<SelectedBrush> out;
    for (const auto& r : ctx.doc.selection()) {
        if (r.item == kNoId) continue;
        const EntityItem* it = ctx.doc.findItem(r.entity, r.item);
        const Primitive* p = it ? it->prim() : nullptr;
        if (!p || p->kind != PrimKind::Brush) continue;
        out.push_back({r.entity, r.item, brushFromPrimitive(*p), p});
    }
    return out;
}

std::optional<Primitive> rebuildBrush(const Primitive& original, const ConvexBrush& b, const std::string& eol) {
    auto fresh = makeBrushPrimitive(b, eol);
    if (!fresh) return std::nullopt;
    std::vector<TextLine> faceLines;
    for (const auto& f : fresh->faces) faceLines.push_back(fresh->lines[f.line]);
    std::set<size_t> oldFaceLines;
    for (const auto& f : original.faces) oldFaceLines.insert(f.line);
    Primitive out;
    out.id = original.id;
    bool inserted = false;
    for (size_t i = 0; i < original.lines.size(); ++i) {
        if (oldFaceLines.count(i)) {
            if (!inserted) {
                for (auto l : faceLines) {
                    l.eol = original.lines[i].eol;
                    out.lines.push_back(l);
                }
                inserted = true;
            }
            continue;
        }
        out.lines.push_back(original.lines[i]);
    }
    if (!inserted) return fresh;
    reparsePrimitive(out);
    out.dirty = true;
    if (out.kind != PrimKind::Brush) return std::nullopt;
    return out;
}

std::string faceSuffix(const EditorContext& ctx) {
    return " " + ctx.currentMaterial + " 128 128 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0";
}

bool createBoxBrush(EditorContext& ctx, const AABB& box) {
    Entity* w = ctx.doc.worldspawn();
    if (!w || !box.valid()) return false;
    for (int a = 0; a < 3; ++a)
        if (box.maxs[a] - box.mins[a] < 0.001) return false;
    auto prim = makeBrushPrimitive(makeBox(box, faceSuffix(ctx)), ctx.doc.eol());
    if (!prim) return false;
    return ctx.doc.editPrimitives({{w->id, kNoId, *prim}}, "Create Brush");
}

namespace {
bool replaceWithPieces(EditorContext& ctx, const std::vector<std::pair<SelectedBrush, std::vector<ConvexBrush>>>& work,
                       const std::string& name) {
    std::vector<Document::PrimEdit> edits;
    for (const auto& [sb, pieces] : work) {
        edits.push_back({sb.entity, sb.item, std::nullopt});
        for (const auto& piece : pieces)
            if (auto prim = rebuildBrush(*sb.source, piece, ctx.doc.eol())) edits.push_back({sb.entity, kNoId, *prim});
    }
    return ctx.doc.editPrimitives(edits, name);
}
}  // namespace

bool hollowSelection(EditorContext& ctx) {
    std::vector<std::pair<SelectedBrush, std::vector<ConvexBrush>>> work;
    for (auto& sb : selectedBrushes(ctx)) {
        auto pieces = hollowBrush(sb.brush, ctx.doc.grid());
        if (!pieces.empty()) work.push_back({sb, std::move(pieces)});
    }
    return !work.empty() && replaceWithPieces(ctx, work, "CSG Hollow");
}

bool subtractSelection(EditorContext& ctx) {
    const auto cutters = selectedBrushes(ctx);
    Entity* w = ctx.doc.worldspawn();
    if (cutters.empty() || !w) return false;
    std::set<Id> cutterItems;
    for (const auto& c : cutters) cutterItems.insert(c.item);
    std::vector<std::pair<SelectedBrush, std::vector<ConvexBrush>>> work;
    for (const auto& it : w->items) {
        const Primitive* p = it.prim();
        if (!p || p->kind != PrimKind::Brush || cutterItems.count(it.id)) continue;
        ConvexBrush target = brushFromPrimitive(*p);
        const AABB tb = brushBounds(target);
        std::vector<ConvexBrush> current{target};
        bool touched = false;
        for (const auto& c : cutters) {
            const AABB cb = brushBounds(c.brush);
            if (cb.maxs.x <= tb.mins.x || cb.mins.x >= tb.maxs.x || cb.maxs.y <= tb.mins.y || cb.mins.y >= tb.maxs.y ||
                cb.maxs.z <= tb.mins.z || cb.mins.z >= tb.maxs.z)
                continue;
            std::vector<ConvexBrush> next;
            for (const auto& piece : current) {
                auto cut = subtractBrush(piece, c.brush);
                if (cut.size() != 1 || brushVolume(cut[0]) != brushVolume(piece)) touched = true;
                next.insert(next.end(), cut.begin(), cut.end());
            }
            current = std::move(next);
        }
        if (touched) work.push_back({SelectedBrush{w->id, it.id, target, p}, current});
    }
    return !work.empty() && replaceWithPieces(ctx, work, "CSG Subtract");
}

bool clipSelection(EditorContext& ctx, const Plane& keep, bool split) {
    std::vector<std::pair<SelectedBrush, std::vector<ConvexBrush>>> work;
    const std::string cap = faceSuffix(ctx);
    for (auto& sb : selectedBrushes(ctx)) {
        std::vector<ConvexBrush> pieces;
        if (auto a = clipBrush(sb.brush, keep, cap)) pieces.push_back(*a);
        if (split)
            if (auto b = clipBrush(sb.brush, Plane{-keep.n, -keep.d}, cap)) pieces.push_back(*b);
        work.push_back({sb, std::move(pieces)});
    }
    return !work.empty() && replaceWithPieces(ctx, work, split ? "Split" : "Clip");
}

namespace {
Vec3 selectionPivot(EditorContext& ctx) {
    const AABB b = ctx.doc.selectionBounds();
    return snapToGrid(b.center(), ctx.doc.grid());
}

bool transformSelection(EditorContext& ctx, const std::string& name, const std::function<ConvexBrush(const ConvexBrush&)>& f,
                        const std::function<Vec3(const Vec3&)>& xf) {
    std::vector<Document::PrimEdit> edits;
    for (auto& sb : selectedBrushes(ctx))
        if (auto prim = rebuildBrush(*sb.source, f(sb.brush), ctx.doc.eol())) {
            if (ctx.doc.textureLock && xf) lockPrimitiveTextures(*sb.source, *prim, xf);
            edits.push_back({sb.entity, sb.item, *prim});
        }
    return ctx.doc.editPrimitives(edits, name);
}
}  // namespace

bool rotateSelection(EditorContext& ctx, int axis, double degrees) {
    const Vec3 pivot = selectionPivot(ctx);
    return transformSelection(ctx, "Rotate", [&](const ConvexBrush& b) { return rotateBrush(b, axis, degrees, pivot); },
                              [&](const Vec3& p) { return rotatePoint(p, axis, degrees, pivot); });
}

bool scaleSelection(EditorContext& ctx, const Vec3& factors) {
    if (factors.x <= 0 || factors.y <= 0 || factors.z <= 0) return false;
    const Vec3 pivot = selectionPivot(ctx);
    return transformSelection(ctx, "Scale", [&](const ConvexBrush& b) { return scaleBrush(b, factors, pivot); }, [&](const Vec3& p) {
        return Vec3{pivot.x + (p.x - pivot.x) * factors.x, pivot.y + (p.y - pivot.y) * factors.y, pivot.z + (p.z - pivot.z) * factors.z};
    });
}

bool rotateSelectionAbout(EditorContext& ctx, int axis, double degrees, const Vec3& pivot) {
    if (axis < 0 || axis > 2 || degrees == 0) return false;
    ctx.doc.beginGroup("Rotate");
    bool any = transformSelection(ctx, "Rotate", [&](const ConvexBrush& b) { return rotateBrush(b, axis, degrees, pivot); },
                                  [&](const Vec3& p) { return rotatePoint(p, axis, degrees, pivot); });
    // Point entities: origin about the pivot; yaw for Z, pitch for Y, roll for X (Quake angles order).
    std::vector<Id> ents;
    for (const auto& r : ctx.doc.selection())
        if (r.item == kNoId) ents.push_back(r.entity);
    for (Id id : ents) {
        const Entity* e = ctx.doc.findEntity(id);
        if (!e) continue;
        if (auto o = e->origin()) {
            const Vec3 n = rotatePoint(*o, axis, degrees, pivot);
            any |= ctx.doc.setKeyValue(id, "origin", formatNumber(n.x) + " " + formatNumber(n.y) + " " + formatNumber(n.z));
        }
        Vec3 ang;
        if (const std::string* a = ctx.doc.findEntity(id)->value("angles")) std::sscanf(a->c_str(), "%lf %lf %lf", &ang.x, &ang.y, &ang.z);
        const int slot = axis == 2 ? 1 : axis == 1 ? 0 : 2;
        ang[slot] = std::fmod(ang[slot] + degrees + 360.0, 360.0);
        any |= ctx.doc.setKeyValue(id, "angles", formatNumber(ang.x) + " " + formatNumber(ang.y) + " " + formatNumber(ang.z));
    }
    ctx.doc.endGroup();
    return any;
}

bool scaleSelectionAbout(EditorContext& ctx, int axis, double factor, const Vec3& pivot) {
    if (axis < 0 || axis > 2 || !(factor > 0) || std::fabs(factor - 1.0) < 1e-6) return false;
    Vec3 f{1, 1, 1};
    f[axis] = factor;
    ctx.doc.beginGroup("Scale");
    bool any = transformSelection(ctx, "Scale", [&](const ConvexBrush& b) { return scaleBrush(b, f, pivot); }, [&](const Vec3& p) {
        return Vec3{pivot.x + (p.x - pivot.x) * f.x, pivot.y + (p.y - pivot.y) * f.y, pivot.z + (p.z - pivot.z) * f.z};
    });
    std::vector<Id> ents;
    for (const auto& r : ctx.doc.selection())
        if (r.item == kNoId) ents.push_back(r.entity);
    for (Id id : ents) {
        const Entity* e = ctx.doc.findEntity(id);
        if (!e || !e->value("model")) continue;
        double ms = 1.0;
        if (const std::string* s = e->value("modelscale")) ms = std::max(0.001, std::atof(s->c_str()));
        any |= ctx.doc.setKeyValue(id, "modelscale", formatNumber(std::max(0.01, ms * factor)));
        if (ents.size() > 1)
            if (auto o = ctx.doc.findEntity(id)->origin()) {
                Vec3 n = *o;
                n[axis] = pivot[axis] + (n[axis] - pivot[axis]) * factor;
                any |= ctx.doc.setKeyValue(id, "origin", formatNumber(n.x) + " " + formatNumber(n.y) + " " + formatNumber(n.z));
            }
    }
    ctx.doc.endGroup();
    return any;
}

bool snapSelectionToGrid(EditorContext& ctx) {
    const double g = ctx.doc.grid();
    if (g <= 0) return false;
    AABB prims;
    std::vector<Id> ents;
    for (const auto& r : ctx.doc.selection()) {
        if (r.item == kNoId) {
            ents.push_back(r.entity);
            continue;
        }
        if (const EntityItem* it = ctx.doc.findItem(r.entity, r.item); it && it->prim()) prims.add(it->prim()->bounds());
    }
    ctx.doc.beginGroup("Snap To Grid");
    bool any = false;
    if (prims.valid()) {
        Vec3 d;
        for (int a = 0; a < 3; ++a) d[a] = std::round(prims.mins[a] / g) * g - prims.mins[a];
        if (std::fabs(d.x) + std::fabs(d.y) + std::fabs(d.z) > 1e-9) {
            // Only the primitives: the drag moves the selection, so drop the point entities from it meanwhile.
            for (Id id : ents) ctx.doc.toggle({id, kNoId});
            ctx.doc.beginDrag();
            ctx.doc.updateDrag(d);
            any |= ctx.doc.endDrag(true);
            for (Id id : ents) ctx.doc.select({id, kNoId}, true);
        }
    }
    for (Id id : ents) {
        const Entity* e = ctx.doc.findEntity(id);
        if (!e || !e->origin()) continue;
        Vec3 o = *e->origin(), n;
        for (int a = 0; a < 3; ++a) n[a] = std::round(o[a] / g) * g;
        if (length(n - o) > 1e-9)
            any |= ctx.doc.setKeyValue(id, "origin", formatNumber(n.x) + " " + formatNumber(n.y) + " " + formatNumber(n.z));
    }
    ctx.doc.endGroup();
    return any;
}

size_t selectByTemplate(EditorContext& ctx, SelectMode mode) {
    // Template volume = bounds of the selected brushes; those brushes are removed.
    AABB box;
    std::vector<Document::PrimEdit> removeTemplate;
    std::set<SelRef> templ;
    for (const auto& sb : selectedBrushes(ctx)) {
        box.add(brushBounds(sb.brush));
        removeTemplate.push_back({sb.entity, sb.item, std::nullopt});
        templ.insert({sb.entity, sb.item});
    }
    if (!box.valid()) return 0;
    const int H = ctx.axisH(), V = ctx.axisV();
    auto matches = [&](const AABB& b) {
        const bool tall = mode == SelectMode::CompleteTall || mode == SelectMode::PartialTall;
        bool inside = true, overlap = true;
        for (int a = 0; a < 3; ++a) {
            if (tall && a != H && a != V) continue;
            inside &= b.mins[a] >= box.mins[a] - 1e-6 && b.maxs[a] <= box.maxs[a] + 1e-6;
            overlap &= b.maxs[a] >= box.mins[a] - 1e-6 && b.mins[a] <= box.maxs[a] + 1e-6;
        }
        return (mode == SelectMode::CompleteTall || mode == SelectMode::Inside) ? inside : overlap;
    };
    std::vector<SelRef> hits;
    for (const auto& e : ctx.doc.map().entities) {
        bool entityHas = false;
        for (const auto& it : e.items) {
            const Primitive* p = it.prim();
            if (!p || p->kind == PrimKind::Unknown || templ.count({e.id, it.id})) continue;
            entityHas = true;
            if (matches(p->bounds())) hits.push_back({e.id, it.id});
        }
        if (!entityHas && !e.isWorldspawn())
            if (auto o = e.origin(); o && matches(AABB{*o, *o})) hits.push_back({e.id, kNoId});
    }
    ctx.doc.beginGroup("Select By Volume");
    ctx.doc.editPrimitives(removeTemplate, "Select By Volume");
    ctx.doc.endGroup();
    ctx.doc.deselectAll();
    for (const auto& r : hits) ctx.doc.select(r, true);
    return hits.size();
}

size_t selectConnected(EditorContext& ctx) {
    const MapFile& m = ctx.doc.map();
    std::map<std::string, std::vector<Id>> byName, byTarget;
    for (const auto& e : m.entities) {
        if (const std::string* tn = e.value("targetname")) byName[*tn].push_back(e.id);
        if (const std::string* t = e.value("target")) byTarget[*t].push_back(e.id);
    }
    std::set<Id> seen;
    std::vector<Id> todo;
    for (const auto& r : ctx.doc.selection()) todo.push_back(r.entity);
    while (!todo.empty()) {
        const Id id = todo.back();
        todo.pop_back();
        if (!seen.insert(id).second) continue;
        const Entity* e = ctx.doc.findEntity(id);
        if (!e) continue;
        if (const std::string* t = e->value("target"))
            for (Id n : byName[*t]) todo.push_back(n);
        if (const std::string* tn = e->value("targetname"))
            for (Id n : byTarget[*tn]) todo.push_back(n);
    }
    for (Id id : seen)
        if (const Entity* e = ctx.doc.findEntity(id); e && !e->isWorldspawn()) ctx.doc.select({id, kNoId}, true);
    return seen.size();
}

bool flipSelection(EditorContext& ctx, int axis) {
    const double about = selectionPivot(ctx)[axis];
    return transformSelection(ctx, "Flip", [&](const ConvexBrush& b) { return flipBrush(b, axis, about); }, [&](const Vec3& p) {
        Vec3 q = p;
        q[axis] = 2.0 * about - q[axis];
        return q;
    });
}

bool prismFromSelection(EditorContext& ctx, int sides) {
    std::vector<Document::PrimEdit> edits;
    for (auto& sb : selectedBrushes(ctx)) {
        const AABB box = brushBounds(sb.brush);
        const std::string suffix = sb.brush.empty() ? faceSuffix(ctx) : sb.brush.front().suffix;
        if (auto prim = rebuildBrush(*sb.source, makePrism(box, sides, 2, suffix), ctx.doc.eol())) edits.push_back({sb.entity, sb.item, *prim});
    }
    return ctx.doc.editPrimitives(edits, "Cylinder");
}

bool applyMaterialToSelection(EditorContext& ctx, const std::string& material) {
    std::vector<Document::PrimEdit> edits;
    if (!ctx.doc.faceSelection().empty()) {
        // Selected faces only, one undo step; the face selection stays.
        std::map<std::pair<Id, Id>, Primitive> work;
        for (const FaceRef& f : ctx.doc.faceSelection()) {
            const EntityItem* it = ctx.doc.findItem(f.entity, f.item);
            if (!it || !it->prim()) continue;
            auto w = work.find({f.entity, f.item});
            if (w == work.end()) w = work.emplace(std::make_pair(f.entity, f.item), *it->prim()).first;
            w->second.setMaterial(material, int(f.face));
        }
        for (auto& [key, p] : work) {
            const EntityItem* it = ctx.doc.findItem(key.first, key.second);
            if (it && it->prim() && p.text() != it->prim()->text()) edits.push_back({key.first, key.second, p});
        }
        return ctx.doc.editPrimitives(edits, "Apply " + material + " to faces", false);
    }
    // Selected models take the material too (misc_model, script_model, dynamic_prop: key iw8r_material, the same key a
    // material dropped on a model sets). Owner report 2026-11-11: a staircase of shape-model cubes kept its old texture.
    std::vector<Id> models;
    for (const auto& r : ctx.doc.selection()) {
        if (r.item == kNoId) {
            const Entity* e = ctx.doc.findEntity(r.entity);
            if (!e || material.empty()) continue;
            const std::string cls = e->classname();
            if (cls != "misc_model" && cls != "script_model" && cls != "dynamic_prop") continue;
            const std::string* cur = e->value("iw8r_material");
            if (!cur || *cur != material) models.push_back(r.entity);
            continue;
        }
        const EntityItem* it = ctx.doc.findItem(r.entity, r.item);
        if (!it || !it->prim()) continue;
        Primitive p = *it->prim();
        if (p.setMaterial(material)) edits.push_back({r.entity, r.item, p});
    }
    if (models.empty()) return ctx.doc.editPrimitives(edits, "Apply " + material);
    // One undo step for brushes and models together.
    ctx.doc.beginGroup("Apply " + material);
    bool changed = !edits.empty() && ctx.doc.editPrimitives(edits, "Apply " + material);
    for (Id id : models) changed = ctx.doc.setKeyValue(id, "iw8r_material", material) || changed;
    ctx.doc.endGroup();
    return changed;
}

std::string materialOfSelection(EditorContext& ctx) {
    for (const FaceRef& f : ctx.doc.faceSelection()) {
        const EntityItem* it = ctx.doc.findItem(f.entity, f.item);
        const Primitive* p = it ? it->prim() : nullptr;
        if (p && f.face < p->faces.size()) return p->faces[f.face].material();
    }
    for (const auto& r : ctx.doc.selection()) {
        const EntityItem* it = r.item ? ctx.doc.findItem(r.entity, r.item) : nullptr;
        const Primitive* p = it ? it->prim() : nullptr;
        if (!p) continue;
        if (p->kind == PrimKind::Patch) return p->patchMaterial;
        if (!p->faces.empty()) return p->faces.front().material();
    }
    return {};
}

namespace {
const QJsonObject& classes() {
    static const QJsonObject c = [] {
        QDir d(QCoreApplication::applicationDirPath());
        for (int up = 0; up < 3; ++up, d.cdUp()) {
            QFile f(d.filePath("data/iw8_entity_classes.json"));
            if (f.open(QIODevice::ReadOnly)) return QJsonDocument::fromJson(f.readAll()).object().value("classes").toObject();
        }
        return QJsonObject();
    }();
    return c;
}
}  // namespace

const QStringList& stockClassNames() {
    static const QStringList names = [] {
        QList<QPair<int, QString>> sorted;
        for (auto it = classes().begin(); it != classes().end(); ++it) sorted.append({it.value().toObject().value("count").toInt(), it.key()});
        std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
        QStringList out;
        for (const auto& s : sorted) out << s.second;
        return out;
    }();
    return names;
}

QStringList brushEntityClasses() {
    QStringList out;
    for (const QString& c : stockClassNames())
        if (c.startsWith("trigger_") || c == "script_brushmodel" || c == "collmap_scriptable" || c == "export_volume") out << c;
    for (const char* extra : {"func_group", "trigger_multiple", "trigger_use_touch", "trigger_hurt", "script_brushmodel"})
        if (!out.contains(extra)) out << extra;
    return out;
}

}  // namespace iw8r::qt
