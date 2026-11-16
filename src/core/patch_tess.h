// IW8 Radiant - patch tessellation for the editor's 3D view and the compile pipeline.
//
// Control grid convention (matches the iwmap 4 reader): ctrl is row-major [rows][cols], index = row * cols + col.
// In the .map text, each "( )" group is one control row and each `v` inside it is one column, so the dimension
// line "W H A B" maps to rows = W and cols = H.
//
// "curve" patches are biquadratic Bezier surfaces. A rows x cols grid with odd dimensions >= 3 is a set of
// (rows-1)/2 x (cols-1)/2 sub-patches; neighbouring sub-patches share their edge control points. Every other
// keyword ("mesh", "terrain", ...), and any curve with invalid dimensions, is triangulated directly from the
// control grid with no smoothing.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "geometry.h"
#include "map_text.h"

namespace iw8r {

struct PatchMesh {
    int width = 0;   // points per grid row (control columns after tessellation)
    int height = 0;  // grid rows of points
    std::vector<Vec3> points;       // row-major: points[row * width + col], size == width * height
    std::vector<Vec3> normals;      // one per point, unit length (averaged face normals; (0,0,1) if degenerate)
    std::vector<uint32_t> indices;  // triangle list, every index < points.size()
};

// Tessellates a control grid. For "curve", `subdiv` is the number of segments per sub-patch per direction and
// is clamped to 1..32. Returns an empty mesh when ctrl.size() != rows * cols or rows/cols are < 1.
PatchMesh tessellatePatch(const std::vector<Vec3>& ctrl, int rows, int cols, const std::string& keyword, int subdiv);

// Tessellates a parsed Patch primitive using its verts' row (group index) and col (index within group).
// Returns an empty mesh when the primitive is not a patch or its vertices do not form a complete grid.
PatchMesh tessellatePrimitive(const Primitive& p, int subdiv);

}  // namespace iw8r
