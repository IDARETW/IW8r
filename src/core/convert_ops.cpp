// IW8 Radiant Suite - brush to patch and patch to terrain conversions (see convert_ops.h).
#include "convert_ops.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <map>
#include <set>
#include <string>
#include <utility>

#include "brush_ops.h"
#include "compile_iw8.h"
#include "document.h"
#include "tex_projection.h"

namespace iw8r {

namespace {

constexpr int kConvertSubdiv = 2;  // a converted face is just its own corners; no interior control points needed

// Bilinear point of a quad. q[0] is (s, t) = (0, 0), q[1] = (1, 0), q[2] = (1, 1), q[3] = (0, 1).
// Bilinear weights of coplanar corners stay in their plane, so the point is on the face.
Vec3 quadPoint(const std::array<Vec3, 4>& q, double s, double t) {
    return q[0] * ((1 - s) * (1 - t)) + q[1] * (s * (1 - t)) + q[2] * (s * t) + q[3] * ((1 - s) * t);
}

// n x n mesh grid over the corners q (CCW about the face normal). Row 0 holds q[0] .. q[1] along the columns and the
// last row holds q[3] .. q[2]. So cross(column step, row step) points along the normal, which is the facing the
// tessellator uses. A repeated corner (q[2] == q[3]) gives a triangle: its last row collapses to one point.
PatchGrid faceGrid(const std::array<Vec3, 4>& q, int n, const std::string& material, const Vec3& normal,
                   const TexParams& tp) {
    PatchGrid g;
    g.rows = n;
    g.cols = n;
    g.keyword = "mesh";  // triangulated directly, no curve smoothing: the triangles stay on the face
    g.material = material;
    g.pos.assign(static_cast<size_t>(n) * static_cast<size_t>(n), Vec3{});
    g.st.assign(g.pos.size(), {0.0, 0.0});
    for (int r = 0; r < n; ++r)
        for (int c = 0; c < n; ++c) {
            const double s = static_cast<double>(c) / (n - 1);
            const double t = static_cast<double>(r) / (n - 1);
            const size_t i = static_cast<size_t>(r) * static_cast<size_t>(n) + static_cast<size_t>(c);
            g.pos[i] = quadPoint(q, s, t);
            g.st[i] = faceUV(normal, tp, g.pos[i]);  // the brush face's own UVs at this point
        }
    return g;
}

// Drops corners that lie on the straight line through their neighbours. They add no area and would make
// zero-area fan triangles.
Winding dropCollinear(Winding w) {
    bool removed = true;
    while (removed && w.size() > 3) {
        removed = false;
        for (size_t i = 0; i < w.size() && w.size() > 3; ++i) {
            const Vec3& prev = w[(i + w.size() - 1) % w.size()];
            const Vec3& cur = w[i];
            const Vec3& next = w[(i + 1) % w.size()];
            const double a = length(cur - prev), b = length(next - cur);
            if (a > 0 && b > 0 && length(cross(cur - prev, next - cur)) <= 1e-7 * a * b) {
                w.erase(w.begin() + static_cast<std::ptrdiff_t>(i));
                removed = true;
                break;
            }
        }
    }
    return w;
}

double faceArea(const Winding& w) {
    double area = 0;
    for (size_t k = 1; k + 1 < w.size(); ++k) area += 0.5 * length(cross(w[k] - w[0], w[k + 1] - w[0]));
    return area;
}

// The patches of face f (its winding given), n x n control points each, appended to `out`.
void appendFacePatches(const Primitive& brush, size_t f, const Winding& winding, bool skipToolFaces, int n, std::vector<PatchGrid>& out) {
    const Winding w = dropCollinear(winding);
    if (w.size() < 3) return;  // degenerate or non-contributing face
    const std::string mat = brush.faces[f].material();
    if (skipToolFaces && (mat.empty() || isIw8ToolMaterial(mat))) return;
    const auto plane = facePlane(brush, f);
    if (!plane) return;
    const Vec3 normal = normalize(plane->n);
    // A face without texture fields gets the compiler's default (tex_projection.h), as the compiler does.
    const TexParams tp = texParamsOfFace(brush, f).value_or(TexParams{});

    if (w.size() == 4) {
        out.push_back(faceGrid({w[0], w[1], w[2], w[3]}, n, mat, normal, tp));
    } else if (w.size() == 3) {
        out.push_back(faceGrid({w[0], w[1], w[2], w[2]}, n, mat, normal, tp));
    } else {
        // Convex n-gon: fan from corner 0 into n - 2 triangle patches (each one exact, all on the face plane).
        for (size_t k = 1; k + 1 < w.size(); ++k) out.push_back(faceGrid({w[0], w[k], w[k + 1], w[k + 1]}, n, mat, normal, tp));
    }
}

}  // namespace

std::vector<PatchGrid> brushFacesToPatches(const Primitive& brush, bool skipToolFaces, int subdiv) {
    std::vector<PatchGrid> out;
    if (brush.kind != PrimKind::Brush) return out;
    const int n = std::max(2, subdiv);
    const std::vector<Winding> windings = faceWindingsOf(brush);  // outward CCW about each face normal
    for (size_t f = 0; f < windings.size(); ++f) appendFacePatches(brush, f, windings[f], skipToolFaces, n, out);
    return out;
}

std::vector<PatchGrid> brushFacePaintMeshes(const Primitive& brush, size_t face, double spacing, bool skipToolFaces) {
    std::vector<PatchGrid> corners, out;
    if (brush.kind != PrimKind::Brush) return out;
    const std::vector<Winding> windings = faceWindingsOf(brush);
    if (face >= windings.size() || face >= brush.faces.size()) return out;
    appendFacePatches(brush, face, windings[face], skipToolFaces, 2, corners);
    for (const PatchGrid& g : corners) {
        // Control points along each side by its longer edge. Row 0 is corners 0 -> 1, the last row 3 -> 2.
        const auto points = [&](double len) {
            if (!(spacing > 0)) return 2;
            return std::clamp(int(std::ceil(len / spacing - 1e-9)) + 1, 2, 65);
        };
        const int cols = points(std::max(length(g.pos[1] - g.pos[0]), length(g.pos[3] - g.pos[2])));
        const int rows = points(std::max(length(g.pos[2] - g.pos[0]), length(g.pos[3] - g.pos[1])));
        if (auto dense = patchToTerrain(g, cols, rows)) out.push_back(std::move(*dense));
    }
    return out;
}

size_t addPaintMeshesToBrushFaces(Document& doc, double spacing, const std::string& caulkMaterial) {
    // The faces to cover, per brush: the face selection, else every face of the selected brushes.
    std::map<std::pair<Id, Id>, std::set<uint32_t>> chosen;
    const bool byFace = !doc.faceSelection().empty();
    if (byFace) {
        for (const FaceRef& f : doc.faceSelection()) chosen[{f.entity, f.item}].insert(f.face);
    } else {
        for (const SelRef& r : doc.selection())
            if (r.item != kNoId) chosen[{r.entity, r.item}];
    }
    std::vector<Document::PrimEdit> edits;
    size_t made = 0;
    for (const auto& [key, faces] : chosen) {
        const EntityItem* it = doc.findItem(key.first, key.second);
        const Primitive* brush = it ? it->prim() : nullptr;
        if (!brush || brush->kind != PrimKind::Brush) continue;
        Primitive covered = *brush;
        std::vector<Document::PrimEdit> patches;
        for (size_t f = 0; f < brush->faces.size(); ++f) {
            if (byFace && !faces.count(uint32_t(f))) continue;
            // A chosen face is covered even when it is a tool face only if it was picked by hand.
            const std::vector<PatchGrid> grids = brushFacePaintMeshes(*brush, f, spacing, !byFace);
            if (grids.empty()) continue;
            size_t here = 0;
            for (const PatchGrid& g : grids) {
                Primitive p = makePatchPrimitive(g, doc.eol(), "");
                if (p.kind == PrimKind::Patch) patches.push_back({key.first, kNoId, std::move(p)}), ++here;
            }
            if (here) covered.setMaterial(caulkMaterial, int(f));
        }
        if (patches.empty()) continue;
        made += patches.size();
        edits.push_back({key.first, key.second, std::move(covered)});
        edits.insert(edits.end(), patches.begin(), patches.end());
    }
    if (edits.empty() || !doc.editPrimitives(edits, "Paint Mesh On Faces")) return 0;
    // The patches are what gets painted: they are the selection, not the brushes under them.
    std::vector<SelRef> keep;
    for (const SelRef& r : doc.selection()) {
        const EntityItem* it = doc.findItem(r.entity, r.item);
        if (it && it->prim() && it->prim()->kind == PrimKind::Patch) keep.push_back(r);
    }
    doc.deselectAll();
    doc.clearFaceSelection();
    for (const SelRef& r : keep) doc.select(r, true);
    return made;
}

bool convertSelectedBrushesToPatches(Document& doc, bool keepBrush, bool skipToolFaces) {
    std::vector<Document::PrimEdit> edits;
    for (const SelRef& r : doc.selection()) {
        if (r.item == kNoId) continue;  // point entities have no brush
        const EntityItem* it = doc.findItem(r.entity, r.item);
        const Primitive* brush = it ? it->prim() : nullptr;
        if (!brush || brush->kind != PrimKind::Brush) continue;

        std::vector<Document::PrimEdit> made;
        for (const PatchGrid& g : brushFacesToPatches(*brush, skipToolFaces, kConvertSubdiv)) {
            Primitive p = makePatchPrimitive(g, doc.eol(), "");
            if (p.kind == PrimKind::Patch) made.push_back({r.entity, kNoId, std::move(p)});
        }
        if (made.empty()) continue;  // nothing to show: keep the brush rather than delete it for nothing
        if (!keepBrush) edits.push_back({r.entity, r.item, std::nullopt});
        edits.insert(edits.end(), made.begin(), made.end());
    }
    if (edits.empty()) return false;
    return doc.editPrimitives(edits, "Brush To Patch");
}

std::optional<PatchGrid> patchToTerrain(const PatchGrid& g, int cols, int rows) {
    if (cols < 2 || rows < 2 || g.rows < 2 || g.cols < 2) return std::nullopt;
    if (g.pos.size() != static_cast<size_t>(g.rows) * static_cast<size_t>(g.cols)) return std::nullopt;
    const bool hasSt = g.st.size() == g.pos.size();
    auto at = [&](int r, int c) { return static_cast<size_t>(r) * static_cast<size_t>(g.cols) + static_cast<size_t>(c); };

    PatchGrid out;
    out.rows = rows;
    out.cols = cols;
    out.keyword = "mesh";  // Create Terrain's grid is a "mesh" patch too
    out.material = g.material;
    out.pos.assign(static_cast<size_t>(rows) * static_cast<size_t>(cols), Vec3{});
    out.st.assign(out.pos.size(), {0.0, 0.0});
    for (int r = 0; r < rows; ++r)
        for (int c = 0; c < cols; ++c) {
            // Fractional source position of this sample, then the cell it falls in.
            const double v = static_cast<double>(r) / (rows - 1) * (g.rows - 1);
            const double u = static_cast<double>(c) / (cols - 1) * (g.cols - 1);
            const int r0 = std::min(static_cast<int>(v), g.rows - 2);
            const int c0 = std::min(static_cast<int>(u), g.cols - 2);
            const double fv = v - r0, fu = u - c0;
            const size_t a = at(r0, c0), b = at(r0, c0 + 1), d = at(r0 + 1, c0), e = at(r0 + 1, c0 + 1);
            const double w00 = (1 - fu) * (1 - fv), w10 = fu * (1 - fv), w01 = (1 - fu) * fv, w11 = fu * fv;
            const size_t i = static_cast<size_t>(r) * static_cast<size_t>(cols) + static_cast<size_t>(c);
            out.pos[i] = g.pos[a] * w00 + g.pos[b] * w10 + g.pos[d] * w01 + g.pos[e] * w11;
            if (hasSt)
                for (size_t k = 0; k < 2; ++k)
                    out.st[i][k] = g.st[a][k] * w00 + g.st[b][k] * w10 + g.st[d][k] * w01 + g.st[e][k] * w11;
        }
    return out;
}

bool convertSelectedPatchesToTerrain(Document& doc, int cols, int rows) {
    std::vector<Document::PrimEdit> edits;
    for (const SelRef& r : doc.selection()) {
        if (r.item == kNoId) continue;
        const EntityItem* it = doc.findItem(r.entity, r.item);
        const Primitive* prim = it ? it->prim() : nullptr;
        if (!prim || prim->kind != PrimKind::Patch) continue;
        const auto src = patchGrid(*prim);
        if (!src) continue;
        const auto terrain = patchToTerrain(*src, cols, rows);
        if (!terrain) continue;
        Primitive p = makePatchPrimitive(*terrain, doc.eol(), "");
        if (p.kind != PrimKind::Patch) continue;
        edits.push_back({r.entity, r.item, std::nullopt});
        edits.push_back({r.entity, kNoId, std::move(p)});
    }
    if (edits.empty()) return false;
    return doc.editPrimitives(edits, "Patch To Terrain");
}

}  // namespace iw8r
