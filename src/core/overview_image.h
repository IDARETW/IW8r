// IW8 Radiant Suite - top-down overview image of a map (callout / minimap style) and PNG writing.
//
// CPU only. The overview is an orthographic top-down raster of the drawn surfaces, unlike minimap.h it is not the
// converter's compass frame: +X is to the right, +Y is up (row 0 = the largest Y), and the image covers the XY
// bounds of the drawn geometry (or of the spawns when nothing is drawn) with one scale on both axes, so the aspect
// ratio follows the bounds.
//
// Surfaces: up-facing brush faces and patch triangles of worldspawn, func_group and script_brushmodel (the same set
// as the minimap). Tool materials (isIw8ToolMaterial) and triggers are not drawn. The nearest surface wins per pixel.
// Colour is the height of the surface (dark at the lowest drawn height, light at the highest) times a slope term, and
// pixels next to a step of more than 8 units are darkened so walls read as outlines.
//
// Markers: spawns are triangles at their XY point pointing along their yaw (allies blue, axis red, other spawns
// green). Objectives are discs with a letter or number: Domination flags (script_gameobjectname "dom ..."; letter from
// script_label), Hardpoint zones ("hardpoint"; number from script_label) and Search & Destroy (bombzone sites, letter
// from script_label, and the "sd" bomb marked B). Labels and the optional grid are drawn last. Text uses a built-in
// 5x7 bitmap font (A-Z, 0-9 and . - _ : + ! /), other characters are skipped.
//
// PNG: RGBA8, colour type 6, one IDAT with a zlib stream of stored (uncompressed) deflate blocks, so the file is
// about the size of the raw pixels (a 2048 x 2048 image is about 16 MB).
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "geometry.h"
#include "map_text.h"

namespace iw8r {

// Encodes an RGBA8 image (w*h*4 bytes, top row first) as a PNG file image. Returns an empty vector when the sizes do
// not match or w or h is below 1.
std::vector<uint8_t> encodePng(int w, int h, const std::vector<uint8_t>& rgba);

// Writes encodePng's output to `path` (parent folders are created). Returns false and sets `err` on failure.
bool writePng(const std::string& path, int w, int h, const std::vector<uint8_t>& rgba, std::string* err);

struct OverviewOptions {
    int width = 2048;             // output width in pixels when unitsPerPixel is 0 (clamped to 16..8192)
    double unitsPerPixel = 0;     // > 0: fixed scale, the width and height follow the bounds; 0 = fit bounds to width
    bool shadeByHeight = true;    // false: every surface uses the highest colour (flat albedo, slope term only)
    bool drawSpawns = true;
    bool drawObjectives = true;
    bool drawGrid = false;
    double gridStep = 512;        // grid spacing in units (lines at multiples of the step)
    // Text labels drawn centred on the XY centre of each box, in map units. Invalid boxes are skipped.
    std::vector<std::pair<std::string, AABB>> labels;
};

// Renders the overview. `rgba` receives w*h*4 bytes (top row first, opaque). Fails (returns false, sets `err`) when the
// map has no drawn geometry and no spawns to take bounds from, or when the requested size cannot be represented
// (a side above 8192 pixels is scaled down to 8192 with the bounds kept).
bool renderOverview(const MapFile& map, const OverviewOptions& opt, std::vector<uint8_t>& rgba, int& w, int& h,
                    std::string* err);

// renderOverview followed by a PNG write. Returns the number of bytes written, 0 on failure (with `err` set).
size_t exportOverviewPng(const MapFile& map, const OverviewOptions& opt, const std::string& path, std::string* err);

}  // namespace iw8r
