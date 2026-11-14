// IW8 Radiant Suite - ink outlines for a toon (cel-shaded) map: thin dark strips along the hard edges of brushes
// (plan M6, owner 2026-11-13: "make it toon-like ... it should render accordingly in game and in the viewport").
//
// The game's lit pass has no scene depth to find edges in a shader (research 114), so the ink is geometry: for every
// edge where two faces of a brush meet at more than `minAngleDeg`, a square strip `width` units thick centred on the
// edge, as an ordinary brush (any material: the editor uses an unlit black one). Edges two brushes share are inked
// once. The result looks the same in the viewport and in game and stays editable.
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "brush_ops.h"
#include "document.h"

namespace iw8r {

struct InkEdge {
    Vec3 a, b;
    Vec3 bisector;  // the mean of the two faces' outward normals (the strip's orientation)
    Vec3 n1, n2;    // the two faces' outward normals
};

struct InkOptions {
    double width = 2.0;         // strip thickness (units)
    double minAngleDeg = 30.0;  // faces meeting more steeply than this are an edge to ink
    double minLength = 4.0;     // shorter edges are left out
    // An edge is inked only when at least one of its two faces is drawn (not caulk, clip, nodraw and the other tool
    // materials): edges between hidden faces are never seen.
    bool skipHiddenFaces = true;
    // uniqueInkEdges: an edge is inked only when both faces are open to the air beside it (a point just off each face,
    // next to the edge, is inside no other drawn brush): seams between flush brushes and edges against or inside
    // neighbours are not creases anyone sees.
    bool skipBuried = true;
};

// The material of a face suffix (its first token).
std::string inkFaceMaterial(const std::string& suffix);

// The hard edges of one convex brush.
std::vector<InkEdge> brushInkEdges(const ConvexBrush& brush, const InkOptions& opt = {});

// The edges of many brushes with the duplicates (the same segment from two touching brushes, either direction)
// removed, and (skipBuried) the edges that are not a visible crease.
std::vector<InkEdge> uniqueInkEdges(const std::vector<ConvexBrush>& brushes, const InkOptions& opt = {});

// A strip brush along an edge (6 faces, all with `suffix`); empty when the edge is degenerate.
ConvexBrush inkStripBrush(const InkEdge& e, const InkOptions& opt, const std::string& suffix);

// The strip as map text: a brush with `contents nonColliding;` (ink never blocks players or bullets).
std::optional<Primitive> inkStripPrimitive(const InkEdge& e, const InkOptions& opt, const std::string& suffix,
                                           const std::string& eol);

// The ink group's iw8r_note and material.
inline constexpr const char* kInkNote = "toon ink outlines";
inline constexpr const char* kInkMaterial = "iw8r_ink";

struct InkResult {
    size_t brushes = 0;          // brushes outlined
    size_t strips = 0;           // strips written
    bool fromSelection = false;  // the selected brushes (else every worldspawn / func_group brush)
    bool replaced = false;       // an earlier ink group was removed
    bool capped = false;         // stopped at maxStrips
};

// Ink strips along the hard edges of the selected brushes, or of every worldspawn / func_group brush when no brush is
// selected, as one func_group (iw8r_note kInkNote, material kInkMaterial, non-colliding) that replaces an earlier ink
// group. One undo step; nothing changes when there are no edges.
InkResult applyInkOutlines(Document& doc, const InkOptions& opt, size_t maxStrips = 40000);

}  // namespace iw8r
