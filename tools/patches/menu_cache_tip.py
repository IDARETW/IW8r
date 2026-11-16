import io

# The start-up tip that offers the one-time full menu cache build, and Tools > Build Full Menu Cache (owner
# 2026-11-16: "have a user starting tip to run the full cache"; "they're meant to still be compressed": the build
# writes the compressed menu packs; its raw exports are temporary).

def rw(p, pairs):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    s = s.replace('\r\n', '\n')
    for a, b in pairs:
        if b in s and a not in s:
            continue
        assert s.count(a) == 1, (p, s.count(a), a[:80])
        s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s.replace('\n', nl))


S = 'E:/IW8-Radiant/src/qtapp/'
rw(S + 'main_window.h', [
('''    int runMenuPack(const QString& kinds, const QString& outDir, bool fill, int limit);''', '''    int runMenuPack(const QString& kinds, const QString& outDir, bool fill, int limit);
    // The full menu cache (menu_pack.h): built in a separate process (`--menu-pack all --fill`, offscreen); the panels
    // read the new packs when it ends. The start-up tip offers it until it has run once.
    void startMenuCacheBuild();
    void maybeOfferMenuCache();
    bool menuCacheBuilding() const { return menuCacheProc_ != nullptr; }'''),
('''    QHash<QString, QString> vfxChildZone_;''', '''    class QProcess* menuCacheProc_ = nullptr;  // the menu cache build running
    QHash<QString, QString> vfxChildZone_;'''),
])
rw(S + 'main_window.cpp', [
('''    addCommand(tools, "GameModes", "&Game Modes (Warzone and mode presets)...", QKeySequence(), [this] { showGameModes(); });''',
 '''    addCommand(tools, "GameModes", "&Game Modes (Warzone and mode presets)...", QKeySequence(), [this] { showGameModes(); });
    addCommand(tools, "BuildMenuCache", "Build Full Menu Cache (pictures, previews and facts of every asset)...", QKeySequence(),
               [this] { startMenuCacheBuild(); });'''),
('''    buildMenus();''', '''    buildMenus();
    // The one-time full menu cache: offered a moment after start-up until it has been built (menu_pack.h).
    QTimer::singleShot(5000, this, [this] { maybeOfferMenuCache(); });'''),
('''QStringList ddsFactsOf(const std::string& headerBytes) { return ddsFacts(headerBytes); }''', '''QStringList ddsFactsOf(const std::string& headerBytes) { return ddsFacts(headerBytes); }

void MainWindow::maybeOfferMenuCache() {
    // Not in automated runs (offscreen), not once built, not after "Don't ask again".
    if (QGuiApplication::platformName() == "offscreen" || qEnvironmentVariable("IW8R_NO_MENU_PACKS") == "1" || MenuPack::complete() ||
        menuCacheProc_ || Settings::instance().value("tips/menuCacheNever", false).toBool())
        return;
    auto* box = new QMessageBox(QMessageBox::Information, "Build the full menu cache?",
                                "The side panels can show a picture, a preview and the facts of every material, model, image, "
                                "effect, sound and prefab at once, without exporting anything while you work, and every effect "
                                "preview can play straight away.\\n\\n"
                                "Building that cache runs once, in the background, in a separate process (a few hours: it exports "
                                "each asset with ACTS, keeps only small compressed packs and deletes the exports). You can keep "
                                "working meanwhile; the panels use the cache as soon as it is done. Tools > Build Full Menu Cache "
                                "runs it later.",
                                QMessageBox::NoButton, this);
    box->setObjectName("menuCacheTip");
    box->setAttribute(Qt::WA_DeleteOnClose);
    QAbstractButton* build = box->addButton("Build now", QMessageBox::AcceptRole);
    box->addButton("Later", QMessageBox::RejectRole);
    QAbstractButton* never = box->addButton("Don't ask again", QMessageBox::DestructiveRole);
    box->setModal(false);
    connect(box, &QMessageBox::buttonClicked, this, [this, build, never](QAbstractButton* b) {
        if (b == build) startMenuCacheBuild();
        else if (b == never) Settings::instance().setValue("tips/menuCacheNever", true);
    });
    box->show();
}

void MainWindow::startMenuCacheBuild() {
    if (menuCacheProc_) {
        ctx_.setStatus("The menu cache is being built already (the Console shows its progress)");
        return;
    }
    // The packs are closed while the builder replaces them; the panels fall back to the caches meanwhile.
    MenuPack::suspend(true);
    auto* p = new QProcess(this);
    menuCacheProc_ = p;
    p->setProgram(QCoreApplication::applicationFilePath());
    p->setArguments({"--menu-pack", "all", "--fill"});
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("QT_QPA_PLATFORM", "offscreen");  // no window
    p->setProcessEnvironment(env);
    p->setProcessChannelMode(QProcess::MergedChannels);
    connect(p, &QProcess::readyRead, this, [this, p] {
        while (p->canReadLine()) {
            const QString line = QString::fromLocal8Bit(p->readLine()).trimmed();
            if (line.isEmpty()) continue;
            EditorLog::instance().add(LogLevel::Info, "menucache", line);
            ctx_.setStatus("Menu cache: " + line);
        }
    });
    connect(p, &QProcess::finished, this, [this, p](int code, QProcess::ExitStatus st) {
        menuCacheProc_ = nullptr;
        p->deleteLater();
        MenuPack::suspend(false);
        for (AssetBrowser* b : {materials_, models_, effects_, images_, sounds_})
            if (b) b->reloadPack();
        if (prefabs_) prefabs_->refresh();
        const bool ok = st == QProcess::NormalExit && code == 0;
        if (toasts_)
            toasts_->showToast(ok ? LogLevel::Info : LogLevel::Warning,
                               ok ? "The full menu cache is built: the panels use it now" : "The menu cache build stopped (Console: menucache)", "menucache");
    });
    p->start();
    if (toasts_)
        toasts_->showToast(LogLevel::Info, "Building the full menu cache in the background (a few hours; Console: menucache)", "menucache");
}'''),
])
print('ok')
