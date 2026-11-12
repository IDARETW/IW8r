// IW8 Radiant Suite - bevel and inset operations on convex brushes (roadmap 2.23).
//
// Same model as brush_ops.h: a ConvexBrush is a list of outward half-space faces with their Radiant suffixes.
// Functions return nullopt (or an empty list) instead of building an invalid brush.
#pragma once

#include <cstddef>
#include <optional>
#include <vector>

#include "brush_ops.h"
#include "brush_ops2.h"  // BrushEdge, brushEdges()
#include "document.h"

namespace iw8r {

// Chamfer one edge. The cut plane has the outward normal normalize(n1 + n2) of the two faces adjacent to the edge,
// and it passes through the points that lie `size` from the edge inside each adjacent face, measured perpendicular
// to the edge. For a 90 degree edge the removed triangle has legs `size`. The new face takes the suffix of the
// adjacent face with the lower index in the brush. nullopt when `edge` is not an edge of `b`, size <= 0, or the cut
// removes a face entirely or leaves an invalid brush (for a 64 cube: size >= 64).
std::optional<ConvexBrush> chamferEdge(const ConvexBrush& b, const BrushEdge& edge, double size);

// Chamfers every edge at once: all cut planes are computed from `b` and intersected, so each edge gets the same
// cut as chamferEdge and the corners are shared. Adds one face per edge (a cube gives 18 faces). nullopt when any
// single cut fails or a face vanishes (for a 64 cube: size >= 32).
std::optional<ConvexBrush> chamferAllEdges(const ConvexBrush& b, double size);

// Inset then recess of one face. Returns, for a face with an inset outline Q (each edge moved `inset` inward,
// mitred at the corners):
//   [0]    the brush with that face moved `depth` inward (the recess floor is the new face),
//   [1..n] one piece per face edge: the part of the brush between the edge's mitre planes, inside Q's outline and
//          inside the top `depth` slab. The pieces together with [0] make the brush minus the recessed centre.
// A box gives 1 + 4 brushes. Limits: depth > 0 and inset > 0 (recess only); the inset outline must keep the face's
// winding (nullopt-like empty result otherwise, e.g. inset larger than the face); the face must contribute.
// The outer silhouette is the brush's own walls, so sloped walls are kept. Pieces that come out invalid are dropped.
std::vector<ConvexBrush> insetFace(const ConvexBrush& b, size_t faceIndex, double inset, double depth);

// Bridge two faces: the convex brush that spans two brush faces, the convex hull of the corners of both faces (when
// the faces look at each other they are its two caps, as a connecting tunnel or beam). Every face of the new brush
// takes the suffix (material, texture scale and lightmap settings) of the first face. nullopt when either face does
// not contribute or the corners are flat (the faces lie in one plane).
std::optional<ConvexBrush> bridgeFaces(const Primitive& a, size_t faceA, const Primitive& b, size_t faceB);

// The two selected brush faces bridged into a new brush of the first face's entity, as ONE undo step (the new brush is
// selected). False when the face selection is not exactly two brush faces or the bridge is flat.
bool bridgeSelectedFaces(Document& doc);

// Chamfers all edges of every selected brush as ONE undo step. Brushes whose chamfer fails are left alone. Returns the
// number of brushes changed (0 changes nothing).
size_t chamferSelection(Document& doc, double size);

// Insets and recesses one brush face (FaceRef::face indexes Primitive::faces) as ONE undo step. The first brush of
// the result replaces the original primitive and the rest are added to the same entity. Returns false when nothing
// changed.
bool insetSelectedFace(Document& doc, const FaceRef& face, double inset, double depth);

}  // namespace iw8r
