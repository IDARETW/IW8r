// IW8 Radiant Suite - the mount edges of a built map (research 125): where a player can rest a weapon on cover.
//
// The game finds a mount only through static edges of the map's edge list (bucket MOUNT); it has no trace fallback.
// Stock maps mark two kinds of edge, and this finds the same two on the map's solid brushes:
//   - wall corners: a (nearly) vertical edge where two walls meet at an outside corner;
//   - ledge tops: a (nearly) level edge where a top face meets a wall below it.
// The same on the convex pieces of a placed model's collision hull (a crate, a barrier, a car's bonnet).
// An edge counts only where it is open: nothing solid in front of either face, room to stand in front of a ledge's
// wall and room for the weapon above it. Parts of one line that several brushes give (a wall built from stacked or
// side-by-side brushes) are joined before the shortest length is asked for. The game itself checks at run time
// whether a player fits at an edge, so an edge too many costs nothing but data.
#pragma once

#include "geometry.h"
#include "map_edges.h"
#include <string>
#include <vector>

namespace iw8r {

// Worldspawn key: "0" builds a map without mount edges.
inline constexpr char kMountEdgesKey[] = "iw8r_mount_edges";

// A convex solid by its planes: dot(n, x) <= d, n outward.
struct MountSolid {
    std::vector<Plane> planes;
    AABB bounds;
    bool source = true;  // false: it only hides edges of others (a clip brush, a model's stand-in box)
    // A piece of a placed model's collision hull: a wall corner needs its two walls at least 60 degrees apart (45 for
    // a brush), so a barrel or a pipe built from facets gives no corners.
    bool model = false;
};

struct MountEdgeOptions {
    double minCorner = 32;   // the shortest wall corner, in units (stock: 14 and up, median 49 to 97)
    double minLedge = 16;    // the shortest ledge top
    double step = 8;         // an edge is tested every `step` units along its length
    size_t maxEdges = 60000; // more than this: the longest are kept
};

struct MountEdgeStats {
    size_t corners = 0, ledges = 0;
    size_t fromModels = 0;  // of those, the edges that come from placed models' hulls
    size_t hidden = 0;   // candidate edges with no open part
    size_t short_ = 0;   // open parts shorter than the minimum
    size_t dropped = 0;  // over maxEdges
};

// The mount edges of the solids (flag kEdgeFlagMount), ledge tops with the top face as the adjacent face.
std::vector<MapEdge> findMountEdges(const std::vector<MountSolid>& solids, const MountEdgeOptions& options = {}, MountEdgeStats* stats = nullptr);

// The edges as the collision file's "mount_edges" value: [[ax,ay,az,bx,by,bz,adjacentNormal,openAngle,flags],...].
std::string mountEdgesJson(const std::vector<MapEdge>& edges);

}  // namespace iw8r
