// IW8 Radiant - per-sound settings (roadmap 12.8). A sidecar file next to a custom sound, <stem>.iw8snd.json,
// overrides the build's defaults for that sound: volume, distances, looping and 2D. Every field is optional.
//   sounds/fire_loop.wav  +  sounds/fire_loop.iw8snd.json  { "volume": 0.8, "dist_min": 120, "dist_max": 1500 }
// Pure core code.
#pragma once

#include <optional>
#include <string>

#include "custom_sounds.h"

namespace iw8r {

struct SoundSettings {
    std::optional<double> volume;   // 0..1
    std::optional<double> distMin;  // >= 0
    std::optional<double> distMax;  // >= 0, and not below distMin when both are set
    std::optional<bool> loop;
    std::optional<bool> twoD;
};

// Parses a sidecar. False (with `err`) on malformed JSON, a wrong type, volume outside 0..1, a negative distance,
// or dist_max below dist_min. Unknown fields are ignored.
bool parseSoundSettings(const std::string& jsonText, SoundSettings& out, std::string* err);

// Only the set fields, pretty printed, ends with a newline.
std::string soundSettingsJson(const SoundSettings& s);

// Same folder and stem as the wav, with ".iw8snd.json".
std::string soundSettingsPathFor(const std::string& wavPath);

// True with all-unset settings when there is no sidecar. False only when the sidecar exists and is invalid.
bool loadSoundSettingsFor(const std::string& wavPath, SoundSettings& out, std::string* err);

// Set fields override the alias (volume, distMin, distMax, looping, ambient2d); unset fields keep the alias value.
void applySoundSettings(const SoundSettings& s, CustomSoundAlias& alias);

}  // namespace iw8r
