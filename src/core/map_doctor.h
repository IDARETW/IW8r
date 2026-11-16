// IW8 Radiant - Map Doctor (roadmap 9.12): Check Map findings that have a mechanical fix, and the fixes.
//
// Fixable findings (each fix is safe to apply without the mapper's judgement):
//   - SpawnAngles     spawn angles with pitch or roll: set to "0 <yaw> 0" (the game ignores pitch / roll and Check Map
//                     flags them).
//   - SpawnFloor      a spawn whose 32x32x72 player box is in a solid, or with no floor within 128 units: moved onto the
//                     nearest walkable floor straight below (up to 1024 units) or, when it is inside a solid, onto the
//                     solid's top when the box fits there (up to 128 units up). Not offered when neither is found.
//   - DanglingTarget  a target / target2..8 key naming no targetname: the key is removed.
//   - BadPrimitive    a brush with fewer than 4 valid planes or a degenerate patch: the primitive is deleted.
// Everything else Check Map reports (duplicate targetnames, spawn spacing, light budget, ...) needs a decision and
// stays a finding only.
#pragma once

#include <string>
#include <vector>

#include "document.h"
#include "map_text.h"

namespace iw8r {

enum class DoctorFixKind { SpawnAngles, SpawnFloor, DanglingTarget, BadPrimitive };

struct DoctorFix {
    DoctorFixKind kind = DoctorFixKind::SpawnAngles;
    Id entity = kNoId;
    Id item = kNoId;            // BadPrimitive: the primitive
    std::string key;            // DanglingTarget: the key to remove; SpawnAngles / SpawnFloor: the key to set
    std::string value;          // the new value (SpawnAngles: angles, SpawnFloor: origin)
    std::string description;    // one line for the Check Map list, e.g. "entity 12 (mp_tdm_spawn): snap to the floor 40 below"
};

// Every fixable finding of the map, in entity order.
std::vector<DoctorFix> diagnoseMap(const MapFile& map);

// Applies the fixes as ONE undo step called `name`. Returns how many were applied (a fix whose entity or primitive no
// longer exists is skipped).
size_t applyDoctorFixes(Document& doc, const std::vector<DoctorFix>& fixes, const std::string& name = "Map Doctor");

}  // namespace iw8r
