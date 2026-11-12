// UV layout for brush faces (roadmap 2.27, UV editor): unfolding a brush into a flat net (a cube becomes a cross) and
// fitting a face's texture projection to target UVs at its vertices.
//
// Format limit: an IW .map brush face stores one projection (TexParams: scale, offset, rotation), not per-vertex UVs.
// A face's UVs are therefore always a rotation + per-axis scale (negative = mirrored) + offset of its dominant-axis
// projection; fitTexParams returns the closest such mapping to arbitrary targets (least squares).
#pragma once

#include <array>
#include <cstddef>
#include <map>
#include <vector>

#include "geometry.h"
#include "map_text.h"
#include "tex_projection.h"

namespace iw8r {

using Uv = std::array<double, 2>;

// The TexParams whose faceUV at `points` is closest to `targets` (same order, at least 2 points). Rotation and the two
// scales come from a least-squares affine fit (scale signs keep a mirrored layout); offsets are refit afterwards.
// Returns `fallback` when the points are degenerate.
TexParams fitTexParams(const Vec3& normal, const std::vector<Vec3>& points, const std::vector<Uv>& targets,
                       const TexParams& fallback);

// Unfolds the brush's faces into one plane, starting from `root` and crossing shared edges breadth first (each face
// keeps its true shape and size, in world units). Result: face index -> its winding's 2D points (same vertex order as
// faceWindingsOf). Faces not reachable through shared edges are left out. The root's first edge lies along +x.
std::map<size_t, std::vector<Uv>> unfoldBrush(const Primitive& p, size_t root);

// Unwrap: every face's TexParams so the texture runs continuously across the unfolded net (a cube as a cross),
// at the root face's current texel size, with the root's current mapping kept. Faces outside the net keep theirs.
std::map<size_t, TexParams> unwrapBrush(const Primitive& p, size_t root);

}  // namespace iw8r
