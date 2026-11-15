import io

# Owner 2026-11-15: "we could include something under the viewport in the middle, maybe for the console/compilation
# logs and other things". The log-type panels (Console, Compile, Replay Log, Build Budget, Script, Live Link) move out
# of the side section into a tabbed area under the camera (the middle column only). It is open by default, at the
# height the mapper last gave it; their side bar icons (a group of their own) open it at that tab, and the icon of the
# tab on top folds it away. View > Bottom Panel (Ctrl+J) folds and unfolds it. The side section keeps the browsers.

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
patch(M, '''constexpr int kDockLayoutVersion = 14;  // 14: the bottom strip became the side bar and side section''',
      '''constexpr int kDockLayoutVersion = 15;  // 15: logs in the bottom area under the camera  // 14: the bottom strip became the side bar and side section''')
patch(M, '''        for (ads::CDockWidget* d : sidePanels_) d->toggleView(false);
    }''', '''        for (ads::CDockWidget* d : sidePanels_)
            if (!isBottomPanel(d)) d->toggleView(false);
        // The bottom area opens on the Console.
        if (bottomArea_)
            for (ads::CDockWidget* d : sidePanels_)
                if (d->objectName() == "Console") d->setAsCurrentTab();
        syncSideActions();
    }''')
patch(M, '''        sideArea_ = dock_->addDockWidget(ads::LeftDockWidgetArea, sidePanels_.front(), camArea);
        for (size_t i = 1; i < sidePanels_.size(); ++i) dock_->addDockWidgetTabToArea(sidePanels_[i], sideArea_);
        connect(sideArea_, &QObject::destroyed, this, [this] { sideArea_ = nullptr; });
        dock_->setSplitterSizes(camArea, {420, 980, 560});''', '''        // Logs and the other wide panels: tabs in the area under the camera (the middle column only).
        static const char* const bottomNames[] = {"Console", "Compile", "Replay Log", "Build Budget", "Script", "Live Link"};
        for (const char* n : bottomNames)
            for (ads::CDockWidget* d : sidePanels_)
                if (d->objectName() == n) bottomPanels_.push_back(d);
        std::vector<ads::CDockWidget*> sideOnly;
        for (ads::CDockWidget* d : sidePanels_)
            if (!isBottomPanel(d)) sideOnly.push_back(d);
        sideArea_ = dock_->addDockWidget(ads::LeftDockWidgetArea, sideOnly.front(), camArea);
        for (size_t i = 1; i < sideOnly.size(); ++i) dock_->addDockWidgetTabToArea(sideOnly[i], sideArea_);
        connect(sideArea_, &QObject::destroyed, this, [this] { sideArea_ = nullptr; });
        if (!bottomPanels_.empty()) {
            bottomArea_ = dock_->addDockWidget(ads::BottomDockWidgetArea, bottomPanels_.front(), camArea);
            for (size_t i = 1; i < bottomPanels_.size(); ++i) dock_->addDockWidgetTabToArea(bottomPanels_[i], bottomArea_);
            connect(bottomArea_, &QObject::destroyed, this, [this] { bottomArea_ = nullptr; });
            connect(bottomArea_, &ads::CDockAreaWidget::currentChanged, this, [this](int) { syncSideActions(); });
            // camArea and the bottom area now share a vertical splitter; the side section, that column and the 2D
            // view column share the horizontal one.
            const int h = std::clamp(Settings::instance().value("ui/bottomPanelHeight", 220).toInt(), 120, 700);
            dock_->setSplitterSizes(bottomArea_, {std::max(300, 960 - h), h});
        }
        dock_->setSplitterSizes(sideArea_, {420, 980, 560});''')
patch(M, '''        for (ads::CDockWidget* d : sidePanels_) {
            const QString name = d->objectName();
            if (name == "IW8 Spawns" || name == "Compile" || name == "Script") sideBar_->addSeparator();
            QAction* a = sideBar_->addAction(d->icon(), d->windowTitle());
            a->setObjectName("side_" + QString(name).replace(' ', '_'));
            a->setCheckable(true);
            a->setToolTip(d->windowTitle() + " (click again to fold the side section away)");
            connect(a, &QAction::triggered, this, [this, d](bool) { toggleSidePanel(d); });
            sideActions_[d] = a;
            connect(d, &ads::CDockWidget::viewToggled, this, [this, d](bool open) { sidePanelToggled(d, open); });
        }''', '''        // The side section's panels first, then (a group of their own) the bottom area's.
        std::vector<ads::CDockWidget*> barOrder;
        for (ads::CDockWidget* d : sidePanels_)
            if (!isBottomPanel(d)) barOrder.push_back(d);
        barOrder.insert(barOrder.end(), bottomPanels_.begin(), bottomPanels_.end());
        for (ads::CDockWidget* d : barOrder) {
            const QString name = d->objectName();
            const bool bottom = isBottomPanel(d);
            if (name == "IW8 Spawns" || (bottom && d == bottomPanels_.front())) sideBar_->addSeparator();
            QAction* a = sideBar_->addAction(d->icon(), d->windowTitle());
            a->setObjectName("side_" + QString(name).replace(' ', '_'));
            a->setCheckable(true);
            a->setToolTip(d->windowTitle() + (bottom ? " (in the area under the camera; click again to fold it away)"
                                                     : " (click again to fold the side section away)"));
            connect(a, &QAction::triggered, this, [this, d](bool) { toggleSidePanel(d); });
            sideActions_[d] = a;
            connect(d, &ads::CDockWidget::viewToggled, this, [this, d](bool open) { sidePanelToggled(d, open); });
        }''')
patch(M, '''void MainWindow::toggleSidePanel(ads::CDockWidget* d) {
    // The open panel's icon folds the section away; any other icon shows its panel there (the other one closes).
    const bool showing = !d->isClosed() && d->dockAreaWidget() && d->dockAreaWidget()->currentDockWidget() == d;
    if (showing) {''', '''bool MainWindow::isBottomPanel(const ads::CDockWidget* d) const {
    return std::find(bottomPanels_.begin(), bottomPanels_.end(), d) != bottomPanels_.end();
}

bool MainWindow::bottomAreaOpen() const {
    for (ads::CDockWidget* d : bottomPanels_)
        if (!d->isClosed()) return true;
    return false;
}

void MainWindow::setBottomArea(bool open, ads::CDockWidget* current) {
    if (!open) {
        // Fold: every tab closes (the area goes with them); the height is remembered for the next time.
        if (bottomArea_ && bottomArea_->isVisible() && bottomArea_->height() >= 100)
            Settings::instance().setValue("ui/bottomPanelHeight", bottomArea_->height());
        for (ads::CDockWidget* d : bottomPanels_)
            if (!d->isClosed()) d->toggleView(false);
        syncSideActions();
        return;
    }
    // Unfold: all its tabs come back (closed ones too), `current` (else the one on top before) in front.
    ads::CDockWidget* front = current ? current : (bottomArea_ ? bottomArea_->currentDockWidget() : nullptr);
    for (ads::CDockWidget* d : bottomPanels_)
        if (d->isClosed()) d->toggleView(true);
    if (!front && !bottomPanels_.empty()) front = bottomPanels_.front();
    if (front) {
        front->setAsCurrentTab();
        front->raise();
    }
    heightenBottomArea();
    syncSideActions();
}

void MainWindow::heightenBottomArea() {
    // Shown again, the area can come back flat (its splitter share went to the camera): the remembered height.
    QTimer::singleShot(0, this, [this] {
        auto* sp = bottomArea_ ? qobject_cast<QSplitter*>(bottomArea_->parentWidget()) : nullptr;
        if (!sp || !bottomArea_->isVisible()) return;
        const int i = sp->indexOf(bottomArea_);
        QList<int> sz = sp->sizes();
        const int want = std::clamp(Settings::instance().value("ui/bottomPanelHeight", 220).toInt(), 120, 700);
        if (i < 0 || i >= sz.size() || sz[i] >= want - 8) return;
        int big = -1;
        for (int j = 0; j < sz.size(); ++j)
            if (j != i && (big < 0 || sz[j] > sz[big])) big = j;
        if (big < 0 || sz[big] - (want - sz[i]) < 200) return;
        sz[big] -= want - sz[i];
        sz[i] = want;
        sp->setSizes(sz);
    });
}

void MainWindow::syncSideActions() {
    // A side section icon is checked while its panel is open; a bottom area icon while its tab is the one on top.
    for (const auto& [panel, action] : sideActions_) {
        const QSignalBlocker block(action);
        if (isBottomPanel(panel))
            action->setChecked(!panel->isClosed() && panel->dockAreaWidget() && panel->dockAreaWidget()->currentDockWidget() == panel);
        else
            action->setChecked(!panel->isClosed());
    }
}

void MainWindow::toggleSidePanel(ads::CDockWidget* d) {
    if (isBottomPanel(d)) {
        // The icon of the tab on top folds the bottom area away; any other unfolds it at that tab.
        const bool onTop = !d->isClosed() && d->dockAreaWidget() && d->dockAreaWidget()->currentDockWidget() == d;
        setBottomArea(!onTop, d);
        return;
    }
    // The open panel's icon folds the section away; any other icon shows its panel there (the other one closes).
    const bool showing = !d->isClosed() && d->dockAreaWidget() && d->dockAreaWidget()->currentDockWidget() == d;
    if (showing) {''')
patch(M, '''    if (open && sideArea_ && d->dockAreaWidget() == sideArea_)
        for (ads::CDockWidget* o : sidePanels_)
            if (o != d && !o->isClosed() && o->dockAreaWidget() == sideArea_) o->toggleView(false);
    for (const auto& [panel, action] : sideActions_) {
        const QSignalBlocker block(action);
        action->setChecked(!panel->isClosed());
    }
    if (open && sideArea_ && d->dockAreaWidget() == sideArea_) widenSideSection();''', '''    if (open && sideArea_ && d->dockAreaWidget() == sideArea_)
        for (ads::CDockWidget* o : sidePanels_)
            if (o != d && !o->isClosed() && o->dockAreaWidget() == sideArea_) o->toggleView(false);
    syncSideActions();
    if (open && sideArea_ && d->dockAreaWidget() == sideArea_) widenSideSection();
    if (open && bottomArea_ && d->dockAreaWidget() == bottomArea_) heightenBottomArea();''')
patch(M, '''void MainWindow::showDock(const QString& name) {
    if (ads::CDockWidget* d = dock_->findDockWidget(name)) {''', '''void MainWindow::showDock(const QString& name) {
    if (ads::CDockWidget* d = dock_->findDockWidget(name); d && isBottomPanel(d) && d->dockAreaWidget() == bottomArea_) {
        setBottomArea(true, d);  // the whole bottom area, this tab in front
        return;
    }
    if (ads::CDockWidget* d = dock_->findDockWidget(name)) {''')
patch(M, '''    addCommand(view, "NextView", "Next 2D View (XY / XZ / YZ)", QKeySequence("Ctrl+Tab"), [this] {''', '''    addCommand(view, "ToggleBottomPanel", "Bottom Panel (Console, Compile, Logs)", QKeySequence("Ctrl+J"), [this] {
        setBottomArea(!bottomAreaOpen());
    });
    addCommand(view, "NextView", "Next 2D View (XY / XZ / YZ)", QKeySequence("Ctrl+Tab"), [this] {''')
# The lost-dock fallback: a bottom panel goes back to the bottom area.
patch(M, '''            if (sideArea_) dock_->addDockWidgetTabToArea(d, sideArea_);  // back in the side section''',
      '''            if (bottomArea_ && isBottomPanel(d)) dock_->addDockWidgetTabToArea(d, bottomArea_);  // back under the camera
            else if (sideArea_) dock_->addDockWidgetTabToArea(d, sideArea_);  // back in the side section''')
# Remember the bottom height on close.
patch(M, '''    Settings::instance().setValue("window/docks", dock_->saveState(kDockLayoutVersion));''',
      '''    Settings::instance().setValue("window/docks", dock_->saveState(kDockLayoutVersion));
    if (bottomArea_ && bottomArea_->isVisible() && bottomArea_->height() >= 100)
        Settings::instance().setValue("ui/bottomPanelHeight", bottomArea_->height());''')

H = 'src/qtapp/main_window.h'
patch(H, '''    void widenSideSection();''', '''    void widenSideSection();
    // The area under the camera (Console, Compile, Replay Log, Build Budget, Script, Live Link), owner 2026-11-15.
    ads::CDockAreaWidget* bottomArea_ = nullptr;
    std::vector<ads::CDockWidget*> bottomPanels_;
    bool isBottomPanel(const ads::CDockWidget* d) const;
    bool bottomAreaOpen() const;
    void setBottomArea(bool open, ads::CDockWidget* current = nullptr);
    void heightenBottomArea();
    void syncSideActions();''')
print('ok')
