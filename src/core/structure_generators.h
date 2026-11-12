// IW8 Radiant Suite - procedural stair and simple building generators.
// The output is pure geometry: axis-aligned boxes, each tagged with a material role. The editor turns every box
// into a brush (see brush_ops.h makeBox); this module does not touch the document.
#pragma once

#include <string>
#include <vector>

#include "geometry.h"

namespace iw8r {

enum class StructurePart { Floor, Wall, Ceiling, Roof, Stair, Landing, Trim };

struct StructureBox {
    AABB box;
    StructurePart part = StructurePart::Wall;
};

struct StairOptions {
    int axis = 0;                 // 0 = climbs along +X, 1 = along +Y (reverse flips the direction)
    bool reverse = false;         // true: climbs along -X / -Y
    double maxRise = 16;          // per step; step count = ceil(height / maxRise), so every rise <= maxRise
    double minRun = 12;           // a step's depth never goes below this; a shorter box gives an empty result
    bool solidUnderneath = true;  // true: each step is a block down to the box bottom; false: thin treads
    double treadThickness = 8;    // used when solidUnderneath = false
};

// A straight staircase filling `box`: from the bottom at the low end to the top at the high end along the axis, as
// wide as the box across. Heights and step edges snap to whole units (fractional box extents are floored).
// Empty, with *err set, when the box is less than 1 unit high, the run is too short for the steps at minRun, or the
// options are invalid. On success *err (when given) is cleared.
std::vector<StructureBox> generateStairs(const AABB& box, const StairOptions& opt, std::string* err = nullptr);

struct BuildingOptions {
    int floors = 1;               // storeys
    double storeyHeight = 128;    // floor-to-floor; each slab is floorThickness thick, walls fill the rest
    double wallThickness = 8;
    double floorThickness = 8;
    double doorWidth = 64, doorHeight = 112;                       // one door on the -Y wall, ground floor (0 = no door)
    double windowWidth = 48, windowHeight = 48, windowSill = 48;  // on the -X, +X and +Y walls, every floor (0 = none)
    bool roof = true;             // a flat roof slab on top
    bool stairs = true;           // an inner staircase from the ground floor to the first upper floor (needs floors >= 2)
};

// A box building in `box`'s XY footprint, starting at box.mins.z. Its height is floors * storeyHeight, plus
// floorThickness for the roof. Walls are split around the openings: no overlapping boxes and no zero-volume boxes.
// The stair shaft is a floor opening in the first upper slab. Empty, with *err set, when the footprint is smaller than
// 128 x 128, the options are invalid, or the stairs do not fit.
std::vector<StructureBox> generateBuilding(const AABB& box, const BuildingOptions& opt, std::string* err = nullptr);

const char* structurePartName(StructurePart p);  // "floor", "wall", "ceiling", "roof", "stair", "landing", "trim"

}  // namespace iw8r
