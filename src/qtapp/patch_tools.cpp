#include "patch_tools.h"
#include "lightmap_fields.h"

#include <algorithm>
#include <cmath>
#include <map>

#include "brush_tools.h"

namespace iw8r::qt {

namespace {

struct SelectedPatch {
    Id entity = kNoId, item = kNoId;
    const Primitive* prim = nullptr;
    PatchGrid grid;
};

std::vector<SelectedPatch> selectedPatches(EditorContext& ctx) {
    std::vector<SelectedPatch> out;
    for (const auto& r : ctx.doc.selection()) {
        const EntityItem* it = r.item ? ctx.doc.findItem(r.entity, r.item) : nullptr;
        const Primitive* p = it ? it->prim() : nullptr;
        if (!p || p->kind != PrimKind::Patch) continue;
        if (auto g = patchGrid(*p)) out.push_back({r.entity, r.item, p, std::move(*g)});
    }
    return out;
}

// Leading "//" comment of a primitive (kept when the patch is regenerated).
std::string leadingComment(const Primitive& p) {
    for (const auto& l : p.lines) {
        const auto first = l.text.find_first_not_of(" \t");
        if (first == std::string::npos) continue;
        if (l.text.compare(first, 2, "//") == 0) return l.text;
        break;
    }
    return {};
}

std::optional<Primitive> rebuilt(const SelectedPatch& sp, const PatchGrid& g, const std::string& eol) {
    Primitive p = makePatchPrimitive(g, eol, leadingComment(*sp.prim));
    if (p.lines.empty()) return std::nullopt;
    reparsePrimitive(p);
    if (p.kind != PrimKind::Patch) return std::nullopt;
    return p;
}

}  // namespace

size_t selectedPatchCount(EditorContext& ctx) { return selectedPatches(ctx).size(); }

bool createPatch(EditorContext& ctx, PatchShape shape) {
    Entity* w = ctx.doc.worldspawn();
    if (!w) return false;
    std::vector<Document::PrimEdit> edits;
    AABB box;
    for (const auto& sb : selectedBrushes(ctx)) {
        box.add(brushBounds(sb.brush));
        edits.push_back({sb.entity, sb.item, std::nullopt});  // Radiant replaces the source brushes
    }
    if (!box.valid()) {
        const double size = std::max(64.0, ctx.doc.grid() * 8);
        Vec3 c;
        c[ctx.axisH()] = snapToGrid(ctx.gridCenterH, ctx.doc.grid());
        c[ctx.axisV()] = snapToGrid(ctx.gridCenterV, ctx.doc.grid());
        box.add(c - Vec3{size / 2, size / 2, size / 2});
        box.add(c + Vec3{size / 2, size / 2, size / 2});
        if (ctx.axisD() == 2 || shape != PatchShape::Flat3x3) {
            box.mins.z = c.z;
            box.maxs.z = c.z + size;
        }
    }
    PatchGrid g;
    const std::string& mat = ctx.currentMaterial;
    switch (shape) {
        case PatchShape::Flat3x3: g = makeFlatPatch(box, ctx.axisD(), 3, 3, mat); break;
        case PatchShape::Flat5x5: g = makeFlatPatch(box, ctx.axisD(), 5, 5, mat); break;
        case PatchShape::Cylinder: g = makeCylinderPatch(box, mat); break;
        case PatchShape::Bevel: g = makeBevelPatch(box, mat); break;
        case PatchShape::Endcap: g = makeEndcapPatch(box, mat); break;
    }
    Primitive p = makePatchPrimitive(g, ctx.doc.eol(), "");
    if (p.lines.empty()) return false;
    reparsePrimitive(p);
    // The new patch goes to worldspawn; replaced brushes may have belonged to another entity.
    edits.push_back({w->id, kNoId, p});
    const char* names[] = {"Create Patch Mesh", "Create Patch Mesh", "Create Cylinder Patch", "Create Bevel Patch", "Create Endcap Patch"};
    return ctx.doc.editPrimitives(edits, names[int(shape)]);
}

bool editSelectedPatches(EditorContext& ctx, const std::function<std::optional<PatchGrid>(const PatchGrid&)>& f,
                         const std::string& name) {
    std::vector<Document::PrimEdit> edits;
    for (const auto& sp : selectedPatches(ctx)) {
        auto g = f(sp.grid);
        if (!g) continue;
        if (auto p = rebuilt(sp, *g, ctx.doc.eol())) edits.push_back({sp.entity, sp.item, *p});
    }
    return ctx.doc.editPrimitives(edits, name);
}

bool invertSelectedPatches(EditorContext& ctx) {
    return editSelectedPatches(ctx, [](const PatchGrid& g) { return std::optional<PatchGrid>(invertPatch(g)); }, "Invert Patch");
}

bool transposeSelectedPatches(EditorContext& ctx) {
    return editSelectedPatches(ctx, [](const PatchGrid& g) { return std::optional<PatchGrid>(transposePatch(g)); }, "Transpose Patch");
}

bool naturalTextureSelectedPatches(EditorContext& ctx) {
    return editSelectedPatches(ctx, [](const PatchGrid& g) { return std::optional<PatchGrid>(naturalTexture(g)); }, "Natural Patch Texture");
}

bool insertPatchRowCol(EditorContext& ctx, bool row) {
    return editSelectedPatches(
        ctx,
        [row](const PatchGrid& g) -> std::optional<PatchGrid> {
            const int n = row ? g.rows : g.cols;
            if (n < 3 || n + 2 > 31) return std::nullopt;
            PatchGrid out = insertRowCol(g, row, n / 2);
            if ((row ? out.rows : out.cols) == n) return std::nullopt;
            return out;
        },
        row ? "Insert Patch Row" : "Insert Patch Column");
}

bool removePatchRowCol(EditorContext& ctx, bool row) {
    return editSelectedPatches(
        ctx,
        [row](const PatchGrid& g) -> std::optional<PatchGrid> {
            const int n = row ? g.rows : g.cols;
            if (n < 5) return std::nullopt;
            PatchGrid out = removeRowCol(g, row, n / 2);
            if ((row ? out.rows : out.cols) == n) return std::nullopt;
            return out;
        },
        row ? "Delete Patch Row" : "Delete Patch Column");
}

bool thickenSelectedPatches(EditorContext& ctx, double amount, bool seams) {
    Entity* w = ctx.doc.worldspawn();
    if (!w) return false;
    std::vector<Document::PrimEdit> edits;
    for (const auto& sp : selectedPatches(ctx)) {
        std::vector<PatchGrid> sides;
        PatchGrid copy = thickenPatch(sp.grid, amount, seams ? &sides : nullptr);
        sides.insert(sides.begin(), std::move(copy));
        for (const PatchGrid& g : sides) {
            Primitive p = makePatchPrimitive(g, ctx.doc.eol(), "");
            if (p.lines.empty()) continue;
            reparsePrimitive(p);
            edits.push_back({sp.entity, kNoId, p});
        }
    }
    return ctx.doc.editPrimitives(edits, "Thicken Patch");
}

std::vector<VertexHandle> selectionVertexHandles(EditorContext& ctx) {
    std::vector<VertexHandle> out;
    for (const auto& r : ctx.doc.selection()) {
        const EntityItem* it = r.item ? ctx.doc.findItem(r.entity, r.item) : nullptr;
        const Primitive* p = it ? it->prim() : nullptr;
        if (!p) continue;
        if (p->kind == PrimKind::Patch) {
            if (auto g = patchGrid(*p)) {
                const auto lk = ctx.patchLocks.find({r.entity, r.item});
                for (size_t i = 0; i < g->pos.size(); ++i) {
                    const bool locked = lk != ctx.patchLocks.end() && i < lk->second.size() && lk->second[i];
                    out.push_back({g->pos[i], r.entity, r.item, true, i, locked});
                }
            }
        } else if (p->kind == PrimKind::Brush) {
            for (const Vec3& v : brushVertices(*p)) out.push_back({v, r.entity, r.item, false, 0});
        }
    }
    return out;
}

bool moveVertexHandles(EditorContext& ctx, const std::vector<VertexHandle>& handles, const Vec3& delta, size_t* rejected,
                       size_t* lockedSkipped) {
    if (rejected) *rejected = 0;
    if (lockedSkipped) *lockedSkipped = 0;
    if (length(delta) < 1e-9 || handles.empty()) return false;
    // Group by primitive: patches move control-point tokens in place, brushes are rebuilt from moved corners.
    std::map<std::pair<Id, Id>, std::vector<const VertexHandle*>> byPrim;
    for (const auto& h : handles) byPrim[{h.entity, h.item}].push_back(&h);
    std::vector<Document::PrimEdit> edits;
    for (const auto& [key, hs] : byPrim) {
        const EntityItem* it = ctx.doc.findItem(key.first, key.second);
        const Primitive* src = it ? it->prim() : nullptr;
        if (!src) continue;
        if (src->kind == PrimKind::Patch) {
            Primitive p = *src;
            std::vector<size_t> idx;
            const auto lk = ctx.patchLocks.find(key);
            for (const VertexHandle* h : hs) {
                if (lk != ctx.patchLocks.end() && h->index < lk->second.size() && lk->second[h->index]) {
                    if (lockedSkipped) ++*lockedSkipped;
                    continue;  // locked control point (2.21)
                }
                idx.push_back(h->index);
            }
            if (idx.empty()) continue;
            if (movePatchVerts(p, idx, delta)) edits.push_back({key.first, key.second, p});
        } else if (src->kind == PrimKind::Brush) {
            std::vector<Vec3> from;
            for (const VertexHandle* h : hs) from.push_back(h->pos);
            auto moved = moveBrushVerts(*src, from, delta, ctx.doc.eol());
            if (!moved) {
                if (rejected) ++*rejected;
                continue;
            }
            edits.push_back({key.first, key.second, *moved});
        }
    }
    return ctx.doc.editPrimitives(edits, "Move Vertices");
}

size_t lockSelectedPatchVertices(EditorContext& ctx, PatchLockMode mode) {
    size_t n = 0;
    for (const auto& sp : selectedPatches(ctx)) {
        const std::pair<Id, Id> key{sp.entity, sp.item};
        if (mode == PatchLockMode::Unlock) {
            n += ctx.patchLocks.erase(key);
            continue;
        }
        std::vector<bool> add = mode == PatchLockMode::Border ? lockBorder(sp.grid) : lockCorners(sp.grid);
        if (add.empty()) continue;
        auto& cur = ctx.patchLocks[key];
        cur.resize(add.size(), false);
        for (size_t i = 0; i < add.size(); ++i) cur[i] = cur[i] || add[i];
        ++n;
    }
    return n;
}

std::optional<PatchGrid> firstSelectedPatchGrid(EditorContext& ctx) {
    auto sp = selectedPatches(ctx);
    if (sp.empty()) return std::nullopt;
    return sp.front().grid;
}

bool applyPatchPoints(EditorContext& ctx, const std::vector<std::array<double, 5>>& points) {
    auto sp = selectedPatches(ctx);
    if (sp.empty() || points.size() != sp.front().grid.pos.size()) return false;
    PatchGrid g = sp.front().grid;
    bool changed = false;
    for (size_t i = 0; i < points.size(); ++i) {
        const Vec3 p{points[i][0], points[i][1], points[i][2]};
        const std::array<double, 2> st{points[i][3], points[i][4]};
        if (length(p - g.pos[i]) > 1e-9 || std::fabs(st[0] - g.st[i][0]) > 1e-9 || std::fabs(st[1] - g.st[i][1]) > 1e-9) changed = true;
        g.pos[i] = p;
        if (i < g.st.size()) g.st[i] = st;
    }
    if (!changed) return false;
    auto prim = rebuilt(sp.front(), g, ctx.doc.eol());
    if (!prim) return false;
    return ctx.doc.editPrimitives({{sp.front().entity, sp.front().item, *prim}}, "Patch Inspector");
}

}  // namespace iw8r::qt
