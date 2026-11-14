import io

# View > Umbra Visibility (plan M2.2): scene overlay lines, the camera's tome visibility pass-through, the prop ids,
# the menu and the camera hook.


def patch(p, a, b, n=1):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


SH = 'E:/IW8-Radiant/src/viewport/scene.h'
patch(SH, '''    std::vector<std::pair<Vec3, Vec3>> navLines;''', '''    // View > Umbra Visibility: the camera's cell, its portals and the visible cells around it (core umbra_view). The
    // version moves with every change (the camera's geometry key reads it).
    std::vector<UmbraLine> tomeLines;
    uint64_t tomeLinesVersion = 0;
    std::vector<std::pair<Vec3, Vec3>> navLines;''')
patch(SH, '#include "face_edges.h"\n', '#include "face_edges.h"\n#include "umbra_view.h"\n')
patch('E:/IW8-Radiant/src/viewport/camera_geometry.cpp', '''        for (const auto& [a, b] : scene.navLines) {  // the build's navmesh (6.23), green''', '''        for (const UmbraLine& l : scene.tomeLines) {  // Umbra cells and portals (M2.2)
            d.mLines.push_back(gv(l.a, l.rgba));
            d.mLines.push_back(gv(l.b, l.rgba));
        }
        for (const auto& [a, b] : scene.navLines) {  // the build's navmesh (6.23), green''')
patch('E:/IW8-Radiant/src/qtapp/camera_view.cpp', '''    mix(ctx_->scene.navLines.size() * 3 + 1);''', '''    mix(ctx_->scene.navLines.size() * 3 + 1);
    mix(ctx_->scene.tomeLinesVersion * 7 + 5);''')
patch('E:/IW8-Radiant/src/qtapp/camera_view.h', '''    size_t referenceTexInstanceMeshes() const { return texInstMaterials_.size(); }''', '''    size_t referenceTexInstanceMeshes() const { return texInstMaterials_.size(); }
    // Cull by tome (View > Umbra Visibility): SceneRenderer::setReferenceTomeVisibility. Empty = off.
    void setTomeVisibility(std::vector<uint32_t> surfaceBits, std::vector<uint32_t> modelBits) {
        renderer_.setReferenceTomeVisibility(std::move(surfaceBits), std::move(modelBits));
        update();
    }
    bool tomeCull() const { return renderer_.referenceTomeCull(); }
    size_t tomeCulledProps() const { return renderer_.tomeCulledProps(); }''')

patch('E:/IW8-Radiant/src/core/stock_smodels.h', '''    uint32_t collection = 0;                             // its GfxStaticModelCollection (the unit the Umbra tome culls)''',
      '''    uint32_t collection = 0xFFFFFFFFu;                   // its GfxStaticModelCollection (the unit the Umbra tome culls:
                                                         // user ID class 1); 0xFFFFFFFF = not a placement of the map''')

M = 'E:/IW8-Radiant/src/qtapp/main_window.cpp'
patch(M, '''            auto& ld = texInstanced[size_t(ti)].light;
            ld.insert(ld.end(), lit, lit + 12);''', '''            auto& ld = texInstanced[size_t(ti)].light;
            ld.insert(ld.end(), lit, lit + 12);
            texInstanced[size_t(ti)].ids.push_back((in.index & 0x80000000u) ? 0xFFFFFFFFu : in.collection);''')
patch(M, '''        QMenu* gmMenu = view->addMenu("Game Mode");''', '''        // M2.2: what the stock map's occlusion data (its Umbra tome) lets the game draw from the camera.
        QMenu* umbra = view->addMenu("Umbra Visibility");
        QAction* cull = addCommand(umbra, "UmbraCullByTome", "Cull by Tome (draw only what the game draws from here)", QKeySequence(), [this] {
            setUmbraView(action("UmbraCullByTome")->isChecked(), action("UmbraShowCells")->isChecked());
        });
        QAction* cells = addCommand(umbra, "UmbraShowCells", "Show Cells and Portals", QKeySequence(), [this] {
            setUmbraView(action("UmbraCullByTome")->isChecked(), action("UmbraShowCells")->isChecked());
        });
        cull->setCheckable(true);
        cells->setCheckable(true);
        umbraCull_ = Settings::instance().value("view/umbraCull", false).toBool();
        umbraOverlay_ = Settings::instance().value("view/umbraOverlay", false).toBool();
        cull->setChecked(umbraCull_);
        cells->setChecked(umbraOverlay_);
        umbraTimer_ = new QTimer(this);
        umbraTimer_->setSingleShot(true);
        umbraTimer_->setInterval(60);
        connect(umbraTimer_, &QTimer::timeout, this, &MainWindow::updateUmbraView);
        connect(&ctx_, &EditorContext::cameraChanged, this, [this] {
            if ((umbraCull_ || umbraOverlay_) && !umbraTimer_->isActive()) umbraTimer_->start();
        });
        QMenu* gmMenu = view->addMenu("Game Mode");''')

H = 'E:/IW8-Radiant/src/qtapp/main_window.h'
patch(H, '''    size_t exportReferenceBrushes();
''', '''    size_t exportReferenceBrushes();
    // M2.2 View > Umbra Visibility (main_window_umbra.cpp): cull = the reference draws only what the tome's query from
    // the camera made visible; overlay = the camera's cell, its portals and the visible cells. Saved in the settings.
    void setUmbraView(bool cull, bool overlay);
    void updateUmbraView();  // reads the tome when needed, then one query for the camera (a worker; one at a time)
    bool umbraCull() const { return umbraCull_; }
    bool umbraOverlay() const { return umbraOverlay_; }
    const UmbraViewResult& umbraLastResult() const { return umbraLast_; }  // counts of the last query (no bit sets)
    bool umbraBusy() const { return umbraLoading_ || umbraQueryRunning_ || umbraQueryPending_; }
''')
patch(H, '''    WorldCache* worldCache_ = nullptr;''', '''    WorldCache* worldCache_ = nullptr;
    void clearUmbraView(const QString& text);
    std::shared_ptr<const UmbraTome> umbraTome_;
    std::shared_ptr<const UmbraTomeIndex> umbraIndex_;
    UmbraViewResult umbraLast_;
    QString umbraZone_, umbraPose_;
    bool umbraCull_ = false, umbraOverlay_ = false, umbraLoading_ = false, umbraFailed_ = false;
    bool umbraQueryRunning_ = false, umbraQueryPending_ = false;
    QLabel* umbraLabel_ = nullptr;
    QTimer* umbraTimer_ = nullptr;''')
s = io.open(H, encoding='utf-8').read()
if '#include "umbra_view.h"' not in s:
    i = s.index('#include "')
    s = s[:i] + '#include "umbra_view.h"\n' + s[i:]
    io.open(H, 'w', encoding='utf-8', newline='').write(s)

U = 'E:/IW8-Radiant/src/qtapp/main_window_umbra.cpp'
patch(U, '''    if (umbraFailed_) return;
    if (umbraQueryRunning_) {
        umbraQueryPending_ = true;
        return;
    }
    // One view query for the camera as it is now.
    const iw8r::app::Camera cam = ctx_.cam;''', '''    if (umbraFailed_) return;
    // One view query for the camera as it is now; none when nothing changed since the last one (showing a result
    // repaints the camera, which reports a camera change).
    const iw8r::app::Camera cam = ctx_.cam;
    const QString pose = QString("%1 %2 %3 %4 %5 %6 %7 %8 %9").arg(cam.pos.x).arg(cam.pos.y).arg(cam.pos.z).arg(cam.yaw).arg(cam.pitch)
                             .arg(cam.fov).arg(camera_->width() * 10000 + camera_->height()).arg(umbraCull_).arg(umbraOverlay_);
    if (pose == umbraPose_ && !umbraQueryPending_) return;
    if (umbraQueryRunning_) {
        umbraQueryPending_ = true;
        return;
    }
    umbraPose_ = pose;''')
patch(U, '''    const iw8r::app::Camera cam = ctx_.cam;
    const double aspect =''', '''    const double aspect =''')
patch(U, '''void MainWindow::setUmbraView(bool cull, bool overlay) {
    umbraCull_ = cull;
    umbraOverlay_ = overlay;''', '''void MainWindow::setUmbraView(bool cull, bool overlay) {
    umbraCull_ = cull;
    umbraOverlay_ = overlay;
    umbraPose_.clear();
    if (QAction* a = action("UmbraCullByTome")) a->setChecked(cull);
    if (QAction* a = action("UmbraShowCells")) a->setChecked(overlay);''')
patch(U, '''                umbraZone_ = zone;
                umbraTome_ = tome;
                umbraIndex_ = index;
                updateUmbraView();''', '''                umbraZone_ = zone;
                umbraTome_ = tome;
                umbraIndex_ = index;
                umbraPose_.clear();
                updateUmbraView();''')
print('ok')
