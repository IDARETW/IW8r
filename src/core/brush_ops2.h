// IW8 Radiant Suite - further convex brush operations (roadmap 2.6, 2.8, 2.9) on top of brush_ops.h.
//
// Same model as brush_ops.h: a ConvexBrush is a list of outward half-space faces (dot(n, x) <= d) with
// their Radiant face suffixes. Functions return nullopt instead of building an invalid brush.
#pragma once

#include <optional>
#include <vector>

#include "brush_ops.h"
#include "geometry.h"

namespace iw8r {

// Radiant CSG Merge: the convex hull of the brushes' vertices when that hull equals their union (no gaps); else nullopt.
// Faces keep the suffix (material + texture fields) of the source face whose plane matches, else the first brush's first suffix.
// nullopt also for an empty list, an invalid input brush, or more than 128 distinct vertices.
std::optional<ConvexBrush> mergeBrushes(const std::vector<ConvexBrush>& brushes);

// 3-point clipper: plane through three points; `keepSide` point decides which half-space is kept (Radiant: the side facing the camera/view).
// The returned plane is in clipBrush() convention: the kept part is dot(n, x) <= d, and keepSide satisfies it strictly.
// nullopt for degenerate (collinear) points or a keepSide point lying on the plane.
std::optional<Plane> clipPlaneFrom3Points(const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& keepSide);

// Extrude (iw3xo "extrude to face"): a new brush built on one face of `b`, extending `distance` along the face normal (the face's winding swept).
// The new brush lies outside `b`, its underside is the face itself, and every face of the new brush carries the source face's suffix.
// nullopt for distance <= 0, a bad face index, a non-contributing face or an invalid brush.
std::optional<ConvexBrush> extrudeFace(const ConvexBrush& b, size_t face, double distance);

// Edge drag (Radiant E mode): unique edges of a brush, and moving one edge (both endpoints) by delta; nullopt if the result is not convex.
// Each face adjacent to the edge is re-planed through its moved vertices. nullopt when the moved face is no longer planar,
// flips (e.g. the edge is dragged through the opposite edge), collapses, or the result is not a valid convex brush.
struct BrushEdge {
    Vec3 a, b;
};
// One entry per unique edge (consecutive winding vertices, deduplicated). A box has 12.
std::vector<BrushEdge> brushEdges(const ConvexBrush& b);
// `edge` must be one of brushEdges(b) (either endpoint order). The face count and suffixes are kept 1:1.
std::optional<ConvexBrush> moveBrushEdge(const ConvexBrush& b, const BrushEdge& edge, const Vec3& delta);

}  // namespace iw8r
