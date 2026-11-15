// IW8 Radiant Suite - the camera view's render thread (owner 2026-11-15: "make the rendering thread for the
// viewport ... so its never frozen, and other workloads on others").
//
// The GUI thread prepares each frame in CameraView::render(): the scene, the uploads, the camera geometry and the frame
// input (prepared_). The render thread draws it and presents it, so the GUI thread no longer waits on the GPU or the
// display. Captures (renderImage), renderNow (tests, the benchmark), a profiled benchmark and a view without a window
// still draw on the calling thread; IW8R_RENDER_THREAD=0 draws every frame there, as before.
//
// While the GUI thread is busy (no frame prepared for 120 ms) and the right mouse button is held over the view, the
// render thread moves the fly camera itself from the keyboard (W A S D Q E, Shift) and the mouse, and keeps drawing;
// the GUI thread takes that camera over at its next frame and drops the mouse moves that queued meanwhile.
//
// Every use of the shared D3D11 immediate context and of the frame data is under gpuMutex() (gpu_lock.h); Present runs
// under swapMx_ only (the context is also multithread-protected, main.cpp). The GUI thread answers window messages
// sent to it while it waits for the render thread or the swap chain (DXGI may send some during Present).
#include "camera_view.h"

#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>

#include <algorithm>
#include <chrono>
#include <cmath>

namespace iw8r::qt {

std::recursive_timed_mutex& gpuMutex() {
    static std::recursive_timed_mutex m;
    return m;
}

namespace {
// Sent window messages (not posted ones) for this thread, so a thread waiting on it can go on.
void answerSentMessages() {
    MSG m;
    PeekMessageW(&m, nullptr, 0, 0, PM_NOREMOVE | PM_QS_SENDMESSAGE);
}
}  // namespace

bool CameraView::renderThreadEnabled() {
    static const bool off = qEnvironmentVariable("IW8R_RENDER_THREAD") == "0";
    return !off;
}

void CameraView::renderNow() {
    syncDraw_ = true;
    render();
    syncDraw_ = false;
}

void CameraView::lockSwap() {
    while (!swapMx_.try_lock_for(std::chrono::milliseconds(5))) answerSentMessages();
}

void CameraView::presentFrame(bool fromGui) {
    if (fromGui) lockSwap();
    else swapMx_.lock();
    std::lock_guard<std::timed_mutex> l(swapMx_, std::adopt_lock);
    if (swap_) swap_->Present(1, 0);
}

bool CameraView::drawPrepared(const Camera& cam) {
    std::lock_guard<std::recursive_timed_mutex> g(gpuMutex());
    if (!prepared_.valid) return false;
    app::FrameInput in = prepared_.in;
    const app::Mat4f vp = app::viewProjection(cam, prepared_.aspect);
    in.viewProj = vp.m;
    in.geometryChanged = prepared_.geometryChanged;
    prepared_.geometryChanged = false;
    prepared_.drawn = true;
    // The game's clock for animated materials (research 133 section 1); a capture shows time 0.
    // IW8R_CAPTURE_TIME (bench switch): the clock of a capture, in seconds, to compare animated materials offline.
    static const double captureTime = qEnvironmentVariableIsSet("IW8R_CAPTURE_TIME") ? qEnvironmentVariable("IW8R_CAPTURE_TIME").toDouble() : 0.0;
    renderer_->setGameTime(in.capture ? captureTime : (maskClock_.isValid() ? maskClock_.elapsed() / 1000.0 : 0.0));
    renderer_->drawFrame(in);
    // A custom surface shader reads the depth of the frame before (iw8rSceneDepth). After a frame from a new view one
    // more shows the settled picture: at once for a capture, else as the next frame (the caller asks for it).
    bool stale = false;
    if (renderer_->sceneDepthStale()) {
        if (in.capture || captureW_ > 0) {
            in.geometryChanged = false;
            renderer_->drawFrame(in);
        } else {
            stale = true;
        }
    }
    if (swap_) {
        ID3D11Texture2D* back = nullptr;
        if (SUCCEEDED(swap_->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&back)))) {
            D3D11_TEXTURE2D_DESC bd{};
            back->GetDesc(&bd);
            if (int(bd.Width) == renderer_->width() && int(bd.Height) == renderer_->height()) dc_->CopyResource(back, renderer_->colorTexture());
            back->Release();
        }
    }
    return stale;
}

void CameraView::startRenderThread() {
    if (renderThread_.joinable()) return;
    // The GUI thread's heartbeat: the render thread flies the camera only while it stops (a long task); an idle GUI
    // thread keeps beating and handles the input itself. A camera the render thread moved is taken over at once.
    if (!beat_) {
        beat_ = new QTimer(this);
        connect(beat_, &QTimer::timeout, this, [this] {
            guiBeatMs_ = static_cast<long long>(GetTickCount64());
            if (rtMovedHint_.exchange(false)) update();
        });
        guiBeatMs_ = static_cast<long long>(GetTickCount64());
        beat_->start(20);
    }
    {
        std::lock_guard l(frameMx_);
        renderQuit_ = false;
    }
    renderThread_ = std::thread([this] { renderLoop(); });
}

void CameraView::stopRenderThread() {
    if (!renderThread_.joinable()) return;
    {
        std::lock_guard l(frameMx_);
        renderQuit_ = true;
    }
    frameCv_.notify_all();
    HANDLE h = static_cast<HANDLE>(renderThread_.native_handle());
    while (MsgWaitForMultipleObjects(1, &h, FALSE, 50, QS_SENDMESSAGE) != WAIT_OBJECT_0) answerSentMessages();
    renderThread_.join();
}

void CameraView::renderLoop() {
    SetThreadDescription(GetCurrentThread(), L"iw8r viewport render");
    const auto quitting = [this] {
        std::lock_guard l(frameMx_);
        return renderQuit_;
    };
    for (;;) {
        bool want = false;
        {
            std::unique_lock l(frameMx_);
            // A frame the GUI thread prepared, or a tick (60 a second) for the fly camera while the GUI thread is busy.
            frameCv_.wait_for(l, std::chrono::milliseconds(16), [this] { return frameWanted_ || renderQuit_; });
            if (renderQuit_) return;
            want = frameWanted_;
            frameWanted_ = false;
        }
        const bool flew = flyTakeover();
        if (!want && !flew) continue;
        std::unique_lock<std::recursive_timed_mutex> g(gpuMutex(), std::defer_lock);
        while (!g.try_lock_for(std::chrono::milliseconds(20)))
            if (quitting()) return;
        if (!prepared_.valid || !swap_) continue;
        const Camera cam = rtMoved_ ? rtCam_ : prepared_.cam;
        const bool stale = drawPrepared(cam);
        ++rtFrames_;
        g.unlock();
        presentFrame(false);
        if (stale) {
            std::lock_guard l(frameMx_);
            frameWanted_ = true;
        }
    }
}

bool CameraView::flyTakeover() {
    if (!lookActive_.load()) {
        rtLastTick_ = 0;
        return false;
    }
    const long long now = static_cast<long long>(GetTickCount64());
    if (now - guiBeatMs_.load() < 120) {  // the GUI thread answers: it moves the camera itself
        rtLastTick_ = 0;
        return false;
    }
    const bool fake = fakeFly_.load();  // smoke test: the right button and W, the view in front
    if (!fake && !(GetAsyncKeyState(VK_RBUTTON) & 0x8000)) return false;
    if (HWND root = static_cast<HWND>(rootHwnd_.load()); !fake && (!root || GetForegroundWindow() != root)) return false;
    const double dt = rtLastTick_ ? std::clamp((now - rtLastTick_) / 1000.0, 0.0, 0.05) : 0.016;
    rtLastTick_ = now;
    const int lx = lookLockX_.load(), ly = lookLockY_.load();
    POINT p{lx, ly};
    if (!fake) GetCursorPos(&p);
    const double s = lookPixelScale_.load();
    const double dx = (p.x - lx) * s, dy = (p.y - ly) * s;
    const auto key = [fake](int vk) { return fake ? vk == 'W' : (GetAsyncKeyState(vk) & 0x8000) != 0; };
    std::unique_lock<std::recursive_timed_mutex> g(gpuMutex(), std::defer_lock);
    if (!g.try_lock_for(std::chrono::milliseconds(20)) || !prepared_.valid) return false;
    if (!rtMoved_) rtCam_ = prepared_.cam;
    bool moved = false;
    // The mouse turns the view as the GUI thread's right-drag look does (0.2 degrees a pixel), then goes back to the
    // lock point.
    if (dx != 0 || dy != 0) {
        rtCam_.yaw = std::fmod(rtCam_.yaw - dx * 0.2 + 360.0, 360.0);
        rtCam_.pitch = std::clamp(rtCam_.pitch - dy * 0.2, -89.0, 89.0);
        SetCursorPos(lx, ly);
        moved = true;
    }
    // W A S D Q E as EditorContext::tickCamera (looking), Shift x3.
    Vec3 move;
    if (key('W')) move += rtCam_.forward();
    if (key('S')) move += -rtCam_.forward();
    if (key('D')) move += rtCam_.right();
    if (key('A')) move += -rtCam_.right();
    if (key('E')) move += Vec3{0, 0, 1};
    if (key('Q')) move += Vec3{0, 0, -1};
    if (length(move) > 0) {
        const double speed = flySpeed_.load() * (key(VK_SHIFT) ? 3.0 : 1.0);
        rtCam_.pos += normalize(move) * (speed * dt);
        moved = true;
    }
    if (moved) rtMoved_ = true, rtMovedHint_ = true;
    return moved;
}

}  // namespace iw8r::qt
