// IW8 Radiant Suite - edits of a stock map zone for the converter's editzone command (roadmap 11.2 / 11.4 / 11.5,
// docs/research/88_stock_map_editing.md).
//
// The converter loads a stock zone, changes it in memory and writes it again:
//   zonetool -game iw8 -path <game folder> editzone <zone> <edits.json> <output folder>
// This file makes the edits file from the open stock document: the entity text (with the compiled spawns as entities
// when they were made editable), the models the document places that the zone does not hold (they are carried from
// another stock zone), and the stock props to hide.
// Pure core code.
#pragma once

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "map_text.h"
#include "stock_install.h"

namespace iw8r {

// Stock model names with the zones that hold them (data/asset_catalog/iw8_models.tsv: name, comma list of zones).
class StockModelZones {
public:
    bool load(const std::string& tsvPath, std::string* err = nullptr);
    bool parse(const std::string& text);
    // The zones of a model (case-insensitive); null when the catalogue does not know it.
    const std::vector<std::string>* zonesOf(const std::string& model) const;
    size_t size() const { return zones_.size(); }

private:
    std::map<std::string, std::vector<std::string>> zones_;
};

// True for a zone every multiplayer match has loaded (common*, global*, code*; not common_br_mp, which only battle
// royale loads).
bool isAlwaysLoadedZone(const std::string& zone);

struct StockEditCarry {
    std::string zone, type, name;
    // The folder that holds `zone` ("" = the game's zone folder): a zone the editor built, such as the brushes added
    // to a stock map (research 88).
    std::string folder;
    bool operator==(const StockEditCarry& o) const { return zone == o.zone && type == o.type && name == o.name; }
};

// The models that entities of the document place (key "model", not a brush model "*N") and that neither the edited
// zone nor an always-loaded zone holds: each is carried from the first other "mp_" zone that holds it. `notes` gets
// one line per model that no known zone holds (it would be missing in game). Sorted by name, each model once.
// `stockModels` (lower case, entityModelNames of the map as it was opened): models the stock map's own entities
// already name are never carried and never noted. A stock map names models it does not hold (objects of other game
// modes), and the catalogue does not list every map; only what the mapper added is the mapper's.
std::vector<StockEditCarry> stockEditCarryList(const MapFile& map, const std::string& zone, const StockModelZones& catalogue,
                                               std::vector<std::string>* notes = nullptr, const std::set<std::string>* stockModels = nullptr);

// The model names the entities of a document place (key "model", not a brush model "*N"), lower case.
std::set<std::string> entityModelNames(const MapFile& map);

// The hidden stock prop indices of the document: worldspawn key iw8r_hide_smodels, whole numbers separated by spaces
// (each once, ascending). Text that is not a number is skipped.
std::vector<uint32_t> stockEditHiddenProps(const MapFile& map);

// The edits file. `entitiesPath` is the entity text file ("" = the entities are not replaced). `lootPoints` is the
// JSON list of lootPointsJson (stock_loot.h); null = the loot points of the map stay as they are. `placedObjects` is
// PlacedObjectEdits::json (stock_placed.h); null or empty = the placed objects stay as they are.
// `stockPatches` (stock_install.h): the saved stock patches of the edits that are installed in the game; the
// converter then reads those zones as the stock game has them, not as the installed edit left them.
std::string stockEditJson(const std::string& entitiesPath, const std::vector<uint32_t>& hiddenProps,
                          const std::vector<StockEditCarry>& carry, const std::string* lootPoints = nullptr,
                          const std::string* placedObjects = nullptr, const std::vector<StockPatchOverride>* stockPatches = nullptr);

}  // namespace iw8r
