// IW8 Radiant Suite - the masks of a volumetric box (research 127 part A): pictures that thin the box's fog, as the
// drifting cloud layers of stock mp_village2 do.
//
// The game multiplies a box's density by the red channel of each mask picture, projected through the box along one of
// its axes and repeated every `size` units; `scroll` moves it (units per second). A box takes up to four.
//   key      mask, mask2, mask3, mask4 on an iw8r_volume
//   value    "<picture> [x|y|z] <sizeU> [<sizeV> [<scrollU> <scrollV> [<offsetU> <offsetV>]]]"
//   picture  a file name without its ending, looked for in <map folder>/volume_masks, then in the editor's
//            data/volume_masks (clouds_soft and clouds_hard are there)
// Where it is sampled, with l the point in the box's own axes (research 127 A.2):
//   uv  = axis x: (l.y, l.z)   y: (l.x, l.z)   z: (l.x, l.y)
//   at  = (uv.x / sizeU, -uv.y / sizeV) + (offset + time * scroll) / size + 0.5
#pragma once

#include "map_text.h"
#include <string>
#include <vector>

namespace iw8r {

inline constexpr int kVolumeMaskCount = 4;
inline constexpr const char* kVolumeMaskKeys[kVolumeMaskCount] = {"mask", "mask2", "mask3", "mask4"};
inline constexpr char kVolumeMaskFolder[] = "volume_masks";

struct VolumeMask {
    std::string image;            // the picture's name, lower case, without folder or ending
    int axis = 2;                 // 0 x, 1 y, 2 z: the box axis the picture is projected along
    double size[2] = {256, 256};  // world units per repeat
    double scroll[2] = {0, 0};    // units per second
    double offset[2] = {0, 0};    // units
};

// Parses a mask value. False with *err for a value that names no picture, has a size of 0 or numbers that are none.
bool parseVolumeMask(const std::string& text, VolumeMask& out, std::string* err = nullptr);
// The value text of a mask (the shortest form that says it all).
std::string formatVolumeMask(const VolumeMask& mask);
// The masks of an iw8r_volume entity, in key order; a bad value is left out with a line in *warnings.
std::vector<VolumeMask> volumeMasksOf(const Entity& entity, std::vector<std::string>* warnings = nullptr);

// The four numbers the game's shader gets for a mask at `time` seconds: 1 / sizeU, 1 / sizeV and the two offset terms
// (offset + time * scroll) / size + 0.5.
void volumeMaskTerms(const VolumeMask& mask, double time, float out[4]);
// Where a point of the box (in the box's own axes, from its centre) reads the picture at `time`: u, v in repeats.
void volumeMaskUv(const VolumeMask& mask, const double local[3], double time, double uv[2]);

// The name the picture has in a built map ("iw8r_vmask_<picture>").
std::string volumeMaskImageName(const std::string& image);
// The picture's file: the first of <folder>/<image>.png, .tga, .jpg, .bmp over the folders, or "".
std::string findVolumeMaskFile(const std::string& image, const std::vector<std::string>& folders);

}  // namespace iw8r
