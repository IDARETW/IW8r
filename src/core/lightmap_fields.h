// IW8 Radiant - lightmap fields of brush faces, patch control-point locks, and the Surface panel edit.
//
// Brush face suffix layout (docs/research/02_iwmap4_format.md, section 3, face token table), counted from the
// first token after the third ')':
//   0       material (MAT)
//   1..6    N1..N6 colour texture fields (texture width, height, x offset, y offset, rotation, unknown N6)
//   7       lightmap material (LMMAT), lightmap_gray in most faces
//   8..13   L1..L6 lightmap fields
//   then    optional `smoothing NAME` tail, kept as written
// The doc names L1 scale X, L2 scale Y, L5 rotation or offset (inferred), and L3, L4, L6 as unknown (0 in the
// corpus). LightmapParams maps them as sx = L1, sy = L2, ox = L3, oy = L4, rot = L5, the same five-field
// convention the colour texture uses, and L6 is never read or written. The doc says these meanings are
// inferred from value distributions, not from a spec.
//
// Patch control points carry no lock field in the map format (doc section 4: `v X Y Z t S T LS LT [c ..] [f ..]`),
// so locks are an editor-side helper: they live in the caller and are applied with applyPatchEditWithLocks.
#pragma once

#include <cstddef>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#include "document.h"
#include "map_text.h"
#include "patch_ops.h"

namespace iw8r {

// Passed as `face` to withLightmapParams to write every face of the brush.
inline constexpr size_t kAllFaces = std::numeric_limits<size_t>::max();

struct LightmapParams {
    std::string material = "lightmap_gray";  // LMMAT token
    double sx = 16384, sy = 16384;           // L1, L2: lightmap scale
    double ox = 0, oy = 0;                   // L3, L4: offsets (unknown in the corpus, written as is)
    double rot = 0;                          // L5: rotation or offset (inferred)
};

// How many times finer than the default a face's lightmap is, from the lightmap fields in its suffix text:
// 16384 / sqrt(L1 x L2), kept inside 1/8 .. 8. 1 when the suffix has no lightmap fields or they are not positive.
double lightmapDensityOfSuffix(const std::string& suffix);

// Reads the lightmap fields of brush face `face`. nullopt when the primitive is not a brush, the index is out of
// range, the face has fewer than 14 suffix tokens, the LMMAT token is numeric, or any of L1..L5 does not parse.
std::optional<LightmapParams> lightmapParamsOfFace(const Primitive& p, size_t face);

// Returns a copy of `p` with the lightmap fields written. Only the tokens whose value differs are rewritten
// (the rest of the line, including L6 and any smoothing tail, is byte for byte the same). A token whose value
// is already equal keeps its text. Returns nullopt for a non-brush, an empty or whitespace-containing material,
// an out-of-range face, or a face without lightmap fields. With kAllFaces, faces without lightmap fields are
// skipped and the result is nullopt only when no face could take the fields.
std::optional<Primitive> withLightmapParams(const Primitive& p, size_t face, const LightmapParams& lp);

// Patch edit with locks: returns `after` with every locked control point put back at its `before` position.
// `locked` is row-major like PatchGrid::pos; missing entries count as unlocked. Only positions are restored;
// texture coordinates come from `after`. When the grids differ in rows, cols or point count, `after` is
// returned unchanged because the points cannot be matched.
PatchGrid applyPatchEditWithLocks(const PatchGrid& before, const PatchGrid& after, const std::vector<bool>& locked);

// Locks the outer ring of control points (first and last row, first and last column). Empty for an invalid grid.
std::vector<bool> lockBorder(const PatchGrid& g);

// Locks the four corner control points. Empty for an invalid grid.
std::vector<bool> lockCorners(const PatchGrid& g);

// Writes `lp` to every selected brush face (Document::faceSelection) as one undo step. The selection is kept.
// Returns false when no selected face changed (nothing selected, or every face already had these values).
bool setLightmapOnSelection(Document& doc, const LightmapParams& lp);

}  // namespace iw8r
