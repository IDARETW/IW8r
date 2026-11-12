// IW8 Radiant Suite - unlit baseline check (roadmap 1.8): what a map without baked lightmaps gets from its sun and
// light entities, and the Check Map warnings for a map that will render dark. Evidence and the rule list are in
// docs/research/55_unlit_baseline.md.
#pragma once

#include <string>
#include <vector>

#include "map_text.h"

namespace iw8r {

struct UnlitBaselineIssue {
    bool error;          // true: the map renders dark by the rules in doc 55; false: a warning
    std::string message;
};

// Checks the worldspawn sun keys (sundirection, suncolor) and the light entity count against what the editor's
// extraction gives an unbaked map. Returns an empty list for a map with a lit sun. Never throws.
std::vector<UnlitBaselineIssue> checkUnlitBaseline(const MapFile& map);

}  // namespace iw8r
