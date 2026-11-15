import io

# Owner 2026-11-14: "the default should be the models use the cached thumbnails, no flag needed". The editor keeps
# the textured picture packs complete by itself: while nobody touches the editor (30 s without input), it pictures
# the next catalogued models and prefabs that have no picture yet (exporting models as needed), a few at a time, and
# stops when a whole sweep finds nothing missing. Started for normal editor runs only (not the GUI test, the smoke
# test or the pack tool).

ROOT = 'E:/IW8-Radiant/'


def patch(p, a, b, count=1):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == count, (p, s.count(a), a[:70])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


H = 'src/qtapp/main_window.h'
patch(H, '''    class TexturedThumbs* thumbs3dForTests() { return texturedThumbs(); }''', '''    class TexturedThumbs* thumbs3dForTests() { return texturedThumbs(); }
    // Keeps the textured picture packs complete while the editor is idle (normal runs only; main.cpp).
    void startThumbnailTopUp();
protected:
    bool eventFilter(QObject* watched, QEvent* event) override;''')
patch(H, '''    class TexturedThumbs* thumbs3d_ = nullptr;''', '''    class TexturedThumbs* thumbs3d_ = nullptr;
    // The idle top-up of the picture packs: the last user input, the catalogue sweep's position and its work list.
    QElapsedTimer lastInput_;
    QStringList topUpNames_;  // models by name, prefabs by .map path (prefixed "p:")
    int topUpAt_ = 0, topUpFound_ = 0;
    void topUpThumbnails();''')

C = 'src/qtapp/main_window.cpp'
patch(C, '''int MainWindow::runThumbTest(const QString& asset, const QString& outDir) {''', '''void MainWindow::startThumbnailTopUp() {
    lastInput_.start();
    qApp->installEventFilter(this);
    auto* timer = new QTimer(this);
    timer->setInterval(5000);
    connect(timer, &QTimer::timeout, this, [this] { topUpThumbnails(); });
    timer->start();
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
    switch (event->type()) {
        case QEvent::KeyPress:
        case QEvent::MouseButtonPress:
        case QEvent::Wheel:
            if (lastInput_.isValid()) lastInput_.restart();
            break;
        default:
            break;
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::topUpThumbnails() {
    constexpr qint64 kIdleMs = 30000;
    constexpr int kPerTick = 12;
    TexturedThumbs* t = texturedThumbs();
    if (!t || !modelCacheStock_ || t->busy() || modelCacheStock_->busy()) return;
    if (!lastInput_.isValid() || lastInput_.elapsed() < kIdleMs) return;
    if (topUpNames_.isEmpty()) {
        // The work list: every catalogued model and every prefab of the search folders (made once per session).
        for (const QString& n : auditCatalogNames("models")) topUpNames_ << n;
        for (const QString& root : PrefabBrowser::searchRoots())
            for (QDirIterator it(root, {"*.map"}, QDir::Files, QDirIterator::Subdirectories); it.hasNext();) topUpNames_ << "p:" + it.next();
        topUpAt_ = 0;
        topUpFound_ = 0;
        if (topUpNames_.isEmpty()) topUpNames_ << QString();  // nothing to do: never built again
    }
    QStringList exportNames;
    int queued = 0;
    while (topUpAt_ < topUpNames_.size() && queued < kPerTick && exportNames.size() < 40) {
        const QString n = topUpNames_[topUpAt_++];
        if (n.isEmpty()) continue;
        if (n.startsWith("p:")) {
            const QString file = n.mid(2);
            if (!t->store(TexturedThumbs::Prefab).contains(file)) t->request(TexturedThumbs::Prefab, file), ++queued;
            continue;
        }
        if (t->store(TexturedThumbs::Model).contains(n)) continue;
        const auto e = modelCacheStock_->lookup(n);
        if (!e) exportNames << n;  // exported in a batch now, pictured on the next sweep
        else if (!e->failed) t->request(TexturedThumbs::Model, n), ++queued;
    }
    if (!exportNames.isEmpty()) modelCacheStock_->request(exportNames);
    topUpFound_ += queued + int(exportNames.size());
    if (topUpAt_ >= topUpNames_.size()) {
        if (topUpFound_ == 0) {
            topUpNames_ = QStringList{QString()};  // a whole sweep found nothing missing: done for this session
            topUpAt_ = 1;
        } else {
            topUpAt_ = 0;  // sweep again for what was being exported
            topUpFound_ = 0;
        }
    }
}

int MainWindow::runThumbTest(const QString& asset, const QString& outDir) {''')

M = 'src/qtapp/main.cpp'
patch(M, '''        if (!guiTest && !smoke) QTimer::singleShot(500, &w, [&w] { w.applyWebServerSettings(); });''',
      '''        if (!guiTest && !smoke) QTimer::singleShot(500, &w, [&w] { w.applyWebServerSettings(); });
        // The textured picture packs of the Models and Prefabs tabs stay complete by themselves (idle top-up).
        if (!guiTest && !smoke) w.startThumbnailTopUp();''')
print('ok')
