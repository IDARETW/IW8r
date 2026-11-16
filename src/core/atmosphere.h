// IW8 Radiant - map atmosphere (doc 31): exponential fog and film grade, stored as editor-owned worldspawn keys
// and written by the compiler as the extraction files x64-zt reads:
//   iw8r_fog  "start halfway r g b"           -> extracted/maps/mp/<id>.gsc   main() { setExpFog(start, halfway, r, g, b); }
//   iw8r_film "contrast brightness desaturation invert lr lg lb dr dg db"
//                                             -> extracted/vision/<id>.vision  r_film* keys only (other tokens are ignored)
// Exposure stays `iw8r_exposure_adjust` (metadata exposureAdjust, -8..8 EV), handled by the compile panel.
// The native world.json fog object is not used: it and setExpFog together make the converter throw (doc 31 section 6).
#pragma once

#include <optional>
#include <cstdint>
#include <string>
#include <vector>

#include "geometry.h"
#include "map_text.h"

namespace iw8r {

struct ExpFog {
    double start = 0;       // >= 0, converter clamps to 0..20000
    double halfway = 1800;  // > 0, converter clamps to 1..70000
    Vec3 color{0.55, 0.60, 0.70};  // 0..1
};

struct FilmGrade {
    double contrast = 1, brightness = 0, desaturation = 0;
    bool invert = false;
    Vec3 lightTint{1, 1, 1}, darkTint{1, 1, 1};
    // The converter drops an identity film (V:326-327).
    bool isIdentity() const;
};

// The game's own fog and volumetric air (research 126): a preset and how much of it. The game fogs every opaque
// pixel and the sky itself (distance and height fog with a sun-side glow, the fog's blend into the sky) and fills the
// air with volumetric fog that the sun lights and shadows (light shafts) and flagged lamps glow in.
struct Haze {
    std::string preset;   // an id of hazePresets()
    double amount = 1.0;  // 0.1 .. 4: scales the densities (1 = the preset as designed)
};
struct HazePreset {
    const char* id;
    const char* label;
    const char* about;
};
const std::vector<HazePreset>& hazePresets();

struct Atmosphere {
    std::optional<Haze> haze;  // iw8r_haze: replaces `fog` in the build
    std::optional<ExpFog> fog;
    std::optional<FilmGrade> film;
    std::string lut;  // iw8r_lut: .cube path ("" = none); a film grade wins over it in the converter
    std::string vision;  // iw8r_vision: "name=value; name=x y z" Replay vision overrides ("" = none)
};

inline constexpr const char* kFogKey = "iw8r_fog";
inline constexpr const char* kFilmKey = "iw8r_film";
// Colour grading LUT (doc 31 section 3.4): path of an Adobe/Resolve .cube 3D LUT, absolute or relative to the map.
inline constexpr const char* kLutKey = "iw8r_lut";
// Vision field overrides (5.6): "name=value;name=value", name a Replay vision dev name (tonemapExposure, ...) or token,
// value a number or "x y z". Written as extracted/vision/<id>.overrides.json (x64-zt fork applies it last).
inline constexpr const char* kVisionKey = "iw8r_vision";
// Parses kVisionKey text into the overrides JSON object text ("" when empty or malformed; `err` says why).
std::string visionOverridesJson(const std::string& text, std::string* err = nullptr);
// `base` with every field that `over` names replaced by over's value, over's other fields added (both are kVisionKey
// text; an item that is not name=value is dropped from `base` and kept in `over` for the parser to report).
std::string mergeVisionText(const std::string& base, const std::string& over);

// The game's own fog and volumetric air: "<preset> [amount]" (kHazeKey). It needs the x64-zt fork that leaves the
// baked fog out when the vision turns the atmospheric fog on.
inline constexpr const char* kHazeKey = "iw8r_haze";
std::optional<Haze> parseHaze(const std::string& v);  // nullopt: empty, an unknown preset or a bad amount
std::string formatHaze(const Haze& h);
// The haze as kVisionKey text. `sunLuminance`: the map sun's luminance in the build's light units (the fog's
// brightness is absolute in the game, so it is shifted from the stock maps' sun level to this one); `sunDir`: unit,
// toward the sun; `groundZ`: where the ground layers sit. "" for an unknown preset.
std::string hazeVisionText(const Haze& h, double sunLuminance, const Vec3& sunDir, double groundZ);

// Parses a .cube 3D LUT (LUT_3D_SIZE N, N^3 rows of "r g b", red fastest, DOMAIN_MIN/MAX honoured) into the
// converter's grading CLUT: N^3 RGBA8 texels, red fastest, alpha 255. N must be 2..256.
bool parseCubeLut(const std::string& text, int& size, std::vector<uint8_t>& rgba, std::string* err = nullptr);

// Parses the worldspawn keys; malformed values are treated as absent and reported through `warn` when given.
Atmosphere readAtmosphere(const Entity& worldspawn, std::string* warn = nullptr);
std::string formatFog(const ExpFog& f);
std::string formatFilm(const FilmGrade& f);
std::optional<ExpFog> parseFog(const std::string& v);
std::optional<FilmGrade> parseFilm(const std::string& v);

// Extraction file bodies. Values are clamped to the converter's accepted ranges.
std::string fogGsc(const ExpFog& f);
std::string filmVision(const FilmGrade& f);

}  // namespace iw8r
