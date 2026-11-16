// IW8 Radiant - stock prefab harvester: a prefab library built from the static-model placements of the shipped
// IW8 maps (the world_cache exports read by stock_smodels.h). Only IW8 content is used.
//
//   Anchor   an instance whose model matches a category keyword (stockPrefabRules, the category table).
//   Cluster  the anchor plus every ok instance within the category radius whose model is attached-looking: the same
//            model family prefix as the anchor, or a frame / trim / handle / glass model, or a model of one of the
//            categories listed as extra for the anchor's category. Floor clutter is left out.
//   Relative the cluster is expressed with the anchor origin at (0,0,0) and the anchor yaw removed from every member,
//            so the anchor faces +X; pitch and roll of the members stay relative (stockClusterRelative).
//   Dedup    clusters with the same multiset of models and placements within 1 inch / 1 degree are one prefab; the
//            count and the source zones are kept.
//   Singles  one misc_model at the origin for every ok model used in a category.
//
// Output (writeStockPrefabLibrary): iwmap 4 .map files under <out>/<category>/<name>.map, index.tsv and README.md.
// Read-only on the export and the status table: nothing is written outside the output folder.
#pragma once

#include <cstddef>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "stock_smodels.h"

namespace iw8r {

// One row of the category table. keywords: space-separated; a keyword matches a letter token of the model's last path
// segment when the token equals it, equals it plus "s", or (keywords of 5+ letters) starts with it. radius: inches an
// attached instance may lie from the anchor (0 = no clusters, singles only). extra: space-separated categories whose
// models count as attached to this category on top of frame, trim, handle and glass.
struct StockPrefabRule {
    const char* category;
    const char* keywords;
    double radius;
    const char* extra;
};

// The category table in priority order: the primary category of a model is the first rule that matches.
const std::vector<StockPrefabRule>& stockPrefabRules();

// Letter tokens (lower case) of the last path segment: "tm2co/me_door_metal_01" -> {me, door, metal}.
std::vector<std::string> stockModelTokens(const std::string& model);
// Primary category ("" when no rule matches) and every matching category in table order.
std::string stockPrefabCategory(const std::string& model);
std::vector<std::string> stockPrefabCategories(const std::string& model);
// Family key: the first two letter tokens of the last segment joined by '_' ("me_door_metal_01" -> "me_door").
std::string stockModelFamily(const std::string& model);
// File-name stem of a model: lower case, every character outside [a-z0-9] becomes '_'.
std::string stockPrefabSafeName(const std::string& model);

// Members of a cluster relative to instances[anchor]: origin minus the anchor origin, rotated about Z by minus the
// anchor yaw (stockModelInstancesAsEntities / axisToAngles convention), axes rotated the same way, scale kept.
// Sorted by model, then origin. members must contain anchor.
std::vector<StockModelInstance> stockClusterRelative(const std::vector<StockModelInstance>& instances, size_t anchor,
                                                     const std::vector<size_t>& members);
// True when both lists hold the same models (as a multiset) with placements within 1 inch (origin, per component),
// 1 degree (pitch, yaw, roll, wrapped) and 0.01 (scale).
bool sameStockPrefab(const std::vector<StockModelInstance>& a, const std::vector<StockModelInstance>& b);
// iwmap 4 text (CRLF, the layout of tests/data/iw8_sample_room.map): an empty worldspawn and one misc_model per member.
std::string stockPrefabMapText(const std::vector<StockModelInstance>& members);

struct StockZone {
    std::string name;                          // world_cache folder name, e.g. mp_shipment
    std::vector<StockModelInstance> instances; // readStockModelInstances order
};

struct StockPrefabOptions {
    size_t minCount = 1;          // a prefab is kept when it was seen at least this often
    size_t maxPerCategory = 400;  // cap on the prefabs (singles and clusters together) written per category
    std::set<std::string> okModels;  // models marked ok in iw8_models_status.tsv; an empty set means no filter
};

struct StockPrefab {
    std::string kind;          // "single" or "cluster"
    std::string category;      // folder
    std::string name;          // file stem
    std::string relPath;       // category/name.map
    std::string anchorModel;
    std::vector<StockModelInstance> members;  // relative placements (singles: one model at the origin)
    size_t count = 0;                         // instances seen in the zones
    std::map<std::string, size_t> zones;      // zone -> instances seen
};

struct StockPrefabLibrary {
    StockPrefabOptions options;
    std::vector<StockPrefab> prefabs;                       // kept prefabs, sorted by category and name
    std::map<std::string, size_t> zoneInstances;            // zone -> instances read
    std::map<std::string, size_t> candidatesPerCategory;    // before min-count and the cap
    std::map<std::string, size_t> droppedPerCategory;       // removed by the cap
    size_t instancesSeen = 0;
    size_t instancesSkipped = 0;        // model not ok
    size_t instancesUncategorised = 0;  // ok model, no category
};

StockPrefabLibrary harvestStockPrefabs(const std::vector<StockZone>& zones, const StockPrefabOptions& opt);

// Reads every folder of cacheDir that holds assets/gfx_map (sorted by name). Folders without one, or whose export does
// not read, are reported in warnings and skipped. Fails only when cacheDir is not a folder.
bool readStockWorldCache(const std::string& cacheDir, std::vector<StockZone>& out, std::vector<std::string>* warnings,
                         std::string* err = nullptr);
// Models whose status column is "ok" in iw8_models_status.tsv (name, TAB, status, TAB, reason).
bool loadStockModelStatus(const std::string& tsvPath, std::set<std::string>& ok, std::string* err = nullptr);

struct StockWriteStats {
    size_t files = 0;
    size_t bytes = 0;
    std::map<std::string, size_t> bytesPerCategory;
};

// Writes the library into outDir (created when missing). When outDir already holds an index.tsv from an earlier
// harvest, the .map files of its category folders are removed first. README.md lists how it was built and the counts.
bool writeStockPrefabLibrary(const std::string& outDir, const StockPrefabLibrary& lib, std::string* err = nullptr,
                             StockWriteStats* stats = nullptr);

}  // namespace iw8r
