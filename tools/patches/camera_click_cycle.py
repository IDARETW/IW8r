import io

# Owner 2026-11-15: overlapping objects selectable round robin. A plain click on the same spot again (within 4 px,
# the same objects under the cursor) selects the next one behind; Shift+Alt+click always steps. A multi-brush entity
# (selected whole) is one stop. Also from the viewport review: Escape cancels a gizmo / ring drag, a lost focus ends
# it, and Ctrl+Shift face picking says what happened. pickAll keeps the list order for equal distances (as pick).

ROOT = 'E:/IW8-Radiant/'


def patch(p, a, b, count=1):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    if s.count(a) == 0 and b in s:
        return
    assert s.count(a) == count, (p, s.count(a), a[:80])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


S = 'src/viewport/scene.cpp'
patch(S, '''    std::sort(hits.begin(), hits.end(), [](const auto& a, const auto& b) { return a.first < b.first; });''',
      '''    // Equal distances keep the list order, so the first of them is the one pick() returns.
    std::stable_sort(hits.begin(), hits.end(), [](const auto& a, const auto& b) { return a.first < b.first; });''')

H = 'src/qtapp/camera_view.h'
patch(H, '''    QPoint pressPos_, lockPos_;''', '''    QPoint pressPos_, lockPos_;
    // Click cycling (owner 2026-11-15): the spot, the objects under it (one per selection unit, nearest first) and
    // which of them the last click selected.
    QPoint cyclePos_{-10000, -10000};
    std::vector<SelRef> cycleHits_;
    size_t cycleIndex_ = 0;
    void cancelViewDrag();  // a gizmo / ring drag back to where it started''')

C = 'src/qtapp/camera_view.cpp'
patch(C, '''    if ((e->modifiers() & Qt::ShiftModifier) && (e->modifiers() & Qt::ControlModifier)) {
        // Radiant face selection: Ctrl+Shift+LMB toggles one brush face.
        if (auto face = ctx_->scene.pickFace(ray)) {
            ctx_->doc.toggleFace(*face);
            const EntityItem* it = ctx_->doc.findItem(face->entity, face->item);
            const Primitive* p = it ? it->prim() : nullptr;
            ctx_->documentEdited(QString("%1 face(s) selected%2")
                                     .arg(ctx_->doc.faceSelection().size())
                                     .arg(p && face->face < p->faces.size() ? " - " + QString::fromStdString(p->faces[face->face].material()) : QString()));
        }
        return;
    }
    if ((e->modifiers() & Qt::ShiftModifier) && (e->modifiers() & Qt::AltModifier)) {
        // Radiant select cycle: the next thing behind the current selection along this ray.
        const auto hits = ctx_->scene.pickAll(ray);
        if (!hits.empty()) {
            size_t next = 0;
            for (size_t i = 0; i < hits.size(); ++i)
                if (ctx_->doc.isSelected(hits[i])) next = (i + 1) % hits.size();
            ctx_->doc.select(hits[next], false);
            ctx_->documentEdited(QString("Select cycle: %1 of %2 under the cursor").arg(next + 1).arg(hits.size()));
        }
        return;
    }
    if (auto hit = ctx_->scene.pick(ray)) {''', '''    if ((e->modifiers() & Qt::ShiftModifier) && (e->modifiers() & Qt::ControlModifier)) {
        // Radiant face selection: Ctrl+Shift+LMB toggles one brush face.
        if (auto face = ctx_->scene.pickFace(ray)) {
            const size_t before = ctx_->doc.faceSelection().size();
            ctx_->doc.toggleFace(*face);
            const size_t after = ctx_->doc.faceSelection().size();
            const EntityItem* it = ctx_->doc.findItem(face->entity, face->item);
            const Primitive* p = it ? it->prim() : nullptr;
            ctx_->documentEdited(QString("Face %1; %2 face(s) selected%3")
                                     .arg(after >= before ? "selected" : "deselected")
                                     .arg(after)
                                     .arg(p && face->face < p->faces.size() ? " - " + QString::fromStdString(p->faces[face->face].material()) : QString()));
        } else {
            ctx_->setStatus("No brush face under the cursor (Ctrl+Shift+click picks faces of brushes only)");
        }
        return;
    }
    const Qt::KeyboardModifiers mods = e->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier);
    const bool cycleKey = mods == (Qt::ShiftModifier | Qt::AltModifier);
    if (mods == Qt::NoModifier || cycleKey) {
        // Overlapping objects (owner 2026-11-15): a click on the same spot again (within 4 px, the same objects under
        // the cursor) selects the next one behind, round robin; Shift+Alt+click always steps to the one after the
        // selection. A brush of an entity selects the whole entity, so an entity is one stop however many of its
        // brushes the ray crosses.
        std::vector<SelRef> hits;
        for (const SelRef& h : ctx_->scene.pickAll(ray)) {
            const std::vector<SelRef> unit = ctx_->doc.selectionUnit(h);
            const bool seen = std::any_of(hits.begin(), hits.end(), [&](const SelRef& o) {
                return std::find(unit.begin(), unit.end(), o) != unit.end();
            });
            if (!seen) hits.push_back(h);
        }
        if (hits.empty()) {
            cycleHits_.clear();
            if (mods == Qt::NoModifier) ctx_->doc.deselectAll();
            ctx_->documentEdited();
            return;
        }
        const bool again = (p - cyclePos_).manhattanLength() <= 4 && hits == cycleHits_;
        size_t index = 0;
        if (again) {
            index = (cycleIndex_ + 1) % hits.size();
        } else if (cycleKey) {
            for (size_t i = 0; i < hits.size(); ++i)
                if (ctx_->doc.isSelected(hits[i])) index = (i + 1) % hits.size();
        }
        cyclePos_ = p, cycleHits_ = hits, cycleIndex_ = index;
        ctx_->doc.select(hits[index], false);
        if (hits.size() > 1)
            ctx_->documentEdited(QString("Selected %1 of %2 overlapping objects under the cursor (click the same spot again for the next)")
                                     .arg(index + 1)
                                     .arg(hits.size()));
        else
            ctx_->documentEdited();
        return;
    }
    cycleHits_.clear();
    if (auto hit = ctx_->scene.pick(ray)) {''')

# Escape cancels a gizmo / ring drag (the window's Escape shortcut would otherwise deselect mid-drag), and a lost
# focus ends one (the release may never come).
patch(C, '''bool CameraView::event(QEvent* e) {
    if (orbit_ && e->type() == QEvent::ShortcutOverride) {
        e->accept();
        return true;
    }
    return QWidget::event(e);
}''', '''bool CameraView::event(QEvent* e) {
    if (orbit_ && e->type() == QEvent::ShortcutOverride) {
        e->accept();
        return true;
    }
    // Escape during a gizmo or ring drag cancels the drag here, before the window's Escape (deselect) shortcut runs.
    if (e->type() == QEvent::ShortcutOverride && (gizmoDragAxis_ >= 0 || ringDragAxis_ >= 0))
        if (static_cast<QKeyEvent*>(e)->key() == Qt::Key_Escape) {
            e->accept();
            return true;
        }
    return QWidget::event(e);
}

void CameraView::cancelViewDrag() {
    if (ringDragAxis_ >= 0) {
        ringDragAxis_ = -1;
        ringDeltaDeg_ = 0;
        ctx_->setStatus("Rotation cancelled");
        update();
    }
    if (gizmoDragAxis_ >= 0) {
        const bool scaling = gizmoScale_;
        gizmoDragAxis_ = -1;
        scaleFactor_ = 1.0;
        if (!scaling) ctx_->doc.endDrag(false);  // the translate drag moved a snapshot: put it back
        ctx_->documentEdited(scaling ? QStringLiteral("Scale cancelled") : QStringLiteral("Move cancelled"));
    }
    lmbDown_ = false;
}''')
patch(C, '''void CameraView::keyPressEvent(QKeyEvent* e) {
    if (orbit_) {''', '''void CameraView::keyPressEvent(QKeyEvent* e) {
    if (e->key() == Qt::Key_Escape && (gizmoDragAxis_ >= 0 || ringDragAxis_ >= 0)) {
        cancelViewDrag();
        return;
    }
    if (orbit_) {''')
patch(C, '''void CameraView::focusOutEvent(QFocusEvent* e) {
    ctx_->heldKeys.clear();''', '''void CameraView::focusOutEvent(QFocusEvent* e) {
    // A drag whose release may never arrive (a dialog opened, the window lost focus): back to where it started.
    if (gizmoDragAxis_ >= 0 || ringDragAxis_ >= 0) cancelViewDrag();
    ctx_->heldKeys.clear();''')
print('ok')
