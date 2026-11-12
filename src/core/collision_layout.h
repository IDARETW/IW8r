// IW8 Radiant Suite - how a built map lays out its collision (roadmap 14.1, docs/research/90_collision_tiles.md).
//
// A small map keeps all collision in its main zone ("one piece", what every core map does). A big map cuts it into
// the game's fixed grid of 32 x 32 tiles of 8192 units around the origin: each tile is a small zone
// (<map>_cg_NNNNN_tr.ff) that a client loads when the player comes near, and one combined package
// (<map>_cg_ls_tr.ff) that a host loads whole. The converter makes the tiles by itself from the same collision
// (collision JSON "collision_tiles": true); this file only decides which layout a map gets.
// Pure core code.
#pragma once

#include <string>
#include <vector>

#include "geometry.h"

namespace iw8r {

enum class CollisionLayout { Auto, OnePiece, Tiles };

inline constexpr double kCollisionTileSize = 8192.0;
inline constexpr int kCollisionTileGrid = 32;
inline constexpr int kCollisionTileCount = kCollisionTileGrid * kCollisionTileGrid;
inline constexpr double kCollisionGridHalf = kCollisionTileSize * kCollisionTileGrid / 2;  // 131072
// Auto: a map wider than this on X or Y gets tiles (the widest stock map with one-piece collision is far smaller;
// Quarry and Verdansk, which use tiles, are far larger).
inline constexpr double kCollisionAutoTilesExtent = 20000.0;

// The worldspawn key iw8r_collision: "" / "auto", "one" (also "onepiece", "full", "single"), "tiles" (also "tiled",
// "streamed"). `known` is false for any other text (the result is then Auto).
CollisionLayout parseCollisionLayout(const std::string& text, bool* known = nullptr);
const char* collisionLayoutName(CollisionLayout layout);  // "auto", "one", "tiles"

struct CollisionLayoutChoice {
    bool tiles = false;
    std::vector<std::string> notes;  // lines for the build log
};
// `bounds` are the bounds of the map's world geometry. Tiles are refused (one piece, with a note) for a map without
// valid bounds. Geometry outside the grid stays in the always-loaded part; a note says so.
CollisionLayoutChoice chooseCollisionLayout(CollisionLayout wanted, const AABB& bounds);

// The tile index (gridY * 32 + gridX) of a point, or -1 outside the grid.
int collisionTileOf(double x, double y);

// The zone names of a tiled map's collision files, without ".ff": the 1,024 tiles in index order, then the package.
std::vector<std::string> collisionTileZoneNames(const std::string& mapId);
std::string collisionTileZoneName(const std::string& mapId, int tile);
std::string collisionPackageZoneName(const std::string& mapId);

// The ACTS loader test cannot check a streaming info that has transient costs: it reports one failed asset on the
// stock mp_br_quarry and mp_donetsk too, and on every map built with tiles. `journal` is the text of the test's
// assets.jsonl. True when it has at least one failed entry and every failed entry is a streaminginfo with that
// reason; then the zone counts as passed.
bool actsOnlyStreamingInfoFailed(const std::string& journal);

}  // namespace iw8r
