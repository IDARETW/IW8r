import io

# Ink outlines: the document-level step moves into core (applyInkOutlines), shared by Tools > Toon Ink Outlines and the
# --ink-outlines command line; the ink material writer moves to brush_tools (writeInkMaterial).


def patch(p, a, b, n=1):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


H = 'E:/IW8-Radiant/src/core/toon_outline.h'
patch(H, '''#include "brush_ops.h"
''', '''#include "brush_ops.h"
#include "document.h"
''')
patch(H, '''}  // namespace iw8r
''', '''// The ink group's iw8r_note and material.
inline constexpr const char* kInkNote = "toon ink outlines";
inline constexpr const char* kInkMaterial = "iw8r_ink";

struct InkResult {
    size_t brushes = 0;          // brushes outlined
    size_t strips = 0;           // strips written
    bool fromSelection = false;  // the selected brushes (else every worldspawn / func_group brush)
    bool replaced = false;       // an earlier ink group was removed
    bool capped = false;         // stopped at maxStrips
};

// Ink strips along the hard edges of the selected brushes, or of every worldspawn / func_group brush when no brush is
// selected, as one func_group (iw8r_note kInkNote, material kInkMaterial, non-colliding) that replaces an earlier ink
// group. One undo step; nothing changes when there are no edges.
InkResult applyInkOutlines(Document& doc, const InkOptions& opt, size_t maxStrips = 40000);

}  // namespace iw8r
''')

C = 'E:/IW8-Radiant/src/core/toon_outline.cpp'
patch(C, '''std::optional<Primitive> inkStripPrimitive(''', '''InkResult applyInkOutlines(Document& doc, const InkOptions& opt, size_t maxStrips) {
    InkResult r;
    std::vector<ConvexBrush> brushes;
    Id oldInk = kNoId;
    for (const auto& e : doc.map().entities)
        if (const std::string* note = e.value("iw8r_note"); note && *note == kInkNote) oldInk = e.id;
    const auto addBrush = [&](const Primitive* p) {
        if (!p || p->kind != PrimKind::Brush) return;
        ConvexBrush b = brushFromPrimitive(*p);
        if (b.empty() || inkFaceMaterial(b.front().suffix) == kInkMaterial) return;
        brushes.push_back(std::move(b));
    };
    for (const auto& s : doc.selection()) {
        if (s.item == kNoId || s.entity == oldInk) continue;
        const EntityItem* it = doc.findItem(s.entity, s.item);
        const Primitive* p = it ? it->prim() : nullptr;
        if (!p || p->kind != PrimKind::Brush) continue;
        addBrush(p);
        r.fromSelection = true;
    }
    if (!r.fromSelection)
        for (const auto& e : doc.map().entities) {
            const std::string c = e.classname();
            if (e.id == oldInk || (c != "worldspawn" && c != "func_group")) continue;
            for (const auto& it : e.items) addBrush(it.prim());
        }
    r.brushes = brushes.size();
    const std::string suffix = std::string(" ") + kInkMaterial + " 64 64 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0";
    std::vector<Primitive> strips;
    for (const InkEdge& e : uniqueInkEdges(brushes, opt)) {
        if (strips.size() >= maxStrips) {
            r.capped = true;
            break;
        }
        if (auto p = inkStripPrimitive(e, opt, suffix, doc.map().eol)) strips.push_back(std::move(*p));
    }
    r.strips = strips.size();
    if (strips.empty()) return r;
    doc.beginGroup("Toon Ink Outlines");
    if (oldInk != kNoId) {
        doc.deselectAll();
        doc.select({oldInk, kNoId}, false);
        r.replaced = doc.deleteSelection();
    }
    doc.addEntity({{"classname", "func_group"}, {"iw8r_note", kInkNote}}, std::move(strips), "Toon ink outlines");
    doc.endGroup();
    return r;
}

std::optional<Primitive> inkStripPrimitive(''')

M = 'E:/IW8-Radiant/src/qtapp/main_window.cpp'
s = io.open(M, encoding='utf-8', newline='').read()
a = s.index('bool MainWindow::ensureInkMaterial(QString* err) {')
b = s.index('size_t MainWindow::exportReferenceRegion() {')
nl = '\r\n' if '\r\n' in s else '\n'
new = '''size_t MainWindow::generateInkOutlines(double width) {
    if (ctx_.doc.path().empty()) {
        ctx_.setStatus("Toon Ink Outlines: save the map first (the ink material goes into its materials folder)");
        return 0;
    }
    QString err;
    bool written = false;
    if (!writeInkMaterial(customMaterialRoots().front(), &written, &err)) {
        ctx_.setStatus("Toon Ink Outlines: " + err);
        return 0;
    }
    if (written) reloadCustomAssets();
    InkOptions opt;
    opt.width = width;
    const InkResult r = applyInkOutlines(ctx_.doc, opt);
    if (!r.strips) {
        ctx_.setStatus(QString("Toon Ink Outlines: no hard edges on %1 brush(es)").arg(r.brushes));
        return 0;
    }
    ctx_.scene.invalidate();
    ctx_.documentEdited(QString("Toon Ink Outlines: %1 strip(s) %2 units wide along the hard edges of %3 brush(es)%4%5%6 "
                                "(unlit iw8r_ink, no collision, a func_group; one undo step)")
                            .arg(r.strips)
                            .arg(width)
                            .arg(r.brushes)
                            .arg(r.fromSelection ? " in the selection" : "")
                            .arg(r.replaced ? ", replacing the earlier outlines" : "")
                            .arg(r.capped ? ", stopped at the strip limit: select fewer brushes" : ""));
    return r.strips;
}

'''.replace('\n', nl)
s = s[:a] + new + s[b:]
io.open(M, 'w', encoding='utf-8', newline='').write(s)

patch('E:/IW8-Radiant/src/qtapp/main_window.h', '''    size_t generateInkOutlines(double width);
    // The project's unlit black ink material (iw8r_ink), written into the map's materials folder when missing.
    bool ensureInkMaterial(QString* err);
''', '''    size_t generateInkOutlines(double width);
''')

BH = 'E:/IW8-Radiant/src/qtapp/brush_tools.h'
patch(BH, '''bool createBoxBrush(EditorContext& ctx, const AABB& box);
''', '''bool createBoxBrush(EditorContext& ctx, const AABB& box);

// M6 toon maps: the unlit near-black ink material iw8r_ink (iw8r_ink.png + iw8r_ink.iw8mat.json) in `materialRoot`,
// written when missing (*written = true then). False with *err when it cannot be written.
bool writeInkMaterial(const QString& materialRoot, bool* written, QString* err);
''')
BC = 'E:/IW8-Radiant/src/qtapp/brush_tools.cpp'
patch(BC, '''bool createBoxBrush(EditorContext& ctx, const AABB& box) {''', '''bool writeInkMaterial(const QString& root, bool* written, QString* err) {
    if (written) *written = false;
    const QString def = root + "/iw8r_ink.iw8mat.json";
    if (QFileInfo::exists(def)) return true;
    if (!QDir().mkpath(root)) {
        if (err) *err = "cannot create " + root;
        return false;
    }
    // Ink a touch off pure black (pure black reads as a hole next to cel-shaded colour).
    QImage img(8, 8, QImage::Format_RGBA8888);
    img.fill(QColor(20, 17, 24));
    if (!img.save(root + "/iw8r_ink.png")) {
        if (err) *err = "cannot write " + root + "/iw8r_ink.png";
        return false;
    }
    QFile f(def);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (err) *err = "cannot write " + def;
        return false;
    }
    f.write("{\\n  \\"color\\": \\"iw8r_ink.png\\",\\n  \\"unlit\\": true,\\n  \\"width\\": 64,\\n  \\"height\\": 64\\n}\\n");
    if (written) *written = true;
    return true;
}

bool createBoxBrush(EditorContext& ctx, const AABB& box) {''')
print('ok')
