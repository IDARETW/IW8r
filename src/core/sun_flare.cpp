#include "sun_flare.h"

#include <cmath>
#include <cstdio>
#include <sstream>

namespace iw8r {
namespace {
std::string num(double v) {
    char buf[48];
    std::snprintf(buf, sizeof buf, "%.6g", v);
    return buf;
}
}  // namespace

std::optional<std::string> sunFileText(const std::string& keyValue, double sunPitch, double sunYaw, std::string* err) {
    std::istringstream in(keyValue);
    double size = 0, flare = 0, blind = 0.5, glare = 0.3;  // blind / glare: the converter's stock values
    if (!(in >> size) || !std::isfinite(size) || size <= 0 || size > 64) {
        if (err) *err = "iw8r_sun_sprite needs \"<size 1..64> [flare alpha] [blind darken] [glare lighten]\"";
        return std::nullopt;
    }
    double v;
    if (in >> v) flare = v;
    if (in >> v) blind = v;
    if (in >> v) glare = v;
    for (double x : {flare, blind, glare})
        if (!std::isfinite(x) || x < 0 || x > 1) {
            if (err) *err = "iw8r_sun_sprite: flare alpha, blind darken and glare lighten are 0..1";
            return std::nullopt;
        }
    std::string t;
    const auto line = [&](const char* key, const std::string& value) { t += std::string(key) + " \"" + value + "\"\n"; };
    line("r_sunsprite_shader", "sun");
    line("r_sunsprite_size", num(size));
    line("r_sun_fx_position", num(sunPitch) + " " + num(sunYaw) + " 0");
    line("r_sunflare_shader", "sun_flare");
    line("r_sunflare_min_size", "0");
    line("r_sunflare_max_size", num(size * 4));  // the flare grows to four sprite sizes looking straight at the sun
    line("r_sunflare_max_alpha", num(flare));
    line("r_sunblind_max_darken", num(blind));
    line("r_sunglare_max_lighten", num(glare));
    return t;
}

}  // namespace iw8r
