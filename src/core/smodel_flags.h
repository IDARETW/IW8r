// Static model (misc_model) lighting, shadow and cull flags (roadmap 5.22, research doc 54).
// The editor writes these as misc_model keys; the build (compile_iw8.cpp) passes them to world.json models[].
// Which bits and keys the x64-zt converter actually reads, and which need a fork change, is in doc 54.
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace iw8r {

// misc_model key names (doc 54 section 5).
inline constexpr const char* kSModelKeyNoShadow = "no_shadow";
inline constexpr const char* kSModelKeyGroundLighting = "ground_lighting";
inline constexpr const char* kSModelKeyCullDist = "cull_dist";
inline constexpr const char* kSModelKeyNoCollision = "no_collision";
inline constexpr const char* kSModelKeyLodScale = "lod_scale";

// smodelFlagBits() bits. Bit 0 is the converter's model_flags & 1 (read today, iw3_fastfile.cpp:3804).
// Bits 1 and 2 are proposed for a fork and are not read by the current converter (doc 54 section 3).
inline constexpr std::uint32_t kSModelBitGroundLighting = 1u;
inline constexpr std::uint32_t kSModelBitNoShadow = 2u;
inline constexpr std::uint32_t kSModelBitNoCollision = 4u;

struct SModelFlags {
    bool noShadow = false;        // no sun shadow and no local-light shadow (doc 54: sun part needs a fork)
    bool groundLighting = false;  // ground-lit placement (model_flags bit 0): one flat colour instead of the grid
    // The packed ground colour (GfxColor: a << 24 | b << 16 | g << 8 | r; the converter's default 0xFF282828).
    // ground_lighting "1" keeps the default; "r g b" (0..1, or 0..255 when any is above 1) sets it.
    std::uint32_t groundColor = 0xFF282828u;
    double cullDistance = 0;      // IW3 units; 0 = no distance cull (the key is omitted)
    bool noCollision = false;     // no collision for this placement (fork only, doc 54)
    double lodScale = 1.0;        // Replay LOD distance scale; one of the 16 table values (doc 54)
};

// Reads the flags from misc_model keys. Missing keys keep the defaults. A bad value keeps the default and
// adds one line to *warnings (warnings may be nullptr).
SModelFlags readSModelFlags(const std::map<std::string, std::string>& keys, std::vector<std::string>* warnings);

// Writes the flags as keys. Values at their defaults erase their key, so a default struct leaves no keys.
// cull_dist is written with enough digits to read back exactly.
void writeSModelFlags(const SModelFlags& flags, std::map<std::string, std::string>& keys);

// The flag word for the flags above (bit values in kSModelBit*). Cull distance and LOD scale are not bits.
std::uint32_t smodelFlagBits(const SModelFlags& flags);

// The game's LOD distance scale for index 0..15 (the low four bits of a placement's instance flags; doc 54). 6 is 1.
// An index out of range gives 1.
double smodelLodScaleOfIndex(unsigned index);

// Parses a ground_lighting colour "r g b" (0..1, or 0..255 when any channel is above 1) into the packed GfxColor
// (alpha 0xFF). Returns false for anything else.
bool parseGroundColor(const std::string& text, std::uint32_t& packed);

}  // namespace iw8r
