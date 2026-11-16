// IW8 Radiant - traversals for bots (roadmap 6.23): a node_negotiation_begin whose target names a
// node_negotiation_end, with the animation (animscript) a bot plays to get from one to the other. The converter writes
// them into the navigation mesh (x64-zt iw3_fastfile.cpp "authored traversals").
//
// The animations are the nine the stock maps use (mp_shipment, mp_backlot2, mp_hideout, mp_emporium and
// mp_aniyah_tac, 821 begin nodes: jump_down_80 224, jump_down_40 196, jump_up_40 190, jump_down_136 74,
// jump_across_100 60, jump_up_80 56, jump_down_160 10, ladder_up 9, window_over_40 2). The number in a name is the
// height (or the gap) in units the animation covers.
// Pure core code.
#pragma once

#include <string>
#include <utility>
#include <vector>

#include "geometry.h"

namespace iw8r {

struct TraversalAnim {
    const char* name;
    const char* title;
    Vec3 end;  // where the end node sits relative to the begin node, facing +X (yaw 0)
};

// The stock animations, most used first.
const std::vector<TraversalAnim>& stockTraversalAnims();
const TraversalAnim* findTraversalAnim(const std::string& name);

// Key/values of a begin / end pair at `origin`, facing `yaw` degrees, named `<name>` (begin targetname, used by
// nothing) and `<name>_end` (the end's targetname, which the begin targets). Begin first.
std::pair<std::vector<std::pair<std::string, std::string>>, std::vector<std::pair<std::string, std::string>>>
traversalPairKeys(const TraversalAnim& anim, const Vec3& origin, double yaw, const std::string& name);

}  // namespace iw8r
