// IW8 Radiant Suite - IW8 (MW2019 Replay 1.20.4.7623265) MapEnts access.
//
// Reads the MapEnts asset ("map_ents", pool 29) of a stock Replay 1.20 zone from an ACTS
// `mw19-asset-json-v1` dump (atian-cod-tools `fastfile -r mw19replay`). The entity string is
// returned byte-exact (including its NUL terminator) so the editor can open it as a document;
// the compiled spawnList records are decoded so spawns, which IW8 does not keep in the entity
// string, can be shown to the mapper.
#pragma once

#include <map>
#include <string>
#include <vector>

#include "geometry.h"

namespace iw8r {

struct Iw8SpawnRecord {
    uint32_t index = 0;
    std::string name;        // e.g. mp_tdm_spawn_allies_start
    std::string target;
    std::string noteworthy;
    Vec3 origin;
    Vec3 angles;
};

// A stock map's own placed effect (MapEnts.clientSideEffects.oneshotEffects: the map's createfx one-shots and loops):
// fire, smoke, steam, falling dust, lamp glows. Research 132 gap 3.
struct Iw8PlacedEffect {
    Vec3 origin, angles;
    std::string effect;      // ParticleSystemDef name, e.g. vfx/iw8_mp/level/shipment/vfx_shipment_knee_high_mist
    std::string sound;       // effectSound alias name ("" = none)
    double radius = 0;       // cull radius (0 = the effect's own)
    int delayMs = 0;         // start delay; negative = repeat every |delay| ms (the createfx loop rule)
};

struct Iw8MapEntsDump {
    std::string assetName;     // maps/mp/<map>.d3dbsp
    std::string profile;       // must be "replay-1.20"
    std::string entityString;  // raw bytes, numEntityChars long (NUL terminator included)
    uint64_t numEntityChars = 0;
    std::vector<Iw8SpawnRecord> spawns;
    std::vector<Iw8PlacedEffect> effects;  // clientSideEffects.oneshotEffects, in record order
    std::map<std::string, uint64_t> sections;  // section name -> record count, for reporting
};

// `assetJson`: .../assets/map_ents/maps/mp/<map>.d3dbsp.0.asset.json
// `scriptStringsJson`: the dump's script_strings.json (optional: spawn names stay numeric without it)
bool loadActsMapEnts(const std::string& assetJson, const std::string& scriptStringsJson, Iw8MapEntsDump& out,
                     std::string* err = nullptr);

}  // namespace iw8r
