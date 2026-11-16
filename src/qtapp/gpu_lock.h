// IW8 Radiant - the GPU lock (owner 2026-11-15: "make the rendering thread for the viewport").
//
// Every camera view (the main one and the 3D preview windows) draws with the editor's one D3D11 immediate context,
// from the GUI thread (preparing frames, uploads, captures) and from its render thread (drawing). A device context is
// not thread-safe and its state (shaders, targets, buffers) is set call by call, so all use of it, and of the frame
// data a render thread reads (the camera geometry streams, the extra batches, the prepared frame), is under this one
// recursive lock. Guarded<T> takes it for each call made through operator-> (renderer_->drawFrame(...)).
// It is a timed mutex so a render thread can give up waiting when its view is being closed.
#pragma once

#include <mutex>

namespace iw8r::qt {

std::recursive_timed_mutex& gpuMutex();

template <class T>
class Guarded {
public:
    template <class P>
    struct Proxy {
        P* p;
        std::unique_lock<std::recursive_timed_mutex> lock;
        P* operator->() const { return p; }
    };
    // The lock is held until the end of the full expression: renderer_->a() is one locked call. A reference a call
    // returns must not be kept past it (copy the value instead).
    Proxy<T> operator->() { return Proxy<T>{&obj_, std::unique_lock<std::recursive_timed_mutex>(gpuMutex())}; }
    Proxy<const T> operator->() const { return Proxy<const T>{&obj_, std::unique_lock<std::recursive_timed_mutex>(gpuMutex())}; }

private:
    T obj_;
};

}  // namespace iw8r::qt
