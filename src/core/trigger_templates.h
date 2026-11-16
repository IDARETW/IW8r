// IW8 Radiant - trigger templates (roadmap 6.17 full trigger set with volume, 6.20 callout zones).
//
// Trigger kinds are the stock classes the build accepts (compile_iw8.cpp acceptedPointClass, lines 487-509). Brush
// triggers are one box brush with the "trigger" tool material (the compiler turns it into a trigger hull); the entity
// origin is the box centre, as in gamemode_templates. trigger_radius is a point entity: origin at the bottom centre of
// the box, radius = half the wider footprint side, height = box height.
//
// Callout zones are brush triggers whose classname is "trigger_multiple_bcs_<area>" (stock: containers, center,
// dumpster, ...). The build accepts that prefix for any area name (compile_iw8.cpp line 509).
#pragma once

#include <string>
#include <utility>
#include <vector>

#include "document.h"
#include "geometry.h"
#include "map_text.h"

namespace iw8r {

struct TriggerKind {
    std::string id;            // "hurt": menu / command id
    std::string classname;     // "trigger_hurt"
    std::string title;         // menu text
    bool point = false;        // true: point entity (trigger_radius); false: brush volume
    std::vector<std::pair<std::string, std::string>> defaultKeys;  // written with the kind (targetname added by insert)
    bool buildSupported = true;
    std::string buildNote;     // why the build does or does not keep it (compile_iw8.cpp lines)
    std::string description;   // one line
};

// The trigger kinds, in menu order. Only stock classes that exist in data/iw8_entity_defs.json (or the stock inventory).
const std::vector<TriggerKind>& triggerKinds();
// nullptr when no kind has this id.
const TriggerKind* findTriggerKind(const std::string& id);

// Inserts one trigger of `kindId` covering `box` (world units) as ONE undo step, selects it, and returns its id.
// kNoId with *err set (document unchanged) for an unknown kind, a kind the build does not keep, an empty or flat box,
// or a targetname with a quote or line break.
Id insertTrigger(Document& doc, const std::string& kindId, const AABB& box, const std::string& targetname,
                 std::string* err = nullptr);

// True when `name` is 1..32 characters of [a-z0-9_].
bool isValidCalloutArea(const std::string& name);

// Inserts a callout zone "trigger_multiple_bcs_<areaName>" over `box` as ONE undo step and returns its id. kNoId with
// *err set for an invalid name or a flat/empty box (document unchanged).
Id insertCalloutZone(Document& doc, const std::string& areaName, const AABB& box, std::string* err = nullptr);

// Sorted, unique area names of the callout entities in the map (the part after "trigger_multiple_bcs_").
std::vector<std::string> calloutNamesInMap(const MapFile& map);

// Warnings about callout zones (empty when clean). Reported: invalid area names, zones with no brush or zero volume,
// and overlapping zones of DIFFERENT names (a point in both would be ambiguous). Same-name pairs are fine (stock has
// pairs), and touching faces do not overlap.
std::vector<std::string> checkCallouts(const MapFile& map);

}  // namespace iw8r
