import io
import re

# The viewport render thread (camera_view_render_thread.cpp): render() prepares the frame on the GUI thread and the
# render thread draws and presents it; every renderer call goes through the GPU lock (Guarded, gpu_lock.h).

ROOT = 'E:/IW8-Radiant/'


def read(p):
    return io.open(ROOT + p, encoding='utf-8', newline='').read()


def write(p, s):
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s)


def sub(s, a, b, count=1):
    assert s.count(a) == count, (s.count(a), a[:90])
    return s.replace(a, b)


H = 'src/qtapp/camera_view.h'
C = 'src/qtapp/camera_view.cpp'

h = read(H)
nl = '\r\n' if '\r\n' in h else '\n'
h = h.replace('\r\n', '\n')
if 'Guarded<app::SceneRenderer> renderer_' not in h:
    h = sub(h, '#include "sky_image.h"\n', '#include "sky_image.h"\n#include "gpu_lock.h"\n')
    h = sub(h, '#include <QWidget>\n', '#include <QWidget>\n#include <atomic>\n#include <condition_variable>\n#include <mutex>\n#include <thread>\n')
    h = sub(h, '    const app::SceneRenderer::FrameStats& frameStats() const { return renderer_.lastStats(); }',
            '    app::SceneRenderer::FrameStats frameStats() const { return renderer_.lastStats(); }')
    h = sub(h, '    const app::SceneRenderer::GameLook& gameLookForTest() const { return renderer_.gameLook(); }',
            '    app::SceneRenderer::GameLook gameLookForTest() const { return renderer_.gameLook(); }')
    h = sub(h, '    void renderNow() { render(); }', '    void renderNow();  // prepared and drawn on this thread (tests, the benchmark)')
    h = sub(h, '    app::SceneRenderer renderer_;\n', '''    Guarded<app::SceneRenderer> renderer_;  // every call takes the GPU lock (gpu_lock.h)
    // The render thread (camera_view_render_thread.cpp): it draws and presents the frames render() prepares, and moves
    // the fly camera while the GUI thread is busy.
    struct PreparedFrame {
        bool valid = false, drawn = true;
        app::FrameInput in;  // viewProj is made from the camera when the frame is drawn
        Camera cam;
        double aspect = 1;
        bool geometryChanged = false;  // a preparation not drawn yet changed the geometry
    };
    PreparedFrame prepared_;  // under gpuMutex()
    std::thread renderThread_;
    std::mutex frameMx_;
    std::condition_variable frameCv_;
    bool frameWanted_ = false, renderQuit_ = false;  // under frameMx_
    std::timed_mutex swapMx_;                        // Present (render thread) vs swap chain changes (GUI thread)
    std::atomic<long long> lastPrepareMs_{0};        // GetTickCount64 of the last prepared frame
    bool syncDraw_ = false;                          // renderNow: drawn and presented on the calling thread
    // The fly camera on the render thread: ctx_->looking at the last frame, the cursor's lock point in screen pixels
    // (set when the right button went down), screen pixels to the view's, the camera speed, the top-level window.
    std::atomic<bool> lookActive_{false};
    std::atomic<int> lookLockX_{0}, lookLockY_{0};
    std::atomic<double> lookPixelScale_{1.0}, flySpeed_{600.0};
    std::atomic<void*> rootHwnd_{nullptr};
    Camera rtCam_;          // the render thread's camera while it moves it (under gpuMutex())
    bool rtMoved_ = false;  // moved since the GUI thread last took it (under gpuMutex())
    std::atomic<unsigned long> dropMouseBefore_{0};  // mouse moves queued while the render thread moved the camera
    long long rtLastTick_ = 0;                       // render thread only
    static bool renderThreadEnabled();
    void startRenderThread();
    void stopRenderThread();
    void renderLoop();
    bool flyTakeover();
    bool drawPrepared(const Camera& cam);  // draws prepared_ and copies it to the back buffer; true: one more frame
    void presentFrame(bool fromGui);
    void lockSwap();  // GUI thread: swapMx_, answering window messages sent to it meanwhile
''')
    h = h.replace('renderer_.', 'renderer_->')
    h = h.replace('renderer_->veil %3', 'renderer_veil %3')  # a string, not a call
    write(H, h.replace('\n', nl))

c = read(C)
nl = '\r\n' if '\r\n' in c else '\n'
c = c.replace('\r\n', '\n')
if 'drawPrepared(ctx_->cam)' not in c:
    c = sub(c, '''CameraView::~CameraView() {
    if (swap_) swap_->Release();
}''', '''CameraView::~CameraView() {
    stopRenderThread();
    if (swap_) swap_->Release();
}''')
    # Swap chain changes: under the GPU lock and swapMx_ (the render thread presents under swapMx_).
    c = sub(c, '''    HWND hwnd = reinterpret_cast<HWND>(winId());
    if (!IsWindow(hwnd)) return;  // e.g. the offscreen platform used by --gui-test
    if (swap_ && swapHwnd_ != hwnd) {''', '''    HWND hwnd = reinterpret_cast<HWND>(winId());
    if (!IsWindow(hwnd)) return;  // e.g. the offscreen platform used by --gui-test
    rootHwnd_ = GetAncestor(hwnd, GA_ROOT);
    if (swap_ && swapHwnd_ == hwnd && w == swapW_ && h == swapH_) return;
    std::lock_guard<std::recursive_timed_mutex> gpuLock(gpuMutex());
    lockSwap();
    std::lock_guard<std::timed_mutex> swapLock(swapMx_, std::adopt_lock);
    if (swap_ && swapHwnd_ != hwnd) {''')
    # render(): the GPU lock for the whole preparation; take over the render thread's camera.
    c = sub(c, '''void CameraView::render() {
    if (!rendererOk_) return;
''', '''void CameraView::render() {
    if (!rendererOk_) return;
    std::unique_lock<std::recursive_timed_mutex> gpuLock(gpuMutex());
    // The render thread moved the fly camera while this thread was busy: carry on from there, and drop the mouse moves
    // that queued meanwhile (it has turned the view by them already).
    if (rtMoved_) {
        ctx_->cam.pos = rtCam_.pos, ctx_->cam.yaw = rtCam_.yaw, ctx_->cam.pitch = rtCam_.pitch;
        rtMoved_ = false;
        dropMouseBefore_ = GetTickCount();
        ctx_->cameraMoved();
    }
''')
    i = c.index('    const app::Mat4f vp = app::viewProjection(ctx_->cam, captureAspect_ > 0 ? captureAspect_ : double(w) / double(h));\n')
    j = c.index('    swap_->Present(1, 0);\n}\n', i) + len('    swap_->Present(1, 0);\n}\n')
    c = c[:i] + '''    // The frame input; the view is made from the camera when the frame is drawn (drawPrepared).
    {
        app::FrameInput in;
        in.clear = kClear;
        in.shadowOnly = &geo_.shadowOnly;
        in.casters = &geo_.casters;
        in.faceSel = &geo_.faceSel;
        in.noCast = &geo_.noCast;
        in.lines = &geo_.lines;
        in.linesMisc = &geo_.linesMisc;
        in.overlay[0] = &geo_.sunArrow;
        in.overlay[1] = &geo_.selLines;
        in.overlay[2] = &geo_.overlayMisc;
        in.overlay[3] = &gizmoLines_;
        in.batches = &geo_.batches();
        in.extraBatches = &extraBatches_;
        in.casterVersion = geo_.casterVersion();
        in.clean = cleanCapture_;  // game surfaces only: no markers, tool faces, volumes or lines
        in.capture = cleanCapture_ || ctx_->scene.captureMode;  // no editor-only reference markers in a capture
        prepared_.in = in;
        prepared_.cam = ctx_->cam;
        prepared_.aspect = captureAspect_ > 0 ? captureAspect_ : double(w) / double(h);
        prepared_.geometryChanged = prepared_.geometryChanged || rebuild;  // until a draw has taken it
        prepared_.valid = true;
        prepared_.drawn = false;
        if (!maskClock_.isValid()) maskClock_.start();
        // While a material with UV animation is drawn, the view keeps redrawing (30 a second, the editor's window active).
        if (uvAnimSeen_ && !in.capture && !uvAnimTick_ && window() && window()->isActiveWindow()) {
            uvAnimTick_ = true;
            QTimer::singleShot(33, this, [this] {
                uvAnimTick_ = false;
                update();
            });
        }
    }
    lookActive_ = ctx_->looking && !ctx_->playing && !orbit_;
    flySpeed_ = ctx_->cam.speed;
    lookPixelScale_ = 1.0 / std::max(0.5, devicePixelRatioF());
    lastPrepareMs_ = static_cast<long long>(GetTickCount64());
    if (moreToLoad_ || refMoreToLoad_)
        QMetaObject::invokeMethod(this, [this] { update(); }, Qt::QueuedConnection);  // finish loading next frame
    ensureSwapChain();
    // Drawn here: captures, renderNow, a profiled benchmark, a view without a window, or IW8R_RENDER_THREAD=0.
    if (syncDraw_ || captureW_ > 0 || !swap_ || app::Prof::on || !renderThreadEnabled()) {
        bool stale = false;
        {
            IW8R_PROF("r.draw");
            stale = drawPrepared(ctx_->cam);
        }
        if (stale) QMetaObject::invokeMethod(this, [this] { update(); }, Qt::QueuedConnection);
        if (!swap_) return;
        IW8R_PROF("r.present");
        presentFrame(true);
        return;
    }
    // Drawn and presented by the render thread.
    startRenderThread();
    {
        std::lock_guard l(frameMx_);
        frameWanted_ = true;
    }
    frameCv_.notify_one();
}
''' + c[j:]
    # Captures hold the GPU lock throughout (captureW_ is read by the render thread).
    c = sub(c, '''QImage CameraView::renderImage(int w, int h, bool clean, double aspect, int settleMs, bool keepGeometry) {
    if (!rendererOk_ || w < 1 || h < 1 || w > 16384 || h > 16384) return {};
''', '''QImage CameraView::renderImage(int w, int h, bool clean, double aspect, int settleMs, bool keepGeometry) {
    if (!rendererOk_ || w < 1 || h < 1 || w > 16384 || h > 16384) return {};
    std::lock_guard<std::recursive_timed_mutex> gpuLock(gpuMutex());
''')
    # grabFrame: a prepared frame the render thread has not drawn yet is drawn first.
    c = sub(c, '''QImage CameraView::grabFrame() {
    std::vector<uint8_t> px;
    int w = 0, h = 0;''', '''QImage CameraView::grabFrame() {
    std::lock_guard<std::recursive_timed_mutex> gpuLock(gpuMutex());
    if (prepared_.valid && !prepared_.drawn) drawPrepared(prepared_.cam);
    std::vector<uint8_t> px;
    int w = 0, h = 0;''')
    c = sub(c, '''size_t CameraView::texturedTriangles() const {
''', '''size_t CameraView::texturedTriangles() const {
    std::lock_guard<std::recursive_timed_mutex> gpuLock(gpuMutex());
''')
    c = sub(c, '''QString CameraView::geometrySelfCheck() {
''', '''QString CameraView::geometrySelfCheck() {
    std::lock_guard<std::recursive_timed_mutex> gpuLock(gpuMutex());
''')
    # The right button's lock point in screen pixels (the render thread's mouse look).
    c = sub(c, '''        lmbDown_ = false;  // the view turns: a left release now would select whatever is under the cursor then
        lockPos_ = e->position().toPoint();''', '''        lmbDown_ = false;  // the view turns: a left release now would select whatever is under the cursor then
        lockPos_ = e->position().toPoint();
        if (POINT cp{}; GetCursorPos(&cp)) lookLockX_ = int(cp.x), lookLockY_ = int(cp.y);''')
    # Mouse moves that queued while the render thread turned the view are dropped.
    c = sub(c, '''    const QPoint p = e->position().toPoint();
    const QPoint d = p - lockPos_;
    if (d.isNull()) return;
    lookTravel_ += std::abs(d.x()) + std::abs(d.y());''', '''    if (const unsigned long drop = dropMouseBefore_.load(); drop && e->timestamp() <= drop) return;  // applied already
    const QPoint p = e->position().toPoint();
    const QPoint d = p - lockPos_;
    if (d.isNull()) return;
    lookTravel_ += std::abs(d.x()) + std::abs(d.y());''')
    c = c.replace('renderer_.', 'renderer_->')
    write(C, c.replace('\n', nl))
print('ok')
