#include "collision_layout.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <sstream>

namespace iw8r {

namespace {

std::string lowerTrim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    std::string out = s.substr(a, b - a);
    for (char& c : out) c = char(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

std::string whole(double v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.0f", v);
    return buf;
}

}  // namespace

CollisionLayout parseCollisionLayout(const std::string& text, bool* known) {
    const std::string t = lowerTrim(text);
    if (known) *known = true;
    if (t.empty() || t == "auto") return CollisionLayout::Auto;
    if (t == "one" || t == "onepiece" || t == "one piece" || t == "full" || t == "single") return CollisionLayout::OnePiece;
    if (t == "tiles" || t == "tiled" || t == "streamed") return CollisionLayout::Tiles;
    if (known) *known = false;
    return CollisionLayout::Auto;
}

const char* collisionLayoutName(CollisionLayout layout) {
    return layout == CollisionLayout::Tiles ? "tiles" : layout == CollisionLayout::OnePiece ? "one" : "auto";
}

int collisionTileOf(double x, double y) {
    if (!std::isfinite(x) || !std::isfinite(y)) return -1;
    const int gx = int(std::floor((x + kCollisionGridHalf) / kCollisionTileSize));
    const int gy = int(std::floor((y + kCollisionGridHalf) / kCollisionTileSize));
    if (gx < 0 || gy < 0 || gx >= kCollisionTileGrid || gy >= kCollisionTileGrid) return -1;
    return gy * kCollisionTileGrid + gx;
}

CollisionLayoutChoice chooseCollisionLayout(CollisionLayout wanted, const AABB& bounds) {
    CollisionLayoutChoice out;
    if (wanted == CollisionLayout::OnePiece) return out;
    if (!bounds.valid()) {
        if (wanted == CollisionLayout::Tiles) out.notes.push_back("collision tiles: the map has no world geometry; collision stays in one piece");
        return out;
    }
    const double sx = bounds.maxs.x - bounds.mins.x, sy = bounds.maxs.y - bounds.mins.y;
    if (wanted == CollisionLayout::Auto && sx <= kCollisionAutoTilesExtent && sy <= kCollisionAutoTilesExtent) return out;
    out.tiles = true;
    // The tiles the bounds touch (the build makes a zone for all 1,024; only these can hold collision).
    const int x0 = int(std::floor((std::max(bounds.mins.x, -kCollisionGridHalf) + kCollisionGridHalf) / kCollisionTileSize));
    const int x1 = int(std::floor((std::min(bounds.maxs.x, kCollisionGridHalf - 1) + kCollisionGridHalf) / kCollisionTileSize));
    const int y0 = int(std::floor((std::max(bounds.mins.y, -kCollisionGridHalf) + kCollisionGridHalf) / kCollisionTileSize));
    const int y1 = int(std::floor((std::min(bounds.maxs.y, kCollisionGridHalf - 1) + kCollisionGridHalf) / kCollisionTileSize));
    const int touched = x1 >= x0 && y1 >= y0 ? (x1 - x0 + 1) * (y1 - y0 + 1) : 0;
    out.notes.push_back(std::string("collision is built as streamed tiles (") + (wanted == CollisionLayout::Auto ? "automatic: the map is " : "asked for; the map is ") +
                        whole(sx) + " x " + whole(sy) + " units): up to " + std::to_string(touched) +
                        " of the 1,024 tiles of 8192 units hold collision; a client loads the tiles near the player, a host the combined "
                        "package. Not seen in game");
    if (bounds.mins.x < -kCollisionGridHalf || bounds.mins.y < -kCollisionGridHalf || bounds.maxs.x > kCollisionGridHalf ||
        bounds.maxs.y > kCollisionGridHalf)
        out.notes.push_back("collision tiles: part of the map lies outside the tile grid (-131072 .. 131072 on X and Y); collision there "
                            "stays always loaded");
    return out;
}

std::string collisionTileZoneName(const std::string& mapId, int tile) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%05d", tile);
    return mapId + "_cg_" + buf + "_tr";
}

std::string collisionPackageZoneName(const std::string& mapId) { return mapId + "_cg_ls_tr"; }

std::vector<std::string> collisionTileZoneNames(const std::string& mapId) {
    std::vector<std::string> out;
    out.reserve(size_t(kCollisionTileCount) + 1);
    for (int i = 0; i < kCollisionTileCount; ++i) out.push_back(collisionTileZoneName(mapId, i));
    out.push_back(collisionPackageZoneName(mapId));
    return out;
}

bool actsOnlyStreamingInfoFailed(const std::string& journal) {
    std::istringstream in(journal);
    size_t failed = 0;
    for (std::string line; std::getline(in, line);) {
        if (line.find("\"status\":\"failed\"") == std::string::npos) continue;
        ++failed;
        if (line.find("\"type\":\"streaminginfo\"") == std::string::npos) return false;
        if (line.find("Missing count context: TransientCosts") == std::string::npos) return false;
    }
    return failed > 0;
}

}  // namespace iw8r
