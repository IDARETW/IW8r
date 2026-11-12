// IW8 Radiant Suite - the placed scripted objects of a stock map (roadmap 13.5, docs/research/89 section 6).
//
// A stock map keeps its scriptable instances (doors, signs, the Warzone buy stations) in MapEnts
// scriptableMapEnts.mapInstances, not in the entity text. Most of them also have a dynamic entity that holds a second
// copy of the placement, and those entities name their instance by index, so the list cannot be reordered or shortened.
// What can be edited safely is an instance whose definition has no dynamic entity at all: the Warzone buy station
// (br_plunder_box; 120 on Quarry and on Verdansk) and a few others. The editor turns those into `scriptable` entities
// (File > Edit Placed Objects); on a rebuild a moved one is written as a move, a new one as a copy of an existing
// instance of the same definition (the converter's editzone "scriptable_instances"). Nothing is removed.
// Pure core code.
#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "geometry.h"
#include "map_text.h"
#include "scriptable_catalog.h"

namespace iw8r {

inline constexpr const char* kPlacedObjectClass = "scriptable";
inline constexpr const char* kPlacedObjectIndexKey = "iw8r_stock_index";

struct StockPlacedObject {
    uint32_t index = 0;       // its row in mapInstances
    std::string definition;   // the definition's name without the leading comma of a name reference
    Vec3 origin, angles;
};

// Reads every scriptable instance from an ACTS map_ents export (the file iw8_mapents.h reads). A map without
// instances gives an empty list and true.
bool readStockPlacedObjects(const std::string& assetJson, std::vector<StockPlacedObject>& out, std::string* err = nullptr);

// True for a definition whose instances can be moved and copied on a stock map: every number is known and an
// instance gets no dynamic entity.
bool editablePlacedDefinition(const ScriptableDefinition& def);

// Key/values of the document entity for one placed object: classname scriptable, scriptable <definition>, origin,
// angles, iw8r_stock_index <row>.
std::vector<std::pair<std::string, std::string>> placedObjectEntityKeys(const StockPlacedObject& object);

// True when the document holds at least one `scriptable` entity.
bool hasPlacedObjects(const MapFile& map);

struct PlacedObjectEdits {
    std::string json;  // {"move":[...],"add":[...]} for the edits file; "" when nothing changed
    size_t moved = 0, added = 0;
    std::vector<std::string> notes;
};

// Compares the `scriptable` entities of the document with the stock list. An entity with the index of a stock
// instance of the same definition is that instance: a changed origin or angles is a move. Every other entity is a
// new object: a copy of the first stock instance of its definition, with the definition's numbers from the
// catalogue. Notes say what cannot be done: a definition that is unknown, has dynamic entities or is not on the
// map yet, and stock objects that were deleted in the document (they stay).
PlacedObjectEdits placedObjectEdits(const MapFile& map, const std::vector<StockPlacedObject>& stock, const ScriptableCatalog& catalog);

}  // namespace iw8r
