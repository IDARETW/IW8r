// Stock map geometry to editable brushes (roadmap 11.8). Each stock world triangle that touches a region becomes a thin
// triangular prism brush: the top face is the triangle itself with the stock material and a texture projection fitted
// to the triangle's own texture coordinates (uv_unwrap.h fitTexParams); the other four faces are caulk. Pure core code.
#pragma once

#include <string>
#include <vector>

#include "geometry.h"
#include "map_text.h"
#include "stock_world.h"

namespace iw8r {

struct StockBrushResult {
    std::vector<Primitive> brushes;
    size_t triangles = 0;   // triangles in the region (sky skipped)
    size_t skipped = 0;     // degenerate or too thin to make a valid brush
    bool capped = false;    // stopped at maxBrushes
    std::vector<std::string> materials;  // distinct stock materials used
};

// Triangles with a vertex inside `region` (sky materials skipped, as stockWorldToObj). thickness: how far the prism
// extends behind the triangle (units). Brush text uses `eol` line endings.
StockBrushResult stockWorldToBrushes(const StockWorld& w, const AABB& region, double thickness, size_t maxBrushes,
                                     const std::string& eol);

}  // namespace iw8r
