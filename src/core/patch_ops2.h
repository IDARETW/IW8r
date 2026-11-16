// IW8 Radiant - second batch of Radiant patch tools: caps, weld, redisperse, split, terrain and sculpting.
//
// Everything here works on PatchGrid (see patch_ops.h for the grid convention and the .map text layout). Every
// result is a plain grid, so it can be written with makePatchPrimitive and re-read with reparsePrimitive.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "geometry.h"
#include "patch_ops.h"

namespace iw8r {

// Radiant Cap: closes the two open ends of a cylinder-like patch. The rings are the first and last rows of the
// grid (each row is one ring of control points, as makeCylinderPatch lays them out). For each end ring a flat
// "mesh" cap is returned: row 0 is the ring itself, row 1 is halfway to the ring centre, row 2 is the centre
// (every column collapsed to the same point). Closed rings (first column equal to last column) stay closed.
// Each cap faces away from the body. The result is {first-row cap, last-row cap}, or empty when the grid has
// fewer than 2 rows or 2 columns. Bevel caps are not generated; only flat caps are.
std::vector<PatchGrid> capPatch(const PatchGrid& g);

// Radiant Connect: joins two grids that share a full edge (same number of points along it, equal within tol)
// into one grid. The edge may be any side of either grid and may run in either direction. The result keeps a's
// keyword, material and facing; points on the shared edge are taken from a. A 3x3 and a 3x3 sharing an edge
// give a 3x5 (or 5x3). nullopt when the keywords differ, a grid is smaller than 2x2, no edge pair matches within
// tol, or the join would not be a complete grid.
std::optional<PatchGrid> weldPatches(const PatchGrid& a, const PatchGrid& b, double tol = 0.01);

// Even spacing of the interior control points. rows == true spaces each row (points along the column index)
// by arc length between its two end points; cols == true does the same down each column. The rows are done
// first, then the columns. Corner and border end points are kept where they are, and st is resampled with the
// same parameter so the texture stays with its points.
PatchGrid redisperse(const PatchGrid& g, bool rows, bool cols);

// Splits a grid into two valid patches that share one control line. `at` is the 0-based index of the shared
// row (alongRows) or column (!alongRows). It must be even, which is an odd control row or column in 1-based
// numbering, must have at least 2 lines before it, and the total count must be odd with at least 3 lines after
// it. Otherwise the result is empty. weldPatches(parts[0], parts[1]) gives the original back.
std::vector<PatchGrid> splitPatch(const PatchGrid& g, bool alongRows, int at);

// Terrain-style flat grid: a "mesh" patch in the XY plane at box.mins.z, rows along Y and columns along X.
// st follows the world size (makeFlatPatch's natural rule). rows and cols must be >= 2 and the box valid,
// otherwise the result has rows == 0.
PatchGrid makeTerrainPatch(const AABB& box, int rows, int cols, const std::string& material);

// Deterministic value noise on every control point (iw3xo vertex noise). The noise is a smooth lattice field
// with a 64 unit cell, so each displacement is within +-amplitude. zOnly moves only z, using a 2D field over
// (x, y). Otherwise x, y and z each get their own independent field. The same seed always gives the same grid.
PatchGrid applyNoise(const PatchGrid& g, double amplitude, uint32_t seed, bool zOnly = true);

// Laplacian smoothing of the heights: each interior point's z becomes the average of its four grid neighbours,
// repeated `iterations` times (Jacobi style). Border points and all x, y values are unchanged. Grids smaller
// than 3x3 and iterations <= 0 come back as they are.
PatchGrid smoothPatch(const PatchGrid& g, int iterations);

// Terrain brush: adds amount * (1 - t^2)^2 to z, where t is the XY distance from center divided by radius.
// The falloff is smooth and reaches zero at the radius, so only points strictly inside radius move. A radius
// <= 0 or amount == 0 leaves the grid unchanged.
PatchGrid raiseLower(const PatchGrid& g, const Vec3& center, double radius, double amount);

// Brush-local sculpt tools. Each one weights a control point by the raiseLower falloff, w = (1 - t^2)^2 with
// t = XY distance / radius, and points at or beyond radius are never touched. A radius <= 0 or non-finite
// input leaves the grid unchanged. strength is clamped to [0, 1]: each step moves a point the fraction
// strength * w of the way toward its target.

// Blends each point's z toward the average z of its grid neighbours (up to four, fewer at the edges). Targets
// come from the input heights, so the result does not depend on the point order (Jacobi style).
PatchGrid smoothAround(const PatchGrid& g, const Vec3& center, double radius, double strength);

// Blends each point's z toward targetZ.
PatchGrid flattenAround(const PatchGrid& g, const Vec3& center, double radius, double targetZ, double strength);

// Adds amplitude * w * n(x, y) to z, where n is the same value-noise field applyNoise(zOnly) uses for this seed,
// so |n| <= 1 and the displacement is within +-amplitude. The same seed always gives the same result.
PatchGrid noiseAround(const PatchGrid& g, const Vec3& center, double radius, double amplitude, uint32_t seed);

}  // namespace iw8r
