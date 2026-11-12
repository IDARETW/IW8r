// Camera paths (8.15): spline sampling, yaw shortest arc, loops, duplicate times, retiming, frames, JSON.
#include <cmath>
#include <string>
#include <utility>
#include <vector>

#include "camera_path.h"
#include "test_harness.h"

using namespace iw8r;

namespace {

bool nearV(const Vec3& a, const Vec3& b, double tol) { return length(a - b) <= tol; }
// Angle difference in degrees, folded to [0, 180].
double angDiff(double a, double b) { return std::fabs(std::remainder(a - b, 360.0)); }
bool finiteKey(const CameraKey& k) {
    return std::isfinite(k.pos.x) && std::isfinite(k.pos.y) && std::isfinite(k.pos.z) && std::isfinite(k.yaw) &&
           std::isfinite(k.pitch);
}

CameraKey key(double x, double y, double z, double yaw, double pitch, double time) {
    CameraKey k;
    k.pos = Vec3(x, y, z);
    k.yaw = yaw;
    k.pitch = pitch;
    k.time = time;
    return k;
}

CameraPath openFlyby() {
    CameraPath p;
    p.name = "flyby";
    p.keys = {key(0, 0, 0, 0, 0, 0),         key(100, 0, 0, 30, 10, 1.5),  key(150, 80, 20, 90, -20, 2.0),
              key(40, 120, 64, 200, 5, 4.0), key(-50, 60, 10, 350, 0, 7.5)};
    return p;
}

CameraPath square(bool loop) {
    CameraPath p;
    p.name = "square";
    p.loop = loop;
    p.keys = {key(0, 0, 0, 0, 0, 0), key(200, 0, 0, 90, 0, 1), key(200, 200, 0, 180, 0, 2), key(0, 200, 0, 270, 0, 3)};
    return p;
}

}  // namespace

TEST(camera_path_passes_through_keys) {
    const CameraPath p = openFlyby();
    for (const CameraKey& k : p.keys) {
        const CameraKey s = sampleCameraPath(p, k.time);
        CHECK(nearV(s.pos, k.pos, 1e-9));
        CHECK(angDiff(s.yaw, k.yaw) < 1e-9);
        CHECK(std::fabs(s.pitch - k.pitch) < 1e-9);
    }
    // Between keys the position stays finite and inside the overall bounding box of the keys.
    for (double t = 0; t <= 7.5; t += 0.05) {
        const CameraKey s = sampleCameraPath(p, t);
        CHECK(finiteKey(s));
        CHECK(s.pos.x > -80 && s.pos.x < 200 && s.pos.y > -20 && s.pos.y < 150);
    }
}

TEST(camera_path_clamps_open_path_time) {
    const CameraPath p = openFlyby();
    CHECK(nearV(sampleCameraPath(p, -5).pos, p.keys.front().pos, 1e-12));
    CHECK(nearV(sampleCameraPath(p, 100).pos, p.keys.back().pos, 1e-12));
    CHECK(std::fabs(cameraPathDuration(p) - 7.5) < 1e-12);
}

TEST(camera_path_yaw_takes_shortest_arc) {
    // 350 -> 10 must sweep through 0 (20 degrees), never through 180.
    CameraPath p;
    p.keys = {key(0, 0, 0, 350, 0, 0), key(100, 0, 0, 10, 0, 2)};
    for (double t = 0; t <= 2.0; t += 0.1) {
        const CameraKey s = sampleCameraPath(p, t);
        CHECK(angDiff(s.yaw, 0) <= 10 + 1e-9);
        CHECK(angDiff(s.yaw, 180) >= 170 - 1e-6);  // endpoint 350 is exactly 170 from 180
    }
    CHECK(angDiff(sampleCameraPath(p, 0).yaw, 350) < 1e-9);
    CHECK(angDiff(sampleCameraPath(p, 2).yaw, 10) < 1e-9);

    // Reverse direction: 10 -> 350 also goes through 0.
    CameraPath r;
    r.keys = {key(0, 0, 0, 10, 0, 0), key(100, 0, 0, 350, 0, 2)};
    for (double t = 0; t <= 2.0; t += 0.1) CHECK(angDiff(sampleCameraPath(r, t).yaw, 0) <= 10 + 1e-9);
}

TEST(camera_path_loop_continuity) {
    const CameraPath p = square(true);
    const double D = cameraPathDuration(p);
    // Closing leg: 200 units at the open path's average speed (600 units / 3 s) = 1 s, so D = 4.
    CHECK(std::fabs(D - 4.0) < 1e-9);
    CHECK(nearV(sampleCameraPath(p, D).pos, sampleCameraPath(p, 0).pos, 1e-9));
    CHECK(angDiff(sampleCameraPath(p, D).yaw, sampleCameraPath(p, 0).yaw) < 1e-9);
    // Just before the wrap the camera is at key 0 again.
    CHECK(nearV(sampleCameraPath(p, D - 1e-7).pos, p.keys[0].pos, 1e-3));
    CHECK(angDiff(sampleCameraPath(p, D - 1e-7).yaw, 0) < 1e-3);
    // Wrapping: t and t + D give the same sample, negatives wrap too.
    CHECK(nearV(sampleCameraPath(p, D + 0.5).pos, sampleCameraPath(p, 0.5).pos, 1e-9));
    CHECK(nearV(sampleCameraPath(p, -0.5).pos, sampleCameraPath(p, D - 0.5).pos, 1e-9));
    // Loop passes through every key at its time, including the one after the closing leg.
    for (const CameraKey& k : p.keys) CHECK(nearV(sampleCameraPath(p, k.time).pos, k.pos, 1e-9));
}

TEST(camera_path_duplicate_times) {
    CameraPath p;
    p.keys = {key(0, 0, 0, 0, 0, 0), key(100, 0, 0, 0, 0, 1), key(100, 50, 0, 0, 0, 1), key(0, 50, 0, 0, 0, 2)};
    // At the shared time the later key wins and nothing divides by zero.
    CHECK(nearV(sampleCameraPath(p, 1.0).pos, Vec3(100, 50, 0), 1e-9));
    CHECK(finiteKey(sampleCameraPath(p, 0.5)));
    CHECK(finiteKey(sampleCameraPath(p, 1.0 - 1e-12)));
    CHECK(finiteKey(sampleCameraPath(p, 1.5)));
    CHECK(nearV(sampleCameraPath(p, 1.5).pos, Vec3(50, 50, 0), 60));  // between the two pieces, not NaN
    CHECK(std::fabs(cameraPathDuration(p) - 2.0) < 1e-12);

    // Every key at one time: duration 0, samples return a finite key.
    CameraPath same;
    same.keys = {key(0, 0, 0, 0, 0, 5), key(10, 0, 0, 0, 0, 5), key(20, 0, 0, 0, 0, 5)};
    CHECK(cameraPathDuration(same) == 0);
    CHECK(nearV(sampleCameraPath(same, 0).pos, Vec3(20, 0, 0), 1e-12));
    CHECK(finiteKey(sampleCameraPath(same, 3)));

    // Stacked positions (zero-length legs) are also finite.
    CameraPath stacked;
    stacked.keys = {key(5, 5, 5, 0, 0, 0), key(5, 5, 5, 0, 0, 1), key(5, 5, 5, 90, 0, 2)};
    for (double t = 0; t <= 2.0; t += 0.25) CHECK(finiteKey(sampleCameraPath(stacked, t)));
}

TEST(camera_path_unsorted_keys_sample_like_sorted) {
    CameraPath sorted = openFlyby();
    CameraPath shuffled = sorted;
    std::swap(shuffled.keys[1], shuffled.keys[3]);
    for (double t = 0; t <= 7.5; t += 0.37) CHECK(nearV(sampleCameraPath(shuffled, t).pos, sampleCameraPath(sorted, t).pos, 1e-9));
    CHECK(std::fabs(cameraPathDuration(shuffled) - 7.5) < 1e-12);
}

TEST(camera_path_retime_constant_speed) {
    CameraPath p = openFlyby();
    p.keys[1].time = 0.1;  // deliberately bad timing
    p.keys[2].time = 5.0;
    p.keys[3].time = 5.2;
    const double speed = 50;
    retimeByDistance(p, speed);

    double total = 0;
    for (size_t i = 1; i < p.keys.size(); ++i) {
        const double dist = length(p.keys[i].pos - p.keys[i - 1].pos);
        const double dt = p.keys[i].time - p.keys[i - 1].time;
        CHECK(dt >= 0);  // monotonic
        if (dist > 0) CHECK(std::fabs(dist / dt - speed) < 1e-9 * speed);
        total += dist;
    }
    CHECK(p.keys[0].time == 0);  // first key keeps its time
    CHECK(std::fabs(cameraPathDuration(p) - total / speed) < 1e-9);

    // Loop: the closing leg runs at the same speed as the rest.
    CameraPath l = square(true);
    retimeByDistance(l, speed);
    const double closeDist = length(l.keys[0].pos - l.keys.back().pos);
    const double closeDt = cameraPathDuration(l) - (l.keys.back().time - l.keys.front().time);
    CHECK(std::fabs(closeDist / closeDt - speed) < 1e-9 * speed);

    // Invalid speed and single-key paths are no-ops.
    CameraPath before = openFlyby();
    CameraPath after = before;
    retimeByDistance(after, 0);
    retimeByDistance(after, -3);
    for (size_t i = 0; i < before.keys.size(); ++i) CHECK(after.keys[i].time == before.keys[i].time);
    CameraPath one;
    one.keys = {key(1, 2, 3, 0, 0, 4)};
    retimeByDistance(one, 10);
    CHECK(one.keys[0].time == 4);
}

TEST(camera_path_frame_count) {
    CameraPath p = openFlyby();
    p.keys = {key(0, 0, 0, 0, 0, 0), key(10, 0, 0, 0, 0, 1), key(20, 0, 0, 0, 0, 2), key(30, 0, 0, 0, 0, 3)};
    CHECK(cameraPathFrames(p, 1).size() == 4);            // floor(3*1)+1
    CHECK(cameraPathFrames(p, 30).size() == 91);          // floor(3*30)+1
    CHECK(cameraPathFrames(p, 0).empty());
    CHECK(cameraPathFrames(p, -24).empty());

    CameraPath q = openFlyby();
    q.keys[0].time = 0;
    q.keys.resize(2);
    q.keys[1].time = 2.5;
    const auto frames = cameraPathFrames(q, 24);
    CHECK(frames.size() == 61);                           // floor(2.5*24)+1
    CHECK(std::fabs(frames.back().time - 2.5) < 1e-9);
    CHECK(nearV(frames.back().pos, q.keys[1].pos, 1e-9));
    CHECK(std::fabs(frames[1].time - 1.0 / 24) < 1e-12);

    CHECK(cameraPathFrames(square(true), 10).size() == 41);  // loop duration 4 s
    CameraPath single;
    single.keys = {key(7, 8, 9, 0, 0, 0)};
    CHECK(cameraPathFrames(single, 24).size() == 1);
}

TEST(camera_path_json_round_trip) {
    std::vector<CameraPath> paths(2);
    paths[0].name = std::string("Flyby \"A\"\\ 1\t\x01 \xC3\xBC");
    paths[0].keys = {key(0.1, -123.456789012345, 1e-7, 0.25, -89.5, 0), key(3.0e10, -0.0, 2.5, 359.999, 45, 12.345678901234),
                     key(1.0 / 3.0, 7, 8, 0, 0, 1e-300)};
    paths[1].name = "";
    paths[1].loop = true;

    const std::string json = cameraPathsToJson(paths);
    std::vector<CameraPath> out;
    std::string err;
    CHECK(cameraPathsFromJson(json, out, &err));
    CHECK(err.empty());
    CHECK(out.size() == 2);
    if (out.size() != 2) return;
    CHECK(out[0].name == paths[0].name);
    CHECK(!out[0].loop);
    CHECK(out[1].name.empty());
    CHECK(out[1].loop);
    CHECK(out[1].keys.empty());
    CHECK(out[0].keys.size() == paths[0].keys.size());
    for (size_t i = 0; i < out[0].keys.size() && i < paths[0].keys.size(); ++i) {
        const CameraKey& a = out[0].keys[i];
        const CameraKey& b = paths[0].keys[i];
        CHECK(a.pos == b.pos);  // shortest round-trip doubles: exact
        CHECK(a.yaw == b.yaw);
        CHECK(a.pitch == b.pitch);
        CHECK(a.time == b.time);
    }

    // Bare array form is accepted; an empty list is valid.
    std::vector<CameraPath> bare;
    CHECK(cameraPathsFromJson("[{\"name\":\"x\",\"keys\":[]}]", bare, &err));
    CHECK(bare.size() == 1 && bare[0].name == "x");
    CHECK(cameraPathsFromJson(cameraPathsToJson({}), bare, &err));
    CHECK(bare.empty());
}

TEST(camera_path_json_rejects_bad_input) {
    std::vector<CameraPath> out = {square(false)};
    std::string err;
    CHECK(!cameraPathsFromJson("{", out, &err));
    CHECK(!err.empty());
    CHECK(out.size() == 1);  // untouched on failure

    CHECK(!cameraPathsFromJson("{\"paths\":[{\"keys\":[{\"pos\":[1,2]}]}]}", out, &err));
    CHECK(err.find("paths[0]") != std::string::npos);
    CHECK(out.size() == 1);

    CHECK(!cameraPathsFromJson("{\"nope\":1}", out, &err));
    CHECK(!cameraPathsFromJson("{\"paths\":[{\"keys\":[],\"loop\":\"yes\"}]}", out, &err));
    CHECK(out.size() == 1);
}
