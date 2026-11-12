// IW8 Radiant Suite - auto-caulk (roadmap 2.6): hidden brush faces get the caulk tool material.
//
// A face is hidden when its whole polygon lies inside a face of another solid brush on the same plane facing the
// opposite way (the two brushes touch there), so no camera can ever see it. Tool faces stay as they are; occluders are
// solid brushes only (no trigger, clip, hint, skip, water, sky or nonColliding brushes). One undo step.
#pragma once

#include <cstddef>
#include <string>

#include "document.h"

namespace iw8r {

// Returns the number of faces changed. selectionOnly: only faces of the selected brushes change (any brush hides).
size_t autoCaulk(Document& doc, bool selectionOnly, const std::string& caulkMaterial = "caulk");

}  // namespace iw8r
