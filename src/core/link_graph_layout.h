// IW8 Radiant - 2D node-view layout of the entity link graph (link_graph.h).
//
// The editor panel draws entities as boxes and links as arrows. This module only computes positions.
//
// Layered (Sugiyama-style, simplified) layout:
//   1. weakly connected components over the edges (self-loops do not connect). Each is laid out on its own, then
//      stacked top to bottom with one empty row between them, largest component first (ties: smallest Id first).
//   2. layer = longest path from a source, computed on the graph with cycles broken (back edges found by a DFS
//      from nodes in ascending Id order are ignored). Self-loops are ignored.
//   3. order within a layer: start by Id, then 4 sweeps of barycenter ordering (down, then up), stable. Only
//      neighbours in the adjacent layer count towards a node's barycenter; a node without one keeps its place.
//   4. x = layer * kColumnGap; y = row * kRowGap, where row is the index within the layer plus the component's
//      row offset. Rows are top-aligned within a component.
//
// Entities that appear only in a self-loop still get a node (own component, layer 0).
// Deterministic: the same graph always gives the same layout.
#pragma once

#include <cstddef>
#include <vector>

#include "link_graph.h"
#include "map_text.h"

namespace iw8r {

constexpr double kColumnGap = 220;  // x distance between layers
constexpr double kRowGap = 60;      // y distance between rows

struct LinkNodeLayout {
    Id entity = kNoId;
    double x = 0, y = 0;  // node centre in layout units
    int layer = 0;        // column index (0 = sources)
    int component = 0;    // connected component index (0 = largest)
};

struct LinkGraphLayout {
    std::vector<LinkNodeLayout> nodes;  // only entities that take part in at least one edge, ascending Id
    // Grid extent: width = (highest layer + 1) * kColumnGap, height = (rows used) * kRowGap. Both 0 when empty.
    double width = 0, height = 0;
};

LinkGraphLayout layoutLinkGraph(const LinkGraph& graph);

// Crossings between edges that join two adjacent layers (layer difference exactly 1). Edges spanning more than one
// layer and self-loops are not counted. Two edges cross when their endpoints swap order between the two layers.
// Edges whose endpoints are missing from the layout are skipped.
size_t countLinkCrossings(const LinkGraph& graph, const LinkGraphLayout& layout);

}  // namespace iw8r
