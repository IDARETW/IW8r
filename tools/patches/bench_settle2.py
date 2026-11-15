import io

# The bench moves the camera between its scenarios (camA / camB). On a Warzone map the near-camera terrain patch is
# then rebuilt on its timer, after the bench's settle had already counted four calm frames: the still frames were drawn
# with the patch (and the terrain cut) of the other position, so the ground near the bench camera showed the coarse
# terrain (2026-11-15). settle() now also waits until the patch is the camera's and the reference has nothing unresolved.

ROOT = 'E:/IW8-Radiant/'


def patch(p, a, b, count=1):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    if s.count(a) == 0 and b in s:
        return  # already applied
    assert s.count(a) == count, (p, s.count(a), a[:70])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


MH = 'src/qtapp/main_window.h'
patch(MH, '''    bool terrainLayersBusy() const { return terrainLayerBusy_; }  // the near-camera terrain patch is being built''',
      '''    bool terrainLayersBusy() const { return terrainLayerBusy_; }  // the near-camera terrain patch is being built
    // The near-camera terrain patch is built for where the camera is now (true on maps without one).
    bool terrainLayersCurrent() const;''')
M = 'src/qtapp/main_window.cpp'
patch(M, '''void MainWindow::updateTerrainLayers() {''', '''bool MainWindow::terrainLayersCurrent() const {
    if (!terrainLayers_ || referenceZone_.isEmpty() || terrainLayers_->zone != referenceZone_) return true;
    if (terrainLayerBusy_) return false;
    const Vec3 c = followCameraPos();
    return std::hypot(c.x - terrainLayerCentre_.x, c.y - terrainLayerCentre_.y) < std::min(2000.0, terrainLayerRadius() / 3.0);
}

void MainWindow::updateTerrainLayers() {''')
B = 'src/qtapp/main.cpp'
patch(B, '''        while (calm < 4 && st.elapsed() < 30000) {
            frame(nullptr);
            QApplication::processEvents(QEventLoop::AllEvents, 5);
            calm = cam->texturesPending() ? 0 : calm + 1;
        }''', '''        // Textures, the reference's unresolved images and layers, and a Warzone map's terrain patch for this camera
        // (rebuilt on its timer after a camera move).
        const auto busy = [&] { return cam->texturesPending() || cam->referencePending() || !w.terrainLayersCurrent(); };
        while (calm < 4 && st.elapsed() < (stockZone ? 120000 : 30000)) {
            frame(nullptr);
            QApplication::processEvents(QEventLoop::AllEvents, 5);
            calm = busy() ? 0 : calm + 1;
        }''')
print('ok')
