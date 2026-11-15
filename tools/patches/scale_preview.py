import io

# Gizmo scale drag (second review, 2026-11-15): the selection only changed on release, so the factor was a number in
# the status bar. While the handle is dragged, the outline of the selection's bounds as they will be after the scale
# (about the gizmo pivot, along the dragged axis) is drawn in the highlight colour. The document is not touched until
# release (scaling brushes and modelscale keys is one undo group there).

ROOT = 'E:/IW8-Radiant/'


def patch(p, a, b, count=1):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    if b in s and (a not in s or a in b):  # applied already (and b is not just text found elsewhere)
        return
    assert s.count(a) == count, (p, s.count(a), a[:80])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


C = 'src/qtapp/camera_view.cpp'
patch(C, '''            if (gizmoScale_) {
                scaleFactor_ = 1.0;
            } else {''', '''            if (gizmoScale_) {
                scaleFactor_ = 1.0;
                scaleStartBounds_ = ctx_->doc.selectionBounds();  // drawn scaled while the handle moves
            } else {''')
patch(C, '''        if (gizmoScale_) {
            const double len = L * (gizmoDragAxis_ == a ? scaleFactor_ : 1.0);
            const Vec3 tip = c + d * len;''', '''        if (gizmoScale_) {
            if (gizmoDragAxis_ == a && scaleStartBounds_.valid()) {
                // Preview: the selection's bounds as they will be after release, scaled about the pivot on this axis.
                AABB b = scaleStartBounds_;
                const double p0 = gizmoStartPivot_[a];
                const double lo = p0 + (b.mins[a] - p0) * scaleFactor_, hi = p0 + (b.maxs[a] - p0) * scaleFactor_;
                b.mins[a] = std::min(lo, hi), b.maxs[a] = std::max(lo, hi);
                Vec3 k[8];
                for (int i = 0; i < 8; ++i) k[i] = Vec3{(i & 1) ? b.maxs.x : b.mins.x, (i & 2) ? b.maxs.y : b.mins.y, (i & 4) ? b.maxs.z : b.mins.z};
                const int boxEdges[12][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3}, {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
                for (const auto& ed : boxEdges) line(k[ed[0]], k[ed[1]]);
                scalePreviewEdges_ = 12;
            }
            const double len = L * (gizmoDragAxis_ == a ? scaleFactor_ : 1.0);
            const Vec3 tip = c + d * len;''')
patch(C, '''void CameraView::addGizmoLines() {
    if (!gizmoActive()) return;''', '''void CameraView::addGizmoLines() {
    scalePreviewEdges_ = 0;
    if (!gizmoActive()) return;''')

H = 'src/qtapp/camera_view.h'
patch(H, '''    double scaleFactor_ = 1.0;  // current factor while dragging a scale handle''',
      '''    double scaleFactor_ = 1.0;  // current factor while dragging a scale handle
    AABB scaleStartBounds_;      // the selection's bounds when the scale drag began (the preview outline)
    int scalePreviewEdges_ = 0;  // edges of the preview outline drawn in the last frame (tests)''')
patch(H, '''    int gizmoHoverAxis() const { return hoverAxis_; }''', '''    int gizmoHoverAxis() const { return hoverAxis_; }
    int scalePreviewEdges() const { return scalePreviewEdges_; }  // > 0 while a scale drag shows its outline''')
print('ok')
