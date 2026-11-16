// IW8 Radiant - Radiant-style patch (curve/mesh) and brush-vertex editing operations.
//
// Grid convention (same as patch_tess.h): a PatchGrid is row-major, index = row * cols + col. In the .map text
// each "( )" group is one row and each `v` inside it is one column, so the dimension line "W H A B" gives
// rows = W (groups) and cols = H (vertices per group).
//
// Text layout written by makePatchPrimitive (matches samples/iw8/iw8_sample_room.map):
//   [comment]
//   {
//    <keyword>                 (" curve" or " mesh")
//    {
//     <material>
//     lightmap_gray
//     <rows> <cols> 16 8       (A and B are written as 16 and 8, the values the sample uses)
//     (
//     <TAB>v x y z t s t 0 0   (texture st, then lightmap 0 0; no colour)
//     )
//    }
//   }
//
// Facing: a grid cell (row i, col j) is split into triangles (a, b, c) and (b, d, c) with a = (i, j),
// b = (i, j+1), c = (i+1, j), d = (i+1, j+1), exactly as tessellatePatch does. The front normal is
// cross(col direction, row direction).
#pragma once

#include <array>
#include <optional>
#include <string>
#include <vector>

#include "geometry.h"
#include "map_text.h"

namespace iw8r {

struct PatchGrid {
    int rows = 0, cols = 0;
    std::vector<Vec3> pos;                    // control points, row-major
    std::vector<std::array<double, 2>> st;    // texture coordinates per control point (same size as pos)
    std::string material;
    std::string keyword = "curve";            // "curve" or "mesh"
};

// Reads a parsed Patch primitive into a grid. Texture coordinates come from the `t` pair of each vertex
// (a `c r g b a` colour block before it is skipped). nullopt when the primitive is not a patch or its
// vertices do not form a complete rows x cols grid.
std::optional<PatchGrid> patchGrid(const Primitive& p);

// Builds the primitive text (optional `comment` line verbatim, then the braces) and parses it back so the
// returned Primitive has its parsed views filled in. An empty material is written as "caulk". A grid with
// rows < 1, cols < 1 or a pos count that is not rows * cols gives a Primitive with no lines (kind Unknown).
Primitive makePatchPrimitive(const PatchGrid& g, const std::string& eol, const std::string& comment);

// Simple patch mesh ("mesh") in the plane normal to `axis` (0 x, 1 y, 2 z), placed at the box centre on that
// axis. Columns run along (axis + 1) % 3 and rows along (axis + 2) % 3, so the front normal is +axis.
// Requires rows >= 2 and cols >= 2 and a valid box, otherwise the result has rows == 0.
// st: s = (column fraction) * (size along the column axis) / 512, t likewise along the rows (512 = texture repeat).
PatchGrid makeFlatPatch(const AABB& box, int axis, int rows, int cols, const std::string& material);

// Radiant cylinder: "curve", 3 rows (z = min, mid, max) by 9 columns around Z. Columns 0,2,4,6,8 lie on the
// box ellipse and the odd columns are the corners of the box (the tangent intersections of a quadratic
// round), so every control point lies inside the box. st from naturalTexture().
PatchGrid makeCylinderPatch(const AABB& box, const std::string& material);

// Radiant bevel: "curve", 3 x 3 quarter round in XZ (floor at min z, wall at max x, corner (max x, min z) as
// the handle), extruded along Y in 3 rows. st from naturalTexture().
PatchGrid makeBevelPatch(const AABB& box, const std::string& material);

// Radiant endcap: "curve", 3 rows (z min, mid, max) by 5 columns over the half ring y >= centre. st from
// naturalTexture().
PatchGrid makeEndcapPatch(const AABB& box, const std::string& material);

// Flips the facing by reversing the column order of every row (st and positions move together).
PatchGrid invertPatch(const PatchGrid& g);

// Swaps rows and columns. This is a reflection, so the facing flips as well. Transposing twice is the identity.
PatchGrid transposePatch(const PatchGrid& g);

// Radiant "natural" texturing: s accumulates the edge lengths along each row from column 0, t accumulates them
// down each column from row 0, both divided by 512 (kTexRepeat, the map units per texture repeat). A flat
// patch made by makeFlatPatch gets the same values.
PatchGrid naturalTexture(const PatchGrid& g);

// Patch texture coordinates in the .map file are texture repeats x 1024 (measured: every patch of CoD4
// mp_backlot_geo.map changes by 1024 / repeat size per unit; T3 T4 are lightmap coordinates at 1 per 16 units).
// patchGrid converts to repeats and makePatchPrimitive writes the file units.
inline constexpr double kPatchStUnits = 1024.0;
// True when the grid's st look like an older editor's repeats (median change under 0.05 per unit) rather than file
// units; patchGrid then keeps them as they are.
bool patchStIsLegacy(const PatchGrid& g);
// Median |d st| / |d position| between neighbouring control points (0 when none).
double patchStRate(const PatchGrid& g);
// Rewrites every control point's texture pair from `st` (row-major like PatchGrid, in repeats), in file units. Other
// tokens are kept. False when the patch is not a complete grid or the sizes differ.
bool setPatchSt(Primitive& p, const std::vector<std::array<double, 2>>& st);
// Texture coordinates (repeats) of tessellatePrimitive(p, subdiv)'s points, from the control points' st with the same
// Bezier grid. Empty when the patch is not a grid or has no texture coordinates at all.
std::vector<std::array<double, 2>> tessellatePatchUVs(const Primitive& p, int subdiv);

// Inserts two control rows (or columns) around the existing row/column `at`: a midpoint between `at` and
// `at - 1`, and a midpoint between `at` and `at + 1`. So the count goes up by 2 and stays odd for curves. The
// new points keep the grid's shape. `at` must satisfy 1 <= at <= count - 2 and the count must be >= 3; otherwise
// g is returned unchanged. After the insert, the kept row/column sits at index at + 1.
PatchGrid insertRowCol(const PatchGrid& g, bool row, int at);

// Inverse of insertRowCol: removes the two rows (columns) on either side of `at`. Needs 1 <= at <= count - 2
// and count >= 5 (so at least 3 remain); otherwise g is returned unchanged. Undo an insert with
// removeRowCol(inserted, row, at + 1).
PatchGrid removeRowCol(const PatchGrid& g, bool row, int at);

// Thickens the patch by offsetting every control point along its control-grid normal by `amount` (same
// convention as the tessellator; a flat +Z patch moves to z = amount). The returned copy keeps the source's
// column order, so its facing matches the source. If `sides` is non-null it receives four 2-row "mesh" seam
// patches that join the source edges to the offset edges (row 0, last row, column 0, last column).
PatchGrid thickenPatch(const PatchGrid& g, double amount, std::vector<PatchGrid>* sides);

// Moves control points (index into the row-major grid, the order patchGrid returns) by delta. Only the xyz
// text of those `v` tokens is rewritten. Every other byte of the primitive, including the texture and colour
// fields of all vertices, is left as it was. delta == 0 leaves the text untouched. Returns false for a
// non-patch, an incomplete grid or an out-of-range index.
bool movePatchVerts(Primitive& p, const std::vector<size_t>& indices, const Vec3& delta);

// Unique corner points of a brush (empty when the primitive is not a brush).
std::vector<Vec3> brushVertices(const Primitive& brush);

// Moves the corner points `from` (each must be one of brushVertices(brush)) by delta. Every face is rebuilt
// from its moved corners, keeping its suffix (material and texture fields) text. nullopt when a `from` point
// is not a corner, a face would no longer be planar or flipped, a corner would lie outside the new planes, or the
// result does not rebuild to the same corner set. So a non-nullopt result is always a valid convex brush.
std::optional<Primitive> moveBrushVerts(const Primitive& brush, const std::vector<Vec3>& from, const Vec3& delta,
                                        const std::string& eol);

}  // namespace iw8r
