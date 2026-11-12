// IW8 Radiant Suite - incremental build support (roadmap 7.10; design in docs/research/44_incremental_builds.md).
//
// Three pieces:
//   - writeIfChanged: an extraction file is rewritten only when its bytes differ, so unchanged files keep their
//     modification time and an unchanged build writes nothing but the marker files.
//   - fingerprintMap / classifyChange: a content hash per kind of input (world geometry, point entities, the map
//     script key, material names, model keys) and the kind of rebuild a change needs: "none", "entities-only",
//     "script-only" or "full".
//   - saveFingerprint / loadFingerprint: the fingerprint of the last build, kept beside the extraction.
#pragma once

#include <cstdint>
#include <string>

#include "map_text.h"

namespace iw8r {

// Writes `bytes` to `path` unless the file already holds exactly these bytes (size first, then content). A changed or
// missing file is written to a temporary sibling and renamed over the target, so a reader never sees a partial file.
// Parent folders are created. `changed` (may be null) reports whether the file was written. Returns false on error.
bool writeIfChanged(const std::string& path, const std::string& bytes, bool* changed);

// FNV-1a 64-bit hashes of the parts of a map that decide what an extraction contains. Equal values mean equal input.
struct InputFingerprint {
    // Brush and patch text (geometry, material and texture fields), brush entities as a whole, and every worldspawn
    // key except iw8r_script (worldspawn keys such as sky, fog, film and LUT feed world.json).
    uint64_t world = 0;
    // Key/values of the point entities (entities without brushes, not worldspawn), in map order.
    uint64_t entities = 0;
    // The iw8r_script key value (the script's path). The script file's contents are NOT hashed; see the research doc.
    uint64_t script = 0;
    // The sorted set of material names used by brushes and patches.
    uint64_t materials = 0;
    // The sorted set of "model" key values of every entity.
    uint64_t models = 0;

    bool operator==(const InputFingerprint& o) const {
        return world == o.world && entities == o.entities && script == o.script && materials == o.materials && models == o.models;
    }
    bool operator!=(const InputFingerprint& o) const { return !(*this == o); }
};

InputFingerprint fingerprintMap(const MapFile& map);

// "none" when nothing changed; "entities-only" when only point-entity key/values changed; "script-only" when only the
// iw8r_script key changed; "full" for anything else (geometry, materials, model keys, worldspawn keys, or both entities
// and script).
std::string classifyChange(const InputFingerprint& before, const InputFingerprint& after);

// Text file "iw8r-fingerprint 1" with one "<name> <16 hex digits>" line per field. Saved with writeIfChanged.
bool saveFingerprint(const std::string& path, const InputFingerprint& fp, std::string* err = nullptr);
bool loadFingerprint(const std::string& path, InputFingerprint& out, std::string* err = nullptr);

// 7.10: one 64-bit hash of a folder's files (relative paths, '/' separated and sorted, plus their bytes). Two folders
// with the same files and contents hash the same; any added, removed, renamed or changed file changes it. 0 when the
// folder does not exist.
uint64_t hashDirectory(const std::string& dir);

}  // namespace iw8r
