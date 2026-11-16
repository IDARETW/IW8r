// IW8 Radiant - roads, paths and rivers along splines that conform to the terrain (roadmap 8.13).
//
// Terrain: the editor has no "terrain" keyword in the text. Create Terrain writes a "mesh" patch made by
// makeTerrainPatch (patch_ops2.h): a regular XY lattice with columns along +X, rows along +Y (row 0 = min Y),
// row-major, index = row * cols + col. So terrainHeightAt bilinearly interpolates the cell that holds (x, y).
// A grid that is not a regular lattice falls back to the triangles of the control grid (the same split as
// tessellatePatch: (a, b, c) and (b, d, c) per cell).
//
// Road strips: a "mesh" patch with one row per path point and crossSegments + 1 columns across the path. Every
// control point is dropped onto the terrain and lifted by `lift`. st: s = 0..1 across, t = distance along the
// road's centreline (measured on the dropped centre points, in 3D) / uvLength, so a road texture tiles along it.
//
// Carving: carveTerrain sets the terrain control points under the road to (road height - depth), where the road
// height is the z of the path points (interpolated along the nearest path segment). Points within width / 2 move
// fully, points in the next `falloff` units move by a smoothstep weight, and points farther out do not move.
//
// Every function returns a value for bad arguments (non-finite numbers, non-positive sizes, too few points) and
// never throws. Edits to the document go through Document::editPrimitives, so each one is one undo step.
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "geometry.h"
#include "patch_ops.h"

namespace iw8r {

class Document;

// Height of the terrain under (x, y): the first grid in `terrains` that covers the point wins, and its control
// grid is bilinearly interpolated (regular lattices) or triangle-interpolated (other grids). *hit (when non-null)
// says whether any grid covers the point. Returns 0 when nothing does.
double terrainHeightAt(const std::vector<PatchGrid>& terrains, double x, double y, bool* hit);

// Road strip along `path` (at least 2 points, in order). `width` is the full width across the path and must be > 0.
// `crossSegments` (1..64) is the number of spans across, so there are crossSegments + 1 columns. `lift` is added to
// every dropped point. `uvLength` (> 0) is the map units per texture repeat along the road. `material` must be a
// non-empty name without whitespace or quotes. A path point that no terrain covers keeps its own z (plus lift).
// nullopt for bad arguments or a path that has no direction at all (every point on one spot).
std::optional<PatchGrid> roadStrip(const std::vector<Vec3>& path, double width, int crossSegments,
                                   const std::vector<PatchGrid>& terrains, double lift, const std::string& material,
                                   double uvLength);

// Conform-terrain carving: changes the z of the control points of `terrains` near `path` (see the file comment).
// `falloff` >= 0 is the width of the smooth band beside the road, `depth` is how far below the road height the
// ground is set. Returns true when any control point moved. False for bad arguments (grids are not touched).
bool carveTerrain(std::vector<PatchGrid>& terrains, const std::vector<Vec3>& path, double width, double falloff,
                  double depth);

struct RoadOptions {
    int crossSegments = 4;
    double lift = 0.25;          // keeps the road just above the ground (avoids z-fighting)
    double uvLength = 256;       // map units per texture repeat along the road
    std::string material = "caulk";
    bool carve = false;          // also carve the terrain under the road (same undo step)
    double falloff = 32;         // smooth band beside the carved road
    double depth = 0;            // ground is carved to road height minus depth
};

// Adds a road built from the origins of the selected point entities (entity order, like the spline wall tool).
// The terrains are all "mesh" patches with a non-zero XY extent in the map, or only the selected ones when any
// patch is selected. The road patch is added to worldspawn; with opts.carve the terrain patches it changes are
// rewritten in the same undo step (movePatchVerts, so the rest of their text is kept). Returns false when fewer
// than 2 points are selected, there is no terrain, the width or options are bad, or nothing could be built.
bool addRoadFromSelection(Document& doc, double width, const RoadOptions& opts = RoadOptions{});

}  // namespace iw8r
