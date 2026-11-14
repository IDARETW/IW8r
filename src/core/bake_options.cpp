#include "bake_options.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace iw8r {

namespace {

// A number alone in the text ("32", "0.5"); false for anything else.
bool number(const std::string& s, double& out) {
    if (s.empty()) return false;
    char* end = nullptr;
    const double v = std::strtod(s.c_str(), &end);
    while (end && *end == ' ') ++end;
    if (!end || *end != '\0' || !std::isfinite(v)) return false;
    out = v;
    return true;
}

}  // namespace

bool MapBakeOptions::skyIsDefault() const {
    const MapBakeOptions d;
    return skyColor.x == d.skyColor.x && skyColor.y == d.skyColor.y && skyColor.z == d.skyColor.z && skyIntensity == d.skyIntensity;
}

MapBakeOptions mapBakeOptions(const Entity* w) {
    MapBakeOptions o;
    if (!w) return o;
    if (const std::string* b = w->value("iw8r_bake_lightmaps")) o.bake = *b == "1";
    if (const std::string* sk = w->value("iw8r_bake_sky")) {
        double v[4] = {0, 0, 0, 0};
        char extra = 0;
        if (std::sscanf(sk->c_str(), "%lf %lf %lf %lf %c", &v[0], &v[1], &v[2], &v[3], &extra) == 4 && v[0] >= 0 && v[1] >= 0 && v[2] >= 0 &&
            v[3] >= 0 && std::isfinite(v[0] + v[1] + v[2] + v[3])) {
            o.skyColor = Vec3{v[0], v[1], v[2]};
            o.skyIntensity = v[3];
        } else {
            o.notes.push_back("worldspawn iw8r_bake_sky '" + *sk + "' is not 'r g b intensity': the default sky is used");
        }
    }
    if (const std::string* ts = w->value("iw8r_lightmap_texel")) {
        double v = 0;
        if (number(*ts, v) && v >= 1) o.texelSize = v;
        else o.notes.push_back("worldspawn iw8r_lightmap_texel '" + *ts + "' is not a size of 1 or more: 16 is used");
    }
    if (const std::string* rs = w->value("iw8r_bake_rays")) {
        double v = 0;
        if (number(*rs, v) && v >= 1) o.skyRays = int(std::min(v, 1024.0));
        else o.notes.push_back("worldspawn iw8r_bake_rays '" + *rs + "' is not a count of 1 or more: 32 is used");
    }
    return o;
}

BakeOptionKeys bakeOptionKeys(const MapBakeOptions& in) {
    MapBakeOptions o = in;
    o.skyColor = Vec3{std::clamp(o.skyColor.x, 0.0, 1.0), std::clamp(o.skyColor.y, 0.0, 1.0), std::clamp(o.skyColor.z, 0.0, 1.0)};
    o.skyIntensity = std::clamp(o.skyIntensity, 0.0, 16.0);
    o.texelSize = std::clamp(o.texelSize, 1.0, 256.0);
    o.skyRays = std::clamp(o.skyRays, 1, 1024);
    BakeOptionKeys k;
    if (!o.skyIsDefault())
        k.sky = formatNumber(o.skyColor.x) + " " + formatNumber(o.skyColor.y) + " " + formatNumber(o.skyColor.z) + " " + formatNumber(o.skyIntensity);
    if (!o.texelIsDefault()) k.texel = formatNumber(o.texelSize);
    if (!o.raysAreDefault()) k.rays = std::to_string(o.skyRays);
    return k;
}

}  // namespace iw8r
