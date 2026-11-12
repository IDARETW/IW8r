// Dynamic props (6.15): the quaternion written for x64-zt must give the same basis as the static-model axis rows.
#include <array>
#include <cmath>

#include "compile_iw8.h"
#include "prefab.h"
#include "test_harness.h"

using namespace iw8r;

namespace {
// x64-zt iw3_fastfile.cpp BasisFromQuaternion: rows forward, left, up.
std::array<Vec3, 3> basisFromQuaternion(const std::array<double, 4>& q) {
    const double x = q[0], y = q[1], z = q[2], w = q[3];
    return {Vec3{1 - 2 * (y * y + z * z), 2 * (x * y + z * w), 2 * (x * z - y * w)},
            Vec3{2 * (x * y - z * w), 1 - 2 * (x * x + z * z), 2 * (y * z + x * w)},
            Vec3{2 * (x * z + y * w), 2 * (y * z - x * w), 1 - 2 * (x * x + y * y)}};
}
bool near(const Vec3& a, const Vec3& b) { return std::fabs(a.x - b.x) < 1e-6 && std::fabs(a.y - b.y) < 1e-6 && std::fabs(a.z - b.z) < 1e-6; }
}  // namespace

TEST(dynamic_prop_quaternion_matches_the_static_model_basis) {
    const double cases[][3] = {{0, 0, 0}, {0, 90, 0}, {0, 180, 0}, {30, 45, 0}, {-20, 200, 15}, {89, 10, -60}, {0, -90, 180}};
    for (const auto& c : cases) {
        const auto q = anglesToDynQuaternion(c[0], c[1], c[2]);
        CHECK(std::fabs(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3] - 1.0) < 1e-9);
        const auto b = basisFromQuaternion(q);
        // The editor's angles convention (prefab.h anglesToMatrix): the rotated X, Y and Z axes.
        const Mat3 r = anglesToMatrix(Vec3{c[0], c[1], c[2]});
        CHECK(near(b[0], r * Vec3{1, 0, 0}));
        CHECK(near(b[1], r * Vec3{0, 1, 0}));
        CHECK(near(b[2], r * Vec3{0, 0, 1}));
    }
    // Yaw 90 turns forward to +Y.
    CHECK(near(basisFromQuaternion(anglesToDynQuaternion(0, 90, 0))[0], Vec3{0, 1, 0}));
}
