import io

# ROADMAP 5.9 / 8.11, the open point "painting layer blends on brushes": a brush face has only its corners, so there
# is nothing to paint on it. One command puts a mesh patch with a control point every N units on the chosen faces
# (same material and texture placement), gives the covered faces the caulk material (the brush keeps its shape and
# its collision) and the patches can be painted with Patch > Vertex Paint Mode.

ROOT = 'E:/IW8-Radiant/'


def patch(p, a, b, n=1):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


H = 'src/core/convert_ops.h'
patch(H, '''#include <optional>
#include <vector>
''', '''#include <cstddef>
#include <optional>
#include <string>
#include <vector>
''')
patch(H, '''// Resamples a patch onto a cols x rows grid of control points''', '''// The flat patches of one face of a brush (see brushFacesToPatches for the shapes), each with a control point about
// every `spacing` units along its sides (2 to 65 a side; spacing <= 0 gives the corners only). Empty for a face that
// does not exist, has no winding, or (skipToolFaces) is a tool face.
std::vector<PatchGrid> brushFacePaintMeshes(const Primitive& brush, size_t face, double spacing, bool skipToolFaces);

// Paint meshes on brush faces (blend painting, ROADMAP 5.9 / 8.11): a brush face has only its corners, so a mesh patch
// with a control point about every `spacing` units is laid on it, textured like the face, and the face gets
// `caulkMaterial` so that only the patch is drawn. The brush keeps its shape and its collision.
// Faces: the document's face selection when there is one, else every face of the selected brushes that is not a tool
// face. One undo step; the new patches become the selection and the face selection is cleared. Returns the number of
// patches made (0: nothing changed).
size_t addPaintMeshesToBrushFaces(Document& doc, double spacing, const std::string& caulkMaterial = "caulk");

// Resamples a patch onto a cols x rows grid of control points''')

C = 'src/core/convert_ops.cpp'
patch(C, '''#include <array>
#include <cstddef>
''', '''#include <array>
#include <cmath>
#include <cstddef>
#include <map>
#include <set>
''')
patch(C, '''}  // namespace

std::vector<PatchGrid> brushFacesToPatches(const Primitive& brush, bool skipToolFaces, int subdiv) {
    std::vector<PatchGrid> out;
    if (brush.kind != PrimKind::Brush) return out;
    const int n = std::max(2, subdiv);
    const std::vector<Winding> windings = faceWindingsOf(brush);  // outward CCW about each face normal
    for (size_t f = 0; f < windings.size(); ++f) {
        const Winding w = dropCollinear(windings[f]);
        if (w.size() < 3) continue;  // degenerate or non-contributing face
        const std::string mat = brush.faces[f].material();
        if (skipToolFaces && (mat.empty() || isIw8ToolMaterial(mat))) continue;
        const auto plane = facePlane(brush, f);
        if (!plane) continue;
        const Vec3 normal = normalize(plane->n);
        // A face without texture fields gets the compiler's default (tex_projection.h), as the compiler does.
        const TexParams tp = texParamsOfFace(brush, f).value_or(TexParams{});

        if (w.size() == 4) {
            out.push_back(faceGrid({w[0], w[1], w[2], w[3]}, n, mat, normal, tp));
        } else if (w.size() == 3) {
            out.push_back(faceGrid({w[0], w[1], w[2], w[2]}, n, mat, normal, tp));
        } else {
            // Convex n-gon: fan from corner 0 into n - 2 triangle patches (each one exact, all on the face plane).
            for (size_t k = 1; k + 1 < w.size(); ++k)
                out.push_back(faceGrid({w[0], w[k], w[k + 1], w[k + 1]}, n, mat, normal, tp));
        }
    }
    return out;
}
''', '''// The patches of face f (its winding given), n x n control points each, appended to `out`.
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
    doc.clearSelection();
    for (const SelRef& r : keep) doc.select(r, true);
    return made;
}
''')
print('ok')
