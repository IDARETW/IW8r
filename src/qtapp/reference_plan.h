#pragma once
// Stock map reference in the 2D views (roadmap 3.19): the reference triangles drawn once per view axis into an image
// (top view shaded by height, front and side views by depth, nearer surfaces drawn last), which the grid views draw
// under the map as a read-only backdrop. Built off the UI thread when the reference loads.
#include <QImage>
#include <vector>

#include "render.h"

namespace iw8r::qt {

struct ReferencePlan {
    int h = 0, v = 1;                          // world axes of the image's x and y (as EditorContext axisH / axisV)
    double minH = 0, maxH = 0, minV = 0, maxV = 0;  // world rectangle the image covers (image top = maxV)
    QImage image;                              // ARGB32, transparent where nothing is
};

// Three plans (top XY, front XZ, side YZ); `maxPixels` caps the long side of each image.
std::vector<ReferencePlan> buildReferencePlans(const std::vector<app::GpuVertex>& tris, int maxPixels = 2048);

}  // namespace iw8r::qt
