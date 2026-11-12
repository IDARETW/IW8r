// Brush-local terrain sculpt tests: smoothAround, flattenAround and noiseAround (src/core/patch_ops2.cpp).
#include <cmath>
#include <cstdint>

#include "patch_ops.h"
#include "patch_ops2.h"
#include "test_harness.h"

using namespace iw8r;

namespace {

constexpr double kEps = 1e-9;

// Flat 5x5 grid at z = 0, 64 units apart in x and y, starting at the origin.
PatchGrid flatGrid5() {
    PatchGrid g;
    g.rows = 5;
    g.cols = 5;
    g.material = "m";
    for (int r = 0; r < 5; ++r)
        for (int c = 0; c < 5; ++c) g.pos.push_back(Vec3{c * 64.0, r * 64.0, 0.0});
    g.st.assign(g.pos.size(), {0.0, 0.0});
    return g;
}

size_t at(const PatchGrid& g, int r, int c) { return static_cast<size_t>(r) * static_cast<size_t>(g.cols) + static_cast<size_t>(c); }

bool samePos(const PatchGrid& a, const PatchGrid& b) {
    if (a.pos.size() != b.pos.size()) return false;
    for (size_t i = 0; i < a.pos.size(); ++i)
        if (a.pos[i].x != b.pos[i].x || a.pos[i].y != b.pos[i].y || a.pos[i].z != b.pos[i].z) return false;
    return true;
}

}  // namespace

TEST(terrain_sculpt_smooth_reduces_spike) {
    PatchGrid g = flatGrid5();
    const Vec3 centre{128, 128, 0};  // the middle control point (2, 2)
    g.pos[at(g, 2, 2)].z = 100.0;

    const PatchGrid s1 = smoothAround(g, centre, 200.0, 0.5);
    const double spike1 = s1.pos[at(s1, 2, 2)].z;
    // Neighbours are at 0, so one step moves the spike by strength * 1 toward 0: 100 -> 50.
    CHECK(std::fabs(spike1 - 50.0) < kEps);
    CHECK(s1.pos[at(s1, 1, 2)].z > 0.0);  // neighbours pick up part of the spike
    CHECK(s1.pos[at(s1, 2, 1)].z > 0.0);

    // Repeated strokes keep lowering the spike.
    PatchGrid s = g;
    double prev = 100.0;
    for (int i = 0; i < 6; ++i) {
        s = smoothAround(s, centre, 200.0, 0.5);
        const double now = s.pos[at(s, 2, 2)].z;
        CHECK(now < prev);
        prev = now;
    }
    CHECK(prev > 0.0);
    CHECK(prev < 50.0);  // below the first step's value

    // Points outside the radius and all x, y values are untouched.
    const PatchGrid small = smoothAround(g, centre, 70.0, 1.0);  // reaches the four direct neighbours only
    CHECK(small.pos[at(small, 0, 0)].z == 0.0);
    CHECK(small.pos[at(small, 4, 4)].z == 0.0);
    for (size_t i = 0; i < g.pos.size(); ++i) {
        CHECK(s1.pos[i].x == g.pos[i].x && s1.pos[i].y == g.pos[i].y);
    }

    // Zero strength and a non-positive radius are no-ops.
    CHECK(samePos(smoothAround(g, centre, 200.0, 0.0), g));
    CHECK(samePos(smoothAround(g, centre, 0.0, 0.5), g));
}

TEST(terrain_sculpt_flatten_moves_toward_target) {
    PatchGrid g = flatGrid5();
    for (Vec3& p : g.pos) p.z = 0.0;
    const Vec3 centre{128, 128, 0};
    const double radius = 128.0, target = -20.0;

    const PatchGrid full = flattenAround(g, centre, radius, target, 1.0);
    CHECK(std::fabs(full.pos[at(full, 2, 2)].z - target) < kEps);  // centre reaches the target at full strength

    const PatchGrid half = flattenAround(g, centre, radius, target, 0.5);
    CHECK(std::fabs(half.pos[at(half, 2, 2)].z - (-10.0)) < kEps);

    for (size_t i = 0; i < g.pos.size(); ++i) {
        const double d = std::hypot(g.pos[i].x - centre.x, g.pos[i].y - centre.y);
        if (d >= radius) {
            // At or beyond the radius nothing moves (the point at distance exactly 128 included).
            CHECK(full.pos[i].z == g.pos[i].z);
            CHECK(half.pos[i].z == g.pos[i].z);
        } else {
            // Inside: moved toward the target, never past it.
            CHECK(full.pos[i].z <= 0.0 && full.pos[i].z >= target - kEps);
            CHECK(half.pos[i].z <= 0.0 && half.pos[i].z >= target - kEps);
            CHECK(std::fabs(full.pos[i].z - target) <= std::fabs(half.pos[i].z - target) + kEps);
        }
        CHECK(full.pos[i].x == g.pos[i].x && full.pos[i].y == g.pos[i].y);
    }

    // Flattening a bump: a point above the target comes down, one below comes up.
    PatchGrid bumpy = flatGrid5();
    bumpy.pos[at(bumpy, 2, 2)].z = 50.0;
    bumpy.pos[at(bumpy, 2, 1)].z = -50.0;
    const PatchGrid f = flattenAround(bumpy, centre, 200.0, 0.0, 0.5);
    CHECK(f.pos[at(f, 2, 2)].z < 50.0 && f.pos[at(f, 2, 2)].z > 0.0);
    CHECK(f.pos[at(f, 2, 1)].z > -50.0 && f.pos[at(f, 2, 1)].z < 0.0);

    CHECK(samePos(flattenAround(g, centre, 0.0, target, 1.0), g));
    CHECK(samePos(flattenAround(g, centre, radius, target, 0.0), g));
}

TEST(terrain_sculpt_noise_deterministic_for_seed) {
    const PatchGrid base = flatGrid5();
    const Vec3 centre{128, 128, 0};
    const double radius = 200.0, amplitude = 8.0;

    const PatchGrid n1 = noiseAround(base, centre, radius, amplitude, 42);
    CHECK(samePos(n1, noiseAround(base, centre, radius, amplitude, 42)));
    CHECK(!samePos(n1, noiseAround(base, centre, radius, amplitude, 43)));
    CHECK(samePos(noiseAround(base, centre, radius, 0.0, 42), base));

    bool moved = false;
    for (size_t i = 0; i < base.pos.size(); ++i) {
        const double d = std::hypot(base.pos[i].x - centre.x, base.pos[i].y - centre.y);
        const double dz = n1.pos[i].z - base.pos[i].z;
        CHECK(std::fabs(dz) <= amplitude + kEps);
        if (d >= radius) CHECK(dz == 0.0);
        if (std::fabs(dz) > 0.01) moved = true;
        CHECK(n1.pos[i].x == base.pos[i].x && n1.pos[i].y == base.pos[i].y);  // z only
    }
    CHECK(moved);

    // Noise is taken from the same field as the whole-patch applyNoise, so the z values agree with it there.
    const PatchGrid whole = applyNoise(base, amplitude, 42);
    const PatchGrid local = noiseAround(base, centre, 1e9, amplitude, 42);  // weight ~1 everywhere inside
    bool same = true;
    for (size_t i = 0; i < base.pos.size(); ++i)
        if (std::fabs(local.pos[i].z - whole.pos[i].z) > 1e-6) same = false;
    CHECK(same);
}
