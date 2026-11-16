// IW8 Radiant - batch material replace (roadmap 12.9): every brush face and patch that uses one
// material gets another. Planning only: the map is not changed. The caller applies `edits` with
// Document::editPrimitives (one undo step).
#pragma once

#include <string>
#include <utility>
#include <vector>

#include "map_text.h"

namespace iw8r {

// The changed copy of one primitive.
struct MaterialReplaceEdit {
    Id entity = kNoId;
    Id item = kNoId;
    Primitive prim;
};

struct MaterialReplaceResult {
    std::vector<MaterialReplaceEdit> edits;  // one per primitive that changes
    size_t faces = 0, patches = 0;           // brush faces and patches that change
    // Entity id and key name of every entity key whose value is `from` and whose key is "iw8r_material"
    // or starts with "iw8r_mat:". The keys are not edited here.
    std::vector<std::pair<Id, std::string>> keys;
};

// Every brush face and patch whose material equals `from` (case-insensitive) gets `to`. Other faces of the
// same brush keep their material. Returns nothing when from == to or either one is empty.
MaterialReplaceResult planMaterialReplace(const MapFile& map, const std::string& from, const std::string& to);

// Materials used in the map (brush faces, patches and iw8r_material keys), lower case, sorted, no duplicates.
std::vector<std::string> materialsInMap(const MapFile& map);

}  // namespace iw8r
