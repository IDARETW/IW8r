import io, re

# Owner 2026-11-15: "the bottom set of editor tabs in a small side-bar in a side section, and the rest rearranged
# accordingly". The 14 panels of the bottom strip become auto-hide docks on the left edge (ADS 4.4 side bar, icons
# only): clicking an icon slides the panel out beside the camera, clicking outside or the icon again folds it away,
# its pin button docks it for good. The camera takes the freed height; the Grid and the Entity group stay on the
# right. Every panel keeps its dock name, so showDock / findDockWidget / the View menu work as before.

ROOT = 'E:/IW8-Radiant/'
M = 'src/qtapp/main_window.cpp'
s = io.open(ROOT + M, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'


def rep(a, b, count=1):
    global s
    a = a.replace('\n', nl)
    b = b.replace('\n', nl)
    assert s.count(a) == count, (s.count(a), a[:80])
    s = s.replace(a, b)


rep('constexpr int kDockLayoutVersion = 12;  // 12: Script, Outliner',
    'constexpr int kDockLayoutVersion = 13;  // 13: the bottom strip became the left side bar  // 12: Script, Outliner')
rep('''        if (!d->dockAreaWidget() && !d->isClosed()) dock_->addDockWidgetTabToArea(d, dock_->findDockWidget("Undo History")->dockAreaWidget());''',
    '''        if (!d->dockAreaWidget() && !d->isClosed()) dock_->addAutoHideDockWidget(ads::SideBarLeft, d);  // back on the side bar''')
rep('''    ads::CDockManager::setConfigFlag(ads::CDockManager::AllTabsHaveCloseButton, false);
    dock_ = new ads::CDockManager(this);''', '''    ads::CDockManager::setConfigFlag(ads::CDockManager::AllTabsHaveCloseButton, false);
    // The side bar (owner 2026-11-15): auto-hide panels as icons on the left edge; set before the manager exists.
    ads::CDockManager::setAutoHideConfigFlags(ads::CDockManager::AutoHideFeatureEnabled | ads::CDockManager::DockAreaHasAutoHideButton |
                                              ads::CDockManager::AutoHideSideBarsIconOnly | ads::CDockManager::AutoHideHasMinimizeButton |
                                              ads::CDockManager::AutoHideCloseButtonCollapsesDock | ads::CDockManager::AutoHideCloseOnOutsideMouseClick);
    dock_ = new ads::CDockManager(this);''')
rep('''    auto* bottomArea = dock_->addDockWidget(ads::BottomDockWidgetArea, histDock, camArea);
    dock_->addDockWidgetTabToArea(spawnDock, bottomArea);''', '''    // The panels of the side bar, placed in its order at the end (sidePanelOrder).
    std::vector<ads::CDockWidget*> sidePanels{histDock, spawnDock};''')
n = 0
def sub(m):
    global n
    n += 1
    return 'sidePanels.push_back(' + m.group(1) + ');'
s = re.sub(r'dock_->addDockWidgetTabToArea\((.+?), bottomArea\);', sub, s)
assert n == 12, n
rep('''    bottomArea->setCurrentDockWidget(histDock);
    dock_->setSplitterSizes(camArea, {960, 640});
    dock_->setSplitterSizes(bottomArea, {700, 220});''', '''    {
        // The side bar, in working order: assets, the map, building, script and game.
        static const std::pair<const char*, char16_t> order[] = {
            {"Materials", 0xE790}, {"Models", 0xF158}, {"Prefabs", 0xE74C}, {"Effects", 0xE945}, {"Sounds", 0xE767},
            {"Images", 0xEB9F}, {"IW8 Spawns", 0xE707}, {"Undo History", 0xE81C}, {"Compile", 0xE90F}, {"Build Budget", 0xE9D2},
            {"Console", 0xE756}, {"Replay Log", 0xE9F9}, {"Script", 0xE943}, {"Live Link", 0xE71B}};
        std::vector<ads::CDockWidget*> placed;
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
    dock_->setSplitterSizes(camArea, {1040, 560});''')
# The icon maker, before buildDocks.
rep('''void MainWindow::buildDocks() {''', '''// A side bar icon: the panel's glyph from Windows' icon font (Segoe MDL2 Assets, Windows 10 and later), in the
// window text colour; the panel's first letters where the font is missing.
QIcon sidePanelIcon(char16_t glyph, const QString& title) {
    static const bool mdl2 = QFontDatabase::families().contains("Segoe MDL2 Assets");
    QIcon icon;
    for (const int side : {32, 64}) {
        QPixmap pm(side, side);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::TextAntialiasing, true);
        QFont f(mdl2 ? "Segoe MDL2 Assets" : "Segoe UI");
        f.setPixelSize(mdl2 ? side * 5 / 8 : side * 3 / 8);
        f.setBold(!mdl2);
        p.setFont(f);
        p.setPen(QApplication::palette().color(QPalette::WindowText));
        QString letters;
        for (const QString& w : title.split(' ', Qt::SkipEmptyParts)) letters += w.left(1);
        p.drawText(pm.rect(), Qt::AlignCenter, mdl2 ? QString(QChar(glyph)) : letters.left(2));
        p.end();
        icon.addPixmap(pm);
    }
    return icon;
}

void MainWindow::buildDocks() {''')
io.open(ROOT + M, 'w', encoding='utf-8', newline='').write(s)
print('ok', n)
