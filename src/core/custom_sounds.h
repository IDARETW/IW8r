// IW8 Radiant - the mapper's own sounds (roadmap 6.22: custom sound import).
//
// A WAV or MP3 in a sounds folder (<map dir>/sounds/<name>.wav|.mp3) becomes a sound alias named <name> in the map's
// sound bank: the build copies the file to extracted/sound/iw8r/<name>.<ext> and writes the IW3 alias definition the
// x64-zt converter reads (extracted/soundaliases/<name>.iw3.json, iw3_sound.cpp ReadZoneAliases). The converter builds
// every alias the map script names as a literal and every zone ambient stream of the sound table, so a sound_emitter or
// an audio zone's iw8r_ambient naming a custom sound plays it. Pure core code.
#pragma once

#include <string>
#include <vector>

namespace iw8r {

// Names: lower-case letters, digits and '_' (no folders), 1 to 60 characters.
bool isValidCustomSoundName(const std::string& name);

// The file for <name> in the roots (<root>/<name>.wav, then .mp3, case-insensitive extension), or "".
std::string findCustomSound(const std::vector<std::string>& roots, const std::string& name);

struct CustomSoundAlias {
    std::string name;
    std::string fileName;    // e.g. fire_loop.wav (stored under sound/iw8r/)
    bool looping = false;
    bool ambient2d = false;  // IW3 channel "ambient" (2D bed) instead of "auto" (3D, positioned)
    double volume = 1;       // 0..1
    double distMin = 120, distMax = 1000;
};

// The gain the converter multiplies a converted alias by (x64-zt iw3_sound.cpp RoleFor / TemplateFor): every looping
// alias 0.18, a one-shot whose name has glass / break / shatter / tv_ / door 0.45, fire / flare / flame / burn 0.35,
// any other one-shot 1. customSoundAliasJson divides the volume by it, so "volume 1" is the sound at full level
// (owner playtest 2, 2026-11-11: a looping emitter of the map's own was not heard; it played at 0.18).
double converterRoleGain(const std::string& aliasName, bool looping);

// The converter's IW3 alias definition (schema of ReadZoneAliases).
std::string customSoundAliasJson(const CustomSoundAlias& a);

}  // namespace iw8r
