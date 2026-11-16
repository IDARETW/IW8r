// IW8 Radiant - texel density of brush faces (texture and lightmap), for the texel-density view.
//
// Pure core: no UI. The camera mode that draws these values is a separate change.
//
// Texture density comes from the shared projection (tex_projection.h): faceUV gives u = S / width + xoff / width
// and v = T / height + yoff / height, so one texture repeat spans |width| world units along the face's +u
// direction and |height| units along +v. Along a unit in-plane direction d the texel density is
//   px per unit = |du / dl| * textureWidthPx      (U)
//   px per unit = |dv / dl| * textureHeightPx     (V)
// with du / dl and dv / dl taken by finite difference through faceUV (it is linear in the point, so the
// difference is exact). U uses faceTangent, V is the in-plane direction perpendicular to it. The face value is
// the geometric mean of the two. Rotation turns both directions together, so it does not change the value.
//
// Default target: kDefaultTexelTarget = 2.0 texture pixels per world unit. IW8 world textures are commonly
// 1024 px across 512 units, which is 2 px per unit (docs/research/02_iwmap4_format.md, section 3, and the
// texture-size note in tex_projection.h). No density constant was found in the source tree.
//
// Lightmap density: the lightmap scale L1 / L2 (lightmap_fields.h, LightmapParams::sx / sy, default 16384 when a
// face has no lightmap fields) is passed in as `lightmapScale`. The converter does not read L1 / L2 (it writes
// lightmap_uv [0,0]), so the unit of the scale is not confirmed. This module treats it as world units per
// lightmap texel, the same way the caller reads it, and returns 0 for a non-positive or non-finite value.
#pragma once

#include <array>
#include <cstddef>
#include <utility>
#include <vector>

#include "map_text.h"

namespace iw8r {

// Default texture target in texture pixels per world unit (see the header comment).
inline constexpr double kDefaultTexelTarget = 2.0;

struct FaceDensity {
    double texturePxPerUnit;    // geometric mean of the U and V texel densities, texture pixels per world unit
    double lightmapUnitsPerTexel;  // world units per lightmap texel from the effective lightmap scale (0 = unknown)
    double area;                // world-space area of the face's winding
};

// Density of face `face` of brush `brush`. The texture size is the pixel size of the material (passed in).
// Missing texture fields use the compiler's default TexParams (width and height 128). All zero for a
// non-brush primitive, an out-of-range face, a degenerate face or a non-positive texture size.
FaceDensity faceDensity(const Primitive& brush, size_t face, int textureWidthPx, int textureHeightPx, double lightmapScale);

// Colour ramp for a view mode: ratio = value / target. ratio 1 -> green (0,1,0), ratio <= 0.5 -> blue (0,0,1),
// ratio >= 2 -> red (1,0,0). Between them the colour is linear in log2(ratio): blue to green on [0.5, 1],
// green to red on [1, 2]. A non-positive or non-finite target gives grey (0.5,0.5,0.5).
std::array<float, 3> densityColor(double value, double target);

// Summary over many faces given as (value, area) pairs. min and max are over all values. mean is weighted by
// area; when the total area is not positive it is the plain average. `outliers` counts values outside
// [target / 2, target * 2]. `faces` is the number of pairs. An empty input gives zeros.
struct DensityStats {
    double min, max, mean;
    int outliers;
    int faces;
};
DensityStats densityStats(const std::vector<std::pair<double, double>>& valueArea, double target);

}  // namespace iw8r
