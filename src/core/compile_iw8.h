// IW8 Radiant - native IW8 map compiler front end.
//
// Turns an editor document (MapFile) into the x64-zt "extraction" folder that
// zonetool::convert::iw3_iw8::convert_extraction consumes. The file layout, JSON keys and value
// rules follow docs/research/15_iw8_extraction_contract.md (cited as "doc 15" in the code).
//
// Output (all under outDir):
//   complete, skeleton/complete
//   extracted/maps/mp/<id>.d3dbsp.replay-world.json
//   extracted/maps/mp/<id>.d3dbsp.replay-collision.json
//   extracted/maps/mp/<id>.d3dbsp.replay-comworld.json
//   extracted/maps/mp/<id>.d3dbsp.ents
//   extracted/materials/<greyMaterial>.json
//   extracted/images/<id>_probe.dds
//   extracted/materials/<custom name>.json and extracted/images/iw8rm_*.dds for each custom material used (doc 21)
#pragma once

#include <array>
#include <cstddef>
#include <map>
#include <string>
#include <vector>

#include "collision_layout.h"
#include "gametype_script.h"
#include "stock_loot.h"
#include "stock_vehicles.h"
#include "scriptable_catalog.h"
#include "custom_materials.h"
#include "geometry.h"
#include "map_text.h"
#include "model_import.h"
#include "prefab_bundle.h"

namespace iw8r {

// The face size of a reflection probe capture and of the cube the build writes (research 123: the game's probe
// image is a 256-texel octahedron, which a cube of about 104-texel faces fills).
constexpr int kIw8ProbeCaptureSize = 128;

struct CompileOptions {
    std::string mapId;                        // mp_ + lower-case, <= 15 chars (doc 03 rule ^mp_[a-z0-9_]{1,12}$)
    std::string greyMaterial = "iw8r/grey";   // material name used by every surface without a custom material (doc 15 §4.2)
    int patchSubdiv = 4;                      // segments per patch sub-patch per direction, clamped to 1..32
    // Mapper's custom materials (doc 21). A surface whose editor material is in the library is emitted with that
    // material and its DDS images; every other surface stays grey. Not owned; must outlive the compile.
    const MaterialLibrary* library = nullptr;
    // Stock scriptable definitions (scriptable_catalog.h). A `scriptable` point entity whose definition is placeable
    // is written to the collision JSON's scriptable_instances; without a catalogue every such entity is left out.
    const ScriptableCatalog* scriptables = nullptr;
    // How the collision is laid out (collision_layout.h): Auto cuts a map wider than 20000 units into the game's
    // streamed collision tiles and keeps a smaller one in one piece. Worldspawn iw8r_collision overrides this.
    CollisionLayout collisionLayout = CollisionLayout::Auto;
    // Stock vehicles (stock_vehicles.h): a vehicle spawn struct (script_struct_mp_atv, ...) makes the build carry the
    // vehicle definition and its model from the stock Warzone map that holds them. Without the catalogue the structs
    // are written and nothing is carried (warned). Not owned.
    const VehicleCatalog* vehicles = nullptr;
    // The loot item tables (stock_loot.h BrLootItemTables): a map with loot points carries the brloot_* definitions
    // of its loot table from common_br_mp (a custom map never loads that zone, research 93). Needs carryScriptables.
    const BrLootItemTables* lootItems = nullptr;
    // Stock script index (gametype_script.h): checks the far calls of a map's game type script (worldspawn
    // iw8r_gametype_script). Without it such a script is left out.
    const StockScriptIndex* stockScripts = nullptr;
    const StockModelZones* stockModelZones = nullptr;
    // Carry stock definitions (doors, cones, cars: definitions that live in stock map zones): the instance names the
    // stock zone and the converter copies the definition with everything it uses into the map. Off: such an entity
    // is left out with a warning. Built like the stock map's own data, not seen in game.
    bool carryScriptables = false;
    // Mapper's custom models (doc 22): a script_model / trigger_radius whose "model" is `<name>` with
    // `<root>/**/<name>.obj` in one of these folders keeps its key and the model is written into the extraction
    // (xmodel/<name>.json + .replay.json). Its surfaces use library materials by usemtl name, else grey.
    std::vector<std::string> modelRoots;
    // 8.2 authored effects (fx_author.h): an fx_origin whose fx key names <root>/<fx>.iw8fx.json in one of these
    // folders gets the converter's IW3 effect graph (extracted/fx/<fx>.iw3.json), its effect materials and images.
    // fxDefaultSprite: the image of elements that name none (a soft round particle).
    std::vector<std::string> fxRoots;
    // 6.22: the mapper's own sounds (<root>/<name>.wav|.mp3, custom_sounds.h): a sound_emitter soundalias or an audio zone's
    // iw8r_ambient naming one is built into the map's sound bank.
    std::vector<std::string> soundRoots;
    std::string fxDefaultSprite;
    double modelScale = 1.0;  // OBJ units -> map units (1 = the file is in map units)
    // Stock Replay xmodels the editor exported through ACTS (its viewport model cache), in inches and Y-up like an OBJ
    // file (the model writer rotates to Z-up), keyed by the stock name ("tm/ee_appliance_dryer"). x64-zt cannot reference stock xmodels by name
    // (research/18), so a script_model / misc_model using one is built as a copy named stockModelBuildName(name),
    // and the entity's model key is rewritten to that name. Not owned; must outlive the compile.
    const std::map<std::string, ImportedModel>* stockModels = nullptr;
    // Re-skinned copies of the mapper's own models (model folders: shape models, imported OBJ / glTF), made by the
    // iw8r_material pre-pass: variant name -> the model it is a copy of and the materials to put on it. The variant is
    // built from the base model's file with the materials replaced. Not owned; must outlive the compile.
    struct CustomModelVariant {
        std::string base;                           // the model in the model folders
        std::string material;                       // every surface, unless a slot names it ("" = slots only)
        std::map<std::string, std::string> bySlot;  // lower-case surface material -> replacement
    };
    const std::map<std::string, CustomModelVariant>* customModelVariants = nullptr;
    // Sky (doc 33): the DDS of the worldspawn `skyname` (or `sky`) image, e.g. the stock sky the editor exported with
    // ACTS (2D hemi-octahedral BC6H) or a ready converter cube map. When worldspawn names a sky and this is set, the
    // image is converted (sky_image.h) to extracted/images/<skyImageName(name)>.dds and world.json "sky" names it.
    // Empty: world.sky stays "" and a named sky is only warned about.
    std::string skyDdsPath;
    int skyFaceSize = 1024;  // cube face edge for a converted sky
    // Placed models (misc_model, and script_model without a targetname) get an invisible solid box fitted to the
    // model, oriented by its angles, in the world collision: x64-zt gives props contents outside the player mask
    // (players pass them, as in IW3) and in game they stopped no bullets either.
    bool modelCollisionBoxes = true;
    // Roadmap 4.20: with modelCollisionBoxes on, a placed model gets one convex brush per convex piece of its mesh
    // (convexPieces, convex_hull.h) instead of one oriented box, so an L-shaped or hollow model does not block its
    // empty corners. A model whose mesh gives no hull (flat or degenerate) keeps the box. Off: a box per model.
    bool modelCollisionHulls = true;
    int modelCollisionMaxPieces = 4;    // convex pieces per model; the smallest pieces merge into the nearest one
    int modelCollisionMaxPlanes = 24;   // planes per piece; a larger hull is simplified (simplifyHull)
    // Which placed models collide and with what shape, by the stock models' own collision (model_physics.h, research
    // 140): a stock model without a physics asset (most bushes, grass) gets none, as in game; one with a physics asset
    // gets that asset's convex pieces; the rest the render mesh's pieces as above. Null: every placed model gets the
    // render mesh's pieces (the build before the rule). Not owned; the editor passes ModelPhysicsCatalog::shared().
    const class ModelPhysicsCatalog* modelPhysics = nullptr;
    // Folder of the source map: relative worldspawn paths (iw8r_lut) resolve against it. Empty: the working folder.
    std::string mapDir;
    // Where a worldspawn "iw8r_surface_shader" is looked for after <mapDir>/shaders (core custom_shader): the
    // editor's data/shaders. The shader goes to the converter in the world JSON, which lights the map with it.
    std::vector<std::string> shaderDirs;
    // Captured reflection probes (5.5, Compile > Capture Reflection Probes): <dir>/probe<N>.rgba holds six 64x64 RGBA
    // faces (D3D cube face order, world Z-up directions as the converter samples them) and <dir>/probe<N>.pos the
    // "x y z" it was captured at. A capture whose position differs from the probe's by more than one unit is ignored
    // (the probe moved); probes without one get the neutral grey cube.
    std::string probeCaptureDir;  // probe<N>.rgba (6 faces of kIw8ProbeCaptureSize^2 RGBA) + probe<N>.pos
    // Loadscreen / map-select preview (1.17): an image file (PNG, ...) written as extracted/images/loadscreen_<id>.dds;
    // the converter turns `loadscreen_<map>` into the package's preview.rgba (x64-zt BuildLoadscreenPreview). Empty or
    // unreadable: no preview (a warning for the second).
    std::string loadscreenImagePath;
    // The map's own HUD images (4.27, core hud_images.h): the folder "<map name>.hud". Each picture becomes
    // extracted/images/hud_<name>.dds, which the converter carries with a 2D material of that name. Empty or not a
    // folder: none.
    std::string hudImageDir;
    // 4.27 HUD fonts (hud_fonts.h): the pictures the editor drew from "<map>.hud/fonts.txt" (labels and glyphs, RGBA),
    // built like the folder's pictures, and the map script's helpers (iw8r_font_init / _text / _clear / _label).
    struct HudRgbaImage {
        std::string name;  // hud_...
        int width = 0, height = 0;
        std::vector<uint8_t> rgba;
    };
    std::vector<HudRgbaImage> hudFontImages;
    std::string hudFontScript;
    // The map's own text (4.27, core map_strings.h): the file "<map name>.str". Its entries become localized strings
    // "<MAP ID>/<REFERENCE>" (extracted/localize/strings.tsv, which the converter registers in eng_<map>). Empty or
    // not a file: none.
    std::string stringsFile;
    // Extra folders where misc_prefab models are looked up (Settings prefabRoots), after the map's own folder.
    std::vector<std::string> prefabRoots;
    // Prefab bundles (docs/PREFAB_BUNDLES.md). prefabSupport: the build features this build has and, optionally, the
    // stock catalogue the bundles' required stock names are checked against; it decides main or fallback contents.
    // The asset folders of the bundles a map places are added to modelRoots, fxRoots and soundRoots for that build.
    // Their materials need one library that holds the mapper's and the bundles' definitions: give materialRoots (the
    // folders `library` was loaded from) and the build loads it; without them, bundle materials are used only when
    // `library` is null or already holds them, and a warning says so otherwise.
    PrefabBuildSupport prefabSupport;
    std::vector<std::string> materialRoots;
    // Set by the prefab step of compileIw8Extraction for the later steps. Callers leave it null.
    const std::vector<MapScriptExtras::PrefabScript>* prefabScripts = nullptr;
};

// "tm/ee_appliance_dryer" -> "iw8r_stock_tm_ee_appliance_dryer" (lower case [a-z0-9_-], at most 64 characters).
std::string stockModelBuildName(const std::string& stockName);

// The custom model file for a model key: <root>/**/<name>.obj, else .glb, else .gltf (case-insensitive); "" = none.
std::string findCustomModelFile(const std::vector<std::string>& roots, const std::string& name);

// Unit quaternion (x, y, z, w) whose x64-zt basis (iw3_fastfile.cpp BasisFromQuaternion: rows forward, left, up) equals
// the editor's angles (pitch yaw roll), as static model placements use them. For dynamic props (6.15).
std::array<double, 4> anglesToDynQuaternion(double pitchDeg, double yawDeg, double rollDeg);

struct CompileReport {
    size_t surfaces = 0, triangles = 0, collisionBrushes = 0, entities = 0, spawns = 0;
    size_t brushModels = 0;  // triggers and script_brushmodel compiled as *N
    size_t customModels = 0; // custom models written into the extraction
    size_t reflectionProbes = 0;  // world.json reflection_probes (1 = the default probe only; doc 29)
    size_t staticModels = 0;      // world.json models[] placements from misc_model entities (doc 29)
    std::string sky;              // world.json "sky" (image name), empty when no sky was written (doc 33)
    std::string skyNote;          // how the sky image was made (source size/format, face size, exposure)
    bool loadscreen = false;      // extracted/images/loadscreen_<id>.dds written (1.17)
    std::vector<std::string> hudImages;  // the hud_* images written (4.27), sorted
    std::vector<std::string> mapStrings; // the localized string keys written (4.27), sorted
    std::vector<std::string> warnings;
    AABB bounds;  // padded by 16 (task rule); this is also the world bounds written to world.json
};

// Writes the extraction folder. Nothing is written when validation or compilation fails.
// Refuses an existing non-empty outDir unless it is a previous extraction (has a `complete` file and
// holds only the entries complete / skeleton / extracted); existing files of the extraction are overwritten.
// 11.4 (research 88): the brushes and patches added to a stock map. A stock map's world is compiled data, so they are
// built as custom models: one per entity that holds them (the worldspawn, each func_group), as a Y-up OBJ text (the
// custom model format: x, z, -y; usemtl = the editor material) centred on `centre`. Brushes whose faces are all tool
// materials (caulk, clip, nodraw, ...) draw nothing and are left out; other brush entities (triggers, brush models)
// are not geometry and are left out too. `notes` says what was left out.
struct StockOverlayModel {
    size_t entity = 0;   // index into map.entities
    std::string obj;     // Y-up OBJ text
    Vec3 centre;         // world position of the model origin
    size_t triangles = 0;
    std::set<std::string> materials;
};
std::vector<StockOverlayModel> stockOverlayModels(const MapFile& map, int patchSubdiv, std::vector<std::string>* notes = nullptr);

bool compileIw8Extraction(const MapFile& map, const std::string& outDir, const CompileOptions& opt, CompileReport& report,
                          std::string* err);

// Map id rule: "mp_" followed by 1..12 characters of [a-z0-9_] (total length <= 15).
bool isValidIw8MapId(const std::string& id);
// Tool materials (task rule): case-insensitive names containing caulk, clip, nodraw, trigger, hint,
// skip, portal, origin or sky. Such faces are not rendered. A stock name with a family prefix ("tm2o/nodraw") is a tool
// material only when its own name is one of those words or starts with one and "_" ("wc/clip_metal"): stock surfaces
// such as "tmo/fence_modular_metal_01_skyblue" or "mo/un_office_paper_stack_01_clipboard" are real surfaces.
bool isIw8ToolMaterial(const std::string& material);
// A stock model surface the game never draws: a tool material with a family prefix ("tm2o/nodraw" on Shipment's small
// dumpsters, "tmo/nodraw", "mo/nodraw"). The editor leaves these surfaces out of the stock props.
bool isStockModelToolSurface(const std::string& material);
// The rule above for one tool word ("sky", "caulk", "trigger", ...): an own name without a family prefix matches when it
// contains the word; a stock name ("<family>/<name>") only when its own name is the word or starts with it and "_".
// "tw/sky_mp_rust" is sky; "tw/periph_skyline_01", "tmo/fence_modular_metal_01_skyblue" and "m2o/equipment_skyhook"
// are surfaces.
bool isIw8ToolWordMaterial(const std::string& material, const char* word);
inline bool isIw8SkyMaterial(const std::string& material) { return isIw8ToolWordMaterial(material, "sky"); }
// A stock family prefix without the variant marks the game puts in front of it: "~tm2o" (spline-deformed models),
// "#tmo" and ",mo" are the families tm2o, tmo and mo. Takes a material or technique set name, or a bare prefix.
std::string stockFamilyOf(const std::string& name);

}  // namespace iw8r
