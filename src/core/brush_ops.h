// IW8 Radiant - convex brush operations (CSG-style edits on half-space brushes).
//
// A ConvexBrush is a list of half-space faces: dot(n, x) <= d with OUTWARD normals, each carrying the
// Radiant face suffix (texture and lightmap parameters, verbatim, starting with whitespace). Faces that
// do not contribute to the solid (empty winding) are dropped by the operations that create new brushes.
// Transform operations keep the face list 1:1 with the input.
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "geometry.h"
#include "map_text.h"

namespace iw8r {

struct BrushFaceSpec {
    Plane plane;        // dot(n, x) == d, n outward
    std::string suffix;  // text after the third ')' of the face line, e.g. " caulk 64 64 0 0 0 0 ..."
};

using ConvexBrush = std::vector<BrushFaceSpec>;

// Planes and suffixes of a parsed brush (one entry per parsed face line; degenerate faces skipped).
ConvexBrush brushFromPrimitive(const Primitive& p);

// Builds a brush primitive: optional `comment` line (written verbatim), "{", one face line per
// contributing face (indent " "), "}". Every line carries `eol`. The three points of each face are
// chosen so that planeFromPoints() reproduces the outward normal. Returns nullopt if the brush is not
// valid or the generated text does not reproduce the planes.
std::optional<Primitive> makeBrushPrimitive(const ConvexBrush& b, const std::string& eol, const std::string& comment = "");

// True when the brush has at least 4 contributing faces, is bounded and has positive volume.
bool isValidBrush(const ConvexBrush& b);

// Bounds of the solid (invalid AABB when the brush has no contributing faces).
AABB brushBounds(const ConvexBrush& b);

// Helpers (also used by the tests).
// One winding per input face, oriented so its Newell normal agrees with the outward plane normal.
// Empty for faces that do not contribute.
std::vector<Winding> brushWindings(const ConvexBrush& b);
// One winding per Primitive::faces entry (same index; empty for a degenerate or non-contributing face).
std::vector<Winding> faceWindingsOf(const Primitive& p);
// Plane of one face from its three points (nullopt when degenerate).
std::optional<Plane> facePlane(const Primitive& p, size_t face);
// Volume by the divergence theorem over brushWindings(); 0 for degenerate or unbounded input.
double brushVolume(const ConvexBrush& b);

// Axis aligned box, 6 faces (+x, -x, +y, -y, +z, -z). `suffix` is the face suffix; a leading space is added
// when missing so the text stays a valid face suffix.
ConvexBrush makeBox(const AABB& box, const std::string& suffix);

// n-sided prism inscribed in the box's ellipse, extruded along `axis` (0/1/2), sides 3..64.
// Vertex 0 lies on the +u axis and the sides are evenly spaced. Returns an empty brush for bad arguments.
ConvexBrush makePrism(const AABB& box, int sides, int axis, const std::string& suffix);

// Keeps the part with dot(n, x) <= d. The new cut face uses `capSuffix`. nullopt when nothing is left.
std::optional<ConvexBrush> clipBrush(const ConvexBrush& b, const Plane& keep, const std::string& capSuffix);

// a minus cutter as disjoint convex pieces (split a by each cutter plane in turn).
// {a} when there is no overlap, {} when a lies entirely inside the cutter.
std::vector<ConvexBrush> subtractBrush(const ConvexBrush& a, const ConvexBrush& cutter);

// Hollows the brush into walls of `thickness` built inward from each face, one piece per face. Each
// piece is clipped by the walls of the earlier faces, so the pieces do not overlap and their union is
// b minus the inner brush. {} for thickness <= 0 or an invalid brush.
std::vector<ConvexBrush> hollowBrush(const ConvexBrush& b, double thickness);

ConvexBrush translateBrush(const ConvexBrush& b, const Vec3& d);
// Rotates by `degrees` (right-handed) about axis 0/1/2 around `pivot`. Quarter turns are exact.
ConvexBrush rotateBrush(const ConvexBrush& b, int axis, double degrees, const Vec3& pivot);
// The point transform rotateBrush applies (same sign convention); used by texture lock.
Vec3 rotatePoint(const Vec3& p, int axis, double degrees, const Vec3& pivot);
// Mirrors across the plane x_axis == about. Normals stay outward.
ConvexBrush flipBrush(const ConvexBrush& b, int axis, double about);
// Scales about `pivot`; all scale components must be positive (returns {} otherwise).
ConvexBrush scaleBrush(const ConvexBrush& b, const Vec3& scale, const Vec3& pivot);
// Pushes face `face` along its outward normal by `distance` (positive grows the brush). The face index
// is kept. nullopt if the face vanishes or the brush becomes invalid.
std::optional<ConvexBrush> moveFace(const ConvexBrush& b, size_t face, double distance);

}  // namespace iw8r
