// Stock Replay effect (ParticleSystemDef, ACTS pool "vfx") inspector data. Read-only.
// The input is one structured ACTS export: `acts ... -a vfx -n <name>` writes
// <out>/mw19replay/<zone>/assets/vfx/<name>.<n>.asset.json in format "mw19-asset-json" (see
// docs/research/50_stock_effects.md for the route and the field paths used here).
#pragma once

#include <string>
#include <vector>

namespace iw8r {

struct VfxInfo {
    std::string name;                       // ParticleSystemDef name as exported, e.g. "vfx/core/mp/core/vfx_hp_chev_grey.vfx"
    std::vector<std::string> materials;     // Material references, unique, first-seen order (shared ',' prefix removed)
    std::vector<std::string> models;        // XModel references, unique
    std::vector<std::string> sounds;        // INIT_SOUND alias strings (inline in the module, not asset references), unique
    std::vector<std::string> childEffects;  // ParticleSystemDef references (INIT_RUNNER and test/event modules), unique
    int emitterCount = 0;                   // fields.numEmitters (or the emitterDefs count when that is absent)
    bool looping = false;                   // true when an emitter has an infinite-particle-life flag (see vfx_info.cpp)
    double lifeSeconds = 0;                 // estimated run time from the emitter timing; -1 when looping
};

// Parses the export text. Returns false and sets *err when the text is not an mw19 ParticleSystemDef export.
bool parseVfxExport(const std::string& jsonText, VfxInfo& out, std::string* err = nullptr);

// Reads `path` and parses it with parseVfxExport. Returns false when the file cannot be read.
bool readVfxExport(const std::string& path, VfxInfo& out, std::string* err = nullptr);

}  // namespace iw8r
