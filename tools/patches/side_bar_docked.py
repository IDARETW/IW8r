import io

# The side bar, docked (owner 2026-11-15). The ADS auto-hide overlay could not be used: the camera is a native D3D
# window, and a sliding overlay ends up behind it. Instead: a slim icon bar on the window's left edge (QToolBar,
# LeftToolBarArea) and a side section (one ADS dock area left of the camera) that shows one panel at a time;
# clicking the open panel's icon folds the section away. Every panel keeps its dock name; opening a panel by any
# route (side bar, View menu, showDock) closes the other panel of the section and updates the bar.

ROOT = 'E:/IW8-Radiant/'


def patch(p, a, b, count=1):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    if s.count(a) == 0 and b in s:
        return
    assert s.count(a) == count, (p, s.count(a), a[:80])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


H = 'src/qtapp/main_window.h'
patch(H, '''namespace ads {
class CDockManager;
class CDockWidget;''', '''namespace ads {
class CDockManager;
class CDockWidget;
class CDockAreaWidget;''')
patch(H, '''    ads::CDockWidget* compileDock_ = nullptr;''', '''    ads::CDockWidget* compileDock_ = nullptr;
    // The side bar (owner 2026-11-15): the panels of the old bottom strip, one at a time in the side section left of
    // the camera, chosen from the icon bar on the window's left edge.
    class QToolBar* sideBar_ = nullptr;
    ads::CDockAreaWidget* sideArea_ = nullptr;  // cleared when ADS removes it (the area's destroyed signal)
    std::vector<ads::CDockWidget*> sidePanels_;
    std::map<ads::CDockWidget*, QAction*> sideActions_;
    void toggleSidePanel(ads::CDockWidget* d);
    void sidePanelToggled(ads::CDockWidget* d, bool open);''')

M = 'src/qtapp/main_window.cpp'
patch(M, 'constexpr int kDockLayoutVersion = 13;  // 13: the bottom strip became the left side bar',
      'constexpr int kDockLayoutVersion = 14;  // 14: the bottom strip became the side bar and side section')
patch(M, '''    // The side bar (owner 2026-11-15): auto-hide panels as icons on the left edge; set before the manager exists.
    ads::CDockManager::setAutoHideConfigFlags(ads::CDockManager::AutoHideFlags() | ads::CDockManager::AutoHideFeatureEnabled | ads::CDockManager::DockAreaHasAutoHideButton |
                                              ads::CDockManager::AutoHideSideBarsIconOnly | ads::CDockManager::AutoHideHasMinimizeButton |
                                              ads::CDockManager::AutoHideCloseButtonCollapsesDock | ads::CDockManager::AutoHideCloseOnOutsideMouseClick);
    dock_ = new ads::CDockManager(this);''', '''    dock_ = new ads::CDockManager(this);''')
patch(M, '''        if (!d->dockAreaWidget() && !d->isClosed()) dock_->addAutoHideDockWidget(ads::SideBarLeft, d);  // back on the side bar''',
      '''        if (!d->dockAreaWidget() && !d->isClosed()) {
            if (sideArea_) dock_->addDockWidgetTabToArea(d, sideArea_);  // back in the side section
            else dock_->addDockWidget(ads::LeftDockWidgetArea, d);
        }''')
patch(M, '''        std::vector<ads::CDockWidget*> placed;
        const auto place = [&](ads::CDockWidget* d, char16_t glyph) {
            d->setIcon(sidePanelIcon(glyph, d->windowTitle()));
            if (ads::CAutoHideDockContainer* c = dock_->addAutoHideDockWidget(ads::SideBarLeft, d)) c->setSize(460);
            placed.push_back(d);
        };
        for (const auto& [name, glyph] : order)
            for (ads::CDockWidget* d : sidePanels)
                if (d->objectName() == name) place(d, glyph);
        for (ads::CDockWidget* d : sidePanels)  // any panel the order does not name yet: at the end
            if (std::find(placed.begin(), placed.end(), d) == placed.end()) place(d, 0xE8A5);
    }
    dock_->setSplitterSizes(camArea, {1040, 560});''', '''        // Docked beside the camera, not sliding over it: the camera is a native D3D window that an overlay ends up behind.
        for (const auto& [name, glyph] : order)
            for (ads::CDockWidget* d : sidePanels)
                if (d->objectName() == name) d->setIcon(sidePanelIcon(glyph, d->windowTitle())), sidePanels_.push_back(d);
        for (ads::CDockWidget* d : sidePanels)  // any panel the order does not name yet: at the end
            if (std::find(sidePanels_.begin(), sidePanels_.end(), d) == sidePanels_.end())
                d->setIcon(sidePanelIcon(0xE8A5, d->windowTitle())), sidePanels_.push_back(d);
        sideArea_ = dock_->addDockWidget(ads::LeftDockWidgetArea, sidePanels_.front(), camArea);
        for (size_t i = 1; i < sidePanels_.size(); ++i) dock_->addDockWidgetTabToArea(sidePanels_[i], sideArea_);
        connect(sideArea_, &QObject::destroyed, this, [this] { sideArea_ = nullptr; });
        dock_->setSplitterSizes(camArea, {420, 980, 560});
        // The bar: one icon per panel, in groups; the open panel's icon is checked.
        sideBar_ = new QToolBar("Side Bar", this);
        sideBar_->setObjectName("sideBar");
        sideBar_->setMovable(false);
        sideBar_->setFloatable(false);
        sideBar_->setOrientation(Qt::Vertical);
        sideBar_->setIconSize(QSize(24, 24));
        sideBar_->setToolButtonStyle(Qt::ToolButtonIconOnly);
        sideBar_->setContextMenuPolicy(Qt::PreventContextMenu);
        addToolBar(Qt::LeftToolBarArea, sideBar_);
        for (ads::CDockWidget* d : sidePanels_) {
            const QString name = d->objectName();
            if (name == "IW8 Spawns" || name == "Compile" || name == "Script") sideBar_->addSeparator();
            QAction* a = sideBar_->addAction(d->icon(), d->windowTitle());
            a->setObjectName("side_" + QString(name).replace(' ', '_'));
            a->setCheckable(true);
            a->setToolTip(d->windowTitle() + " (click again to fold the side section away)");
            connect(a, &QAction::triggered, this, [this, d](bool) { toggleSidePanel(d); });
            sideActions_[d] = a;
            connect(d, &ads::CDockWidget::viewToggled, this, [this, d](bool open) { sidePanelToggled(d, open); });
        }
        // The side section starts folded away: the camera has the room until a panel is asked for.
        for (ads::CDockWidget* d : sidePanels_) d->toggleView(false);
    }
    dock_->setSplitterSizes(entArea, {560, 360});''')
patch(M, '''void MainWindow::showDock(const QString& name) {''', '''void MainWindow::toggleSidePanel(ads::CDockWidget* d) {
    // The open panel's icon folds the section away; any other icon shows its panel there (the other one closes).
    const bool showing = !d->isClosed() && d->dockAreaWidget() && d->dockAreaWidget()->currentDockWidget() == d;
    if (showing) {
        d->toggleView(false);
    } else {
        d->toggleView(true);
        d->setAsCurrentTab();
        d->raise();
    }
    sidePanelToggled(d, !showing);
}

void MainWindow::sidePanelToggled(ads::CDockWidget* d, bool open) {
    // One panel at a time in the side section, however it was opened (side bar, View menu, showDock, a command).
    if (open && sideArea_ && d->dockAreaWidget() == sideArea_)
        for (ads::CDockWidget* o : sidePanels_)
            if (o != d && !o->isClosed() && o->dockAreaWidget() == sideArea_) o->toggleView(false);
    for (const auto& [panel, action] : sideActions_) {
        const QSignalBlocker block(action);
        action->setChecked(!panel->isClosed());
    }
}

void MainWindow::showDock(const QString& name) {''')
print('ok')
