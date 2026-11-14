// The background job pool the camera view decodes textures with (core keyed_job_pool.h).
#include "keyed_job_pool.h"

#include <atomic>
#include <chrono>
#include <stdexcept>
#include <string>
#include <thread>

#include "test_harness.h"

using namespace iw8r;

namespace {

// Waits until `ready()` or about two seconds have passed.
template <class F>
bool waitFor(F ready) {
    for (int i = 0; i < 2000; ++i) {
        if (ready()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return ready();
}

}  // namespace

TEST(keyed_job_pool_result_arrives_later_and_is_taken_once) {
    KeyedJobPool<int> pool(2);
    std::atomic<int> runs{0}, notified{0};
    pool.setReadyCallback([&] { ++notified; });
    const auto job = [&] {
        ++runs;
        return 42;
    };
    int out = 0;
    CHECK(!pool.take("a", job, out));  // queued: the caller goes on without it
    CHECK(pool.pending() == 1);
    CHECK(waitFor([&] { return notified.load() == 1; }));
    CHECK(pool.take("a", job, out) && out == 42);
    CHECK(pool.pending() == 0 && runs.load() == 1);
    // Taken: the key is forgotten, so asking again computes it again.
    out = 0;
    CHECK(!pool.take("a", job, out));
    CHECK(waitFor([&] { return notified.load() == 2; }));
    CHECK(pool.take("a", job, out) && out == 42 && runs.load() == 2);
}

TEST(keyed_job_pool_one_job_per_key_while_pending) {
    KeyedJobPool<int> pool(1);
    std::atomic<int> runs{0};
    std::atomic<bool> release{false};
    const auto slow = [&] {
        ++runs;
        while (!release.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        return 7;
    };
    int out = 0;
    for (int i = 0; i < 20; ++i) CHECK(!pool.take("k", slow, out));  // a frame asks again and again
    CHECK(pool.pending() == 1);
    release = true;
    CHECK(waitFor([&] { return pool.take("k", slow, out); }));
    CHECK(out == 7 && runs.load() == 1);
}

TEST(keyed_job_pool_wait_returns_the_result) {
    KeyedJobPool<std::string> pool(1);
    std::string out;
    // Nothing asked before: computed on the calling thread.
    CHECK(pool.take("x", [] { return std::string("inline"); }, out, true) && out == "inline");
    CHECK(pool.pending() == 0);
    // Asked before and still running: waits for the worker's result, and the job is not run twice.
    std::atomic<int> runs{0};
    std::atomic<bool> started{false}, release{false};
    const auto slow = [&] {
        ++runs;
        started = true;
        while (!release.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        return std::string("worker");
    };
    CHECK(!pool.take("y", slow, out));
    CHECK(waitFor([&] { return started.load(); }));
    std::thread later([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        release = true;
    });
    CHECK(pool.take("y", slow, out, true) && out == "worker" && runs.load() == 1);
    later.join();
    CHECK(pool.pending() == 0);
    // Queued behind a busy worker: taken back and run here.
    release = false;
    started = false;
    CHECK(!pool.take("busy", slow, out));
    CHECK(waitFor([&] { return started.load(); }));
    CHECK(!pool.take("z", [] { return std::string("queued"); }, out));
    CHECK(pool.take("z", [] { return std::string("here"); }, out, true) && out == "here");
    release = true;
    CHECK(waitFor([&] { return pool.take("busy", slow, out); }));
}

TEST(keyed_job_pool_full_pool_and_stale_results) {
    // Room for two keys; results nobody takes go after 30 ms once the room is needed.
    KeyedJobPool<int> pool(1, 2, 30);
    int out = 0;
    CHECK(!pool.take("a", [] { return 1; }, out));
    CHECK(!pool.take("b", [] { return 2; }, out));
    CHECK(waitFor([&] { return pool.pending() == 2; }));
    std::atomic<int> ran{0};
    const auto third = [&] {
        ++ran;
        return 3;
    };
    CHECK(!pool.take("c", third, out));  // full, and nothing is stale yet (or the stale ones just went)
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    CHECK(!pool.take("c", third, out));  // a and b are stale now: dropped, c is queued
    CHECK(waitFor([&] { return pool.take("c", third, out); }));
    CHECK(out == 3 && ran.load() == 1);
    // clear() forgets what is queued and what is ready.
    CHECK(!pool.take("d", [] { return 4; }, out));
    CHECK(waitFor([&] { return pool.pending() == 1; }));
    pool.clear();
    CHECK(pool.pending() == 0);
}

TEST(keyed_job_pool_destructor_ends_with_work_queued) {
    std::atomic<int> runs{0};
    {
        KeyedJobPool<int> pool(1);
        int out = 0;
        for (int i = 0; i < 10; ++i)
            pool.take("k" + std::to_string(i), [&] {
                ++runs;
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
                return 0;
            }, out);
    }  // joins: the running job ends, the queued ones are dropped
    CHECK(runs.load() <= 10);
}

TEST(keyed_job_pool_a_throwing_job_ends_as_an_empty_result) {
    KeyedJobPool<int> pool(1);
    std::atomic<int> notified{0};
    pool.setReadyCallback([&] { ++notified; });
    const auto bad = []() -> int { throw std::runtime_error("decoder ran out of memory"); };
    int out = 99;
    CHECK(!pool.take("bad", bad, out));
    CHECK(waitFor([&] { return notified.load() == 1; }));  // the entry became ready all the same
    CHECK(pool.take("bad", bad, out) && out == 0);
    // The worker is still alive for the next job.
    CHECK(!pool.take("good", [] { return 5; }, out));
    CHECK(waitFor([&] { return pool.take("good", [] { return 5; }, out); }));
    CHECK(out == 5);
    // Waiting for a job that throws returns too.
    out = 99;
    CHECK(pool.take("bad2", bad, out, true) && out == 0);
}
