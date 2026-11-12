// IW8 Radiant Suite - convex collision for custom models (roadmap 4.20).
//
// Pipeline: an imported model's triangles -> convex pieces (one hull per connected component, smallest
// components merged into their nearest neighbour) -> plane sets (3D quickhull, nearly coplanar faces merged,
// optionally simplified to a plane budget) -> convex brushes placed with the model's origin, angles and scale.
// Planes are dot(n, x) <= d with unit OUTWARD normals (geometry.h convention).
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "geometry.h"
#include "map_text.h"
#include "model_import.h"

namespace iw8r {

// Convex hull of a point cloud as outward planes. Quickhull with an epsilon of 1e-7 times the cloud's diagonal;
// duplicate and interior points are ignored. Faces whose normals differ by at most mergeAngleDeg are merged into
// one plane whose offset is the largest dot(n, p) over all input points, so every input point stays inside.
// Empty when there are fewer than 4 points or the points are degenerate (all coplanar, collinear or coincident).
std::vector<Plane> convexHullPlanes(const std::vector<Vec3>& points, double mergeAngleDeg = 2.0);

// Reduces a plane set to at most maxPlanes planes (values below 4 are treated as 4). Planes are removed in
// rounds, smallest face area first (the face area of the solid, clipped from a large box, is the proxy for the
// volume a removal adds: a removed plane only grows the solid). A removal that would leave the solid unbounded
// is refused. Removing planes only grows the solid, so every point inside the input stays inside the result.
// Plane normals must be unit length.
std::vector<Plane> simplifyHull(const std::vector<Plane>& planes, size_t maxPlanes);

// Convex decomposition of a model in its own coordinates. Triangles are welded per mesh on a 0.01 grid, and each
// connected component (vertices linked by triangles) becomes a point set. Components are sorted by bounds volume;
// while there are more than maxPieces (at least 1), the smallest one is merged into the piece with the nearest
// bounds. Each piece gives one hull, simplified to maxPlanesPerPiece planes when that is 4 or more. Pieces that
// are degenerate (flat) have no hull and are left out.
std::vector<std::vector<Plane>> convexPieces(const ImportedModel& m, int maxPieces, size_t maxPlanesPerPiece);

// Brushes for convex pieces placed with a uniform scale, then rotated by anglesPitchYawRoll (degrees, the
// anglesToMatrix convention of prefab.h) and moved to origin: point -> origin + scale * R * point. Every face of a
// brush uses the tool material (for example "clip"; "clip" when empty). Invalid brushes are skipped.
// Returns nothing when scale is not positive.
std::vector<Primitive> hullBrushes(const std::vector<std::vector<Plane>>& pieces, const Vec3& origin,
                                   const Vec3& anglesPitchYawRoll, double scale, const std::string& material,
                                   const std::string& eol = "\r\n");

}  // namespace iw8r
