// IW8 Radiant Suite - basic geometry used by the editor core.
// Coordinates follow Radiant/IW conventions: Z up, X east, Y north, units = inches.
#pragma once

#include <cmath>
#include <optional>
#include <vector>

namespace iw8r {

struct Vec3 {
    double x = 0, y = 0, z = 0;

    constexpr Vec3() = default;
    constexpr Vec3(double x_, double y_, double z_) : x(x_), y(y_), z(z_) {}

    constexpr Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    constexpr Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    constexpr Vec3 operator*(double s) const { return {x * s, y * s, z * s}; }
    constexpr Vec3 operator-() const { return {-x, -y, -z}; }
    Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
    constexpr bool operator==(const Vec3& o) const { return x == o.x && y == o.y && z == o.z; }
    constexpr bool operator!=(const Vec3& o) const { return !(*this == o); }
    double& operator[](int i) { return i == 0 ? x : (i == 1 ? y : z); }
    double operator[](int i) const { return i == 0 ? x : (i == 1 ? y : z); }
};

inline double dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline double length(const Vec3& v) { return std::sqrt(dot(v, v)); }
inline Vec3 normalize(const Vec3& v) {
    const double l = length(v);
    return l > 0 ? v * (1.0 / l) : Vec3{};
}

// Plane: dot(n, x) == d. n points out of the brush (Radiant/Quake winding convention).
struct Plane {
    Vec3 n;
    double d = 0;
};

// Builds the plane of a brush face from its three map points.
// normal = normalize(cross(p0 - p1, p2 - p1)), which is outward-facing for Radiant maps.
std::optional<Plane> planeFromPoints(const Vec3& p0, const Vec3& p1, const Vec3& p2);

struct AABB {
    Vec3 mins{1e30, 1e30, 1e30};
    Vec3 maxs{-1e30, -1e30, -1e30};
    bool valid() const { return mins.x <= maxs.x && mins.y <= maxs.y && mins.z <= maxs.z; }
    void add(const Vec3& p);
    void add(const AABB& b);
    Vec3 center() const { return (mins + maxs) * 0.5; }
    bool contains(const Vec3& p, double eps = 0) const;
};

using Winding = std::vector<Vec3>;

// One winding per input plane (empty when the plane does not contribute a face).
std::vector<Winding> buildBrushWindings(const std::vector<Plane>& planes);

struct Ray {
    Vec3 origin;
    Vec3 dir;  // normalized
};

// Ray vs convex brush (intersection of half spaces dot(n,x) <= d). Returns entry distance.
std::optional<double> rayConvex(const Ray& ray, const std::vector<Plane>& planes);
// Ray vs axis aligned box. Returns entry distance (0 when origin is inside).
std::optional<double> rayAABB(const Ray& ray, const AABB& box);

// Grid helpers.
double snapToGrid(double v, double grid);
Vec3 snapToGrid(const Vec3& v, double grid);

}  // namespace iw8r
