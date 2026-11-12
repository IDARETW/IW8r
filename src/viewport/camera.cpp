#include "camera.h"

#include <algorithm>
#include <cmath>

namespace iw8r::app {

namespace {
constexpr double kPi = 3.14159265358979323846;
double rad(double d) { return d * kPi / 180.0; }
}  // namespace

Vec3 Camera::forward() const {
    const double p = rad(pitch), y = rad(yaw);
    return {std::cos(p) * std::cos(y), std::cos(p) * std::sin(y), std::sin(p)};
}
Vec3 Camera::right() const { return normalize(cross(forward(), Vec3{0, 0, 1})); }
Vec3 Camera::up() const { return cross(right(), forward()); }

Mat4f viewProjection(const Camera& c, double aspect) {
    const Vec3 f = c.forward(), r = c.right(), u = c.up(), e = c.pos;
    const double view[16] = {r.x, u.x, f.x, 0, r.y, u.y, f.y, 0, r.z, u.z, f.z, 0, -dot(r, e), -dot(u, e), -dot(f, e), 1};
    const double zn = 4.0, zf = 131072.0;
    const double tx = std::tan(rad(c.fov) * 0.5);
    const double xs = 1.0 / tx, ys = aspect / tx;
    const double proj[16] = {xs, 0, 0, 0, 0, ys, 0, 0, 0, 0, zf / (zf - zn), 1, 0, 0, -zn * zf / (zf - zn), 0};
    Mat4f out{};
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) {
            double s = 0;
            for (int k = 0; k < 4; ++k) s += view[i * 4 + k] * proj[k * 4 + j];
            out.m[i * 4 + j] = float(s);
        }
    return out;
}

Ray cameraRay(const Camera& c, double nx, double ny, double aspect) {
    const double tx = std::tan(rad(c.fov) * 0.5), ty = tx / aspect;
    return {c.pos, normalize(c.forward() + c.right() * (nx * tx) + c.up() * (ny * ty))};
}

void frameBounds(Camera& c, const AABB& box, double distanceScale) {
    if (!box.valid()) return;
    const double r = std::max(64.0, length(box.maxs - box.mins) * 0.5);
    c.pos = box.center() - c.forward() * (r * distanceScale);
}

}  // namespace iw8r::app
