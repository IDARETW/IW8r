import io


def patch(p, pairs):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    for a, b in pairs:
        a = a.replace('\n', nl); b = b.replace('\n', nl)
        assert s.count(a) == 1, (p, s.count(a), a[:80])
        s = s.replace(a, b, 1)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


s = io.open('src/qtapp/grid_view.h', encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
i = s.index('private:', s.index('class GridView'))
add = ('private:' + nl +
       '    // A camera move repaints only around the old and the new camera marker; any other camera notification (also used' + nl +
       '    // as "repaint every view") repaints the whole view.' + nl +
       '    void cameraChangedRepaint();' + nl +
       '    QRect cameraMarkerRect() const;' + nl +
       '    QRect lastCameraMarker_;' + nl +
       '    double lastCam_[5] = {0, 0, 0, 0, 0};' + nl)
s = s[:i] + add + s[i + len('private:'):]
io.open('src/qtapp/grid_view.h', 'w', encoding='utf-8', newline='').write(s)

patch('src/qtapp/grid_view.cpp', [
 ('''    connect(ctx_, &EditorContext::cameraChanged, this, qOverload<>(&QWidget::update));
}''', '''    connect(ctx_, &EditorContext::cameraChanged, this, &GridView::cameraChangedRepaint);
}

QRect GridView::cameraMarkerRect() const {
    const AxisScope axisScope(const_cast<GridView*>(this));
    const int H = ctx_->axisH(), V = ctx_->axisV();
    const QPointF c = worldToWidget(ctx_->cam.pos[H], ctx_->cam.pos[V]);
    return QRect(int(c.x()) - 30, int(c.y()) - 30, 61, 61);  // the dot (4) and the heading line (24), with the pen
}

void GridView::cameraChangedRepaint() {
    const double now[5] = {ctx_->cam.pos.x, ctx_->cam.pos.y, ctx_->cam.pos.z, ctx_->cam.yaw, ctx_->cam.pitch};
    bool moved = false;
    for (int k = 0; k < 5; ++k) moved |= now[k] != lastCam_[k];
    const QRect marker = cameraMarkerRect();
    // Only the marker moved: the rest of the view is as it was. The light-overlap heat map follows the camera height,
    // so it repaints whole.
    if (moved && !lastCameraMarker_.isNull() && !(ctx_->heatMap == 1 && now[2] != lastCam_[2])) update(QRegion(lastCameraMarker_) + QRegion(marker));
    else update();
    for (int k = 0; k < 5; ++k) lastCam_[k] = now[k];
    lastCameraMarker_ = marker;
}'''),
])
print('ok')
