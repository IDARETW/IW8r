import io


def patch(p, pairs):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    for a, b in pairs:
        a = a.replace('\n', nl); b = b.replace('\n', nl)
        assert s.count(a) == 1, (p, s.count(a), a[:70])
        s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


patch('src/core/bevel_ops.h', [
 ('''// Chamfers all edges of every selected brush as ONE undo step.''',
  '''// Bridge two faces: the convex brush that spans two brush faces, the convex hull of the corners of both faces (when
// the faces look at each other they are its two caps, as a connecting tunnel or beam). Every face of the new brush
// takes the suffix (material, texture scale and lightmap settings) of the first face. nullopt when either face does
// not contribute or the corners are flat (the faces lie in one plane).
std::optional<ConvexBrush> bridgeFaces(const Primitive& a, size_t faceA, const Primitive& b, size_t faceB);

// The two selected brush faces bridged into a new brush of the first face's entity, as ONE undo step (the new brush is
// selected). False when the face selection is not exactly two brush faces or the bridge is flat.
bool bridgeSelectedFaces(Document& doc);

// Chamfers all edges of every selected brush as ONE undo step.'''),
])
patch('src/core/bevel_ops.cpp', [
 ('''bool insetSelectedFace(Document& doc, const FaceRef& face, double inset, double depth) {''',
  '''std::optional<ConvexBrush> bridgeFaces(const Primitive& a, size_t faceA, const Primitive& b, size_t faceB) {
    if (a.kind != PrimKind::Brush || b.kind != PrimKind::Brush || faceA >= a.faces.size() || faceB >= b.faces.size()) return std::nullopt;
    const std::vector<Winding> wa = faceWindingsOf(a), wb = faceWindingsOf(b);
    if (faceA >= wa.size() || faceB >= wb.size() || wa[faceA].size() < 3 || wb[faceB].size() < 3) return std::nullopt;
    std::vector<Vec3> points(wa[faceA].begin(), wa[faceA].end());
    points.insert(points.end(), wb[faceB].begin(), wb[faceB].end());
    const std::vector<Plane> planes = convexHullPlanes(points, 0.5);
    if (planes.size() < 4) return std::nullopt;
    ConvexBrush out;
    for (const Plane& pl : planes) out.push_back(BrushFaceSpec{pl, a.faces[faceA].suffix});
    if (!isValidBrush(out)) return std::nullopt;
    return out;
}

bool bridgeSelectedFaces(Document& doc) {
    if (doc.faceSelection().size() != 2) return false;
    const FaceRef fa = *doc.faceSelection().begin(), fb = *std::next(doc.faceSelection().begin());
    const EntityItem* ia = doc.findItem(fa.entity, fa.item);
    const EntityItem* ib = doc.findItem(fb.entity, fb.item);
    const Primitive* pa = ia ? ia->prim() : nullptr;
    const Primitive* pb = ib ? ib->prim() : nullptr;
    if (!pa || !pb) return false;
    const auto brush = bridgeFaces(*pa, fa.face, *pb, fb.face);
    if (!brush) return false;
    auto prim = makeBrushPrimitive(*brush, doc.eol());
    if (!prim) return false;
    return doc.editPrimitives({{fa.entity, kNoId, std::move(prim)}}, "Bridge Faces");
}

bool insetSelectedFace(Document& doc, const FaceRef& face, double inset, double depth) {'''),
])
patch('src/qtapp/main_window.cpp', [
 ('''    addCommand(brush, "InsetFace", "Inset Selected Face (recess)...", QKeySequence(), [this] {''',
  '''    addCommand(brush, "BridgeFaces", "Bridge Two Faces", QKeySequence(), [this] {
        if (ctx_.doc.faceSelection().size() != 2) return ctx_.setStatus("Bridge: select exactly two brush faces first (Ctrl+Shift+click in the camera)");
        if (!bridgeSelectedFaces(ctx_.doc)) return ctx_.setStatus("Bridge: the two faces lie in one plane (nothing to span)");
        ctx_.scene.invalidate();
        ctx_.documentEdited("Bridged two faces with a new brush (one undo step)");
    });
    addCommand(brush, "InsetFace", "Inset Selected Face (recess)...", QKeySequence(), [this] {'''),
])
print('ok')
