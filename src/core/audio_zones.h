// IW8 Radiant - audio zones and reverb (roadmap 6.22, research 63).
//
// The x64-zt converter builds a map sound bank from extracted/sound/soundtable.json (iw3_sound.cpp ReadSoundTable)
// and client-side audio triggers from extracted/maps/clienttriggers.json (iw3_fastfile.cpp ReadClientTriggers). This
// module turns map keys into both files:
//   worldspawn iw8r_reverb "<room type> [dry wet fade]"   the map's own zone (everywhere outside other zones)
//   trigger_* brush with iw8r_audio_zone "<zone name>"    a stateless zone inside the brush's box, with its own
//     iw8r_reverb, iw8r_audio_priority (default 30; higher wins where zones overlap) and iw8r_audio_interior 1
//     (the stock "interior" occlusion and filter presets instead of "default"; also on worldspawn)
// Occlusion and filter presets are the stock bank's (mp_shipment.all: "default" 83 occlusion rows + 1 filter,
// "interior" 84 + 4; row ids = HashName(preset + channel [+ band]), checked on every row).
// Zone ambience: iw8r_ambient "<sound>" on worldspawn or a zone names one of the mapper's own sounds (custom_sounds.h);
// the build gives it an alias definition, so the converter carries it. A stock alias there would be silent (the bank
// carries only aliases it has a source definition for: "alias ... has no IW3 definition; left silent"), so stock names
// are warned about and dropped; a looping sound_emitter plays stock ambience instead.
// Ambient one-shots: iw8r_ambient_oneshots "<min s> <max s> <sound>[:weight] ..." (the map's own sounds) plays one of the
// sounds at random every min..max seconds in the zone (stock ambient_defs / events / elements; ids as in the stock bank:
// def HashName(defName), event HashName(defName + sound), element HashName(sound); defName = <zone>_elements).
// Ids follow the stock bank (mp_shipment.all, checked): zone id = soundHashName(zoneName), reverb id =
// soundHashName(reverbName), roomTypeId = soundHashName(roomType). Room types are the 59 of the stock bank.
// Pure core code.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "geometry.h"
#include "map_text.h"

namespace iw8r {

// Replay's sound name hash (x64-zt replay_sound.cpp HashName: 65599 multiplier from 5381, lower-cased, 0 -> 1).
uint32_t soundHashName(const std::string& name);

struct ReverbRoom {
    const char* roomType;
    double dry, wet, fade;  // a stock example (mp_shipment.all)
};
// The stock room types, sorted by name.
const std::vector<ReverbRoom>& reverbRooms();
const ReverbRoom* findReverbRoom(const std::string& roomType);

struct AudioZone {
    std::string name;       // zone name (lower case)
    std::string roomType;   // empty: no reverb
    double dry = 1, wet = 0.5, fade = 0.66;

    AABB bounds;            // trigger box (the map zone: the whole map)
    double priority = 30;
    std::string ambient;    // ambientStream: a custom sound name, "" for none
    double oneShotMin = 6, oneShotMax = 12;                  // seconds between ambient one-shots
    std::vector<std::pair<std::string, double>> oneShots;    // (custom sound, weight)
    bool interior = false;  // the "interior" occlusion / filter presets
};

struct AudioSetup {
    std::string mapId;
    AudioZone mapZone;              // name = mapId, priority 0
    std::vector<AudioZone> zones;   // from iw8r_audio_zone triggers
    bool any() const { return !mapZone.roomType.empty() || !mapZone.ambient.empty() || !mapZone.oneShots.empty() || !zones.empty(); }
};

// Reads the keys above. worldBounds is the map's extent (the map zone's trigger). Problems (unknown room type, a zone
// without a brush, a duplicate name) are skipped with a line in *warnings.
// isCustomSound: true for a name the build carries as the mapper's own sound (iw8r_ambient accepts only those).
AudioSetup readAudioSetup(const MapFile& map, const std::string& mapId, const AABB& worldBounds, std::vector<std::string>* warnings,
                          const std::function<bool(const std::string&)>& isCustomSound = {});

// extracted/sound/soundtable.json and extracted/maps/clienttriggers.json for the setup.
std::string soundTableJson(const AudioSetup& setup);
std::string clientTriggersJson(const AudioSetup& setup);

}  // namespace iw8r
