// Live hot channels (roadmap 7.7 and 7.9): dvar channels, written by the editor (Live Link) while the game runs, that
// drive effects, sounds and the sun. They extend the placement channel iw8r_hot (7.4, map_script.h): same idea, one
// dvar per channel, a sequence number in front so a repeated move is still a new value, and a GSC thread that polls
// the dvars with getdvar and applies each new value once.
//
// Channels and value formats (all fields space separated, no quotes or ';', ASCII; numbers use formatNumber):
//   iw8r_fx   "<seq> fx <n> [<effect> x y z pitch yaw roll <play>]*n"
//             effect: stock vfx/ name (safe characters only); play 1 = playfx at the origin with the angles,
//             play 0 = only loadfx (the handle is cached, nothing is played).
//   iw8r_snd  "<seq> snd <alias> x y z <loop> <stop>"
//             loop 1 = playloopsound on a script_origin kept per alias (the origin moves with each message),
//             loop 0 = playsoundatpos once; stop 1 = stoploopsound and delete that origin (loop 0 is ignored then).
//   iw8r_sun  "<seq> sun dx dy dz r g b intensity"
//             direction as the vector given to setsundirection, colour and intensity to setsuncolorandintensity.
//
// Every encoder returns "" when there is nothing to send, a value is invalid (unsafe name, non-finite number,
// negative intensity) or the value is longer than kHotChannelMaxLen. The existing channel has no splitting either;
// a caller that needs more effects than fit sends them in several messages with increasing seq. Note that the
// GSC reads one value per channel per poll, so a message sent within 0.1 s of another can replace it.
#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace iw8r {

inline constexpr const char* kHotFxDvar = "iw8r_fx";
inline constexpr const char* kHotSndDvar = "iw8r_snd";
inline constexpr const char* kHotSunDvar = "iw8r_sun";

// Same limit the editor uses for dvar values in this design (docs/research/47 section 6.1).
inline constexpr std::size_t kHotChannelMaxLen = 200;

struct HotFx {
    std::string effect;     // "vfx/..." stock Replay effect name
    double origin[3]{};
    double angles[3]{};     // pitch yaw roll, degrees
    bool play = false;      // true: playfx now; false: loadfx only
};

struct HotSound {
    std::string alias;      // stock sound alias (no '/')
    double origin[3]{};
    bool loop = false;      // playloopsound on a kept script_origin, else playsoundatpos once
    bool stop = false;      // stoploopsound and delete the kept origin for this alias
};

struct HotSun {
    double dir[3]{};        // sun angles pitch yaw roll in degrees, as the worldspawn sundirection key
    float color[3]{};       // colour for setsuncolorandintensity
    double intensity = 1;   // intensity for setsuncolorandintensity (>= 0)
};

// The sequence number is the first field of the value; the caller increments it for every new send.
std::string encodeHotFx(const std::vector<HotFx>& items, unsigned seq = 1);
std::string encodeHotSound(const HotSound& sound, unsigned seq = 1);
std::string encodeHotSun(const HotSun& sun, unsigned seq = 1);

// Matching decoders: nullopt for "" or any malformed value. `seq`, when given, receives the sequence number.
std::optional<std::vector<HotFx>> decodeHotFx(const std::string& value, unsigned* seq = nullptr);
std::optional<HotSound> decodeHotSound(const std::string& value, unsigned* seq = nullptr);
std::optional<HotSun> decodeHotSun(const std::string& value, unsigned* seq = nullptr);

// GSC source of the thread iw8r_hot_channels() (polls iw8r_fx, iw8r_snd, iw8r_sun). Start it with
// `level thread iw8r_hot_channels();` in main(). It defines iw8r_hot_channels, iw8r_hot_fx, iw8r_hot_fx_get,
// iw8r_hot_snd and iw8r_hot_sun, and stores its handles in level.iw8r_hotfx and level.iw8r_hotsnd.
std::string hotChannelsGsc();

// Worldspawn keys to a sun value: sundirection "pitch yaw roll" is kept as angles (the GSC calls lerpsunangles like
// stock map scripts), suncolor "r g b" in 0..1, sunlight the intensity (1 when absent or not a number).
HotSun hotSunFromKeys(const std::string& sundirection, const std::string& suncolor, const std::string& sunlight);

}  // namespace iw8r
