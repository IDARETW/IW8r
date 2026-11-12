#pragma once
// What each texture slot of a Replay world material holds (roadmap 4.16, Material Info).
// From the x64-zt converter's stock material route (iw3_render_assets.cpp: slot 0x00 colour, 0x09 packed normal + gloss,
// 0x1B alpha / reveal, one slot further per extra layer) and the ACTS material bodies the editor's cache reads.
#include <string>

namespace iw8r {

// A short label for slot `index` of a material's texture table, e.g. "colour (specular in alpha)". Slots the editor has
// no meaning for come back as "slot N".
std::string iw8TextureSlotName(int index);

// True for the slots the editor builds from (colour, normal + gloss, cut-out mask).
bool iw8TextureSlotIsBuilt(int index);

}  // namespace iw8r
