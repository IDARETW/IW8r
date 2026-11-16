// IW8 Radiant - Radiant layers: `"NAME" flags ...` declarations in the header and the optional
// `layer "NAME"` first line inside a brush or patch block (docs/research/02_iwmap4_format.md §1).
#pragma once

#include <string>
#include <vector>

#include "map_text.h"

namespace iw8r {

struct LayerDecl {
    std::string name;
    std::string flags;  // text after `flags`, verbatim (e.g. " active", " prefab expanded ")
};

std::vector<LayerDecl> declaredLayers(const std::string& header);
// Header with one more declaration appended after the existing ones (no change if already declared).
std::string headerWithLayer(const std::string& header, const std::string& name, const std::string& eol);

// The primitive's layer ("" = none / the default layer).
std::string primitiveLayer(const Primitive& p);
// Sets (or with name == "" removes) the primitive's layer line, touching only that line. Returns true on change.
bool setPrimitiveLayer(Primitive& p, const std::string& name, const std::string& eol);

bool isValidLayerName(const std::string& name);  // non-empty, no quote, no line break

}  // namespace iw8r
