// IW8 Radiant Suite - top-down minimap render (the HUD compass image) and its DDS writer.
//
// The image is what x64-zt's converter reads as images/compass_map_<id>.dds (research: docs/research/27_minimap.md).
// Frame: the converter's SOURCE frame, the one it turns by 90 - northyaw degrees clockwise (convert.cpp:2264-2281).
// The Radiant .ents has no northyaw, so the converter turns the image 90 degrees clockwise. For that reason:
//   - row 0 (top) is world +X (north of the source), and the bottom row is -X;
//   - column 0 (left) is world +Y, and the right column is -Y.
// After the converter's clockwise turn, the in-game minimap has +X on the right and +Y at the top.
//
// The covered rectangle (MinimapImage::worldRect, z range = drawn geometry) is square. It is the two
// script_origin entities with targetname minimap_corner when there are exactly two (their XY box), squared about
// its centre to the longer span as convert.cpp:2226-2245 does. Without them it is the drawn geometry's XY box,
// squared about its centre. Pixel (px, py) covers world
//   x = worldRect.maxs.x - (py + 0.5) * span / height,   y = worldRect.maxs.y - (px + 0.5) * span / width.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "geometry.h"
#include "map_text.h"

namespace iw8r {

struct MinimapImage {
    int width = 0, height = 0;
    // Row-major, top row first, 4 bytes per pixel (R, G, B, A), not premultiplied. A is 0 where nothing is drawn.
    std::vector<uint8_t> rgba;
    // Covered world rectangle (XY; z = range of the drawn geometry). Invalid when the map draws nothing and has no
    // minimap_corner pair.
    AABB worldRect;
};

// Software top-down render of the drawn surfaces: brush faces and patches of worldspawn, func_group and
// script_brushmodel. Tool materials (isIw8ToolMaterial) and triggers are not drawn; only faces that face up
// are rasterised (walls show as their caps). The nearest up-facing surface wins per pixel. Colour is a flat
// albedo shaded by normal (one fixed light from above) and by height: higher surfaces are lighter.
// `size` is clamped to 4..8192 and rounded up to a power of two.
MinimapImage renderMinimap(const MapFile& map, int size = 512);

// Writes a legacy FourCC 'DXT5' (BC3) 2D DDS (128-byte header, DDSD_LINEARSIZE) with the full mip chain down to
// 1x1, the form the compass reader accepts (games/iw8/dump/read_image.cpp:218-300). Mips are box-filtered with alpha
// weighting, then each level is BC3-encoded. The image must be square with a power-of-two side. Returns false and
// sets `err` on failure.
bool writeMinimapDds(const MinimapImage& img, const std::string& ddsPath, std::string* err);
// The same file as bytes (the map's own HUD images use the same form, core hud_images).
bool encodeMinimapDds(const MinimapImage& img, std::vector<uint8_t>& file, std::string* err);

}  // namespace iw8r
