// IW8 Radiant - iw3xo-style generators: rope/wire patches, mesh painter scatter and drop to floor.
//
// Angles follow the Radiant placement convention used by Placement::angles (degrees):
//   x = pitch (rotation about Y), y = yaw (about Z), z = roll (about X).
// The rotation is R = Ry(x) * Rx(z) * Rz(y), so yaw turns the model about its own up axis first. With
// alignToSurface the model's up axis (0,0,1) is mapped onto the surface normal n:
//   z = asin(-n.y), x = atan2(n.x, n.z).
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "geometry.h"
#include "map_text.h"
#include "patch_ops.h"

namespace iw8r {

// Rope / wire: a parabolic sag (close to a catenary for small sags) between a and b. The sag is taken straight
// down (-Z) at the middle, so the lowest point is (a + b) / 2 - sag * Z. Returns segments + 1 points from a to b
// (endpoints exact); segments < 1 is treated as 1.
std::vector<Vec3> ropeCurve(const Vec3& a, const Vec3& b, double sag, int segments);

// Rope as a tube "curve" patch: rows = segments + 1 along the curve (use an even segments for an odd control-row
// count, as Radiant curves expect), cols = 9 around the tube (8 sides, the last column repeats the first). Each
// row is a circle of `radius` around the curve point, perpendicular to the curve tangent. st from naturalTexture.
// Returns an empty grid (rows == 0) when segments < 2 or radius <= 0.
PatchGrid ropePatch(const Vec3& a, const Vec3& b, double sag, double radius, int segments, const std::string& material);

// Mesh painter placement: origin on an upward-facing brush face, angles as described above, uniform scale.
struct Placement {
    Vec3 origin;
    Vec3 angles;
    double scale = 1;
};

struct ScatterOptions {
    double radius = 256;          // XY radius of the circle around the centre
    int count = 20;               // target number of placements (fewer when the surfaces run out)
    double minScale = 0.8, maxScale = 1.2;
    bool randomYaw = true;        // yaw uniform in [0, 360) when true, else 0
    bool alignToSurface = false;  // pitch/roll from the face normal when true, else pitch = roll = 0
    double maxSlopeDeg = 40;      // faces steeper than this (angle from vertical) are not used
    double minSpacing = 0;        // minimum XY distance between placements
    uint32_t seed = 1;            // same seed, same options and same map give the same placements
};

// Deterministic scatter on the top (upward-facing, slope <= maxSlopeDeg) faces of the colliding brushes of
// worldspawn and func_group (detail brushes included). A face is picked uniformly, then a point is sampled in the
// circle until it lands on that face. Every placement is inside the circle, lies on its face and keeps minSpacing
// from the earlier ones. Occlusion by other brushes is not tested. Empty for bad options or no surfaces.
std::vector<Placement> scatterOnBrushes(const MapFile& map, const Vec3& center, const ScatterOptions& opt);

// Drop to floor: the highest colliding brush top at or below p, found by a ray straight down from p over the
// worldspawn and func_group brushes (detail included). Brushes that contain p are skipped. nullopt when no top is
// within maxDrop below p, or when p is not finite.
std::optional<Vec3> dropToFloor(const MapFile& map, const Vec3& p, double maxDrop = 4096);

}  // namespace iw8r
