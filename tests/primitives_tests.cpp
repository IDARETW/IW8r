// Blockout primitive generators (src/core/primitives.h): validity, bounds, step layout, arch segments, wedge.
#include <cmath>
#include <string>
#include <vector>

#include "brush_ops.h"
#include "primitives.h"
#include "test_harness.h"

using namespace iw8r;

namespace {

const char* kSuffix = " caulk 64 64 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0";

AABB boxOf(Vec3 a, Vec3 b) {
    AABB r;
    r.add(a);
    r.add(b);
    return r;
}

bool near(double a, double b, double eps) { return std::fabs(a - b) <= eps; }

bool sameBounds(const AABB& a, const AABB& b, double eps) {
    for (int i = 0; i < 3; ++i)
        if (!near(a.mins[i], b.mins[i], eps) || !near(a.maxs[i], b.maxs[i], eps)) return false;
    return true;
}

// True when the bounds of `inner` lie inside `outer` (with tolerance).
bool insideBounds(const AABB& inner, const AABB& outer, double eps) {
    for (int i = 0; i < 3; ++i)
        if (inner.mins[i] < outer.mins[i] - eps || inner.maxs[i] > outer.maxs[i] + eps) return false;
    return true;
}

double shoelace(const std::vector<std::pair<double, double>>& poly) {
    double a = 0;
    for (size_t i = 0; i < poly.size(); ++i) {
        const auto& p = poly[i];
        const auto& q = poly[(i + 1) % poly.size()];
        a += p.first * q.second - q.first * p.second;
    }
    return std::fabs(a) * 0.5;
}

}  // namespace

TEST(primitives_cone_valid_and_bounds) {
    const AABB box = boxOf({-64, -32, 0}, {64, 32, 128});
    for (int axis = 0; axis < 3; ++axis) {
        for (int sides = 3; sides <= 12; ++sides) {
            const ConvexBrush b = makeCone(box, sides, axis, kSuffix);
            CHECK(isValidBrush(b));
            CHECK(b.size() == static_cast<size_t>(sides + 1));
            CHECK(sameBounds(brushBounds(b), box, 1e-6));
        }
    }
}

TEST(primitives_cone_apex_and_base) {
    const AABB box = boxOf({0, 0, 0}, {40, 40, 80});
    const ConvexBrush b = makeCone(box, 8, 2, kSuffix);
    CHECK(isValidBrush(b));
    // The base face is the only one with normal -z at mins.z; the apex sits at maxs.z.
    bool haveBase = false;
    for (const auto& f : b)
        if (dot(f.plane.n, Vec3{0, 0, -1}) > 0.999999 && std::fabs(f.plane.d - 0.0) < 1e-9) haveBase = true;
    CHECK(haveBase);
    // Apex at the top: the volume is one third of the bounding cylinder-like prism with the inscribed base.
    CHECK(brushVolume(b) < 40.0 * 40.0 * 80.0 / 3.0 + 1.0);
    CHECK(brushVolume(b) > 0.0);
}

TEST(primitives_cone_rejects_bad_args) {
    const AABB box = boxOf({0, 0, 0}, {10, 10, 10});
    CHECK(makeCone(box, 2, 0, kSuffix).empty());
    CHECK(makeCone(box, 65, 0, kSuffix).empty());
    CHECK(makeCone(box, 4, 3, kSuffix).empty());
    CHECK(makeCone(AABB{}, 4, 0, kSuffix).empty());
}

TEST(primitives_sphere_valid_and_bounds) {
    const AABB box = boxOf({-50, -50, -50}, {50, 50, 50});
    for (int sides : {4, 6, 8, 12, 16}) {
        const auto pieces = makeSphere(box, sides, kSuffix);
        CHECK(pieces.size() == 1);
        if (pieces.size() != 1) continue;
        CHECK(isValidBrush(pieces[0]));
        CHECK(sameBounds(brushBounds(pieces[0]), box, 1.0));
        CHECK(pieces[0].size() >= 6);
    }
    // Non-cubic box: ellipsoid bounds still match the box.
    const AABB flat = boxOf({0, 0, 0}, {120, 40, 24});
    const auto pieces = makeSphere(flat, 10, kSuffix);
    CHECK(pieces.size() == 1);
    if (pieces.size() == 1) {
        CHECK(isValidBrush(pieces[0]));
        CHECK(sameBounds(brushBounds(pieces[0]), flat, 1.0));
    }
}

TEST(primitives_sphere_rejects_bad_args) {
    const AABB box = boxOf({0, 0, 0}, {10, 10, 10});
    CHECK(makeSphere(box, 3, kSuffix).empty());
    CHECK(makeSphere(box, 64, kSuffix).empty());
    CHECK(makeSphere(AABB{}, 8, kSuffix).empty());
}

TEST(primitives_stairs_layout) {
    const AABB box = boxOf({0, 0, 0}, {128, 64, 96});
    for (int dir = 0; dir < 4; ++dir) {
        const int steps = 6;
        const auto st = makeStairs(box, steps, dir, kSuffix);
        CHECK(st.size() == static_cast<size_t>(steps));
        if (st.size() != static_cast<size_t>(steps)) continue;

        double volume = 0;
        for (int i = 0; i < steps; ++i) {
            CHECK(isValidBrush(st[i]));
            const AABB bb = brushBounds(st[i]);
            CHECK(insideBounds(bb, box, 1e-6));
            // Step i tops out at mins.z + (i + 1) * H / steps; the last step reaches box.maxs.z.
            const double top = (i == steps - 1) ? box.maxs.z : box.mins.z + 96.0 * (i + 1) / steps;
            CHECK(near(bb.maxs.z, top, 1e-6));
            CHECK(near(bb.mins.z, box.mins.z, 1e-6));
            volume += brushVolume(st[i]);
        }
        // Each step is a slab of run L/steps from the floor up: total = L * W * H * (steps + 1) / (2 * steps).
        const double expected = 128.0 * 64.0 * 96.0 * (steps + 1) / (2.0 * steps);
        CHECK(near(volume, expected, expected * 1e-6));

        // Neighbouring steps touch face to face along the run axis and do not overlap.
        const int axis = dir < 2 ? 0 : 1;
        const bool rising = dir % 2 == 0;
        for (int i = 0; i + 1 < steps; ++i) {
            const AABB a = brushBounds(st[i]);
            const AABB b = brushBounds(st[i + 1]);
            if (rising) {
                CHECK(near(a.maxs[axis], b.mins[axis], 1e-6));
            } else {
                CHECK(near(a.mins[axis], b.maxs[axis], 1e-6));
            }
        }
        // The highest step is on the side the direction points to.
        const AABB top = brushBounds(st.back());
        if (rising) {
            CHECK(near(top.maxs[axis], box.maxs[axis], 1e-6));
        } else {
            CHECK(near(top.mins[axis], box.mins[axis], 1e-6));
        }
    }
}

TEST(primitives_stairs_rejects_bad_args) {
    const AABB box = boxOf({0, 0, 0}, {64, 64, 64});
    CHECK(makeStairs(box, 0, 0, kSuffix).empty());
    CHECK(makeStairs(box, 4, 4, kSuffix).empty());
    CHECK(makeStairs(box, 4, -1, kSuffix).empty());
}

TEST(primitives_arch_segments_convex_and_inside) {
    const double kPi = 3.14159265358979323846;
    const AABB box = boxOf({0, 0, 0}, {96, 32, 64});  // axis 1 (opening along y): profile in x and z
    // Outer semi-axes 48 x 64, inner 42 x 58: band area of the half ellipse, times depth 32.
    const double exact = 0.5 * kPi * (48.0 * 64.0 - 42.0 * 58.0) * 32.0;
    const int segCounts[] = {8, 16};
    const double tol[] = {0.12, 0.04};
    for (int k = 0; k < 2; ++k) {
        const int segs = segCounts[k];
        const auto blocks = makeArchBlocks(box, segs, 6.0, 1, kSuffix);
        CHECK(blocks.size() == static_cast<size_t>(segs));
        double vol = 0;
        for (const auto& b : blocks) {
            CHECK(isValidBrush(b));
            // Convex quadrilateral extruded along y: 4 lateral faces plus 2 caps.
            CHECK(b.size() == 6);
            CHECK(insideBounds(brushBounds(b), box, 1e-6));
            vol += brushVolume(b);
        }
        CHECK(std::fabs(vol - exact) < tol[k] * exact);
    }
}

TEST(primitives_arch_each_segment_is_its_quad) {
    // Each block's volume equals its profile quad's area times the depth, so the hull is the quad itself
    // (a non-convex block would have a larger hull).
    const double kPi = 3.14159265358979323846;
    const AABB box = boxOf({0, 0, 0}, {80, 40, 40});
    const int segs = 4;
    const double t = 5.0;
    const auto blocks = makeArchBlocks(box, segs, t, 0, kSuffix);  // opening along x: profile in y (width), z (height)
    CHECK(blocks.size() == static_cast<size_t>(segs));
    const double ru = 20.0, rv = 40.0, ri = ru - t, rh = rv - t;
    const double cu = 20.0, bot = 0.0;
    auto pt = [&](double r1, double r2, int j) {
        const double th = kPi - kPi * j / segs;
        return std::pair<double, double>{cu + r1 * std::cos(th), bot + r2 * std::sin(th)};
    };
    for (int k = 0; k < static_cast<int>(blocks.size()); ++k) {
        const std::vector<std::pair<double, double>> quad = {pt(ru, rv, k), pt(ru, rv, k + 1), pt(ri, rh, k + 1),
                                                             pt(ri, rh, k)};
        const double expected = shoelace(quad) * 80.0;
        CHECK(isValidBrush(blocks[k]));
        CHECK(near(brushVolume(blocks[k]), expected, expected * 1e-6));
    }
}

TEST(primitives_arch_rejects_bad_args) {
    const AABB box = boxOf({0, 0, 0}, {64, 32, 48});
    CHECK(makeArchBlocks(box, 1, 4.0, 1, kSuffix).empty());     // needs at least two segments
    CHECK(makeArchBlocks(box, 4, 0.0, 1, kSuffix).empty());     // no thickness
    CHECK(makeArchBlocks(box, 4, 32.0, 1, kSuffix).empty());    // thickness reaches the inner ellipse (min semi-axis 32)
    CHECK(makeArchBlocks(box, 4, 4.0, 3, kSuffix).empty());     // bad axis
}

TEST(primitives_ramp_wedge) {
    const AABB box = boxOf({-32, -16, 0}, {32, 16, 48});
    for (int dir = 0; dir < 4; ++dir) {
        const ConvexBrush r = makeRamp(box, dir, kSuffix);
        CHECK(r.size() == 5);
        CHECK(isValidBrush(r));
        CHECK(sameBounds(brushBounds(r), box, 1e-6));
        // Triangular prism: half the box volume.
        CHECK(near(brushVolume(r), 64.0 * 32.0 * 48.0 * 0.5, 1e-6));
    }
    CHECK(makeRamp(box, 4, kSuffix).empty());
    CHECK(makeRamp(AABB{}, 0, kSuffix).empty());
}

TEST(primitives_ramp_rises_toward_direction) {
    const AABB box = boxOf({0, 0, 0}, {64, 64, 64});
    const ConvexBrush r = makeRamp(box, 0, kSuffix);  // rising along +x: high edge at x = 64
    const auto ws = brushWindings(r);
    // The vertical back face is at x = 64 and the floor edge at x = 0 sits on z = 0.
    bool haveBack = false;
    for (const auto& f : r)
        if (dot(f.plane.n, Vec3{1, 0, 0}) > 0.999999 && std::fabs(f.plane.d - 64.0) < 1e-9) haveBack = true;
    CHECK(haveBack);
    // The lowest point on the low end is z = 0 and the top reaches z = 64 only at x = 64.
    double zAtLowEnd = 1e30;
    for (const Winding& w : ws)
        for (const Vec3& p : w)
            if (p.x < 1e-9) zAtLowEnd = std::min(zAtLowEnd, p.z);
    CHECK(near(zAtLowEnd, 0.0, 1e-6));
}
