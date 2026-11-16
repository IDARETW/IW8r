// IW8 Radiant - free-fly camera math shared by the editor shells.
#pragma once

#include "geometry.h"

namespace iw8r::app {

struct Camera {
    Vec3 pos{-384, -384, 256};
    double yaw = 45;     // degrees around +Z, 0 = +X
    double pitch = -25;  // degrees, + looks up
    double fov = 90;     // horizontal degrees
    double speed = 600;  // units per second
    Vec3 forward() const;
    Vec3 right() const;
    Vec3 up() const;
};

struct Mat4f {
    float m[16];
};

// Row-major, row-vector (v * M) view-projection, D3D depth range [0, 1].
Mat4f viewProjection(const Camera& c, double aspect);
// Ray through normalized device coordinates nx, ny in [-1, 1] (y up).
Ray cameraRay(const Camera& c, double nx, double ny, double aspect);
// Places the camera so `box` fills the view along the current direction.
void frameBounds(Camera& c, const AABB& box, double distanceScale = 1.4);

}  // namespace iw8r::app
