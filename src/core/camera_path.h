// IW8 Radiant Suite - camera path (fly-through) keys, sampling, timing and JSON storage.
// Core only: no dependency on src/viewport. Callers map a CameraKey onto their own camera.
#pragma once

#include <string>
#include <vector>

#include "geometry.h"

namespace iw8r {

struct CameraKey {
    Vec3 pos;
    double yaw = 0, pitch = 0;  // degrees: yaw around +Z (0 = +X), pitch + looks up
    double time = 0;            // seconds
};

struct CameraPath {
    std::string name;
    std::vector<CameraKey> keys;  // path order; times should be non-decreasing
    bool loop = false;            // closes last key -> first key
};

// Duration in seconds. Open paths: last.time - first.time. Loops add the closing leg (see .cpp).
double cameraPathDuration(const CameraPath& path);

// Position/yaw/pitch at `t` seconds from the first key (clamped to [0, duration], wrapped for loops).
// The returned key's time is the absolute path time that was sampled. Passes exactly through every key.
CameraKey sampleCameraPath(const CameraPath& path, double t);

// Rewrites key times so the camera moves at a constant `unitsPerSecond` along the key polyline.
// The first key keeps its time. No-op for fewer than two keys or a non-positive speed.
void retimeByDistance(CameraPath& path, double unitsPerSecond);

// Samples at i / fps for i = 0 .. floor(duration * fps), i.e. floor(duration*fps)+1 frames.
// Returns empty when fps <= 0.
std::vector<CameraKey> cameraPathFrames(const CameraPath& path, double fps);

// JSON storage (the UI saves these next to the map as "<map>.campaths.json").
std::string cameraPathsToJson(const std::vector<CameraPath>& paths);
// Replaces `out` on success. Accepts the object form {"paths":[...]} or a bare array of paths.
// On failure returns false, sets *err (if non-null) and leaves `out` untouched.
bool cameraPathsFromJson(const std::string& text, std::vector<CameraPath>& out, std::string* err);

}  // namespace iw8r
