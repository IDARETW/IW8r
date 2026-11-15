import io

# Editing quirks from the 2026-11-15 review:
#  - holding Alt+arrow nudged once per key repeat, one undo step each (dozens of Ctrl+Z for one move): nudges in a row
#    with nothing else in between are one undo step;
#  - the grid size and Snap To Grid are remembered between sessions;
#  - the status bar shows the size of the selection.

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
patch(M, '''void MainWindow::runNudge(int dh, int dv) {
    Vec3 d;
    d[ctx_.axisH()] = dh * ctx_.doc.grid();
    d[ctx_.axisV()] = dv * ctx_.doc.grid();
    if (ctx_.doc.translateSelection(d, "Nudge")) ctx_.documentEdited("Nudged " + qs(formatNumber(ctx_.doc.grid())));
}''', '''void MainWindow::runNudge(int dh, int dv) {
    Vec3 d;
    d[ctx_.axisH()] = dh * ctx_.doc.grid();
    d[ctx_.axisV()] = dv * ctx_.doc.grid();
    // Nudges in a row (a held Alt+arrow repeats) are one undo step: when nothing at all changed since the last nudge
    // (same document revision) and it was moments ago, it is undone and redone as one move by the total.
    const bool chain = nudgeRevision_ == ctx_.doc.revision() && nudgeClock_.isValid() && nudgeClock_.elapsed() < 1500 &&
                       ctx_.doc.canUndo() && !ctx_.doc.canRedo() && !ctx_.doc.history().empty() &&
                       ctx_.doc.history().back().name == "Nudge";
    Vec3 total = d;
    if (chain && ctx_.doc.undo()) total = nudgeTotal_ + d;
    if (ctx_.doc.translateSelection(total, "Nudge")) {
        nudgeTotal_ = total;
        nudgeRevision_ = ctx_.doc.revision();
        nudgeClock_.start();
        ctx_.documentEdited(QString("Nudged %1 (%2, %3, %4 in all)")
                                .arg(qs(formatNumber(ctx_.doc.grid())), qs(formatNumber(total.x)), qs(formatNumber(total.y)), qs(formatNumber(total.z))));
        nudgeRevision_ = ctx_.doc.revision();
    } else {
        nudgeRevision_ = 0;
    }
}''')
patch(M, '''        addCommand(grid, g.cmd, "Grid " + qs(formatNumber(size)), QKeySequence(g.key), [this, size] {
            ctx_.doc.setGrid(size);
            ctx_.documentEdited();
        });
    }
    grid->addSeparator();
    addCommand(grid, "GridDown", "Grid Smaller", QKeySequence(Qt::Key_BracketLeft), [this] {
        ctx_.doc.gridDown();
        ctx_.documentEdited();
    });
    addCommand(grid, "GridUp", "Grid Larger", QKeySequence(Qt::Key_BracketRight), [this] {
        ctx_.doc.gridUp();
        ctx_.documentEdited();
    });
    QAction* snap = addCommand(grid, "ToggleSnapToGrid", "Snap To Grid", QKeySequence("Ctrl+Alt+G"), [this] {
        ctx_.snap = !ctx_.snap;
        ctx_.documentEdited(ctx_.snap ? "Snap on" : "Snap off");
    });
    snap->setCheckable(true);
    snap->setChecked(true);''', '''        addCommand(grid, g.cmd, "Grid " + qs(formatNumber(size)), QKeySequence(g.key), [this, size] {
            ctx_.doc.setGrid(size);
            Settings::instance().setValue("grid/size", ctx_.doc.grid());
            ctx_.documentEdited();
        });
    }
    grid->addSeparator();
    addCommand(grid, "GridDown", "Grid Smaller", QKeySequence(Qt::Key_BracketLeft), [this] {
        ctx_.doc.gridDown();
        Settings::instance().setValue("grid/size", ctx_.doc.grid());
        ctx_.documentEdited();
    });
    addCommand(grid, "GridUp", "Grid Larger", QKeySequence(Qt::Key_BracketRight), [this] {
        ctx_.doc.gridUp();
        Settings::instance().setValue("grid/size", ctx_.doc.grid());
        ctx_.documentEdited();
    });
    // The grid size and snap of the last session (the document keeps its grid across File > Open).
    ctx_.doc.setGrid(Settings::instance().value("grid/size", 8.0).toDouble());
    ctx_.snap = Settings::instance().value("grid/snap", true).toBool();
    QAction* snap = addCommand(grid, "ToggleSnapToGrid", "Snap To Grid", QKeySequence("Ctrl+Alt+G"), [this] {
        ctx_.snap = !ctx_.snap;
        Settings::instance().setValue("grid/snap", ctx_.snap);
        ctx_.documentEdited(ctx_.snap ? "Snap on" : "Snap off");
    });
    snap->setCheckable(true);
    snap->setChecked(ctx_.snap);''')
patch(M, '''    selLabel_->setText(ctx_.doc.faceSelection().empty() ? QString("%1 selected").arg(ctx_.doc.selection().size())
                                                        : QString("%1 faces selected").arg(ctx_.doc.faceSelection().size()));''',
      '''    if (!ctx_.doc.faceSelection().empty()) {
        selLabel_->setText(QString("%1 faces selected").arg(ctx_.doc.faceSelection().size()));
    } else if (ctx_.doc.selection().empty()) {
        selLabel_->setText("0 selected");
    } else {
        // The size of what is selected, as Radiant shows it (width x depth x height in units).
        const AABB b = ctx_.doc.selectionBounds();
        const QString size = b.valid() ? QString("  %1 x %2 x %3").arg(qs(formatNumber(b.maxs.x - b.mins.x)), qs(formatNumber(b.maxs.y - b.mins.y)),
                                                                        qs(formatNumber(b.maxs.z - b.mins.z)))
                                       : QString();
        selLabel_->setText(QString("%1 selected%2").arg(ctx_.doc.selection().size()).arg(size));
    }''')

H = 'src/qtapp/main_window.h'
patch(H, '''    void runNudge(int dh, int dv);''', '''    void runNudge(int dh, int dv);
    // Nudges in a row are one undo step (runNudge).
    uint64_t nudgeRevision_ = 0;
    Vec3 nudgeTotal_;
    QElapsedTimer nudgeClock_;''')
print('ok')
