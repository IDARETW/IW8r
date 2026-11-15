// Sky cube maps for the x64-zt extraction (docs/research/33_sky_route.md).
//
// x64-zt reads world.json "sky" as an image name and loads extracted/images/<name>.dds (iw3_render_assets.cpp:6186-6200,
// LoadImage :1285-1340). DecodeDds (:753-881) takes a legacy (DX9) DDS: a cube map (caps2 0xFE00) of six faces in D3D order,
// each followed by its mips, DXT1/3/5 or 8/16/24/32-bit uncompressed. A DX10 header is accepted only for a 2D BC7 image,
// so a DX10 cube or any BC6H image is refused. The IW7 route writes the same shape (export_sky.cpp:836-878: B8G8R8A8,
// one mip, DDS_FLAGS_FORCE_DX9_LEGACY, sRGB bytes).
//
// Replay 1.20 stock skies (sky_mp_*) export through ACTS as a 2D DX10 BC6H_UF16 image (2048x2048, one mip): a
// hemi-octahedral map of the upper hemisphere (centre = zenith, square border = horizon). skyCubemapFromDds turns such
// an image (or any decodable 2D DDS of the same layout) into the legacy B8G8R8A8 cube; a cube the converter already
// accepts is passed through unchanged.
#pragma once

#include <string>

namespace iw8r {

struct SkyConvertOptions {
    int faceSize = 1024;      // texels per cube face edge (4..2048)
    // HDR sources: linear colour times this before the sRGB encode; <= 0 picks it from the image (the 99.5th
    // percentile luminance maps to 0.9, see the .cpp).
    float exposure = 0.0f;
    // HDR sources: times that exposure (worldspawn iw8r_sky_brightness, kSkyBrightnessKey). Above 1 the sky is
    // brighter and its brightest parts (sun, cloud edges) go to white.
    float brightness = 1.0f;
    // The map's worldspawn "skyrotation" (pitch yaw roll, degrees; key 47372): the game turns the view direction into
    // the sky's frame with it before the lookup (sky VS a6bc8f6e: o1 = cb7[153..155] x direction). Mp_m_speedball:
    // 0 -111 0. Only hemi-octahedral sources (a converter cube passes through unchanged).
    float rotation[3] = {0, 0, 0};
    // HDR sources: RGBM faces for the editor's Game Lighting (research 132 G10): the colour times the exposure divided
    // by a multiplier M (stored in alpha as M / rgbmRange, rounded up to 1/255), sRGB-encoded; decode
    // srgbToLinear(rgb) * alpha * rgbmRange. Nothing clips below rgbmRange. Not for the converter (its cube is 8-bit).
    bool rgbm = false;
    float rgbmRange = 16.0f;
};

// An own map's sky brightness: a number from 0.1 to 16, 1 when absent. `bad` is set when the text is not one.
inline constexpr const char* kSkyBrightnessKey = "iw8r_sky_brightness";
float parseSkyBrightness(const std::string* text, bool* bad = nullptr);

struct SkyConvertInfo {
    bool passedThrough = false;  // the source was already a converter cube map
    bool hdr = false;            // BC6H or float source (exposure applied)
    int sourceWidth = 0, sourceHeight = 0;
    unsigned sourceDxgiFormat = 0;  // 0 for a legacy header
    int faceSize = 0;
    float exposure = 1.0f;
    float meanLuminance = 0.0f, highLuminance = 0.0f;  // source linear luminance: mean and 99.5th percentile
    bool rgbm = false;  // the faces are RGBM (SkyConvertOptions::rgbm on an HDR source)
};

// True when `dds` is a DDS the converter loads as a complete cube map (legacy header, caps2 cube with six faces,
// square faces, DXT1/3/5 or uncompressed 8/16/24/32-bit, the payload size matches).
bool isConverterSkyCubemap(const std::string& dds, std::string* why = nullptr);

// Converts a sky DDS (file bytes) into a converter cube map (legacy B8G8R8A8, six faces, one mip). Returns false with
// *err on an unreadable or unsupported source.
bool skyCubemapFromDds(const std::string& dds, std::string& cube, const SkyConvertOptions& opt = {},
                       SkyConvertInfo* info = nullptr, std::string* err = nullptr);

// The image name used for a sky: "tw/sky_mp_shipment" or "Sky_MP_Shipment" -> "sky_mp_shipment" (the part after the
// last '/' or '\\', lower case, characters outside [a-z0-9_-] become '_', at most 64). Empty for an empty name.
std::string skyImageName(const std::string& skyname);

}  // namespace iw8r
