import io

# Plan 3.1, the open point: the streamed world tiles, the near terrain layers and the Warzone probes follow one
# camera. While a browser's live view is being drawn that is the browser's camera; a few seconds after its last
# frame it is the editor's own again. One focus at a time: two cameras asking for different tiles would evict each
# other's from the cache.

ROOT = 'E:/IW8-Radiant/'


def patch(p, a, b, n=1):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


H = 'src/qtapp/main_window.h'
patch(H, '''    QByteArray webRenderFrame(const struct WebCamera& cam, int w, int h, int quality);''',
      '''    QByteArray webRenderFrame(const struct WebCamera& cam, int w, int h, int quality);
    // Where the streamed tiles, the terrain layers and the Warzone probes are kept around: the camera of a browser's
    // live view while it draws frames (and webFollowHoldMs after its last one), else the editor's own camera.
    Vec3 followCameraPos() const;
    void setWebFollowHoldMs(int ms) { webFollowHoldMs_ = ms; }  // tests''')
patch(H, '''    QTimer* streamTimer_ = nullptr;''', '''    QTimer* streamTimer_ = nullptr;
    Vec3 webFollowPos_;
    QElapsedTimer webFollowAt_;
    int webFollowHoldMs_ = 4000;''')
M = 'src/qtapp/main_window.cpp'
patch(M, '''    const app::Camera saved = ctx_.cam;
    ctx_.cam.pos = Vec3{cam.pos[0], cam.pos[1], cam.pos[2]};
    ctx_.cam.yaw = cam.yaw, ctx_.cam.pitch = cam.pitch, ctx_.cam.fov = cam.fov;
    const QImage img = camera_->renderImage(w, h, false, 0, 0, true);''', '''    const app::Camera saved = ctx_.cam;
    ctx_.cam.pos = Vec3{cam.pos[0], cam.pos[1], cam.pos[2]};
    ctx_.cam.yaw = cam.yaw, ctx_.cam.pitch = cam.pitch, ctx_.cam.fov = cam.fov;
    // The streamed world follows this view while it is in use (followCameraPos).
    webFollowPos_ = ctx_.cam.pos;
    webFollowAt_.restart();
    const QImage img = camera_->renderImage(w, h, false, 0, 0, true);''')
patch(M, '''bool MainWindow::webPick(const WebCamera& cam, double x, double y, double aspect, bool add) {''',
      '''Vec3 MainWindow::followCameraPos() const {
    return webFollowAt_.isValid() && webFollowAt_.elapsed() < webFollowHoldMs_ ? webFollowPos_ : ctx_.cam.pos;
}

bool MainWindow::webPick(const WebCamera& cam, double x, double y, double aspect, bool add) {''')
patch(M, '''    if (!terrainLayers_ || terrainLayerBusy_ || referenceZone_.isEmpty() || terrainLayers_->zone != referenceZone_) return;
    const Vec3 c = ctx_.cam.pos;''', '''    if (!terrainLayers_ || terrainLayerBusy_ || referenceZone_.isEmpty() || terrainLayers_->zone != referenceZone_) return;
    const Vec3 c = followCameraPos();''')
patch(M, '''    if (streamZones_.empty() || !worldCache_ || !stockWorld_ || !stockWorld_->surfaceTable || referenceZone_.isEmpty()) return;
    const Vec3 c = ctx_.cam.pos;''', '''    if (streamZones_.empty() || !worldCache_ || !stockWorld_ || !stockWorld_->surfaceTable || referenceZone_.isEmpty()) return;
    const Vec3 c = followCameraPos();''')
patch(M, '''    const auto wanted = streamedZonesNear(streamZones_, ctx_.cam.pos.x, ctx_.cam.pos.y, radius, limit);
    size_t built = 0, empty = 0, notExported = 0;''', '''    const Vec3 focus = followCameraPos();
    const auto wanted = streamedZonesNear(streamZones_, focus.x, focus.y, radius, limit);
    size_t built = 0, empty = 0, notExported = 0;''')
s = io.open(ROOT + M, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
at = s.index('void MainWindow::updateProbeSubset')
seg_end = s.index(nl + '}' + nl, at)
seg = s[at:seg_end]
assert seg.count('const Vec3 c = ctx_.cam.pos;') == 1, seg.count('const Vec3 c = ctx_.cam.pos;')
s = s[:at] + seg.replace('const Vec3 c = ctx_.cam.pos;', 'const Vec3 c = followCameraPos();') + s[seg_end:]
io.open(ROOT + M, 'w', encoding='utf-8', newline='').write(s)

patch('src/qtapp/qt_gui_test.cpp', '''                r.check(bad1.status == 400 && bad2.status == 400, "a frame request without a full camera or with a size out of range is refused");''',
      '''                r.check(bad1.status == 400 && bad2.status == 400, "a frame request without a full camera or with a size out of range is refused");
                {
                    // The streamed world's focus: the browser's camera while its view draws, the editor's own after.
                    w.setWebFollowHoldMs(150);
                    vweb.handle("GET", "/api/frame?pos=9000,-7000,500&yaw=0&pitch=0&fov=90&w=128&h=72&q=50", {}, true, {});
                    const Vec3 during = w.followCameraPos();
                    QTest::qWait(220);
                    const Vec3 after = w.followCameraPos();
                    w.setWebFollowHoldMs(4000);
                    r.check(std::fabs(during.x - 9000) < 1e-6 && std::fabs(during.y + 7000) < 1e-6 && after.x == ctx.cam.pos.x && after.y == ctx.cam.pos.y &&
                                ctx.cam.pos.x == camWas.pos.x,
                            "streamed tiles and terrain layers follow the browser's live view while it draws, and the editor's camera after");
                }''')
print('ok')
