// IW8 Radiant Suite - editing the compiled spawns of a stock map (roadmap 6.1, docs/research/26).
//
// A stock MapEnts keeps its spawns in spawnList, not in the entity string. To edit them, the editor turns the spawn
// records into ordinary point entities of the document (Map > Edit Compiled Spawns). On export the document is split
// again: entities whose class is a spawnList class become the spawnList (iw8r iw8d-spawns --set), the rest is the entity
// string (iw8r iw8d-set-ents); tools/m2_rebuild_mapents.ps1 -Spawns rebuilds the zone with both.
#pragma once

#include <string>
#include <utility>
#include <vector>

#include "iw8_mapents.h"
#include "map_text.h"
#include "spawnlist.h"

namespace iw8r {

// Classes kept in spawnList rather than the entity string: mp_*spawn* (mp_tdm_spawn, mp_dom_spawn_axis_start, ...).
bool isSpawnListClass(const std::string& classname);

// Key/values of a document entity for one compiled spawn (named keys; canonical numbers are resolved by the document).
std::vector<std::pair<std::string, std::string>> spawnEntityKeys(const Iw8SpawnRecord& spawn);

// Splits a stock document: `entityString` is the map text without the spawn entities (unchanged entities keep their
// exact text), `spawns` the spawn entities in document order. Entities without an origin are reported in `err` and
// left out of `spawns`. Loot point entities (stock_loot.h) and `scriptable` entities (stock_placed.h) are in neither:
// they are written through the edits file.
void splitStockDocument(const MapFile& map, std::string& entityString, std::vector<SpawnRecord>& spawns,
                        std::string* err = nullptr);

}  // namespace iw8r
