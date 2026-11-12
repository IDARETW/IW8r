// IW8 Radiant Suite - read and write the compiled spawnList of an x64-zt IW8D map_ents dump (Replay 1.20).
//
// A stock MapEnts keeps its spawns out of the entity string: `spawnList` is a u16 count at MapEnts +0x128 and a
// 40-byte record array (index, padding, name/target/noteworthy script strings, origin, angles). In the IW8D container
// the array is one data record of 40 * count bytes (the script ids are zeroed there), followed by three script_string
// records per spawn (name, target, script_noteworthy). See docs/research/26_spawnlist_writeback.md.
//
// writeSpawnList keeps every untouched spawn byte-identical. A changed spawn gets its 40 bytes and three new script
// records. A count change is allowed: the header count, the data record and the script records follow it, and offset
// records behind the block are renumbered. It is refused when the block cannot be located (for example an empty list,
// which has no record to insert next to).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "geometry.h"
#include "iw8_dump.h"

namespace iw8r {

struct SpawnRecord {
    // Stored in the spawnList record.
    std::string classname;        // the spawn name, e.g. mp_dm_spawn_start (script string)
    std::string target;           // script string
    std::string script_noteworthy;  // script string
    Vec3 origin;                  // inches, Z up
    Vec3 angles;                  // pitch yaw roll, degrees
    uint16_t index = 0;           // position in the array; writeSpawnList sets it from the position
    uint16_t padding = 0;         // raw u16 at record +2 (zero in stock)
    // Not stored in spawnList (the stock compiler drops it). readSpawnList leaves it empty and writeSpawnList ignores it.
    std::string targetname;
};

// Reads the spawnList of a map_ents dump. An empty list reads as success with no spawns.
bool readSpawnList(const Iw8Dump& mapEnts, std::vector<SpawnRecord>& out, std::string* err = nullptr);

// Replaces the spawnList of a map_ents dump. Atomic: on failure the dump is unchanged.
bool writeSpawnList(Iw8Dump& mapEnts, const std::vector<SpawnRecord>& spawns, std::string* err = nullptr);

// Spawn list as entity text (numeric canonical keys, one entity per spawn): 212 classname, 1070 target,
// 845 script_noteworthy, 709 origin, 80 angles, and 1072 targetname when set.
std::string spawnsToEntityText(const std::vector<SpawnRecord>& spawns);
// Parses the text written by spawnsToEntityText (or any entity text with the same keys). Every entity needs a classname
// and an origin.
bool spawnsFromEntityText(const std::string& text, std::vector<SpawnRecord>& out, std::string* err = nullptr);

}  // namespace iw8r
