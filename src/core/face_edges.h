// IW8 Radiant Suite - face edges: a geometric input for custom surface shaders (core custom_shader, plan M6): how far a
// pixel is from the nearest edge of its brush face, and what lies across that edge. Nothing here decides a look: a
// shader may draw outlines with it (data/shaders/toon.iw8shader.hlsl), wear, highlights, or ignore it. The data is
// only made for maps whose shaders read the edge inputs (customShaderUsesEdges).
//
// The game's lit pass has no scene depth or normals a shader could find edges in (research 114), so the edges come
// with the geometry: every brush face is cut into convex pieces in which "the distance to the nearest edge of this
// face" is exactly linear, and each vertex carries that distance. A pixel shader turns it into a distance in screen
// pixels with the position derivatives (kIw8rEdgeHelper), exact at corners and independent of the camera distance.
//
// A vertex carries three numbers (packEdge: four bytes, as the build's vertex colour and the viewport's edge attribute):
//   edge  distance to the nearest edge / kEdgeRange, 0 on the edge .. 1 at kEdgeRange units or more
//   fold  the angle between this face's normal and the normal of the face across that edge, 0 .. pi (a box edge pi / 2)
//   dir   the direction in the face that leads away from that edge, as an angle in edgeFaceBasis(normal)
// fold and dir are the same on all vertices of a triangle (they belong to one edge), so they arrive unblended.
//
// Which edges count: every edge of a drawn face, except where the surface carries on past it into a drawn face of
// another brush of the same set in the same plane (a seam: no edge) or in a plane bent away by less than
// kEdgeSoftAngle (a soft fold: kept with its small fold, so a shader can tell it from a hard edge; bent toward the
// viewer side it is no edge). Nor where the face itself is hidden beside the edge, inside or against another drawn
// brush of the set (overlapping brushes, the side faces between the slices of a wall). A brush touching only part of
// an edge splits it.
#pragma once

#include <cstdint>
#include <vector>

#include "geometry.h"

namespace iw8r {

inline constexpr double kEdgeRange = 4.0;       // world units the edge distance is kept over
inline constexpr double kEdgeSoftAngle = 0.35;  // radians (20 degrees): flatter folds between brushes are soft

// One brush for the edge search: planes[i] (dot(n, x) = d, n outward) with its winding (empty: no face), whether the
// face is drawn (not a tool material), and its set (brushes see only brushes of their own set: the static world is
// one set, each moving brush entity its own).
struct EdgeBrush {
    std::vector<Plane> planes;
    std::vector<Winding> wins;
    std::vector<uint8_t> drawn;
    AABB bounds;
    int set = 0;
};
// Fills bounds from the windings.
void finishEdgeBrush(EdgeBrush& b);

// For each brush, the brushes of its set whose bounds touch its own (sorted; without itself).
std::vector<std::vector<uint32_t>> edgeNeighbours(const std::vector<EdgeBrush>& brushes);

// A stretch of a face's boundary that is an edge. cutA / cutB: the stretch ends inside a polygon edge (a square end) instead of
// at a corner of the face.
struct EdgeSpan {
    Vec3 a, b;
    double fold = 0;  // radians
    bool cutA = false, cutB = false;
};
std::vector<EdgeSpan> faceEdgeSpans(const std::vector<EdgeBrush>& brushes, size_t self, size_t face,
                                  const std::vector<uint32_t>& neighbours);

struct EdgeVertex {
    Vec3 pos;
    float edge = 1;  // 0 .. 1
    float fold = 0;  // radians, 0 .. pi
    float dir = 0;   // radians, 0 .. 2 pi
};
struct EdgeFaceMesh {
    std::vector<EdgeVertex> verts;
    std::vector<uint32_t> idx;  // triangles, wound as the face's winding
};

// The convex pieces of one face for its spans, triangulated. Without spans: the winding as a fan, edge 1 everywhere.
EdgeFaceMesh edgeFaceMesh(const Winding& face, const Vec3& normal, const std::vector<EdgeSpan>& spans, double range = kEdgeRange);

// The faces of other brushes that are drawn instead of face `face` of brushes[self] where they lie on it: drawn faces
// of the same set in the same plane, facing the same way, that overlap it and win (the smaller face; of two equal ones
// the later brush). Without this both would be drawn in one plane and fight for every pixel.
std::vector<Winding> coplanarCovers(const std::vector<EdgeBrush>& brushes, size_t self, size_t face,
                                    const std::vector<uint32_t>& neighbours);

// Every face of brushes[self]: meshes[face] (empty for a face without a winding, or wholly under coplanar faces of
// other brushes: coplanarCovers). Vertices the pieces of one face put on an edge are added to the other pieces along
// it, across the brush's faces too, so no seam can open between them. withEdges false: no edge data (edge 1
// everywhere), only the faces themselves with the covered parts cut away.
// allNear (edgeNeighbours of `brushes`): when given, the vertices the touching brushes' own pieces put on this brush's
// edges are added here too, so the seam between two brushes is as tight as the edges inside one (both sides then hold
// the same vertices along it). The result then depends on the neighbours and on their neighbours.
std::vector<EdgeFaceMesh> edgeBrushMeshes(const std::vector<EdgeBrush>& brushes, size_t self,
                                        const std::vector<uint32_t>& neighbours, double range = kEdgeRange, bool withEdges = true,
                                        const std::vector<std::vector<uint32_t>>* allNear = nullptr);

// The in-face axes the direction angle is measured in: a = normalize(cross(z, n)) (x made perpendicular to n when n is
// within 2.5 degrees of +-z), b = cross(n, a). The shader computes the same (kIw8rEdgeHelper).
void edgeFaceBasis(const Vec3& n, Vec3& a, Vec3& b);

// R edge x 255, G fold / pi x 255, B dir / (2 pi) x 256 (wrapped), A 255; R in the low byte. 0xFFFFFFFF = no edge.
uint32_t packEdge(const EdgeVertex& v);

}  // namespace iw8r
