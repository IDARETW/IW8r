// IW8 Radiant Suite - selection layout tools (roadmap 2.25). See layout_tools.h.
#include "layout_tools.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "brush_ops.h"
#include "patch_ops.h"
#include "tex_projection.h"

namespace iw8r {
namespace {

using BrushXf = std::function<ConvexBrush(const ConvexBrush&)>;
using PointXf = std::function<Vec3(const Vec3&)>;

// Float noise and -0 would otherwise be written into the map text.
double clean(double v) { return std::fabs(v) < 1e-9 ? 0.0 : v; }

double wrapDeg(double v) {
    v = std::fmod(v, 360.0);
    if (v < 0) v += 360.0;
    return clean(v);
}

std::string vecText(const Vec3& v) { return formatNumber(clean(v.x)) + " " + formatNumber(clean(v.y)) + " " + formatNumber(clean(v.z)); }

// Writes a key: replaces the existing line of that name, else appends one.
void setEntityKey(Entity& e, const std::string& key, const std::string& value, const std::string& eol) {
    for (auto& it : e.items)
        if (KeyValueLine* kv = it.kv(); kv && kv->name() == key) {
            kv->set(value);
            return;
        }
    EntityItem it;
    it.v = makeKeyValue(key, value, eol, e.usesCanonicalKeys());
    e.items.push_back(std::move(it));
}

// Angle slot of a rotation axis in "pitch yaw roll" (Z = yaw, Y = pitch, X = roll), as Arbitrary Rotation uses.
int angleSlot(int axis) { return axis == 2 ? 1 : (axis == 1 ? 0 : 2); }

// Edits a point entity's angles. `f` changes "pitch yaw roll" (the key is added when missing). An entity with only a
// yaw-only `angle` key gets `yawF` applied to that key instead. The Quake values -1 (up) and -2 (down) are not
// angles and are left alone.
void editEntityAngles(Entity& e, const std::string& eol, const std::function<void(Vec3&)>& f,
                      const std::function<double(double)>& yawF) {
    const std::string* angles = e.value("angles");
    const std::string* angle = e.value("angle");
    if (!angles && angle) {
        double yaw = 0;
        if (std::sscanf(angle->c_str(), "%lf", &yaw) != 1 || yaw == -1 || yaw == -2) return;
        const std::string text = formatNumber(wrapDeg(yawF(yaw)));
        setEntityKey(e, "angle", text, eol);
        return;
    }
    Vec3 ang;
    if (angles) std::sscanf(angles->c_str(), "%lf %lf %lf", &ang.x, &ang.y, &ang.z);
    f(ang);
    for (int i = 0; i < 3; ++i) ang[i] = wrapDeg(ang[i]);
    setEntityKey(e, "angles", vecText(ang), eol);
}

// Rotation of a point entity: origin about the pivot, angles advanced by `deg` about the axis.
void rotateEntity(Entity& e, int axis, double deg, const Vec3& pivot, const std::string& eol) {
    if (auto o = e.origin()) setEntityKey(e, "origin", vecText(rotatePoint(*o, axis, deg, pivot)), eol);
    const int slot = angleSlot(axis);
    editEntityAngles(
        e, eol, [=](Vec3& a) { a[slot] += deg; }, [=](double yaw) { return axis == 2 ? yaw + deg : yaw; });
}

// Mirror of a point entity across the plane axis == planeCoord. The origin is reflected. Yaw follows the forward
// direction (X plane: 180 - yaw; Y plane: -yaw; Z plane: unchanged), pitch changes only under a Z mirror, and roll
// is negated for every plane (the reflection reverses handedness).
void mirrorEntity(Entity& e, int axis, double planeCoord, const std::string& eol) {
    if (auto o = e.origin()) {
        Vec3 q = *o;
        q[axis] = 2.0 * planeCoord - q[axis];
        setEntityKey(e, "origin", vecText(q), eol);
    }
    editEntityAngles(
        e, eol,
        [axis](Vec3& a) {
            if (axis == 0) a.y = 180.0 - a.y;
            if (axis == 1) a.y = -a.y;
            if (axis == 2) a.x = -a.x;
            a.z = -a.z;
        },
        [axis](double yaw) { return axis == 0 ? 180.0 - yaw : (axis == 1 ? -yaw : yaw); });
}

// The primitive's text with its face lines replaced by those of `b`; the other lines of the brush are kept as
// written (the same rebuild the editor's brush tools do). Nullopt when `b` cannot be written as a brush.
std::optional<Primitive> rebuildBrushKeepingLines(const Primitive& original, const ConvexBrush& b, const std::string& eol) {
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

// The first line of a primitive when it is a `//` comment (patch rebuilds write it back), else "".
std::string leadingComment(const Primitive& p) {
    for (const TextLine& l : p.lines) {
        const size_t s = l.text.find_first_not_of(" \t");
        if (s == std::string::npos) continue;
        if (l.text.compare(s, 2, "//") == 0) return l.text.substr(s);
        break;
    }
    return "";
}

// Rotation or mirror of a copied brush or patch. `mirrored` reverses a patch's facing, so the reflected patch faces
// the reflected side (flipBrush does the same for brush faces). Texture lock follows the move as in Document.
bool transformPrimitive(Primitive& p, const BrushXf& brushXf, const PointXf& pointXf, bool mirrored, const std::string& eol,
                        bool textureLock) {
    if (p.kind == PrimKind::Brush) {
        auto out = rebuildBrushKeepingLines(p, brushXf(brushFromPrimitive(p)), eol);
        if (!out) return false;
        if (textureLock) lockPrimitiveTextures(p, *out, pointXf);
        p = std::move(*out);
        return true;
    }
    if (p.kind == PrimKind::Patch) {
        auto g = patchGrid(p);
        if (!g) return false;
        for (Vec3& v : g->pos) v = pointXf(v);
        if (mirrored) *g = invertPatch(*g);
        Primitive out = makePatchPrimitive(*g, eol, leadingComment(p));
        if (out.kind != PrimKind::Patch) return false;
        p = std::move(out);
        return true;
    }
    return false;
}

// Translation keeps the primitive text exactly (Primitive::translate); texture lock follows the move as in Document.
bool translatePrimitive(Primitive& p, const Vec3& d, bool textureLock) {
    const Primitive before = p;
    p.translate(d);
    if (textureLock && p.kind == PrimKind::Brush) lockPrimitiveTextures(before, p, [d](const Vec3& v) { return v + d; });
    return true;
}

// One object for align and distribute: the refs that move together, and their bounds.
struct LayoutObject {
    std::vector<SelRef> refs;
    AABB box;
};

// Selected objects: each selected point entity or brush entity (with all its brushes) and each selected worldspawn
// brush. A point entity's box is the 16-unit cube selectionBounds() uses around its origin.
std::vector<LayoutObject> selectedObjects(const Document& doc) {
    std::set<Id> entities;
    std::vector<SelRef> brushes;
    for (const SelRef& r : doc.selection()) {
        const Entity* e = doc.findEntity(r.entity);
        if (!e) continue;
        if (!e->isWorldspawn()) entities.insert(r.entity);
        else if (r.item != kNoId) brushes.push_back(r);
    }
    std::vector<LayoutObject> out;
    for (Id id : entities) {
        const Entity* e = doc.findEntity(id);
        LayoutObject o;
        for (const auto& it : e->items)
            if (const Primitive* p = it.prim()) {
                o.refs.push_back({id, it.id});
                o.box.add(p->bounds());
            }
        if (o.refs.empty()) {
            auto origin = e->origin();
            if (!origin) continue;
            o.refs.push_back({id, kNoId});
            o.box.add(*origin - Vec3{8, 8, 8});
            o.box.add(*origin + Vec3{8, 8, 8});
        }
        out.push_back(std::move(o));
    }
    for (const SelRef& r : brushes) {
        const EntityItem* it = doc.findItem(r.entity, r.item);
        const Primitive* p = it ? it->prim() : nullptr;
        if (!p) continue;
        LayoutObject o;
        o.refs.push_back(r);
        o.box = p->bounds();
        out.push_back(std::move(o));
    }
    return out;
}

double edgeOf(const AABB& b, int axis, AlignMode mode) {
    if (mode == AlignMode::Min) return b.mins[axis];
    if (mode == AlignMode::Max) return b.maxs[axis];
    return 0.5 * (b.mins[axis] + b.maxs[axis]);
}

double extent(const LayoutObject& o, int axis) { return o.box.maxs[axis] - o.box.mins[axis]; }

void addMove(std::vector<std::pair<SelRef, Vec3>>& moves, const LayoutObject& o, const Vec3& d) {
    for (const SelRef& r : o.refs) moves.push_back({r, d});
}

}  // namespace

size_t arrayDuplicate(Document& doc, const Vec3& step, int count) {
    if (count < 1) return 0;
    const bool lock = doc.textureLock;
    const std::string eol = doc.eol();
    std::vector<DuplicateTransform> copies;
    for (int k = 1; k <= count; ++k) {
        const Vec3 d = step * static_cast<double>(k);
        DuplicateTransform x;
        x.prim = [d, lock](Primitive& p) { return translatePrimitive(p, d, lock); };
        x.ent = [d, eol](Entity& e) {
            if (auto o = e.origin()) setEntityKey(e, "origin", vecText(*o + d), eol);
            return true;
        };
        copies.push_back(std::move(x));
    }
    return doc.duplicateSelection(copies, "Array Duplicate");
}

size_t radialDuplicate(Document& doc, const Vec3& center, int axis, int count, double totalDegrees) {
    if (count < 1 || axis < 0 || axis > 2 || !(totalDegrees > 0)) return 0;
    // Full circle: count + 1 slots around the circle. Arc: count + 1 objects including both ends.
    const bool full = totalDegrees >= 360.0;
    const double step = full ? 360.0 / (count + 1) : totalDegrees / count;
    const bool lock = doc.textureLock;
    const std::string eol = doc.eol();
    std::vector<DuplicateTransform> copies;
    for (int k = 1; k <= count; ++k) {
        const double deg = step * k;
        DuplicateTransform x;
        x.prim = [=](Primitive& p) {
            return transformPrimitive(
                p, [=](const ConvexBrush& b) { return rotateBrush(b, axis, deg, center); },
                [=](const Vec3& v) { return rotatePoint(v, axis, deg, center); }, false, eol, lock);
        };
        x.ent = [=](Entity& e) {
            rotateEntity(e, axis, deg, center, eol);
            return true;
        };
        copies.push_back(std::move(x));
    }
    return doc.duplicateSelection(copies, "Radial Duplicate");
}

bool mirrorClone(Document& doc, int axis, double planeCoord) {
    if (axis < 0 || axis > 2) return false;
    const bool lock = doc.textureLock;
    const std::string eol = doc.eol();
    const PointXf reflect = [axis, planeCoord](const Vec3& v) {
        Vec3 q = v;
        q[axis] = 2.0 * planeCoord - q[axis];
        return q;
    };
    DuplicateTransform x;
    x.prim = [=](Primitive& p) {
        return transformPrimitive(
            p, [=](const ConvexBrush& b) { return flipBrush(b, axis, planeCoord); }, reflect, true, eol, lock);
    };
    x.ent = [=](Entity& e) {
        mirrorEntity(e, axis, planeCoord, eol);
        return true;
    };
    return doc.duplicateSelection({x}, "Mirror Clone") > 0;
}

bool alignSelection(Document& doc, int axis, AlignMode mode) {
    if (axis < 0 || axis > 2) return false;
    const auto objs = selectedObjects(doc);
    if (objs.empty()) return false;
    AABB all;
    for (const auto& o : objs) all.add(o.box);
    const double target = edgeOf(all, axis, mode);
    std::vector<std::pair<SelRef, Vec3>> moves;
    for (const auto& o : objs) {
        Vec3 d;
        d[axis] = clean(target - edgeOf(o.box, axis, mode));
        addMove(moves, o, d);
    }
    return doc.moveRefs(moves, "Align");
}

bool distributeSelection(Document& doc, int axis) {
    if (axis < 0 || axis > 2) return false;
    auto objs = selectedObjects(doc);
    if (objs.size() < 3) return false;
    std::stable_sort(objs.begin(), objs.end(), [axis](const LayoutObject& a, const LayoutObject& b) {
        return edgeOf(a.box, axis, AlignMode::Center) < edgeOf(b.box, axis, AlignMode::Center);
    });
    // The first and last objects stay. The n - 1 gaps share the space left after the middle objects' extents.
    const LayoutObject& first = objs.front();
    const LayoutObject& last = objs.back();
    double middle = 0;
    for (size_t i = 1; i + 1 < objs.size(); ++i) middle += extent(objs[i], axis);
    const double gap = (last.box.mins[axis] - first.box.maxs[axis] - middle) / static_cast<double>(objs.size() - 1);
    std::vector<std::pair<SelRef, Vec3>> moves;
    double cursor = first.box.maxs[axis];
    for (size_t i = 1; i + 1 < objs.size(); ++i) {
        const double newMin = cursor + gap;
        Vec3 d;
        d[axis] = clean(newMin - objs[i].box.mins[axis]);
        addMove(moves, objs[i], d);
        cursor = newMin + extent(objs[i], axis);
    }
    return doc.moveRefs(moves, "Distribute");
}

}  // namespace iw8r
