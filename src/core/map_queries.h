// IW8 Radiant - map queries: Radiant's Select by material / class / key, material usage,
// find & replace of materials and the "Check map" problem list.
//
// Matching rules: materials compare ASCII case-insensitively; classnames, keys and key values compare
// case-sensitively. A point entity is reported as one SelRef {entity, 0}; a primitive (brush or patch)
// as one SelRef {entity, primitive}, including every primitive of a brush entity.
#pragma once

#include <string>
#include <vector>

#include "document.h"
#include "map_text.h"

namespace iw8r {

// Brush faces and patches whose material matches (exact, or substring when `substring`). One SelRef per
// primitive. Empty `material` matches nothing.
std::vector<SelRef> findByMaterial(const MapFile& m, const std::string& material, bool substring);

// Entities with the given classname (see the SelRef rule above).
std::vector<SelRef> findByClass(const MapFile& m, const std::string& classname);

// Entities that have `key` with a value equal to (or, when `substring`, containing) `value`.
// value "" = the key is present with any value.
std::vector<SelRef> findByKeyValue(const MapFile& m, const std::string& key, const std::string& value, bool substring);

struct MaterialUse {
    std::string material;
    size_t faces = 0;        // brush faces, plus one per patch
    size_t primitives = 0;   // primitives that use the material at least once
};
// Every material used by a brush face or patch, sorted by faces (descending), then name.
std::vector<MaterialUse> materialUsage(const MapFile& m);

// Find & replace materials across the whole map, or only the selected primitives. An exact match replaces
// the whole material name; a substring match replaces every occurrence inside it. `replace` must be a
// non-empty name without whitespace (otherwise nothing changes). Returns the number of faces changed
// (a patch counts as one). The whole operation is ONE undo step; the edited primitives become the selection.
size_t replaceMaterial(Document& doc, const std::string& find, const std::string& replace, bool substring, bool selectionOnly);

// Radiant "Check map". Returns one message per problem: duplicate targetnames, targets with no matching
// targetname, entities without classname, brushes with fewer than 4 valid planes, degenerate patches
// (no control points, or control points spanning at most one axis), point entities without a valid origin.
std::vector<std::string> mapProblems(const MapFile& m);

}  // namespace iw8r
