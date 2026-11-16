// IW8 Radiant - small helpers of the camera's incremental geometry (docs/research/85). Header only, no D3D
// and no Qt, so the core test suite can include it.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "draw_chunks.h"
#include "frustum.h"
#include "geometry.h"

namespace iw8r::app {

// The parts of a vertex stream that changed since the GPU copy was last updated: a few half-open ranges
// [lo, hi) in vertices, kept sorted and merged. Past `kMax` ranges the two closest neighbours are joined, so the
// set never grows (joining only uploads a little more than needed).
class DirtyRanges {
public:
    static constexpr size_t kMax = 8;
    struct Range {
        size_t lo = 0, hi = 0;
    };
    void add(size_t lo, size_t hi) {
        if (hi <= lo) return;
        // Insert in order, then join everything that touches or overlaps.
        size_t at = 0;
        while (at < n_ && r_[at].lo < lo) ++at;
        for (size_t k = n_; k > at; --k) r_[k] = r_[k - 1];
        r_[at] = {lo, hi};
        ++n_;
        size_t out = 0;
        for (size_t k = 1; k < n_; ++k) {
            if (r_[k].lo <= r_[out].hi) r_[out].hi = std::max(r_[out].hi, r_[k].hi);
            else r_[++out] = r_[k];
        }
        n_ = out + 1;
        if (n_ > kMax) {
            // Join the two neighbours with the smallest gap.
            size_t best = 0;
            for (size_t k = 1; k + 1 < n_; ++k)
                if (r_[k + 1].lo - r_[k].hi < r_[best + 1].lo - r_[best].hi) best = k;
            r_[best].hi = r_[best + 1].hi;
            for (size_t k = best + 1; k + 1 < n_; ++k) r_[k] = r_[k + 1];
            --n_;
        }
    }
    void clear() { n_ = 0; }
    bool empty() const { return n_ == 0; }
    size_t size() const { return n_; }
    const Range& operator[](size_t k) const { return r_[k]; }
    size_t total() const {
        size_t t = 0;
        for (size_t k = 0; k < n_; ++k) t += r_[k].hi - r_[k].lo;
        return t;
    }

private:
    Range r_[kMax + 1];
    size_t n_ = 0;
};

// Calls draw(firstTriangle, triangleCount) for each run of neighbouring chunks that pass the frustum test, in chunk
// order: the same ranges as core visibleDrawRanges, without a vector per call. Returns the number of chunks left out.
template <class Draw>
size_t forVisibleRuns(const Frustum& fr, const std::vector<DrawChunk>& chunks, Draw&& draw) {
    size_t culled = 0;
    bool open = false;
    uint32_t first = 0, count = 0;
    for (const DrawChunk& c : chunks) {
        if (!frustumIntersectsAabb(fr, c.bounds)) {
            ++culled;
            continue;
        }
        if (open && first + count == c.firstTriangle) {
            count += c.triangleCount;
        } else {
            if (open) draw(first, count);
            first = c.firstTriangle;
            count = c.triangleCount;
            open = true;
        }
    }
    if (open) draw(first, count);
    return culled;
}

}  // namespace iw8r::app
