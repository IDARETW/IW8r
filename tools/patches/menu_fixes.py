import io

# Menu and command quirks from the 2026-11-15 review (owner: "make the editor more streamlined"):
#  - leaving Play mode restores each command's own enabled state (it re-enabled commands disabled on purpose);
#  - Edit > Select All (Ctrl+A); Select All and Invert Selection take only what is visible (hidden, filtered,
#    layer-hidden, outside-region and mode-hidden things are neither drawn nor pickable, so they are not selected);
#  - a hotkey preset resets the commands it does not list to their default key, and the status names Tools > Hotkeys;
#  - the two Rebuild Stock Zone entries say which is fast; Clear camera path asks first; the Entity Inspector (N) is in
#    the View menu; the About box drops the milestone wording.

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
patch(M, '''    connect(&ctx_, &EditorContext::playModeChanged, this, [this](bool playing) {
        for (QAction* a : actions_)
            if (a->objectName() != "PlayPreview") a->setEnabled(!playing);
        if (QAction* p = action("PlayPreview")) p->setEnabled(!playing);  // Esc / P are handled by the camera itself
    });''', '''    {
        // Play mode disables every command; leaving it enables again only those it disabled, so commands disabled on
        // purpose (Clip Selected outside clip mode, trigger kinds the build does not support) stay disabled.
        auto disabledByPlay = std::make_shared<std::vector<QPointer<QAction>>>();
        connect(&ctx_, &EditorContext::playModeChanged, this, [this, disabledByPlay](bool playing) {
            if (playing) {
                disabledByPlay->clear();
                for (QAction* a : actions_)
                    if (a->isEnabled()) {
                        a->setEnabled(false);  // PlayPreview too: Esc / P are handled by the camera itself
                        disabledByPlay->push_back(a);
                    }
            } else {
                for (const QPointer<QAction>& a : *disabledByPlay)
                    if (a) a->setEnabled(true);
                disabledByPlay->clear();
            }
        });
    }''')
patch(M, '''    addCommand(edit, "InvertSelection", "&Invert Selection", QKeySequence(Qt::Key_I), [this] {
        ctx_.doc.invertSelection();
        ctx_.documentEdited();
    });''', '''    addCommand(edit, "SelectAll", "Select &All", QKeySequence(Qt::CTRL | Qt::Key_A), [this] {
        const std::set<SelRef> all = visibleSelectable();
        ctx_.doc.deselectAll();
        for (const SelRef& r : all) ctx_.doc.select(r, true);
        ctx_.documentEdited(QString("Selected %1 visible object(s) (hidden and filtered ones are left out)").arg(all.size()));
    });
    addCommand(edit, "InvertSelection", "&Invert Selection", QKeySequence(Qt::Key_I), [this] {
        // Only what is visible: a hidden or filtered object selected by Invert would be moved or deleted unseen.
        std::set<SelRef> now;
        for (const SelRef& r : ctx_.doc.selection()) now.insert(r);
        ctx_.doc.deselectAll();
        size_t n = 0;
        for (const SelRef& r : visibleSelectable()) {
            const std::vector<SelRef> unit = ctx_.doc.selectionUnit(r);
            const bool wasSelected = std::any_of(unit.begin(), unit.end(), [&](const SelRef& u) { return now.count(u) != 0; });
            if (!wasSelected) ctx_.doc.select(r, true), ++n;
        }
        ctx_.documentEdited(QString("Inverted the selection: %1 visible object(s) selected").arg(n));
    });''')
patch(M, '''void MainWindow::showDock(const QString& name) {''', '''std::set<SelRef> MainWindow::visibleSelectable() const {
    // What a click could select: brushes and patches of worldspawn one by one, every other entity once (its first
    // primitive, which selects the whole entity, or the point entity itself); hidden, filtered, layer-hidden,
    // outside-region and mode-hidden things left out, as the scene leaves them out of drawing and picking.
    const SceneCache& sc = ctx_.scene;
    const auto hiddenId = [&](Id id) {
        return sc.hidden.count(id) || sc.hiddenByLayer.count(id) || sc.hiddenByRegion.count(id) || sc.hiddenByMode.count(id);
    };
    std::set<SelRef> out;
    for (const Entity& e : ctx_.doc.map().entities) {
        if (!e.isWorldspawn()) {
            if (hiddenId(e.id)) continue;
            const std::string cls = e.classname();
            bool filtered = false;
            for (const std::string& p : sc.hiddenClassPrefixes) filtered = filtered || cls.rfind(p, 0) == 0;
            if (filtered) continue;
        }
        bool any = false;
        for (const auto& it : e.items) {
            if (!it.isPrimitive() || hiddenId(it.id)) continue;
            out.insert({e.id, it.id});
            any = true;
            if (!e.isWorldspawn()) break;  // one primitive selects the whole entity
        }
        if (!any && !e.isWorldspawn()) out.insert({e.id, kNoId});
    }
    return out;
}

void MainWindow::showDock(const QString& name) {''')
patch(M, '''    a->setShortcut(Settings::instance().hasShortcut(command) ? Settings::instance().shortcut(command) : def);''',
      '''    a->setShortcut(Settings::instance().hasShortcut(command) ? Settings::instance().shortcut(command) : def);
    a->setProperty("defaultShortcut", def);  // hotkey presets return unlisted commands to it''')
patch(M, '''                // The preset's keys over the defaults; commands it does not list return to their default key.
                const HotkeyPreset* def = nullptr;
                (void)def;
                size_t n = 0;
                for (auto it = actions_.begin(); it != actions_.end(); ++it) {
                    const auto k = p.keys.find(it.key().toStdString());
                    if (k == p.keys.end()) continue;
                    const QKeySequence seq(qs(k->second));
                    Settings::instance().setShortcut(it.key(), seq);
                    it.value()->setShortcut(seq);
                    ++n;
                }
                ctx_.setStatus(QString("Hotkey preset %1: %2 command(s) rebound (Edit > Keyboard Shortcuts to adjust)").arg(qs(p.name)).arg(n));''',
      '''                // The preset's keys over the defaults; commands it does not list return to their default key (a
                // mix of two presets' layouts is what applying one after another used to leave).
                size_t n = 0;
                for (auto it = actions_.begin(); it != actions_.end(); ++it) {
                    const auto k = p.keys.find(it.key().toStdString());
                    const QKeySequence seq = k != p.keys.end() ? QKeySequence(qs(k->second)) : it.value()->property("defaultShortcut").value<QKeySequence>();
                    if (it.value()->shortcut() == seq && !Settings::instance().hasShortcut(it.key())) continue;
                    Settings::instance().setShortcut(it.key(), seq);
                    it.value()->setShortcut(seq);
                    ++n;
                }
                ctx_.setStatus(QString("Hotkey preset %1: %2 command(s) set (Tools > Hotkeys to adjust)").arg(qs(p.name)).arg(n));''')
patch(M, '''    addCommand(file, "RebuildStockZoneFast", "Rebuild Stock Zone...", QKeySequence(), [this] { rebuildStockZoneFast(); });''',
      '''    addCommand(file, "RebuildStockZoneFast", "Rebuild Stock Zone (fast)...", QKeySequence(), [this] { rebuildStockZoneFast(); });''')
patch(M, '''    addCommand(file, "RebuildStockZone", "Rebuild Stock Zone (dump script, slow)...", QKeySequence(), [this] {''',
      '''    addCommand(file, "RebuildStockZone", "Rebuild Stock Zone (full dump, slow)...", QKeySequence(), [this] {''')
patch(M, '''        addCommand(cp, "CameraPathClear", "Clear", QKeySequence(), [this] {
            camPathTimer_.stop();''', '''        addCommand(cp, "CameraPathClear", "Clear...", QKeySequence(), [this] {
            // Not on the undo history: ask first when there is something to lose.
            if (!camPath_.keys.empty() &&
                QMessageBox::question(this, "Clear Camera Path", QString("Remove all %1 key(s) of the camera path? This cannot be undone.").arg(camPath_.keys.size()),
                                      QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes)
                return;
            camPathTimer_.stop();''')
patch(M, '''    addCommand(nullptr, "ViewEntityInfo", "Entity Inspector", QKeySequence(Qt::Key_N), [this] {''',
      '''    addCommand(view, "ViewEntityInfo", "Entity Inspector", QKeySequence(Qt::Key_N), [this] {''')
patch(M, '''                           QString("<b>IW8 Radiant Suite 0.1.0</b> (milestone 1: edit &rarr; save &rarr; reopen)<br><br>"''',
      '''                           QString("<b>IW8 Radiant Suite 0.1.0</b><br><br>"''')
patch(M, '''                                   "IW8 key table: %1 canonical ids<br>Qt %2 &middot; Qt Advanced Docking System 4.4.1<br><br>"
                                   "<i>In-game loading of edited data is not verified by this build.</i>")''',
      '''                                   "IW8 key table: %1 canonical ids<br>Qt %2 &middot; Qt Advanced Docking System 4.4.1<br><br>"
                                   "<i>docs\\\\TEST_CHECKLIST.md lists what has been seen in game and what has not.</i>")''')

H = 'src/qtapp/main_window.h'
patch(H, '''    void showDock(const QString& name);''', '''    void showDock(const QString& name);
    // Everything visible a click could select (Select All, Invert Selection): one ref per selection unit.
    std::set<SelRef> visibleSelectable() const;''')
print('ok')
