// IW8 Radiant Suite - the loot points of a Warzone map (roadmap 13.5, docs/research/87 section 2.4 and 89).
//
// A stock Warzone map keeps the places where loot can appear in MapEnts.spawnGroupLoot, not in the entity text:
//   points    origin, angles, cluster (u16), zone (u16)
//   clusters  authored boxes that group points (a cluster index per point; 0xFFFD = a free point outside every
//             cluster, 0xFFFB = a loot cache point, 0xFFFF = none)
//   zones     authored named boxes (zone number 1.. per point, 0 = none)
// To edit them, the editor turns the points into point entities of class iw8r_loot_point (File > Edit Loot Points).
// On a rebuild they leave the entity text again and go to the converter as "loot_points" of the edits file
// (editzone), which rebuilds the point lists of the clusters and zones and the counts.
// Checked on mp_br_quarry, mp_donetsk, mp_br_tut and mp_backlot2: every clustered point is inside its cluster's box,
// every zoned point that is not a cache point is inside its zone's box, and "the first box that holds the point"
// gives the stock cluster and zone of every such point. So a point that was moved out of its box is written as
// "auto" and the converter finds its new cluster and zone the same way.
// Pure core code.
#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "geometry.h"
#include "map_text.h"

namespace iw8r {

inline constexpr uint16_t kLootFreePoint = 0xFFFD;   // outside every cluster
inline constexpr uint16_t kLootCachePoint = 0xFFFB;  // a loot cache (supply box) point
inline constexpr uint16_t kLootNoCluster = 0xFFFF;
inline constexpr const char* kLootPointClass = "iw8r_loot_point";
inline constexpr size_t kLootPointLimit = 65000;     // the converter's limit (u16 indices)

struct LootBox {
    Vec3 mid, half;
    bool known = false;  // false: the export did not give the box
    bool holds(const Vec3& p, double tolerance = 0.01) const;
};

struct LootPoint {
    Vec3 origin, angles;
    uint16_t cluster = kLootFreePoint;  // cluster index or one of the special values
    uint16_t zone = 0;                  // 0 = none, else the zone number (1-based)
};

struct StockLoot {
    std::vector<LootPoint> points;
    std::vector<LootBox> clusters;      // box of cluster i
    std::vector<LootBox> zones;         // box of zone i + 1
    std::vector<std::string> zoneNames; // name of zone i + 1 ("" when the script strings are not known)
};

// Reads spawnGroupLoot from an ACTS map_ents export (the file iw8_mapents.h reads). A map without loot gives an
// empty block and true. `scriptStringsJson` may be empty (zone names stay empty).
bool readStockLoot(const std::string& assetJson, const std::string& scriptStringsJson, StockLoot& out, std::string* err = nullptr);

bool isLootPointClass(const std::string& classname);

// Key/values of the document entity for one loot point: classname, origin, angles, loot_cluster ("free", "cache" or
// the cluster number), loot_zone (the zone number, 0 = none) and, when known, loot_zone_name (information only).
std::vector<std::pair<std::string, std::string>> lootPointEntityKeys(const LootPoint& point, const StockLoot* block = nullptr);

// One loot point of the document as the edits file takes it: `cluster` and `zone` are a number or "free" / "cache" /
// "auto" (cluster) and "auto" (zone).
struct LootPointEdit {
    Vec3 origin, angles;
    std::string cluster = "auto", zone = "auto";
    bool operator==(const LootPointEdit& o) const {
        return origin == o.origin && angles == o.angles && cluster == o.cluster && zone == o.zone;
    }
};

// The loot point entities of the document, in document order. Keys that are missing or not valid for `block`
// (a cluster or zone number the map does not have) become "auto"; so does the cluster of a point that left its
// cluster's box, and the zone of a point that left its zone's box (not for a cache point: stock cache points name a
// zone they are not inside). `notes` gets one line per kind of correction, with counts. Entities without an origin
// are left out (noted).
std::vector<LootPointEdit> documentLootPoints(const MapFile& map, const StockLoot& block, std::vector<std::string>* notes = nullptr);

// True when the document holds at least one loot point entity.
bool hasLootPoints(const MapFile& map);

// The document without its loot point entities (they never go into the entity text of a zone).
MapFile withoutLootPoints(const MapFile& map);

// The JSON list for the edits file: [{"origin":[x,y,z],"angles":[p,y,r],"cluster":...,"zone":...}, ...].
std::string lootPointsJson(const std::vector<LootPointEdit>& points);

}  // namespace iw8r
