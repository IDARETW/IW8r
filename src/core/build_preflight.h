#pragma once
// Build preflight: checks a compiled extraction folder (compile_iw8.cpp output, `extracted\`) before it is shipped to the
// game. Errors are things that make the map fail or throw in game (a material, image or model the build does not carry,
// a script call the game does not know, broken brackets, no main()). Warnings are effects and sounds that are not in the
// always-loaded catalogs, so they may play silently.
#include <set>
#include <utility>
#include <string>
#include <vector>

namespace iw8r {

struct PreflightIssue {
    bool error;
    std::string category;  // build | material | image | model | script | effect | sound
    std::string message;
};

// Names the checks look up. Sets hold lower-case names: GSC names are matched without regard to case (the engine hashes
// lower-cased names), and so are effect and sound names.
struct PreflightCatalogs {
    std::set<std::string> functions;  // builtin functions (data/generated/iw8_gsc_builtins.txt, f rows)
    std::set<std::string> methods;    // builtin methods called on an entity (m rows)
    std::set<std::string> effects;    // always-loaded effects (data/asset_catalog/iw8_common_vfx.tsv, first column)
    std::set<std::string> sounds;     // always-loaded sound aliases (data/asset_catalog/iw8_common_sounds.tsv, first column)
    // IW3 functions the x64-zt converter bakes into the map (doc 31 section 5.6a): setExpFog is not an IW8 builtin, but the
    // generated map script calls it and the converter turns it into a fog setting, so it is not a script error.
    std::set<std::string> converterFunctions = {"setexpfog"};
};

// Key / value pairs of each entity block of a .ents text, in file order (the parser the preflight uses; the build report
// reuses it). Returns false and sets `err` on a malformed block.
using EntityKeyValues = std::vector<std::pair<std::string, std::string>>;
bool parseEntityBlocks(const std::string& text, std::vector<EntityKeyValues>& out, std::string* err);

// Reads the catalogs from `dataDir` (the repo's data folder). Returns false and sets `err` when a file is missing.
bool loadPreflightCatalogs(const std::string& dataDir, PreflightCatalogs& cat, std::string* err);

// Checks the build of map `mapId` in `extractedDir` (the `extracted` folder). Returns the issues in check order; the build
// is shippable when no issue has `error` set.
std::vector<PreflightIssue> preflightExtraction(const std::string& extractedDir, const std::string& mapId,
                                                const PreflightCatalogs& cat);

}  // namespace iw8r
