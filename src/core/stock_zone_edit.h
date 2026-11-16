// IW8 Radiant - edits of a stock map zone for the converter's editzone command (roadmap 11.2 / 11.4 / 11.5,
// docs/research/88_stock_map_editing.md).
//
// The converter loads a stock zone, changes it in memory and writes it again:
//   zonetool -game iw8 -path <game folder> editzone <zone> <edits.json> <output folder>
// This file makes the edits file from the open stock document: the entity text (with the compiled spawns as entities
// when they were made editable), the models the document places that the zone does not hold (they are carried from
// another stock zone), and the stock props to hide.
// Pure core code.
#pragma once

#include <functional>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "geometry.h"
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
// A hidden stock prop whose collision is taken out too (editzone "remove_static_model_collision"): its model and
// place, as the world's static model table has them.
struct StockPropCollision {
    std::string model;
    double origin[3] = {0, 0, 0};
};
// `removeCollision`: the hidden props whose collision goes too (a small map keeps it in its collision map; a big map in
// its collision tiles, which this does not change).
// `stockPatches` (stock_install.h): the saved stock patches of the edits that are installed in the game; the
// converter then reads those zones as the stock game has them, not as the installed edit left them.
// `stockZones`: the same for edits installed as whole zones (editzone "stock_zones": zone file name -> the kept stock
// file). `patchFiles` false: the converter writes no <zone>.fp ("patch": false); the game does not load them on its
// signed zones (research 116).
std::string stockEditJson(const std::string& entitiesPath, const std::vector<uint32_t>& hiddenProps,
                          const std::vector<StockEditCarry>& carry, const std::string* lootPoints = nullptr,
                          const std::string* placedObjects = nullptr, const std::vector<StockPatchOverride>* stockPatches = nullptr,
                          const std::vector<StockPropCollision>* removeCollision = nullptr, const std::string* lootBoxes = nullptr,
                          const std::string* sun = nullptr, const std::string* replaceMaterials = nullptr,
                          const std::string* removeWorld = nullptr, const std::string* litFilter = nullptr,
                          const std::vector<StockPatchOverride>* stockZones = nullptr, bool patchFiles = true);
// The "lit_filter" value for stockEditJson: the compiled filter program (customShaderFilterProgramSource, core
// custom_shader) and the shared zones a lit shader's program may live in when the map's techset zone only names it.
// `screenProgramPath`: the compiled iw8rFilterScreen program ("" = none), used for every lit shader that has the
// screen position and a slot for the scene depth ("screen_program"; the converter re-points that slot).
std::string stockLitFilterJson(const std::string& programPath, const std::string& screenProgramPath = {});

// The first-person look (research 128): the weapon and hands a player holds keep their materials and technique sets in
// a zone every map loads. The filter goes only into the sets of that zone's materials whose name matches the pattern
// (a regular expression, searched without case), and no set or shader changes its name, because other zones use them
// by name. While the result is installed, the look is on every map.
inline constexpr char kViewmodelLookZone[] = "global_stream_mp";
inline constexpr char kViewmodelLookMaterials[] = "(_vm_|/vm_|_vm$|_arm_[lr]_)";
// The edits file of such a look for the converter's editzone (only the techset zone is written, no patch file).
// `stockZones` as for stockEditJson: the kept stock files of the edits that are installed in the game.
std::string stockSharedLookEditJson(const std::string& programPath, const std::string& screenProgramPath,
                                    const std::string& materials = kViewmodelLookMaterials,
                                    const std::vector<StockPatchOverride>* stockZones = nullptr);

// 11.5: stock world geometry taken out (docs/research/96_stock_world_removal.md). An iw8r_remove_world brush entity's
// brushes (and patches) each give a world-space box; on File > Rebuild Stock Zone the converter (editzone
// "remove_world_surfaces") stops drawing every world triangle whose three vertices lie inside a box, and takes the
// collision triangles and brush hulls inside it out of the world collision. Keys of the entity: "render" 0 keeps the
// triangles (only the collision goes), "collision" 0 keeps the collision. The entity never goes into the entity text.
// Static models are not world geometry: hide them with Hide Stock Props In Selection.
inline constexpr const char* kRemoveWorldClass = "iw8r_remove_world";
bool isRemoveWorldClass(const std::string& classname);

struct WorldRemovalBox {
    Vec3 mins, maxs;
    bool render = true, collision = true;
    bool operator==(const WorldRemovalBox& o) const {
        return mins == o.mins && maxs == o.maxs && render == o.render && collision == o.collision;
    }
};

// The boxes of the document's iw8r_remove_world entities, one per brush or patch, in document order. An entity
// without brushes, a flat box and an entity with "render" 0 and "collision" 0 are left out (noted).
std::vector<WorldRemovalBox> documentWorldRemovals(const MapFile& map, std::vector<std::string>* notes = nullptr);

// Whether the removal bakes the stock sun shadow again without the removed triangles: worldspawn
// iw8r_remove_world_sun "rebake" (the default) or "keep" (the stock shadow stays, with the removed geometry's shadow).
bool stockWorldRemovalRebakesSun(const MapFile& map);

// The edits file's "remove_world_surfaces" object ({"boxes":[{"mins":[..],"maxs":[..]}, ...], "rebake_sun_shadow":
// true, "acts": ...}); "" when there is no box. `acts` (acts.exe) is written with the re-bake: the converter exports
// the static models' streamed geometry with it.
std::string stockWorldRemovalJson(const std::vector<WorldRemovalBox>& boxes, bool rebakeSunShadow, const std::string& acts = {});

// 11.6: another stock map's sky for this stock map (worldspawn iw8r_sky_from "<map id>", e.g. mp_backlot2): the carry
// entry for its sky material (tw/sky_<map id> from techsets_<map id>) and the edits file's "replace_materials" list
// ([{"from":"@sky","to":"tw/sky_<map id>"}]). False (nothing) when the key is unset, not a map id, or this map's own.
bool stockSkyEdit(const MapFile& map, const std::string& thisZone, StockEditCarry& carry, std::string& replaceMaterials,
                  std::vector<std::string>* notes = nullptr);

// Retexturing a stock map (worldspawn iw8r_stock_retexture "from=to; from=to"): every world surface of material `from`
// gets material `to`. `zoneOf` names the stock zone the catalogue lists for a material ("" when unknown); a material
// of a map zone is carried from its techsets companion (techsets_<zone>), one listed in a techsets zone from that zone.
// Appends the carry entries and the replace_materials entries (JSON objects, no brackets) and returns how many pairs
// were taken; a pair whose material is unknown or held only by an always-loaded zone is left out with a note.
size_t stockRetextureEdits(const MapFile& map, const std::function<std::string(const std::string&)>& zoneOf,
                           std::vector<StockEditCarry>& carry, std::vector<std::string>& replaceEntries,
                           std::vector<std::string>* notes = nullptr);

// 11.6: the stock sun the document asks for (worldspawn iw8r_sun_color "r g b", linear 0..16, iw8r_sun_intensity
// 0..1000000 and iw8r_sun_direction "pitch yaw" in degrees, the sundirection convention: toward the sun
// (cos p cos y, cos p sin y, -sin p), a negative pitch above the horizon, -90 to -1) as the edits file's "sun" object;
// "" when no key is set. A bad value is left out with a note. A new direction makes the converter bake the stock
// zone's sun shadow again, which needs ACTS for the static models' streamed geometry: `acts` (acts.exe) is written
// as "acts" beside the direction when it is not empty.
std::string stockSunJson(const MapFile& map, std::vector<std::string>* notes = nullptr, const std::string& acts = {});

// iw8r_sun_direction / sundirection text "pitch yaw [roll]" -> pitch and yaw (the roll is ignored); false when it is
// not two or three numbers or the pitch is not -90 to -1 (the sun above the horizon).
bool parseStockSunDirection(const std::string& text, double& pitch, double& yaw);

}  // namespace iw8r
