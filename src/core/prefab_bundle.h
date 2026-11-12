// IW8 Radiant Suite - prefab bundles (docs/PREFAB_BUNDLES.md, design: docs/research/83, Part B).
//
// A bundle is a folder named <name>.iw8prefab that holds everything one prefab needs:
//   prefab.json   the manifest (this file reads, checks and writes it)
//   prefab.map    the contents, an ordinary iwmap 4 prefab
//   thumb.png     optional picture for the browser
//   script.gsc    optional script, included once per prefab kind and started once per placement
//   fallback.map / fallback.gsc   optional second contents for a build that lacks a feature the prefab needs
//   materials/ models/ sounds/ effects/   the mapper's own asset files the prefab uses
// A misc_prefab names the folder in its "model" key. A plain .map prefab keeps working unchanged and counts as a
// bundle with an empty manifest ("bare").
//
// This file has no window code. It also holds the per-placement name scoping (two placements of one prefab never
// share a local entity name) and the core of "Save Selection As Prefab".
#pragma once

#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "document.h"
#include "geometry.h"
#include "json_lite.h"
#include "map_script.h"
#include "map_text.h"

namespace iw8r {

inline constexpr const char* kPrefabBundleSuffix = ".iw8prefab";
inline constexpr const char* kPrefabBundleFormat = "iw8r-prefab-1";
inline constexpr const char* kPrefabManifestFile = "prefab.json";
// Key on a misc_prefab: the placement id ("p7"). Local names of the prefab become "<pid>_<name>" in the build.
inline constexpr const char* kPrefabPlacementIdKey = "iw8r_pid";

// ------------------------------------------------------------------ manifest

struct PrefabAssetFile {  // assets.materials[], assets.models[], assets.effects[]: {name, file}
    std::string name, file;
};
struct PrefabSoundFile {  // assets.sounds[]: {alias, file, loop}
    std::string alias, file;
    bool loop = false;
};
struct PrefabStockRef {  // requires.stock_models[], stock_materials[], scriptable_defs[]: {name, zones[]}
    std::string name;
    std::vector<std::string> zones;
};
// One inspector control. `apply` is either {selector, key, template} (writes iw8r_ovr:<selector>:<key> with {name}
// in the template replaced by the value) or {material} (writes iw8r_mat:<material>).
struct PrefabParameter {
    std::string name, label;
    std::string type = "text";  // number, choice, material, text
    std::string defaultValue;   // as text
    std::optional<double> min, max;
    std::vector<std::string> choices;
    std::string selector, key, valueTemplate;
    std::string material;
};

// The manifest as the file says it. An empty text or list means "not given" (the defaults are in the comments).
struct PrefabManifest {
    std::string format = kPrefabBundleFormat;
    std::string id;                    // 1-40 characters of a-z 0-9 _
    std::string title;                 // default: the folder name
    std::string description;
    std::string category;              // default: the parent folder's name
    std::vector<std::string> tags;
    std::string author;
    std::optional<double> version;
    std::string map;                   // contents file; default prefab.map
    std::string thumbnail;             // default: drawn from the contents (thumb.png is used when it exists)
    std::string pivot;                 // what the origin means, a hint for the mapper
    std::optional<AABB> bounds;        // cached size, relative to the origin
    std::string surface = "floor";     // placement.surface: floor, wall, ceiling, free
    double snap = 0;                   // placement.snap; 0 = the editor grid
    double yawStep = 90;               // placement.yaw_step
    bool hasLocalNames = false;        // names.local is present (even when empty): nothing else is local
    std::vector<std::string> namesLocal, namesGlobal, nameKeys;
    std::vector<PrefabParameter> parameters;
    std::string scriptFile;            // default script.gsc when that file exists
    std::string scriptEntry = "prefab_main";
    std::string scriptPrefix = "prefab_";
    std::string scriptOnce;
    std::vector<PrefabAssetFile> materials, models, effects;
    std::vector<PrefabSoundFile> sounds;
    std::vector<PrefabStockRef> stockModels, stockMaterials, scriptableDefs;
    std::vector<std::string> stockSounds, stockEffects, stockStrings, stockScripts;
    std::vector<std::string> buildFeatures;      // requires.build
    std::vector<std::string> fallbackWhenMissing;
    std::string fallbackMap, fallbackScript, fallbackNote;
    std::vector<std::string> gameModes;
    // The document as it was read. Fields this editor does not know stay in it and are written back.
    json::Value raw;
};

// 1-40 characters of a-z 0-9 _.
bool isValidPrefabId(const std::string& id);
// "Jump Pad (v2)" -> "jump_pad_v2": lower case, other characters become one '_', at most 40; "prefab" when empty.
std::string prefabIdFromName(const std::string& name);

// Reads the manifest text. Returns false and says why in *err ("prefab.json: 'id' ...") on the first problem.
// A `format` other than iw8r-prefab-1 is not an error here (only format, id, title and description are read then):
// loadPrefabBundle lists such a bundle and refuses to place it.
bool parsePrefabManifest(const std::string& jsonText, PrefabManifest& out, std::string* err);

// The manifest as JSON text (UTF-8, two-space indent, LF). Fields of `raw` this editor does not know are kept;
// fields that hold their default and were not in `raw` are left out.
std::string prefabManifestText(const PrefabManifest& m);

// The key and value an inspector control writes on the misc_prefab for `value`: iw8r_ovr:<selector>:<key> with
// {name} of the template replaced, or iw8r_mat:<material> with the value as the new material.
std::pair<std::string, std::string> prefabParameterKey(const PrefabParameter& p, const std::string& value);
// The value a placement holds for a parameter, read back from the key prefabParameterKey writes (the text that
// stands where {name} is in the template, or the new material). nullopt when the placement has no such key or its
// value does not fit the template (edited by hand): the inspector then shows the parameter's default.
std::optional<std::string> prefabParameterValue(const PrefabParameter& p, const Entity& placement);

// ------------------------------------------------------------------ bundle folders

struct PrefabBundle {
    bool bare = false;         // a plain .map prefab: empty manifest, nothing but mapFile is set
    std::string folder;        // the .iw8prefab folder
    PrefabManifest manifest;
    bool placeable = true;     // false: it can be listed but not placed or built; `problem` says why
    std::string problem;
    std::vector<std::string> warnings;  // things that do not stop it (a listed asset file that is missing)
    std::string title, category;        // with the defaults applied
    // Full paths; empty when the bundle has no such file.
    std::string mapFile, scriptFile, thumbFile, fallbackMapFile, fallbackScriptFile;
    // The asset folders that exist (full paths, else empty).
    std::string materialsDir, modelsDir, soundsDir, effectsDir;
};

// True when the path is a folder whose name ends with .iw8prefab (any letter case).
bool isPrefabBundleFolder(const std::string& path);

// Reads and checks a bundle folder. Returns false when it cannot be placed: *err (and out.problem) say why. For a
// manifest of another format the id, title and description are still filled in so a browser can list it.
bool loadPrefabBundle(const std::string& folder, PrefabBundle& out, std::string* err = nullptr);

// What a misc_prefab "model" refers to.
struct PrefabRef {
    enum class Kind { Missing, BareMap, Bundle } kind = Kind::Missing;
    std::string path;     // the .map file (BareMap) or the bundle folder (Bundle)
    std::string mapFile;  // the contents file: `path` itself, or the bundle's main contents
};
// Looks the model up exactly as resolvePrefabPath does (map folder, its ancestors, then the roots). A model that
// names a .iw8prefab folder, or the contents file inside one, is a bundle; any other .map is a bare prefab.
PrefabRef resolvePrefabReference(const std::string& mapPath, const std::string& model, const std::vector<std::string>& searchRoots);
// The contents file of a bundle folder for callers that only want geometry ("" when the folder has none).
std::string prefabBundleContentsFile(const std::string& folder);

struct PrefabBundleEntry {
    std::string root;   // the prefab root it was found under
    std::string model;  // what a misc_prefab "model" key holds for it: the path under the root, forward slashes
    PrefabBundle bundle;  // also set for bundles that cannot be placed (bundle.placeable is false)
};
// Every .iw8prefab folder under the roots (searched recursively, not inside bundles), sorted by root, then model.
std::vector<PrefabBundleEntry> listPrefabBundles(const std::vector<std::string>& roots);

// The bundle folders a map places, prefabs inside prefabs included (up to 8 levels), in order of first use.
std::vector<std::string> prefabBundleFoldersOf(const MapFile& map, const std::string& mapPath, const std::vector<std::string>& roots);

// The asset folders of the bundles a map places: add them to the material, model, sound and effect folders of the
// editor and of the build. Each list has no duplicates.
struct PrefabAssetRoots {
    std::vector<std::string> materials, models, sounds, effects;
};
PrefabAssetRoots prefabBundleAssetRoots(const MapFile& map, const std::string& mapPath, const std::vector<std::string>& roots);

// ------------------------------------------------------------------ requires / fallback

// Stock asset names by kind (lower case) with the zones that hold them, from data/asset_catalog. A kind whose list is
// empty is "not known": prefabs are not checked against it.
struct PrefabStockCatalog {
    std::map<std::string, std::vector<std::string>> models, materials, sounds, effects;
    // Reads iw8_models.tsv, iw8_materials.tsv, iw8_common_sounds.tsv and iw8_common_vfx.tsv from the folder (first
    // column: name, last column: comma list of zones). A missing file leaves its kind empty. Returns the files read.
    int load(const std::string& assetCatalogDir);
};

// Build features a prefab can ask for in requires.build. Today's build has custom_sounds only.
inline constexpr const char* kPrefabFeatureScriptableInstances = "scriptable_instances";
inline constexpr const char* kPrefabFeatureScriptableDefCarry = "scriptable_def_carry";
inline constexpr const char* kPrefabFeatureStockScriptCalls = "stock_script_calls";
inline constexpr const char* kPrefabFeatureCustomSounds = "custom_sounds";
std::set<std::string> defaultPrefabBuildFeatures();

// What the build can provide.
struct PrefabBuildSupport {
    std::set<std::string> features = defaultPrefabBuildFeatures();
    const PrefabStockCatalog* stock = nullptr;  // not owned; null: stock names are not checked
};

struct PrefabContentsChoice {
    bool fallback = false;                     // the fallback contents are built
    std::vector<std::string> missingFeatures;  // of requires.build, not in the build
    std::vector<std::string> missingAssets;    // "stock sound 'x'": required names the catalogue does not have
    std::string mapFile, scriptFile;           // file names inside the bundle (scriptFile may be empty)
    std::string reason;                        // one line for the build log; empty when there is nothing to say
};
// Main or fallback contents, and why. The fallback is chosen when the bundle has one (fallback.map) and a feature of
// fallback.when_missing is absent. When it is chosen, scriptable definitions and stock script calls are not checked
// (the fallback exists because the build lacks them).
PrefabContentsChoice choosePrefabContents(const PrefabBundle& bundle, const PrefabBuildSupport& support);

// ------------------------------------------------------------------ names per placement

// Keys whose values are entity names in every prefab: targetname, target, target2..target8, script_linkname,
// script_linkto, and the editor's own behaviour keys that name another entity (iw8r_teleport, iw8r_visual).
const std::vector<std::string>& prefabNameKeys();

struct PrefabNameScope {
    std::string pid;                // "" = nothing is renamed
    std::set<std::string> local;    // names that become "<pid>_<name>"
    std::vector<std::string> keys;  // every key whose value holds names (prefabNameKeys plus the manifest's names.keys)
};

// 1-24 characters of letters, digits and '_'.
bool isValidPrefabPlacementId(const std::string& pid);
// The smallest "p<N>" (N from 1) that no misc_prefab of the map uses in iw8r_pid.
std::string nextPrefabPlacementId(const MapFile& map);
// "<pid>_<name>".
std::string scopedPrefabName(const std::string& pid, const std::string& name);

// Names both defined (targetname, script_linkname) and referred to (the other name keys) inside the contents.
std::set<std::string> prefabLinkedNames(const MapFile& contents, const std::vector<std::string>& extraKeys = {});
// The names taken as local when a manifest has no names.local list: the linked names, the targetname of every entity
// that refers to one of them (the trigger that opens a door: the generated script finds it by its name), and the
// targetname of every entity with one of the editor's behaviour keys (prefabBehaviourKeys).
std::set<std::string> prefabInferredLocalNames(const MapFile& contents, const std::vector<std::string>& extraKeys = {});
// Keys that make the generated map script look an entity up by its targetname: iw8r_door, iw8r_teleport, iw8r_jump,
// iw8r_damage, iw8r_lift, iw8r_spin, iw8r_bob, iw8r_anim, iw8r_vision, iw8r_no_collision.
const std::vector<std::string>& prefabBehaviourKeys();
// The local names of a prefab: the manifest's names.local when it has that list, else the inferred ones; names in
// names.global are never local.
std::set<std::string> prefabLocalNames(const MapFile& contents, const PrefabManifest& manifest);
// The scope for one placement of a bundle.
PrefabNameScope prefabNameScope(const MapFile& contents, const PrefabManifest& manifest, const std::string& pid);

// Rewrites the names in one entity's key list. A value may hold several names separated by spaces (script_linkto).
// Returns how many names were rewritten.
size_t scopePrefabNames(std::vector<std::pair<std::string, std::string>>& keys, const PrefabNameScope& scope);

// ------------------------------------------------------------------ scripts

// Problems of a prefab snippet the author should fix (empty = fine): a missing entry function, functions whose name
// does not start with the prefix, #include / #using lines, a missing once function.
std::vector<std::string> checkPrefabScript(const std::string& text, const PrefabManifest& manifest);

// ------------------------------------------------------------------ the build's view (prefab.cpp fills it)

struct PrefabBundleUse {
    std::string id, folder, model;
    size_t placements = 0;
    bool fallback = false;
    bool script = false;           // a snippet was included
    size_t scriptablesLeftOut = 0; // `scriptable` entities dropped because the build cannot write them
};
struct PrefabFlattenInfo {
    size_t bundlePlacements = 0;                          // placements of bundles (bare .map prefabs not counted)
    std::vector<PrefabBundleUse> bundles;                 // one per bundle kind, in order of first use
    std::vector<MapScriptExtras::PrefabScript> scripts;   // for MapScriptExtras::prefabScripts
    PrefabAssetRoots assetRoots;                          // the bundles' own asset folders
};

// ------------------------------------------------------------------ save selection as a bundle

struct PrefabSaveOptions {
    std::string name;                // folder name without the suffix, and the default title
    std::string id;                  // "" = prefabIdFromName(name)
    std::string description, category, author;
    std::vector<std::string> tags;
    std::optional<Vec3> pivot;       // unset: the bottom centre of the selection's bounds, snapped to `grid`
    double grid = 8;                 // <= 0: no snap
    // The mapper's own asset folders: files found there are copied into the bundle, every other name is stock.
    std::vector<std::string> materialRoots, modelRoots, soundRoots, fxRoots;
    const PrefabStockCatalog* stock = nullptr;  // optional: gives the zones written into `requires`
    std::string scriptText;          // optional snippet, saved as script.gsc
};
struct PrefabSaveFile {
    std::string kind;    // material, image, model, sound, effect
    std::string name;    // the asset name it belongs to
    std::string source;  // full path of the file to copy
    std::string dest;    // path inside the bundle, forward slashes
};
struct PrefabLinkedEntity {  // B.2.4 item 2: an entity outside the selection that is linked to it
    Id entity = kNoId;
    std::string text;        // "trigger_use 'door1_trig' targets 'door1' and is not selected"
};
struct PrefabSaveResult {
    bool ok = false;
    std::string error;                   // why not (nothing selected)
    PrefabManifest manifest;
    std::string mapText;                 // prefab.map: world brushes in worldspawn, brush entities kept with their keys
    std::string scriptText;              // script.gsc, may be empty
    Vec3 pivot;
    std::vector<PrefabSaveFile> files;   // the mapper's own files to copy
    std::vector<std::pair<std::string, std::string>> generated;  // dest -> text (material definitions with local image paths)
    std::vector<PrefabLinkedEntity> linkedOutside;
    std::vector<std::string> notes;      // things the mapper should know (a material that is neither theirs nor stock)
};
// The bundle for the document's selection. Nothing is written and the document is not changed.
PrefabSaveResult prefabBundleFromSelection(const Document& doc, const PrefabSaveOptions& opt);
// Writes the bundle folder: prefab.json, prefab.map, script.gsc and the asset files. Fails when the folder already
// holds a prefab.json and overwrite is false.
bool writePrefabBundleFolder(const std::string& folder, const PrefabSaveResult& result, std::string* err, bool overwrite = false);

}  // namespace iw8r
