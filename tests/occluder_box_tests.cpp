// The occluder box of a brush that is not an axis-aligned box (core occluder_box.h): it must lie inside the brush
// (then it can only hide less than the brush), and it should be a useful size for ordinary shapes.
#include "occluder_box.h"

#include <cmath>
#include <cstdio>
#include <random>

#include "brush_ops.h"
#include "test_harness.h"

using namespace iw8r;

namespace {

struct Solid {
    std::vector<Plane> planes;
    std::vector<Vec3> corners;
};

// The planes and corner points of a convex brush made by brush_ops.
Solid solidOf(const ConvexBrush& b) {
    Solid s;
    const auto prim = makeBrushPrimitive(b, "\n");
    if (!prim) return s;
    s.planes = prim->planes();
    // Corners: every point where three planes meet that is inside all the others.
    for (size_t i = 0; i < s.planes.size(); ++i)
        for (size_t j = i + 1; j < s.planes.size(); ++j)
            for (size_t k = j + 1; k < s.planes.size(); ++k) {
                const Plane &a = s.planes[i], &bb = s.planes[j], &c = s.planes[k];
                const Vec3 bc = cross(bb.n, c.n);
                const double det = dot(a.n, bc);
                if (std::fabs(det) < 1e-9) continue;
                const Vec3 p = (bc * a.d + cross(c.n, a.n) * bb.d + cross(a.n, bb.n) * c.d) * (1.0 / det);
                bool in = true;
                for (const Plane& pl : s.planes) in = in && dot(pl.n, p) - pl.d < 1e-6;
                if (in) s.corners.push_back(p);
            }
    return s;
}

bool boxInside(const Solid& s, const AABB& b) {
    for (int k = 0; k < 8; ++k) {
        const Vec3 p{k & 1 ? b.maxs.x : b.mins.x, k & 2 ? b.maxs.y : b.mins.y, k & 4 ? b.maxs.z : b.mins.z};
        for (const Plane& pl : s.planes)
            if (dot(pl.n, p) - pl.d > 1e-6) return false;
    }
    return true;
}

double volume(const AABB& b) { return (b.maxs.x - b.mins.x) * (b.maxs.y - b.mins.y) * (b.maxs.z - b.mins.z); }

}  // namespace

// An axis-aligned box gives itself back (less the margin).
TEST(occluder_box_of_a_box_is_the_box) {
    const Solid s = solidOf(makeBox({{-64, 0, 16}, {192, 128, 144}}, " tm/x 128 128 0 0 0 0"));
    CHECK(s.planes.size() == 6 && s.corners.size() == 8);
    const auto b = inscribedBox(s.planes, s.corners);
    CHECK(b.has_value());
    if (!b) return;
    CHECK(boxInside(s, *b));
    CHECK(std::fabs(b->mins.x + 64) < 0.2 && std::fabs(b->maxs.x - 192) < 0.2 && std::fabs(b->mins.z - 16) < 0.2 && std::fabs(b->maxs.z - 144) < 0.2);
}

// Prisms (a round tower, a six-sided pillar): the box is inside and holds a good part of the solid.
TEST(occluder_box_of_prisms_is_inside_and_large) {
    for (int sides : {3, 5, 6, 8, 12, 24})
        for (int axis = 0; axis < 3; ++axis) {
            const AABB bounds{{0, 0, 0}, {256, 256, 256}};
            const Solid s = solidOf(makePrism(bounds, sides, axis, " tm/x 128 128 0 0 0 0"));
            CHECK(s.planes.size() == size_t(sides) + 2);
            const auto b = inscribedBox(s.planes, s.corners);
            CHECK(b.has_value());
            if (!b) continue;
            CHECK(boxInside(s, *b));
            // A triangle's best box is half its area (a quarter of the bounds at most) and the search does not find the
            // best one; a circle's is 2 / pi. A third of the bounds is asked of the others, a tenth of a triangle.
            if (!(volume(*b) > volume(bounds) * (sides == 3 ? 0.1 : 0.33)))
                std::printf("  sides %d axis %d: box %.3f of the bounds\n", sides, axis, volume(*b) / volume(bounds));
            CHECK(volume(*b) > volume(bounds) * (sides == 3 ? 0.1 : 0.33));
        }
}

// Random convex solids (a box cut by slanted planes): always inside, and nothing when the solid is a sliver.
TEST(occluder_box_random_cuts_stay_inside) {
    std::mt19937 rng(20261114);
    std::uniform_real_distribution<double> u(-1.0, 1.0);
    int found = 0;
    for (int n = 0; n < 300; ++n) {
        Solid s = solidOf(makeBox({{-100, -100, -100}, {100, 100, 100}}, " tm/x 128 128 0 0 0 0"));
        const int cuts = 1 + n % 4;
        for (int c = 0; c < cuts; ++c) {
            Vec3 nrm{u(rng), u(rng), u(rng)};
            const double len = length(nrm);
            if (len < 0.2) continue;
            nrm = nrm * (1.0 / len);
            s.planes.push_back(Plane{nrm, 20.0 + 70.0 * std::fabs(u(rng))});  // keeps the origin side
        }
        // The corner list of the cut solid: the box's corners that survive, plus the centre region; the search only needs
        // points whose average is inside, and the origin is inside every cut.
        std::vector<Vec3> corners;
        for (const Vec3& p : s.corners) {
            bool in = true;
            for (const Plane& pl : s.planes) in = in && dot(pl.n, p) - pl.d < 1e-6;
            if (in) corners.push_back(p);
        }
        while (corners.size() < 4) corners.push_back(Vec3{0, 0, 0});
        // Pull the average to the origin's side by adding the origin as often as there are corners.
        const size_t had = corners.size();
        for (size_t k = 0; k < had * 4; ++k) corners.push_back(Vec3{0, 0, 0});
        const auto b = inscribedBox(s.planes, corners);
        if (!b) continue;
        ++found;
        CHECK(boxInside(s, *b));
        CHECK(b->maxs.x - b->mins.x >= 1.0 && b->maxs.y - b->mins.y >= 1.0 && b->maxs.z - b->mins.z >= 1.0);
    }
    CHECK(found > 200);
}

// A thin slanted wall holds no box worth using: the result is thin or absent, never outside the wall.
TEST(occluder_box_thin_slanted_wall) {
    Solid s;
    const double r = std::sqrt(0.5);
    // A slab 8 units thick along the diagonal x = y, 400 long, 128 high.
    s.planes = {Plane{{r, -r, 0}, 4}, Plane{{-r, r, 0}, 4}, Plane{{r, r, 0}, 200}, Plane{{-r, -r, 0}, 200}, Plane{{0, 0, 1}, 128}, Plane{{0, 0, -1}, 0}};
    for (double along : {-200.0, 200.0})
        for (double across : {-4.0, 4.0})
            for (double z : {0.0, 128.0}) s.corners.push_back(Vec3{r * along + r * across, r * along - r * across, z});
    const auto b = inscribedBox(s.planes, s.corners);
    if (b) {
        CHECK(boxInside(s, *b));
        CHECK(std::min(b->maxs.x - b->mins.x, b->maxs.y - b->mins.y) < 12.0);  // never wider than the wall allows
    }
}
