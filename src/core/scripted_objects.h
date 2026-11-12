// IW8 Radiant Suite - scripted object presets (roadmap 6.5).
//
// Replay map objects that react in game come through the x64-zt compatibility layer that every map script now loads
// (maps\mp\_load::main -> iw3compat_load_main):
//  - destructible props: a script_model with targetname "destructable" (IW3 _destructables) breaks with sparks after 40
//    damage (x64-zt keeps such script_models live, fork change);
//  - scripted props: a live script_model (targeted by a script_origin) that the map's own GSC (worldspawn iw8r_script,
//    user_main) moves, rotates, hides; it is made solid at load.
// Both operate on the selected model entities (misc_model or script_model) as one undo step.
#pragma once

#include <set>
#include <string>

#include "document.h"

namespace iw8r {

// Returns how many entities were changed.
int makeDestructible(Document& doc, const std::set<Id>& entities);
// Gives each entity a unique targetname (kept when it has one) and a script_origin targeting it; returns the count.
// `names` receives the targetnames, for the status line.
int makeScripted(Document& doc, const std::set<Id>& entities, std::string* names = nullptr);

}  // namespace iw8r
