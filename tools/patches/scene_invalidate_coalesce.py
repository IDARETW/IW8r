import io


def patch(p, a, b, n=1):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


MH = 'src/qtapp/main_window.h'
patch(MH, '''    bool lodPreview_ = false;''', '''    bool lodPreview_ = false;
    // Models and LODs arrive by the thousand on Warzone maps: one scene invalidation for a burst (each was a full
    // re-sync of every entity on the next paint; Verdansk's 47,545 MapEnts entities kept the UI busy for a minute).
    bool sceneInvalidateQueued_ = false;
    void invalidateSceneSoon();''')

M = 'src/qtapp/main_window.cpp'
patch(M, '''        if (auto* d = findChild<QDialog*>("modelInfoDialog"); d && d->isVisible() && d->windowTitle() == "Model Info: " + name)
            showModelInfo(name);
        ctx_.scene.invalidate();
        ctx_.cameraMoved();
    });''', '''        if (auto* d = findChild<QDialog*>("modelInfoDialog"); d && d->isVisible() && d->windowTitle() == "Model Info: " + name)
            showModelInfo(name);
        invalidateSceneSoon();
    });''')
patch(M, '''    connect(modelCacheStock_, &ModelCache::lodReady, this, [this](const QString& name, int) {
        ctx_.scene.invalidate();
        ctx_.cameraMoved();
        // A stock prop's LOD (plan 1.13): its kept mesh is rebuilt with it; the props once, when every LOD fetch is done.
        refPropBuilt_.erase(name.toStdString());''', '''    connect(modelCacheStock_, &ModelCache::lodReady, this, [this](const QString& name, int lod) {
        // The scene draws LOD0 except in the LOD preview.
        if (lodPreview_) invalidateSceneSoon();
        // A stock prop's LOD (plan 1.13): its kept mesh is rebuilt with it (not when the LOD could not be had: Verdansk
        // asks for thousands that fail, and each dropped a kept mesh); the props once, when every LOD fetch is done.
        if (modelCacheStock_->hasLod(name, lod)) {
            if (const auto it = refPropBuilt_.find(name.toStdString()); it != refPropBuilt_.end()) {
                refPropBuiltTris_ -= std::min(refPropBuiltTris_, it->second->tris);
                refPropBuilt_.erase(it);
            }
        }''')
patch(M, '''bool MainWindow::propLightFor(const PropLightKey& k, float out[12]) const {''', '''void MainWindow::invalidateSceneSoon() {
    if (sceneInvalidateQueued_) return;
    sceneInvalidateQueued_ = true;
    QTimer::singleShot(300, this, [this] {
        sceneInvalidateQueued_ = false;
        ctx_.scene.invalidate();
        ctx_.cameraMoved();
    });
}

bool MainWindow::propLightFor(const PropLightKey& k, float out[12]) const {''')
print('ok')
