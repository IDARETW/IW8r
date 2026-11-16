// IW8 Radiant - stock scriptable definitions (roadmap 6.14, research 62 / 83 / 86).
//
// A scriptable is a stock object with built-in behaviour (parts, states, events): hardpoint markers, flags, doors,
// traffic cones, cars. A map places one as an instance record that names its definition; the definition itself lives in
// a stock zone. The catalogue data/asset_catalog/iw8_scriptables.tsv (tools/build_scriptable_catalog.py) lists every
// stock definition with the numbers the map build must write for one instance: the converter cannot read them from the
// game at build time, and a wrong count is a crash at load.
//
// Only a definition in a zone the game always has loaded (always_loaded) can be placed today: the map zone carries a
// name-only reference. Definitions that live in stock map zones (doors, cones, cars) need the definition copied into
// the map, which the converter cannot do yet.
// Pure core code.
#pragma once

#include <string>
#include <vector>

namespace iw8r {

struct ScriptableDefinition {
    std::string name;
    std::string zones;         // comma-separated stock zones that hold it (the first six)
    bool alwaysLoaded = false; // in a zone every multiplayer match has loaded
    int parts = 0;
    int serverInstancedParts = 0;
    int usableParts = 0;
    int maxDynentParts = 1;
    int physicsDynents = 0;    // dynamic entities of kind "scriptable physics" per instance
    bool instanceDynent = true;  // one dynamic entity of kind "scriptable instance" per instance
    bool sparseOccluder = false; // that entity is a sparse occluder (doors)
    int collisionFlags = 0;
    // The definition's share of the map's scriptable physics limits (research 89: definition flag bits 13+14 for
    // the main rigid body and motion limits, 13+15 for the detailed rigid body limits; exact on 21 stock maps).
    bool physicsLimit = false;       // main and motion limits (doors, signs, fences, buy stations)
    bool physicsLimitDetail = false; // detailed limits (doors, buy stations; not signs or fences)
    std::string model;         // first model of the definition (for drawing); may be empty
    bool observed = false;     // numbers read from stock map instances (else a guess)
    bool verified = false;     // observed: every number a build writes is known
    // Held by the battle royale zone (common_br_mp), which the game loads for Warzone matches: a map built for the
    // br game type places it by name.
    bool brZone() const;
    // What a map build accepts without carrying anything: the definition is always loaded and every number is known.
    bool placeable() const { return alwaysLoaded && verified; }
    // The stock map zone the converter takes the definition from when the map carries it ("" when it is always
    // loaded or no map zone holds it): the first "mp_" zone of the list.
    std::string carryZone() const;
    // Buildable when the map may carry stock definitions (CompileOptions::carryScriptables).
    bool carryable() const { return verified && !alwaysLoaded && !carryZone().empty(); }
    // A stock door (usable, sparse occluder): the stock door script must be started for it.
    bool door() const { return sparseOccluder && usableParts > 0; }
};

class ScriptableCatalog {
public:
    // Tab-separated rows in the column order of the file's header line; '#' comments. False (and *err) when unreadable.
    bool load(const std::string& tsvPath, std::string* err = nullptr);
    bool parse(const std::string& text);
    // Case-insensitive; a leading "scriptable_" class prefix is NOT stripped (door definitions really start with it).
    const ScriptableDefinition* find(const std::string& name) const;
    const std::vector<ScriptableDefinition>& all() const { return defs_; }
    size_t size() const { return defs_.size(); }
    // The catalogue beside the editor (data/asset_catalog/iw8_scriptables.tsv found from `startDir` upward); empty when
    // the file is not there.
    static const ScriptableCatalog& shared(const std::string& startDir);

private:
    std::vector<ScriptableDefinition> defs_;  // sorted by lower-case name
};

// One placed scriptable as the converter reads it (collision JSON "scriptable_instances").
struct ScriptablePlacement {
    const ScriptableDefinition* def = nullptr;
    double origin[3] = {0, 0, 0};
    double angles[3] = {0, 0, 0};
    std::string targetname, target, linkname, linkto, noteworthy;
    std::string carryZone;  // "" = the definition is always loaded (a name reference)
};

// The JSON object of one placement (no trailing comma).
std::string scriptableInstanceJson(const ScriptablePlacement& p);

}  // namespace iw8r
