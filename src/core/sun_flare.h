// Sun sprite, flare, blind and glare (roadmap 5.16, research 39 section 3). The x64-zt converter reads
// extracted/sun/<map>.sun (IW3 `key "value"` lines): ReadMapSun draws a sun sprite in the sky when r_sunsprite_shader is
// set (direction r_sun_fx_position "pitch yaw roll", size r_sunsprite_size in 480-line virtual pixels), and ReadSunFlare
// fills GfxWorld's sun flare / blind / glare. The editor writes the file from worldspawn
//   iw8r_sun_sprite "<size> [flare alpha] [blind darken] [glare lighten]"
// with the direction of worldspawn sundirection. Pure core code.
#pragma once

#include <optional>
#include <string>

namespace iw8r {

// The .sun text, or nullopt with *err for a value that does not parse (size 0..64, the others 0..1).
std::optional<std::string> sunFileText(const std::string& keyValue, double sunPitch, double sunYaw, std::string* err = nullptr);

}  // namespace iw8r
