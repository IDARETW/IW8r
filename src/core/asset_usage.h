// IW8 Radiant - asset usage (roadmap 12.9): which materials, models, prefabs, effects and sounds a map
// names, and which of the mapper's own asset files the map uses. Pure core code, no Qt.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "map_text.h"

namespace iw8r {

struct AssetUse {
    std::string name;  // lower case
    size_t count = 0;
};

// Each list is sorted by count (descending), then name.
struct MapAssetUsage {
    std::vector<AssetUse> materials, models, effects, sounds, prefabs;
};

// Counts every asset name the map refers to. Names are compared case-insensitively and stored in lower case.
MapAssetUsage collectAssetUsage(const MapFile& map);

struct OwnAssetFile {
    std::string kind;  // "material", "model", "sound" or "effect"
    std::string name;  // as the asset is named in the map (see findOwnAssetFiles)
    std::string path;  // full path of the file
    bool used = false; // the name appears in the matching usage list (case-insensitive)
};

// Lists the mapper's own asset files under the given roots (missing roots are skipped):
//   materials: *.iw8mat.json, name = the JSON "name" field, else the file name without ".iw8mat.json"
//   models:    .obj / .glb / .gltf, name = file stem
//   sounds:    .wav, name = file stem
//   effects:   *.iw8fx.json, name = path relative to the root without ".iw8fx.json", forward slashes
std::vector<OwnAssetFile> findOwnAssetFiles(const MapAssetUsage& usage, const std::vector<std::string>& materialRoots,
                                            const std::vector<std::string>& modelRoots,
                                            const std::vector<std::string>& soundRoots,
                                            const std::vector<std::string>& fxRoots);

}  // namespace iw8r
