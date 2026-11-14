// IW8 Radiant Suite - top-down minimap render (the HUD compass image) and its DDS writer.
//
// The image is what x64-zt's converter reads as images/compass_map_<id>.dds (research: docs/research/27_minimap.md).
// Frame: the game shows the picture with world +Y at the top and +X on the right. The converter takes a picture
// drawn with the map's north (worldspawn northyaw) at the top and turns it clockwise by 90 - northyaw degrees
// (convert.cpp, "compass: rotating the minimap"), which gives the game's frame for every quarter turn. So the picture
// is drawn north up, for the northyaw the build writes (MinimapImage::northQuarter):
//   quarter 0 (northyaw 0, north +X):   row 0 is +X, column 0 is +Y   (the converter turns it by 90)
//   quarter 1 (northyaw 90, north +Y):  row 0 is +Y, column 0 is -X   (not turned: the build's default)
//   quarter 2 (northyaw 180, north -X): row 0 is -X, column 0 is -Y
//   quarter 3 (northyaw 270, north -Y): row 0 is -Y, column 0 is +X
// (Until 2026-11-14 it was always drawn as quarter 0 while the build wrote northyaw 90: a quarter turn off in game.)
//
// The covered rectangle (MinimapImage::worldRect, z range = drawn geometry) is square. It is the two
// script_origin entities with targetname minimap_corner when there are exactly two (their XY box), squared about
// its centre to the longer span as convert.cpp:2226-2245 does. Without them it is the drawn geometry's XY box,
// squared about its centre. With quarter 0, pixel (px, py) covers world
//   x = worldRect.maxs.x - (py + 0.5) * span / height,   y = worldRect.maxs.y - (px + 0.5) * span / width;
// minimapPixel gives the pixel of a world point for any quarter.
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
    // Which way is up in the picture: 0 +X, 1 +Y, 2 -X, 3 -Y (the map's north, in quarter turns of northyaw).
    int northQuarter = 0;
};

// The quarter turn of a northyaw in degrees (0..3); *exact is false when it is not a multiple of 90 (the nearest
// quarter is returned).
int minimapNorthQuarter(double northYawDegrees, bool* exact = nullptr);
// The northyaw a build writes for a map: worldspawn northyaw, or 90 (north = +Y, the top of the editor's XY view).
double mapNorthYaw(const MapFile& map);
// The quarter a build draws the map's minimap with. A northyaw that is no quarter turn is left unturned by the
// converter, so the picture is drawn in the game's own frame (quarter 1) and *warning says so.
int minimapQuarterForBuild(const MapFile& map, std::string* warning = nullptr);
// The pixel (column, row; fractions) a world point has in a picture of `size` pixels over `rect` drawn with `quarter`.
void minimapPixel(const AABB& rect, int size, int quarter, double x, double y, double& column, double& row);

// Software top-down render of the drawn surfaces: brush faces and patches of worldspawn, func_group and
// script_brushmodel. Tool materials (isIw8ToolMaterial) and triggers are not drawn; only faces that face up
// are rasterised (walls show as their caps). The nearest up-facing surface wins per pixel. Colour is a flat
// albedo shaded by normal (one fixed light from above) and by height: higher surfaces are lighter.
// `size` is clamped to 4..8192 and rounded up to a power of two.
// `northQuarter`: which way is up (see the top of this file); a build passes minimapQuarterForBuild(map).
MinimapImage renderMinimap(const MapFile& map, int size = 512, int northQuarter = 0);

// Writes a legacy FourCC 'DXT5' (BC3) 2D DDS (128-byte header, DDSD_LINEARSIZE) with the full mip chain down to
// 1x1, the form the compass reader accepts (games/iw8/dump/read_image.cpp:218-300). Mips are box-filtered with alpha
// weighting, then each level is BC3-encoded. The image must be square with a power-of-two side. Returns false and
// sets `err` on failure.
bool writeMinimapDds(const MinimapImage& img, const std::string& ddsPath, std::string* err);
// The same file as bytes (the map's own HUD images use the same form, core hud_images).
bool encodeMinimapDds(const MinimapImage& img, std::vector<uint8_t>& file, std::string* err);

}  // namespace iw8r
