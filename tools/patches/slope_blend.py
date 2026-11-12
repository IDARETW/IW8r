import io


def patch(p, pairs):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    for a, b in pairs:
        a = a.replace('\n', nl); b = b.replace('\n', nl)
        assert s.count(a) == 1, (p, s.count(a), a[:70])
        s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


patch('src/core/vertex_paint.h', [
 ('''// True when any control point of the patch has a colour block (painted or authored).''',
  '''// Roadmap 8.14: the slope blend of a terrain patch written into the vertex alpha (the layer blend of a layered
// material): alpha 0 where the surface is flatter than startDeg, 255 where it is steeper than endDeg, linear between
// (heightmap.h slopeBlendAlpha); `invert` swaps them. Red, green and blue stay. Returns how many control points
// changed; -1 when the patch is not a complete grid or the angles are not valid (endDeg > startDeg).
int applySlopeBlendAlpha(Primitive& patch, double startDeg, double endDeg, bool invert = false);

// True when any control point of the patch has a colour block (painted or authored).'''),
])
patch('src/core/vertex_paint.cpp', [
 ('''#include "patch_tess.h"
''', '''#include "heightmap.h"
#include "patch_ops.h"
#include "patch_tess.h"
'''),
])
s = io.open('src/core/vertex_paint.cpp', encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
end = s.rstrip().rfind('}')  # the namespace's closing brace
add = '''
int applySlopeBlendAlpha(Primitive& patch, double startDeg, double endDeg, bool invert) {
    if (patch.kind != PrimKind::Patch) return -1;
    const auto grid = patchGrid(patch);
    if (!grid) return -1;
    const std::vector<float> weights = slopeBlendAlpha(*grid, startDeg, endDeg);
    if (weights.size() != patch.verts.size()) return -1;
    const std::vector<Rgba8> colours = readPatchColours(patch);
    int changed = 0;
    for (size_t i = 0; i < weights.size(); ++i) {
        const double w = std::clamp(double(invert ? 1.0f - weights[i] : weights[i]), 0.0, 1.0);
        Rgba8 c = i < colours.size() ? colours[i] : kDefaultVertexRgba;
        const auto alpha = static_cast<std::uint8_t>(std::lround(w * 255.0));
        if (c[3] == alpha && hasPatchColour(patch, i)) continue;
        c[3] = alpha;
        if (setPatchVertexColour(patch, i, c)) ++changed;
    }
    return changed;
}

'''.replace('\n', nl)
s = s[:end] + add.lstrip(nl) + s[end:]
io.open('src/core/vertex_paint.cpp', 'w', encoding='utf-8', newline='').write(s)

patch('src/qtapp/tool_commands.h', [
 ('''bool erodeSelectedPatches(EditorContext& ctx, bool hydraulic, int iterations);''',
  '''bool erodeSelectedPatches(EditorContext& ctx, bool hydraulic, int iterations);
// Roadmap 8.14: the slope blend of every selected terrain patch written into its vertex alpha (one undo step).
// Returns how many patches changed.
int slopeBlendSelectedPatches(EditorContext& ctx, double startDeg, double endDeg, bool invert);'''),
])
patch('src/qtapp/tool_commands.cpp', [
 ('''bool capSelectedPatches(EditorContext& ctx) {''', '''int slopeBlendSelectedPatches(EditorContext& ctx, double startDeg, double endDeg, bool invert) {
    std::vector<Document::PrimEdit> edits;
    for (const auto& r : ctx.doc.selection()) {
        const EntityItem* it = r.item ? ctx.doc.findItem(r.entity, r.item) : nullptr;
        const Primitive* p = it ? it->prim() : nullptr;
        if (!p || p->kind != PrimKind::Patch) continue;
        Primitive copy = *p;
        if (applySlopeBlendAlpha(copy, startDeg, endDeg, invert) > 0) edits.push_back({r.entity, r.item, copy});
    }
    if (edits.empty() || !ctx.doc.editPrimitives(edits, "Slope Blend")) return 0;
    return int(edits.size());
}

bool capSelectedPatches(EditorContext& ctx) {'''),
])
patch('src/qtapp/main_window.cpp', [
 ('''        addCommand(hm, "TerrainErodeHydraulic", "Hydraulic Erosion (rain carves)", QKeySequence(), [this, patchCmd] {
            ctx_.scene.invalidate();
            patchCmd(erodeSelectedPatches(ctx_, true, 30), "Hydraulic erosion");
        });
''', '''        addCommand(hm, "TerrainErodeHydraulic", "Hydraulic Erosion (rain carves)", QKeySequence(), [this, patchCmd] {
            ctx_.scene.invalidate();
            patchCmd(erodeSelectedPatches(ctx_, true, 30), "Hydraulic erosion");
        });
        addCommand(hm, "TerrainSlopeBlend", "Blend By Slope (vertex alpha)...", QKeySequence(), [this] {
            bool ok = false;
            const double lo = QInputDialog::getDouble(this, "Blend By Slope", "Flat up to (degrees, alpha 0):", 25, 0, 89, 1, &ok);
            if (!ok) return;
            const double hi = QInputDialog::getDouble(this, "Blend By Slope", "Fully blended from (degrees, alpha 255):", 45, lo + 0.5, 90, 1, &ok);
            if (!ok) return;
            const int n = slopeBlendSelectedPatches(ctx_, lo, hi, false);
            if (!n) return ctx_.setStatus("Blend By Slope: select terrain patches first (nothing changed)");
            ctx_.scene.invalidate();
            ctx_.documentEdited(QString("Blend By Slope: %1 patch(es), vertex alpha from the slope (%2 to %3 degrees); the second layer of a "
                                        "layered material shows on the steep parts (one undo step)").arg(n).arg(lo).arg(hi));
        });
'''),
])
print('ok')
