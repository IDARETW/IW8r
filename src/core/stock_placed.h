// IW8 Radiant - the placed scripted objects of a stock map (roadmap 13.5, docs/research/89 section 6).
//
// A stock map keeps its scriptable instances (doors, signs, the Warzone buy stations) in MapEnts
// scriptableMapEnts.mapInstances, not in the entity text. Most of them also have dynamic entities that hold a second
// copy of the placement and name their instance by index. The converter's editzone "scriptable_instances" moves, copies
// and removes instances together with their dynamic entities (research 89 section 8: the entries, their spatial
// partitions and bucket chains, the counts and the GfxWorld client arrays are rebuilt; proof tools/
// dynent_copy_remove_test.py). The editor turns the instances of verified definitions into `scriptable` entities
// (File > Edit Placed Objects); on a rebuild a moved one is written as a move, a new one as a copy of an existing
// instance of the same definition, a deleted one as a removal. What the converter refuses depends on the instance (a
// streamed entry, an entry placed apart from its instance, a door moved out of its area, a removal on a map whose
// streamed zones name later instances): the rebuild stops with the reason. Removals happen only when the document holds
// placed objects at all (File > Edit Placed Objects was used), so a document without them never drops anything.
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

// True for a definition whose instances can be copied and removed on a stock map: every number is known (verified).
// The converter copies or drops the instance's dynamic entities (signs, fences, props, doors, physics entries).
bool editablePlacedDefinition(const ScriptableDefinition& def);
// True for a definition whose instances can be moved: every number is known. The instance's dynamic entities follow
// the move (research 89 section 8); a door moves only inside the area of the stock doors that share its spatial
// index, and the converter refuses a move out of it or an instance whose entity is placed apart.
bool movablePlacedDefinition(const ScriptableDefinition& def);

// Key/values of the document entity for one placed object: classname scriptable, scriptable <definition>, origin,
// angles, iw8r_stock_index <row>.
std::vector<std::pair<std::string, std::string>> placedObjectEntityKeys(const StockPlacedObject& object);

// True when the document holds at least one `scriptable` entity.
bool hasPlacedObjects(const MapFile& map);

struct PlacedObjectEdits {
    std::string json;  // {"move":[...],"add":[...],"remove":[...]} for the edits file; "" when nothing changed
    size_t moved = 0, added = 0, removed = 0;
    std::vector<std::string> notes;
};

// Compares the `scriptable` entities of the document with the stock list. An entity with the index of a stock
// instance of the same definition is that instance: a changed origin or angles is a move. Every other entity is a
// new object: a copy of the first stock instance of its definition, with the definition's numbers from the
// catalogue. A stock instance of an editable kind with no entity left is removed. Notes say what cannot be done: a
// definition that is unknown, whose numbers are not known, or that is not on the map yet.
PlacedObjectEdits placedObjectEdits(const MapFile& map, const std::vector<StockPlacedObject>& stock, const ScriptableCatalog& catalog);

}  // namespace iw8r
