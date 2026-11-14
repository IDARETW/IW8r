// Plan 1.9: a placed model's light from the build's light grid: the point sampler (core light_grid
// lightGridSamplesAt, shared by the build and the viewport) and the reader that follows the build's world shader
// (modelLightFromSamples: the converter's InstanceAmbient).
#include "light_grid.h"
#include "ray_bvh.h"
#include "test_harness.h"

#include <array>
#include <cmath>

using namespace iw8r;

TEST(model_light_reads_the_samples_as_the_build_shader) {
    std::array<std::array<float, 3>, 56> flat{};
    for (auto& s : flat) s = {100.0f, 50.0f, 25.0f};
    // One colour in every direction: that byte x 2 (the shader's "x 2"), whatever the normal.
    for (const Vec3& n : {Vec3{0, 0, 1}, Vec3{0, 0, -1}, Vec3{1, 0, 0}, Vec3{0.3, -0.8, 0.5}}) {
        const Vec3 l = modelLightFromSamples(flat, n);
        CHECK(std::fabs(l.x - 200.0 / 255.0) < 1e-9 && std::fabs(l.y - 100.0 / 255.0) < 1e-9 && std::fabs(l.z - 50.0 / 255.0) < 1e-9);
    }
    // Light from above only: the top is lit, the bottom dark, a side in between; a byte cannot hold more than 255.
    std::array<std::array<float, 3>, 56> top{};
    const auto& dirs = lightGridSampleDirections();
    for (size_t s = 0; s < 56; ++s) top[s] = dirs[s].z > 0.5 ? std::array<float, 3>{400.0f, 400.0f, 400.0f} : std::array<float, 3>{0, 0, 0};
    const Vec3 up = modelLightFromSamples(top, Vec3{0, 0, 1}), down = modelLightFromSamples(top, Vec3{0, 0, -1}), side = modelLightFromSamples(top, Vec3{1, 0, 0});
    CHECK(std::fabs(up.x - 2.0) < 1e-9 && down.x < 1e-9 && side.x >= 0 && side.x < up.x);
    // A zero normal is read as up.
    CHECK(std::fabs(modelLightFromSamples(top, Vec3{0, 0, 0}).x - 2.0) < 1e-9);
}

TEST(model_light_sampler_sees_sky_roof_and_colour) {
    // Open ground far from anything: the sample that points up holds the sky's light, the one pointing down none.
    std::vector<RayBvh::Triangle> far{{{90000, 90000, 0}, {90010, 90000, 0}, {90000, 90010, 0}, 0}};
    RayBvh open;
    open.build(far);
    const Vec3 sky{0.2, 0.3, 0.4};
    std::array<std::array<float, 3>, 56> s{};
    lightGridSamplesAt(open, {}, Vec3{0, 0, 1}, Vec3{1, 1, 1}, sky, 5000.0, Vec3{0, 0, 64}, s);
    const Vec3 up = modelLightFromSamples(s, Vec3{0, 0, 1}), down = modelLightFromSamples(s, Vec3{0, 0, -1});
    // The shader's x 2 on an upward face under an open sky of irradiance `sky`.
    CHECK(std::fabs(up.x - 2 * sky.x) < 0.2 * 2 * sky.x && std::fabs(up.z - 2 * sky.z) < 0.2 * 2 * sky.z);
    CHECK(down.x < 0.15 * up.x);
    // Under a roof and over a red floor in the sun's reach: no sky from above, and the floor sends red light up.
    std::vector<RayBvh::Triangle> room;
    const auto quad = [&](double z, uint32_t id) {
        room.push_back({{-2000, -2000, z}, {2000, -2000, z}, {2000, 2000, z}, id});
        room.push_back({{-2000, -2000, z}, {2000, 2000, z}, {-2000, 2000, z}, id});
    };
    quad(0, 1);     // the floor, colour 1
    quad(4096, 0);  // a roof far above the model, grey
    RayBvh closed;
    closed.build(room);
    std::array<std::array<float, 3>, 56> r{};
    // The sun comes in low from the side, under the roof's edge: it reaches the floor around the model.
    const Vec3 lowSun = normalize(Vec3{1, 0, 0.02});
    lightGridSamplesAt(closed, {Vec3{0.8, 0.1, 0.1}}, lowSun, Vec3{1, 1, 1}, Vec3{0, 0, 0}, 20000.0, Vec3{0, 0, 64}, r);
    const Vec3 under = modelLightFromSamples(r, Vec3{0, 0, -1});
    CHECK(under.x > 0 && under.y > 0 && under.x > 6.0 * under.y);  // the red floor's bounce, 0.8 : 0.1
    CHECK(modelLightFromSamples(r, Vec3{0, 0, 1}).x < under.x);     // more light from below than from the dark roof
}
