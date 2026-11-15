import io

# Wiring for the 2D view fixes (grid_view / editor_context, 2026-11-15 review):
#  - Next 2D View keeps the same world point in the middle (EditorContext::nextGridAxis / setGridAxis);
#  - View > Zoom In (2D) / Zoom Out (2D) / Fit 2D View to Map (stock Radiant's Delete / Insert, and Home);
#  - the position readout shows only the view's two axes and is cleared when the mouse leaves the view.

ROOT = 'E:/IW8-Radiant/'


def patch(p, a, b, count=1):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    if s.count(a) == 0 and b in s:
        return
    assert s.count(a) == count, (p, s.count(a), a[:80])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


M = 'src/qtapp/main_window.cpp'
patch(M, '''                if (i != 0 && ctx_.gridAxis != 0) ctx_.gridAxis = 0;  // drawn in the XY top view''',
      '''                if (i != 0 && ctx_.gridAxis != 0) ctx_.setGridAxis(0);  // drawn in the XY top view''')
patch(M, '''    addCommand(view, "NextView", "Next 2D View (XY / XZ / YZ)", QKeySequence("Ctrl+Tab"), [this] {
        ctx_.gridAxis = (ctx_.gridAxis + 1) % 3;
        ctx_.cameraMoved();
    });''', '''    addCommand(view, "NextView", "Next 2D View (XY / XZ / YZ)", QKeySequence("Ctrl+Tab"), [this] {
        ctx_.nextGridAxis();  // the same world point stays in the middle
    });
    {
        // The 2D view with the keyboard focus (a four-view layout has three), else the main one.
        const auto focusedGrid = [this] {
            GridView* g = qobject_cast<GridView*>(QApplication::focusWidget());
            return g ? g : grid_;
        };
        addCommand(view, "ZoomIn2D", "Zoom In (2D)", QKeySequence(Qt::Key_Delete), [focusedGrid] { focusedGrid()->zoomIn(); });
        addCommand(view, "ZoomOut2D", "Zoom Out (2D)", QKeySequence(Qt::Key_Insert), [focusedGrid] { focusedGrid()->zoomOut(); });
        addCommand(view, "FitAll2D", "Fit 2D View to Map", QKeySequence(Qt::Key_Home), [focusedGrid] { focusedGrid()->fitAll(); });
    }''')
patch(M, '''    connect(grid_, &GridView::mouseWorld, this, [this](double x, double y, double z) {
        posLabel_->setText(QString("%1 %2 %3").arg(qs(formatNumber(x)), qs(formatNumber(y)), qs(formatNumber(z))));
    });''', '''    // The view's own two axes (the third is not under the mouse); cleared when the mouse leaves the view.
    connect(grid_, &GridView::positionText, posLabel_, &QLabel::setText);''')
patch(M, '''    ctx_.gridAxis = 0;
    ads::CDockWidget* camDock = dock_->findDockWidget("Camera");''', '''    ctx_.setGridAxis(0);
    ads::CDockWidget* camDock = dock_->findDockWidget("Camera");''')
patch(M, '''            connect(g, &GridView::mouseWorld, this, [this](double x, double y, double z) {
                posLabel_->setText(QString("%1 %2 %3").arg(qs(formatNumber(x)), qs(formatNumber(y)), qs(formatNumber(z))));
            });''', '''            connect(g, &GridView::positionText, posLabel_, &QLabel::setText);''')
print('ok')
