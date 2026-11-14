// The game's distance rule for a placed model's level of detail and for leaving it out (research 119; read in the
// IW8 game-test source, INFERRED for Replay):
//   dist     = from the camera to the nearest point of the placement's world box (0 inside it)
//   level    = by dist x fovFactor against the model's LOD distances
//   left out = dist x min(fovFactor, 1) / cullScale >= the model's last LOD distance
//   fovFactor = tanHalfFovY x 2.1186731 (1 at the game's own view)
// Header only; used by the viewport's placement culling (SceneRenderer::cullTexInstances).
#pragma once

#include <algorithm>
#include <cmath>

namespace iw8r {

inline constexpr float kLodFovUnit = 2.1186731f;  // 1 / tan(25.27 degrees)

// The field-of-view factor on LOD distances, kept inside 0.05 .. 8. A tangent that is not positive gives 1.
inline float lodFovFactor(float tanHalfFovY) {
    return tanHalfFovY > 1e-6f ? std::clamp(tanHalfFovY * kLodFovUnit, 0.05f, 8.0f) : 1.0f;
}

// Distance from `eye` to the world box of a model-space box (mid, half) placed by `rows` (3 rows of a 3x4 transform:
// world_i = dot(row_i, (x, y, z, 1))). 0 when the eye is inside the box.
inline float placementBoxDistance(const float* rows, const float mid[3], const float half[3], const float eye[3]) {
    float d2 = 0;
    for (int k = 0; k < 3; ++k) {
        const float* r = rows + k * 4;
        const float c = r[0] * mid[0] + r[1] * mid[1] + r[2] * mid[2] + r[3];
        const float h = std::fabs(r[0]) * half[0] + std::fabs(r[1]) * half[1] + std::fabs(r[2]) * half[2];
        const float d = std::max(std::fabs(eye[k] - c) - h, 0.0f);
        d2 += d * d;
    }
    return std::sqrt(d2);
}

// True when the game leaves the placement out by distance. lodOut: the model's last LOD distance (0 or less: never).
// cullScale: the placement's lodScale (0 or less: never left out).
inline bool placementLodCulled(float dist, float fovFactor, float cullScale, float lodOut) {
    if (!(lodOut > 0) || !(cullScale > 0)) return false;
    return dist * std::min(fovFactor, 1.0f) / cullScale >= lodOut;
}

}  // namespace iw8r
