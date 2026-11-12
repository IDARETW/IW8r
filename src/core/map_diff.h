// IW8 Radiant Suite - entity-level change report between two maps (roadmap 11.7 / 2.28), exportable as a JSON patch.
//
// Entity identity. Entities of map A and map B are paired by the first rule that applies. Each pass only pairs
// entities that are still unpaired, and passes run over the whole map before the next one starts:
//   0. worldspawn pairs with worldspawn (first with first).
//   1. same classname + same targetname, when that pair occurs exactly once in A and exactly once in B.
//   2. same classname + origin equal exactly (greedy, in file order).
//   3. same classname + origin within 1 unit (greedy, nearest first).
//   4. brush entities: same classname + equal multiset of normalized brush texts.
// Brush text is normalized by collapsing every whitespace run to one space, so re-wrapped brushes compare equal.
// Unpaired entities of A are Removed, unpaired entities of B are Added.
//
// Consequences of the rule: a point entity without a unique targetname that moves more than 1 unit is reported as
// Removed + Added. A brush entity without a unique targetname whose brushes change is also Removed + Added, because
// its brushes are its identity.
//
// Change records. One record per kind per paired entity. Moved carries from/to (the origin). KeysChanged lists
// "key: old -> new" ("(absent)" for a missing side); the origin key is never listed there when both sides have a
// parsable origin, because Moved covers it. BrushesChanged counts brushes only in B (brushesAdded) or only in A
// (brushesRemoved). Added/Removed carry the origin in to/from (zero when there is none).
#pragma once

#include <string>
#include <vector>

#include "map_text.h"

namespace iw8r {

class Document;

struct EntityChange {
    enum Kind { Added, Removed, Moved, KeysChanged, BrushesChanged } kind = Added;
    std::string classname;
    std::string label;  // targetname, else model, else "entity N" (N = index in the map the record refers to)
    Vec3 from, to;
    std::vector<std::string> keyChanges;  // "key: old -> new"
    int brushesAdded = 0, brushesRemoved = 0;
};

// Name is diffMapEntities, not diffMaps: document.h already declares `MapDiff diffMaps(const MapFile&, const MapFile&)`
// with the same parameters, and the two cannot share a name in namespace iw8r.
std::vector<EntityChange> diffMapEntities(const MapFile& a, const MapFile& b);

// One line per change, "\n"-terminated, sorted by kind, then classname, then label. Example lines:
//   Added light light_01
//   Moved light light_01 from 0 0 0 to 64 0 0
//   Keys light light_01: intensity: 1 -> 2; color: (absent) -> 1 1 1
//   Brushes worldspawn entity 0: +2 -1
std::string formatMapDiff(const std::vector<EntityChange>& changes);

// JSON patch that turns A into B at entity level (format "iw8r-map-patch", version 1):
//   "changed": [{"match": {"keys": [[k,v],...], "brushes": [normalized,...]}, "origin": "x y z" (only when moved),
//                "set": [[k,v],...], "remove": [k,...], "brushesAdd": [raw brush text,...],
//                "brushesRemove": [normalized,...]}]
//   "removed": [{"keys": [[k,v],...], "brushes": [normalized,...]}]
//   "added":   [{"keys": [[k,v],...], "brushes": [raw brush text,...]}]
// "match" identifies an entity of A by its full key list in file order plus its sorted normalized brush list (worldspawn
// is identified by its keys only, and its brushes are never listed). "brushes" in "added" are raw text to insert.
std::string mapPatchJson(const MapFile& a, const MapFile& b);

// Applies a patch from mapPatchJson to `doc` as ONE undo step. Every target entity is resolved and every brush is parsed
// before anything changes, so a failing patch (error set in *err, false returned) leaves the document untouched.
// Removal goes through Document::select + deleteSelection; the selection the user had is restored afterwards, but a
// brush entity that was selected by one of its brushes comes back with all its brushes selected.
bool applyMapPatch(Document& doc, const std::string& json, std::string* err = nullptr);

}  // namespace iw8r
