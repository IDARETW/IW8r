// Image adjustments on 8-bit RGBA buffers: colour grading, channel mapping, resize and a
// height-to-normal-map conversion. Pure C++, no Qt. Pixels are row-major, 4 bytes each (R, G, B, A),
// top row first.
#pragma once

#include <cstdint>
#include <vector>

namespace iw8r {

struct ImageAdjust {
    double brightness = 0.0;   // added to each colour channel, -1..1
    double contrast = 1.0;     // scale around mid grey, 0..3
    double saturation = 1.0;   // 0 = grey, 1 = unchanged, up to 3
    double hueShiftDeg = 0.0;  // hue rotation in degrees
    double gamma = 1.0;        // out = in^(1/gamma); above 1 brightens the midtones
    double tint[3] = {1.0, 1.0, 1.0};  // per-channel multiplier
    bool invert = false;
    // Output channel i takes input channel channelMap[i] (0..3 = R,G,B,A).
    // 4 = constant 0, 5 = constant 255.
    int channelMap[4] = {0, 1, 2, 3};
};

// True when adjustImage would leave every pixel unchanged.
bool isIdentity(const ImageAdjust& a);

// Applies the adjustments in place. Order: channel map, tint, brightness, contrast, saturation,
// hue, gamma, invert. Alpha is only changed by the channel map.
void adjustImage(std::vector<uint8_t>& rgba, int width, int height, const ImageAdjust& a);

// Resizes to dw x dh. Shrinking uses a box filter, enlarging uses bilinear sampling. Returns false
// on bad sizes or when src is not sw * sh * 4 bytes.
bool resizeImage(const std::vector<uint8_t>& src, int sw, int sh, int dw, int dh, std::vector<uint8_t>& dst);

// Builds a tangent-space normal map from the height (luminance of src) with a Sobel filter.
// strength scales the slope. wrap = tile the edges, otherwise edge pixels are clamped.
// Convention: red = +X (right), green = +Y (up, row 0 is the top of the image), blue = +Z (out of
// the surface). Each axis is stored as 0..255 with 128 = zero. Output alpha is 255.
bool normalMapFromHeight(const std::vector<uint8_t>& src, int w, int h, double strength, bool wrap,
                         std::vector<uint8_t>& dst);

}  // namespace iw8r
