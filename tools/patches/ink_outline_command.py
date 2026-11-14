import io

# Tools > Toon Ink Outlines: thin unlit black strip brushes along the hard edges of the map's brushes (plan M6,
# the toon Cinderrail example). Core: src/core/toon_outline.


def patch(p, a, b, n=1):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


H = 'E:/IW8-Radiant/src/qtapp/main_window.h'
patch(H, '''    size_t exportReferenceBrushes();
''', '''    size_t exportReferenceBrushes();
    // M6 toon maps: ink strips (unlit black, non-colliding brushes) along the hard edges of the selected brushes, or of
    // every worldspawn / func_group brush when nothing is selected; replaces an earlier ink group. One undo step.
    size_t generateInkOutlines(double width);
    // The project's unlit black ink material (iw8r_ink), written into the map's materials folder when missing.
    bool ensureInkMaterial(QString* err);
''')

C = 'E:/IW8-Radiant/src/qtapp/main_window.cpp'
patch(C, '''#include "iw8_contents.h"
''', '''#include "iw8_contents.h"
#include "toon_outline.h"
''')
patch(C, '''size_t MainWindow::exportReferenceRegion() {''', '''bool MainWindow::ensureInkMaterial(QString* err) {
    const QString root = customMaterialRoots().front();
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
    f.close();
    reloadCustomAssets();
    return true;
}

size_t MainWindow::generateInkOutlines(double width) {
    static const char* kNote = "toon ink outlines";
    if (ctx_.doc.path().empty()) {
        ctx_.setStatus("Toon Ink Outlines: save the map first (the ink material goes into its materials folder)");
        return 0;
    }
    QString err;
    if (!ensureInkMaterial(&err)) {
        ctx_.setStatus("Toon Ink Outlines: " + err);
        return 0;
    }
    // The brushes to outline: the selection, else every brush of the worldspawn and func_groups (not the old ink).
    std::vector<ConvexBrush> brushes;
    Id oldInk = kNoId;
    const auto addBrush = [&](const Primitive* p) {
        if (!p || p->kind != PrimKind::Brush) return;
        ConvexBrush b = brushFromPrimitive(*p);
        if (b.empty() || inkFaceMaterial(b.front().suffix) == "iw8r_ink") return;
        brushes.push_back(std::move(b));
    };
    for (const auto& e : ctx_.doc.map().entities) {
        const std::string* note = e.value("iw8r_note");
        if (note && *note == kNote) oldInk = e.id;
    }
    bool fromSelection = false;
    for (const auto& r : ctx_.doc.selection()) {
        if (r.item == kNoId || r.entity == oldInk) continue;
        const EntityItem* it = ctx_.doc.findItem(r.entity, r.item);
        addBrush(it ? it->prim() : nullptr);
        fromSelection = true;
    }
    if (!fromSelection)
        for (const auto& e : ctx_.doc.map().entities) {
            const std::string c = e.classname();
            if (e.id == oldInk || (c != "worldspawn" && c != "func_group")) continue;
            for (const auto& it : e.items) addBrush(it.prim());
        }
    InkOptions opt;
    opt.width = width;
    const std::vector<InkEdge> edges = uniqueInkEdges(brushes, opt);
    constexpr size_t kMaxStrips = 40000;
    std::vector<Primitive> strips;
    const std::string suffix = " iw8r_ink 64 64 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0";
    for (const InkEdge& e : edges) {
        if (strips.size() >= kMaxStrips) break;
        if (auto p = inkStripPrimitive(e, opt, suffix, ctx_.doc.eol())) strips.push_back(std::move(*p));
    }
    if (strips.empty()) {
        ctx_.setStatus(QString("Toon Ink Outlines: no hard edges on %1 brush(es)").arg(brushes.size()));
        return 0;
    }
    const size_t n = strips.size();
    ctx_.doc.beginGroup("Toon Ink Outlines");
    if (oldInk != kNoId) {
        ctx_.doc.deselectAll();
        ctx_.doc.select({oldInk, kNoId}, false);
        ctx_.doc.deleteSelection();
    }
    ctx_.doc.addEntity({{"classname", "func_group"}, {"iw8r_note", kNote}}, std::move(strips), "Toon ink outlines");
    ctx_.doc.endGroup();
    ctx_.scene.invalidate();
    ctx_.documentEdited(QString("Toon Ink Outlines: %1 strip(s) %2 units wide along the hard edges of %3 brush(es)%4%5 "
                                "(unlit iw8r_ink, no collision, a func_group; one undo step)")
                            .arg(n)
                            .arg(width)
                            .arg(brushes.size())
                            .arg(fromSelection ? " in the selection" : "")
                            .arg(oldInk != kNoId ? ", replacing the earlier outlines" : ""));
    return n;
}

size_t MainWindow::exportReferenceRegion() {''')
patch(C, '''    addCommand(tools, "AdjustImage", "Adjust Image (brightness, tint, resize, normal map from height)...", QKeySequence(), [this] {''',
      '''    addCommand(tools, "ToonInkOutlines", "Toon Ink Outlines (hard brush edges)...", QKeySequence(), [this] {
        // M6: cel-shaded maps (iw8r_surface_shader "toon") get their outlines as geometry; the game's lit pass has no
        // depth to find edges in (research 114).
        bool ok = false;
        const double w = QInputDialog::getDouble(this, "Toon Ink Outlines",
                                                 "Ink strips along the hard edges of the selected brushes (all world brushes "
                                                 "when nothing is selected).\\nStrip width (units):",
                                                 2.0, 0.25, 16.0, 2, &ok);
        if (ok) generateInkOutlines(w);
    });
    addCommand(tools, "AdjustImage", "Adjust Image (brightness, tint, resize, normal map from height)...", QKeySequence(), [this] {''')
print('ok')
