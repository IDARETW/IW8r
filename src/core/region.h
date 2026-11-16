// IW8 Radiant - map regions (Radiant Region: Set From Selection / Set From XY View / Off) and
// subset writing for File > Save Selected and File > Save Region.
//
// A region is an axis-aligned box. Primitives whose bounds do not overlap it, and point entities whose origin
// lies outside it, are "outside": the editor hides them in every view (SceneCache::hiddenByRegion). Save Region
// writes everything that is not outside, so a brush straddling the boundary is kept whole.
#pragma once

#include <functional>
#include <set>
#include <string>

#include "geometry.h"
#include "map_text.h"

namespace iw8r {

// Ids hidden by a region: primitive item ids whose bounds do not overlap `region`, and entity ids of point
// entities (no primitives, origin set) whose origin lies outside it. Unknown primitives and entities without an
// origin are never reported (the camera does not draw them either).
std::set<Id> itemsOutside(const MapFile& map, const AABB& region);

// Keep predicate for mapTextSubset. `item` is a primitive's item id; for a point entity (no primitives) it is
// kNoId and asks about the entity itself.
using KeepFn = std::function<bool(Id entity, Id item)>;

// iwmap 4 text of the subset of `map` that `keep` accepts. The header (layer declarations) and trailer are kept
// verbatim; entity prefixes are renumbered. Rules:
//  - worldspawn keeps all its key/value lines and each primitive with keep(entity, item).
//  - another entity with primitives keeps all its key/value lines and each primitive with keep(entity, item);
//    it is written only when at least one primitive is kept.
//  - a point entity (no primitives) is kept when keep(entity, kNoId) holds.
std::string mapTextSubset(const MapFile& map, const KeepFn& keep);

// Region text for File > Save Region: everything not outside `region` (see itemsOutside).
std::string mapTextInRegion(const MapFile& map, const AABB& region);

}  // namespace iw8r
