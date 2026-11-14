#include "sky_sun.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "sky_image.h"

namespace iw8r {

namespace {

Vec3 faceDir(int f, double u, double v) {
    switch (f) {
        case 0: return {1, -v, -u};
        case 1: return {-1, -v, u};
        case 2: return {u, 1, v};
        case 3: return {u, -1, -v};
        case 4: return {u, -v, 1};
        default: return {-u, -v, -1};
    }
}

}  // namespace

std::optional<SkySun> sunFromSkyCube(const std::string& cube) {
    if (cube.size() <= 128) return std::nullopt;
    const int fs = int(std::lround(std::sqrt((cube.size() - 128) / 24.0)));
    if (fs <= 0 || cube.size() != 128 + size_t(6) * size_t(fs) * size_t(fs) * 4) return std::nullopt;
    const auto* px = reinterpret_cast<const uint8_t*>(cube.data()) + 128;
    struct Texel {
        Vec3 dir;
        double lum, weight;
    };
    std::vector<Texel> up;
    up.reserve(size_t(fs) * size_t(fs) * 3);
    double peak = 0, sum = 0, area = 0;
    for (int f = 0; f < 6; ++f)
        for (int y = 0; y < fs; ++y)
            for (int x = 0; x < fs; ++x) {
                const double u = 2.0 * (x + 0.5) / fs - 1.0, v = 2.0 * (y + 0.5) / fs - 1.0;
                const Vec3 d = faceDir(f, u, v);
                const double len = length(d);
                const Vec3 n = d * (1.0 / len);
                if (n.z <= 0.02) continue;  // the sun is above the horizon
                const uint8_t* t = px + (size_t(f) * size_t(fs) * size_t(fs) + size_t(y) * size_t(fs) + size_t(x)) * 4;
                // B8G8R8A8; Rec. 709 luminance.
                const double lum = (0.2126 * t[2] + 0.7152 * t[1] + 0.0722 * t[0]) / 255.0;
                const double w = 1.0 / (len * len * len);  // texel solid angle on a cube face
                up.push_back({n, lum, w});
                peak = std::max(peak, lum);
                sum += lum * w;
                area += w;
            }
    if (up.empty() || peak <= 0) return std::nullopt;
    Vec3 acc;
    for (const Texel& t : up) {
        if (t.lum < peak * 0.98) continue;
        acc += t.dir * (std::pow(t.lum / peak, 16.0) * t.weight);
    }
    if (length(acc) < 1e-12) return std::nullopt;
    SkySun s;
    s.dir = normalize(acc);
    s.peak = peak;
    s.contrast = area > 0 && sum > 0 ? peak / (sum / area) : 0;
    return s;
}

std::optional<SkySun> sunFromSkyDds(const std::string& dds, std::string* err) {
    std::string cube;
    SkyConvertOptions so;
    so.faceSize = 128;
    if (!skyCubemapFromDds(dds, cube, so, nullptr, err)) return std::nullopt;
    return sunFromSkyCube(cube);
}

double autoSkyRotationYaw(const std::string& dds, double sunYaw, double* imageSunYaw) {
    return autoSkyRotationYaw(sunFromSkyDds(dds), sunYaw, imageSunYaw);
}

double autoSkyRotationYaw(const std::optional<SkySun>& s, double sunYaw, double* imageSunYaw) {
    double image = 0;
    if (s && s->contrast >= 1.6 && (s->dir.x != 0 || s->dir.y != 0))
        image = std::atan2(s->dir.y, s->dir.x) * 180.0 / 3.14159265358979323846;
    if (imageSunYaw) *imageSunYaw = image;
    double r = std::fmod(sunYaw - image, 360.0);
    if (r > 180) r -= 360;
    if (r <= -180) r += 360;
    return r;
}

}  // namespace iw8r
