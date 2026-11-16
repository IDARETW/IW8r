// IW8 Radiant - model preview image (roadmap 4.3): a three-quarter view of a model, flat shaded, depth sorted,
// drawn with QPainter (no GPU), for the Models browser.
#pragma once

#include <QImage>

#include "model_import.h"

namespace iw8r::qt {

// `m` in the model cache convention (glTF Y-up, inches; the editor maps it to Z-up with (x, -z, y)).
// yawDeg turns the model; a null image when the model has no triangles. At most `maxTriangles` are drawn
// (evenly sampled) to keep it interactive.
QImage renderModelPreview(const ImportedModel& m, int size = 256, double yawDeg = 35, size_t maxTriangles = 40000);

}  // namespace iw8r::qt
