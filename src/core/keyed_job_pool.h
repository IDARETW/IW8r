// A small pool of worker threads that computes results by key and keeps each one until it is taken.
//
// The camera view decodes texture files with it (qtapp CameraView::decodedTexture): the frame asks for a file, goes on
// without it while a worker reads and decodes it, and finds it ready a few frames later. Only the upload to the GPU is
// left for the GUI thread.
//
//   take(key, job, out)        ready: the result is moved into `out`, the key is forgotten, true.
//                              not ready: false; the job is queued unless it already is (or the pool is full).
//   take(key, job, out, true)  the same, but it does not return before the result is there (run by a worker that
//                              already has it, else on the calling thread). Always true.
//
// A result nobody takes is dropped after `staleMs` once the pool is full. The ready callback is called on a worker
// thread after each finished job, without the pool's lock held. Header only.
#pragma once

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace iw8r {

template <class Result>
class KeyedJobPool {
public:
    using Job = std::function<Result()>;

    // threads 0: one fewer than the machine's, at least 1 and at most 4.
    explicit KeyedJobPool(unsigned threads = 0, size_t maxPending = 64, int staleMs = 5000) : maxPending_(std::max<size_t>(1, maxPending)), staleMs_(staleMs) {
        if (threads == 0) {
            const unsigned hw = std::thread::hardware_concurrency();
            threads = std::clamp(hw > 1 ? hw - 1 : 1u, 1u, 4u);
        }
        for (unsigned i = 0; i < threads; ++i) workers_.emplace_back([this] { run(); });
    }
    ~KeyedJobPool() {
        {
            std::lock_guard<std::mutex> lock(m_);
            stop_ = true;
            queue_.clear();
        }
        wake_.notify_all();
        for (std::thread& t : workers_) t.join();
    }
    KeyedJobPool(const KeyedJobPool&) = delete;
    KeyedJobPool& operator=(const KeyedJobPool&) = delete;

    bool take(const std::string& key, const Job& job, Result& out, bool wait = false) {
        std::unique_lock<std::mutex> lock(m_);
        auto it = entries_.find(key);
        if (it != entries_.end() && it->second->state == State::Ready) {
            out = std::move(it->second->result);
            entries_.erase(it);
            return true;
        }
        if (!wait) {
            if (it != entries_.end()) return false;  // queued or running
            if (entries_.size() >= maxPending_) dropStale();
            if (entries_.size() >= maxPending_) return false;  // full: asked again later
            auto e = std::make_shared<Entry>();
            e->job = job;
            entries_.emplace(key, e);
            queue_.push_back(key);
            lock.unlock();
            wake_.notify_one();
            return false;
        }
        if (it != entries_.end() && it->second->state == State::Running) {
            const std::shared_ptr<Entry> e = it->second;
            done_.wait(lock, [&] { return e->state == State::Ready; });
            out = std::move(e->result);
            // The entry may have been cleared or replaced meanwhile: remove it only if it is still this one.
            if (const auto now = entries_.find(key); now != entries_.end() && now->second == e) entries_.erase(now);
            return true;
        }
        if (it != entries_.end()) {  // queued: taken back and run here
            entries_.erase(it);
            queue_.erase(std::remove(queue_.begin(), queue_.end(), key), queue_.end());
        }
        lock.unlock();
        out = job();
        return true;
    }

    void setReadyCallback(std::function<void()> cb) {
        std::lock_guard<std::mutex> lock(m_);
        ready_ = std::move(cb);
    }

    // Queued, running and ready-but-not-taken keys.
    size_t pending() const {
        std::lock_guard<std::mutex> lock(m_);
        return entries_.size();
    }

    // Forgets every queued job and every result; a job that is running ends and its result is dropped.
    void clear() {
        std::lock_guard<std::mutex> lock(m_);
        queue_.clear();
        entries_.clear();
    }

private:
    enum class State { Queued, Running, Ready };
    struct Entry {
        State state = State::Queued;
        Job job;
        Result result{};
        std::chrono::steady_clock::time_point readyAt{};
    };

    void dropStale() {  // m_ held
        const auto now = std::chrono::steady_clock::now();
        for (auto it = entries_.begin(); it != entries_.end();) {
            const bool stale = it->second->state == State::Ready &&
                               std::chrono::duration_cast<std::chrono::milliseconds>(now - it->second->readyAt).count() >= staleMs_;
            it = stale ? entries_.erase(it) : std::next(it);
        }
    }

    void run() {
        for (;;) {
            std::shared_ptr<Entry> e;
            {
                std::unique_lock<std::mutex> lock(m_);
                wake_.wait(lock, [&] { return stop_ || !queue_.empty(); });
                if (stop_) return;
                const std::string key = std::move(queue_.front());
                queue_.pop_front();
                const auto it = entries_.find(key);
                if (it == entries_.end() || it->second->state != State::Queued) continue;  // cleared meanwhile
                e = it->second;
                e->state = State::Running;
            }
            Result r = e->job();
            std::function<void()> cb;
            {
                std::lock_guard<std::mutex> lock(m_);
                e->result = std::move(r);
                e->readyAt = std::chrono::steady_clock::now();
                e->state = State::Ready;
                e->job = nullptr;
                cb = ready_;
            }
            done_.notify_all();
            if (cb) cb();
        }
    }

    mutable std::mutex m_;
    std::condition_variable wake_, done_;
    std::map<std::string, std::shared_ptr<Entry>> entries_;
    std::deque<std::string> queue_;
    std::vector<std::thread> workers_;
    std::function<void()> ready_;
    size_t maxPending_;
    int staleMs_;
    bool stop_ = false;
};

}  // namespace iw8r
