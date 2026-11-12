// IW8 Radiant Suite - spline tools (roadmap 2.26): a centripetal Catmull-Rom path through control points, and
// walls, fence posts and pipes built along it as convex brushes.
//
// Coordinates follow the editor convention: Z up, units = inches. Every brush is a ConvexBrush (see brush_ops.h)
// turned into a brush Primitive with makeBrushPrimitive(), so each returned Primitive passes isValidBrush().
// Faces use the texture fields "64 64 0 0 0 0" with the material as the first token. An empty result means bad
// arguments: non-finite points, non-positive sizes, too few points, or a material name that is empty or holds
// whitespace or a quote.
//
// Corners (walls and pipes): one convex brush per polyline segment. At an interior vertex the two neighbouring
// segments are both cut by one mitre plane through the vertex. Its normal is the bisector of the two directions
// (horizontal directions for walls, 3D directions for pipes). The outside corner is therefore filled, and the
// segments meet without a gap or an overlap. Opposite directions (a reversal) fall back to flat caps. A
// vertical-only step has no horizontal direction, so a wall gets no brush for it and its neighbours use flat caps.
//
// Walls keep the bottom at the segment's z. The bottom plane contains the segment, so a sloped run gets a sloped
// bottom. The top sits `height` above it in Z.
#pragma once

#include <string>
#include <vector>

#include "geometry.h"
#include "map_text.h"

namespace iw8r {

// Centripetal (alpha = 0.5) Catmull-Rom curve through the control points, resampled so each control span gets
// ceil(spanLength / segmentLength) pieces. Piece lengths are therefore at most segmentLength and close to it.
// Every control point is in the output exactly (open curves also keep the last one). Open curves need >= 2
// points, closed loops need >= 3. Returns the samples in order; for a closed loop the first point is not repeated.
// Empty for bad arguments.
std::vector<Vec3> sampleSpline(const std::vector<Vec3>& controlPoints, double segmentLength, bool closed);

// Wall of `thickness` (centred on the polyline) and `height`, one brush per segment of `pts` (a polyline, usually
// from sampleSpline). Duplicate points are dropped. A closed ring also joins the last point back to the first.
// Empty for bad arguments.
std::vector<Primitive> wallAlongSpline(const std::vector<Vec3>& pts, double thickness, double height,
                                       const std::string& material, bool closed);

// Square posts of side `size` and height `height`, one every `spacing` of arc length along the polyline, starting at
// the first point and ending at the last one when the length is a multiple of spacing. The count is
// floor(length / spacing) + 1. Each post's bottom is at its point's z. Empty for bad arguments or more than 100000
// posts.
std::vector<Primitive> postsAlongSpline(const std::vector<Vec3>& pts, double spacing, double size, double height,
                                        const std::string& material);

// Pipe of `radius` (the vertex circle radius) with `sides` (4..16) flat sides, one prism per segment, joined with
// the same mitre caps as walls. Each brush has sides + 2 faces. Empty for bad arguments.
std::vector<Primitive> pipeAlongSpline(const std::vector<Vec3>& pts, double radius, int sides,
                                       const std::string& material);

}  // namespace iw8r
