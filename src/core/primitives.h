// IW8 Radiant Suite - blockout primitive generators (cone, sphere, stairs, arch, ramp).
//
// Every generator fills the box `box` and returns convex brushes built from half-space faces (see
// brush_ops.h). Each face carries the same Radiant face suffix `suffix` (a leading space is added when
// missing). Non-contributing faces are dropped, so isValidBrush() holds for each returned brush.
// Invalid arguments (degenerate box, out-of-range counts, bad axis or direction) return an empty result.
//
// Direction codes for upDir / upDir-style arguments: 0 = +x, 1 = -x, 2 = +y, 3 = -y.
// Coordinates follow the editor convention: Z up, units = inches.
#pragma once

#include <string>
#include <vector>

#include "brush_ops.h"
#include "geometry.h"

namespace iw8r {

// Cone with its base polygon inscribed in the box cross-section (the polygon's vertices touch all four
// sides of the box's rectangle in the two non-axis directions, so the bounds equal `box` for any sides).
// The base sits on box.mins[axis]; the apex is at the box center in the other two axes and at
// box.maxs[axis]. `sides` is 3..64. Result: sides lateral faces plus the base, so sides + 1 faces.
// Empty for bad arguments.
ConvexBrush makeCone(const AABB& box, int sides, int axis, const std::string& suffix);

// Faceted sphere (ellipsoid when the box is not cubic) as ONE convex brush: the intersection of support
// planes whose normals are a latitude/longitude grid plus the six axis directions. Each plane is tangent
// to the box's inscribed ellipsoid, so the brush circumscribes it and its bounds equal `box` exactly.
// `sides` (longitude segments) is 4..32. The vector always holds exactly one brush for valid input and is
// empty otherwise (no split into pieces is ever needed, since the faceted sphere is convex).
std::vector<ConvexBrush> makeSphere(const AABB& box, int sides, const std::string& suffix);

// Solid staircase: `steps` boxes, one per step, rising along the direction upDir (0..3, see above). Each
// step runs from the box floor (mins.z) up to its own top, so step i tops out at
// mins.z + (i + 1) * H / steps and the last (highest) step's top equals box.maxs.z. The run is split
// into `steps` equal slabs; neighbours share a face and do not overlap. Returned in ascending height
// order, so index 0 is the lowest step. `steps` is 1..256.
std::vector<ConvexBrush> makeStairs(const AABB& box, int steps, int upAxisDir, const std::string& suffix);

// Semicircular arch of `segments` convex blocks (segments >= 2). The arch is the upper half of the
// ellipse inscribed in the box's profile rectangle, so it is circular when that rectangle is square and
// elliptical otherwise. The profile spans the two axes other than `axis`; height is Z unless axis == 2,
// in which case height is Y. The arch extrudes along `axis` over the full box extent, so the opening
// runs along `axis`. `thickness` is the radial band width and must satisfy 0 < thickness < min(semi-axes).
// Each block is the convex quadrilateral between two radial cuts, extruded along `axis`. Blocks are
// returned from the left springing line to the right one.
std::vector<ConvexBrush> makeArchBlocks(const AABB& box, int segments, double thickness, int axis,
                                        const std::string& suffix);

// Wedge (triangular prism, 5 faces) rising along upDir: the low edge is on the box's min side, the high
// edge on the max side at full height. The floor is box.mins.z, the top edge is at box.maxs.z.
ConvexBrush makeRamp(const AABB& box, int upDir, const std::string& suffix);

}  // namespace iw8r
