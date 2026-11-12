// IW8 Radiant Suite - conversions between brushes and patches, and between patches and terrain.
//
// Brush faces become flat "mesh" patches. A mesh patch is triangulated straight from its control grid (no curve
// smoothing), so the triangles lie exactly on the face. Texture coordinates are the brush face's own UVs
// (faceUV of tex_projection.h) at every control point. faceUV is affine on the face plane and the patch's
// bilinear interpolation of the coordinates is affine too, so the patch shows the same texture as the brush.
//
// Terrain: the editor has no "terrain" keyword in the text. Create Terrain writes a "mesh" patch (makeTerrainPatch
// builds it with makeFlatPatch), so a terrain here is a "mesh" grid of control points.
#pragma once

#include <optional>
#include <vector>

#include "patch_ops.h"

namespace iw8r {

class Document;

// One flat "mesh" patch per visible face of a brush (faces with no contributing winding are skipped).
// skipToolFaces drops tool materials (isIw8ToolMaterial) and faces without a material.
// Every patch is subdiv x subdiv control points (subdiv < 2 is treated as 2).
// Non-quad faces: a triangle becomes one patch whose grid collapses its last row onto the third corner, so
// (subdiv - 1) of its triangles have zero area. A polygon with n >= 5 corners is fanned from its first corner into
// n - 2 such triangle patches. Every patch stays on the face plane, and the patches cover the face exactly once.
std::vector<PatchGrid> brushFacesToPatches(const Primitive& brush, bool skipToolFaces, int subdiv);

// Converts every selected brush (primitive refs with an item id) into patches in the same entity. One undo step.
// The brush is deleted unless keepBrush. A brush that yields no patch (every face skipped) is left alone.
// The new patches become the selection. False when nothing changed.
bool convertSelectedBrushesToPatches(Document& doc, bool keepBrush, bool skipToolFaces);

// Resamples a patch onto a cols x rows grid of control points (rows along the patch's rows, cols along its columns).
// The new grid is "mesh" and keeps the material. Positions and texture coordinates are sampled bilinearly from
// the source control net, so the result follows the net, not the smooth curve of a "curve" patch. For a flat patch
// the sample is exact and the bounds are kept; a bumpy patch can lose bounds it only reaches between samples.
// nullopt when cols or rows < 2 or the source grid is smaller than 2 x 2 or inconsistent.
std::optional<PatchGrid> patchToTerrain(const PatchGrid& g, int cols, int rows);

// Replaces every selected patch with its terrain resample (patchToTerrain). One undo step; the new patches are
// selected. False when nothing changed.
bool convertSelectedPatchesToTerrain(Document& doc, int cols, int rows);

}  // namespace iw8r
