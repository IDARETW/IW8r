// IW8 Radiant - terrain height-map import/export, erosion and slope-based blend weights.
//
// A HeightMap is a plain float grid, w columns by h rows. Column 0 is the west edge (min X) and row 0 is the
// NORTH edge, i.e. the maximum Y. So an image read top-left first lines up with the map seen from above.
// Patch grids use the patch_ops.h convention (row 0 = min Y), so heightMapFromPatch and applyHeightMap flip
// the rows when they cross between the two.
//
// Heights: z holds whatever the source provides (world units from heightMapFromPatch, 0..1 from an image).
// applyHeightMap always stretches the map's own lowest value onto zMin and its highest onto zMax, so both
// kinds of map are accepted. A round trip patch -> map -> patch with the patch's own min/max is the identity.
#pragma once

#include <string>
#include <vector>

#include "patch_ops.h"

namespace iw8r {

struct HeightMap {
    int w = 0, h = 0;
    std::vector<float> z;  // row-major: z[row * w + col], size == w * h
};

// Control-point heights of a patch as a map (w = cols, h = rows, row 0 = max Y). Empty when the grid is
// inconsistent (pos.size() != rows * cols).
HeightMap heightMapFromPatch(const PatchGrid& g);

// Sets the z of every control point from the map. Same-sized grid and map: a direct copy. Otherwise bilinear
// sampling over the grid's extent (corners map to corners). The map's min..max is stretched onto zMin..zMax; a
// flat map gives zMin. Only z changes. False (grid untouched) when the map is invalid, the grid is inconsistent,
// the range is not finite or zMax < zMin.
bool applyHeightMap(PatchGrid& g, const HeightMap& hm, double zMin, double zMax);

// Reads a height map. A file starting with "P5" is read as binary PGM (8- or 16-bit, big-endian samples), with
// values v / maxval in 0..1. Anything else goes through decodeImageFile (PNG/TGA/JPG/BMP), using Rec. 601 luminance
// in 0..1 (alpha ignored). Note that stb_image gives 8-bit samples, so 16-bit PNGs lose precision here.
bool readHeightMapImage(const std::string& path, HeightMap& out, std::string* err);

// Writes a 16-bit binary PGM (P5, maxval 65535). Heights are encoded as (z - zMin) / (zMax - zMin), clamped to
// 0..1; the file's first row is hm row 0 (the north edge). Needs zMax > zMin and finite heights.
bool writeHeightMapPgm(const std::string& path, const HeightMap& hm, double zMin, double zMax, std::string* err);

// Thermal (talus) erosion. For each cell the steepest downhill neighbour (8-connected, diagonal distance sqrt 2) is
// checked; when its slope exceeds talus, half of the excess drop moves to that neighbour. Each step reads the
// old heights (Jacobi style), so the result does not depend on the scan order. The total height sum is conserved.
// talus is a slope (height per cell); iterations <= 0 or an invalid map leave hm unchanged.
void thermalErosion(HeightMap& hm, int iterations, double talus);

// Simple deterministic water erosion. Every step each cell receives rain * (0.5 .. 1.5) of water, with the factor
// from a splitmix hash of (seed, step, cell), so the same seed always gives the same map. The water carries
// solubility * water * slope of material to the steepest downhill neighbour, capped at half the drop. Material only
// moves, so the total height sum is conserved. rain and solubility must be > 0.
void hydraulicErosionSimple(HeightMap& hm, int iterations, double rain, double solubility, unsigned seed);

// Per control point blend weight from the surface slope: the angle between the local surface normal and +Z
// (0 for flat, 90 for a vertical wall). The weight is 0 at or below startDeg, 1 at or above endDeg, linear between.
// Empty when the grid is empty or the angles are not finite with endDeg > startDeg.
// Patches carry no per-vertex colour or alpha in the model (PatchVertex has no such field and makePatchPrimitive
// writes none), so this returns the values only. They can be used later if the patch format gains a field.
std::vector<float> slopeBlendAlpha(const PatchGrid& g, double startDeg, double endDeg);

}  // namespace iw8r
