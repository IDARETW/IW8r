// Sun from a sky cube (src/core/sky_sun.h).
#include <cmath>
#include <string>

#include "../src/core/sky_panorama.h"
#include "../src/core/sky_sun.h"
#include "test_harness.h"

using namespace iw8r;

namespace {

// A legacy cube DDS body (128-byte header left zero: the reader only checks the size) with a grey sky and a white
// disc around `sun` (unit vector), using the camera's face mapping.
std::string cubeWithSun(int fs, const Vec3& sun, double discCos = 0.995) {
    std::string out(128 + size_t(6) * size_t(fs) * size_t(fs) * 4, '\0');
    auto* px = reinterpret_cast<uint8_t*>(out.data()) + 128;
    for (int f = 0; f < 6; ++f)
        for (int y = 0; y < fs; ++y)
            for (int x = 0; x < fs; ++x) {
                const double u = 2.0 * (x + 0.5) / fs - 1.0, v = 2.0 * (y + 0.5) / fs - 1.0;
                Vec3 d;
                switch (f) {
                    case 0: d = {1, -v, -u}; break;
                    case 1: d = {-1, -v, u}; break;
                    case 2: d = {u, 1, v}; break;
                    case 3: d = {u, -1, -v}; break;
                    case 4: d = {u, -v, 1}; break;
                    default: d = {-u, -v, -1}; break;
                }
                d = normalize(d);
                const bool inSun = dot(d, sun) > discCos;
                const uint8_t g = inSun ? 255 : uint8_t(90 + 60 * std::max(0.0, d.z));
                uint8_t* t = px + (size_t(f) * size_t(fs) * size_t(fs) + size_t(y) * size_t(fs) + size_t(x)) * 4;
                t[0] = t[1] = t[2] = g;
                t[3] = 255;
            }
    return out;
}

Vec3 sunVector(double pitch, double yaw) {
    const double p = pitch * 3.14159265358979 / 180.0, y = yaw * 3.14159265358979 / 180.0;
    return {std::cos(p) * std::cos(y), std::cos(p) * std::sin(y), -std::sin(p)};
}

}  // namespace

TEST(sky_sun_finds_the_disc_on_every_side) {
    for (const auto& [pitch, yaw] : std::vector<std::pair<double, double>>{{-45, 30}, {-20, 200}, {-70, -100}, {-10, 90}, {-80, 0}}) {
        const Vec3 want = sunVector(pitch, yaw);
        const auto s = sunFromSkyCube(cubeWithSun(64, want));
        CHECK(s.has_value());
        if (!s) continue;
        CHECK(dot(s->dir, want) > 0.999);  // within about 2.5 degrees
        CHECK(s->contrast > 1.5);
        // The worldspawn key from it points the same way (sky_panorama sunDirectionKeyFor).
        double p = 0, y = 0;
        CHECK(std::sscanf(sunDirectionKeyFor(s->dir).c_str(), "%lf %lf", &p, &y) == 2);
        CHECK(dot(sunVector(p, y), want) > 0.999);
    }
}

TEST(sky_sun_ignores_light_below_the_horizon_and_bad_input) {
    // Only a bright spot below the horizon: the brightest sky above is the grey gradient's zenith.
    const auto s = sunFromSkyCube(cubeWithSun(32, normalize(Vec3{1, 0, -0.5})));
    CHECK(s && s->dir.z > 0.9);
    CHECK(!sunFromSkyCube(std::string(100, '\0')));
    CHECK(!sunFromSkyCube(std::string(128 + 6 * 4 * 4 * 4 + 3, '\0')));
}
