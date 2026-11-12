// Pure helpers of the camera's kept geometry (src/viewport/dirty_ranges.h, docs/research/85): the changed-range
// set of a vertex stream and the allocation-free visible-chunk loop the renderer culls with.
#include "../src/viewport/dirty_ranges.h"

#include <cstdint>
#include <random>
#include <utility>
#include <vector>

#include "draw_chunks.h"
#include "frustum.h"
#include "test_harness.h"

using namespace iw8r;
using iw8r::app::DirtyRanges;

namespace {

AABB boxAt(double x, double y, double z, double half) {
    AABB b;
    b.add(Vec3{x - half, y - half, z - half});
    b.add(Vec3{x + half, y + half, z + half});
    return b;
}

bool covered(const DirtyRanges& d, size_t v) {
    for (size_t k = 0; k < d.size(); ++k)
        if (v >= d[k].lo && v < d[k].hi) return true;
    return false;
}

}  // namespace

TEST(dirty_ranges_merge_overlapping_and_touching) {
    DirtyRanges d;
    CHECK(d.empty());
    d.add(10, 20);
    d.add(30, 40);
    CHECK(d.size() == 2);
    d.add(20, 30);  // touches both: one range
    CHECK(d.size() == 1);
    CHECK(d[0].lo == 10 && d[0].hi == 40);
    d.add(5, 12);  // overlaps the start
    CHECK(d.size() == 1 && d[0].lo == 5 && d[0].hi == 40);
    d.add(100, 100);  // empty: ignored
    d.add(7, 3);      // reversed: ignored
    CHECK(d.size() == 1);
    CHECK(d.total() == 35);
    d.clear();
    CHECK(d.empty() && d.total() == 0);
}

TEST(dirty_ranges_stay_sorted_and_within_the_cap) {
    DirtyRanges d;
    // Far more separate ranges than the cap: the closest neighbours are joined, nothing marked is lost.
    std::vector<std::pair<size_t, size_t>> added;
    std::mt19937 rng(7);
    for (int i = 0; i < 200; ++i) {
        const size_t lo = rng() % 100000, len = 1 + rng() % 50;
        d.add(lo, lo + len);
        added.push_back({lo, lo + len});
        CHECK(d.size() <= DirtyRanges::kMax);
        for (size_t k = 1; k < d.size(); ++k) CHECK(d[k - 1].hi < d[k].lo);  // sorted, not touching
    }
    bool all = true;
    for (const auto& [lo, hi] : added)
        for (size_t v = lo; v < hi; ++v) all = all && covered(d, v);
    CHECK(all);
}

TEST(dirty_ranges_join_the_smallest_gap_first) {
    DirtyRanges d;
    for (size_t k = 0; k < DirtyRanges::kMax; ++k) d.add(k * 1000, k * 1000 + 10);
    CHECK(d.size() == DirtyRanges::kMax);
    d.add(3015, 3020);  // five vertices after the range at 3000: that gap is the smallest
    CHECK(d.size() == DirtyRanges::kMax);
    CHECK(d[3].lo == 3000 && d[3].hi == 3020);
    CHECK(d[4].lo == 4000);
}

TEST(visible_runs_match_core_visible_draw_ranges) {
    // The renderer's loop gives the ranges of core visibleDrawRanges, for any mix of visible and culled chunks.
    const Frustum fr = makeFrustum(Vec3{0, 0, 0}, 0, 0, 60, 1, 4, 131072);
    std::mt19937 rng(11);
    for (int round = 0; round < 50; ++round) {
        std::vector<DrawChunk> chunks;
        uint32_t first = 0;
        const int n = 1 + int(rng() % 40);
        for (int i = 0; i < n; ++i) {
            DrawChunk c;
            c.firstTriangle = first;
            c.triangleCount = 1 + rng() % 2048;
            first += c.triangleCount;
            const bool ahead = rng() % 2 == 0;
            c.bounds = boxAt(ahead ? 500.0 + double(rng() % 400) : -900.0, double(int(rng() % 200)) - 100.0, 0, 20);
            chunks.push_back(c);
        }
        size_t culledCore = 0;
        const std::vector<DrawRange> want = visibleDrawRanges(fr, chunks, &culledCore);
        std::vector<DrawRange> got;
        const size_t culled = app::forVisibleRuns(fr, chunks, [&](uint32_t f, uint32_t count) { got.push_back({f, count}); });
        CHECK(culled == culledCore);
        CHECK(got.size() == want.size());
        bool same = got.size() == want.size();
        for (size_t k = 0; same && k < got.size(); ++k)
            same = got[k].firstTriangle == want[k].firstTriangle && got[k].triangleCount == want[k].triangleCount;
        CHECK(same);
    }
}
