// Link graph node-view layout: layers, ordering, components, cycles, crossings and determinism.
#include <utility>
#include <vector>

#include "link_graph.h"
#include "link_graph_layout.h"
#include "test_harness.h"

using namespace iw8r;

namespace {

LinkGraph graphOf(const std::vector<std::pair<Id, Id>>& edges) {
    LinkGraph g;
    for (const auto& [from, to] : edges) g.edges.push_back(LinkEdge{from, to, "target"});
    return g;
}

const LinkNodeLayout* nodeOf(const LinkGraphLayout& layout, Id id) {
    for (const LinkNodeLayout& nd : layout.nodes) {
        if (nd.entity == id) return &nd;
    }
    return nullptr;
}

bool samePositions(const LinkGraphLayout& a, const LinkGraphLayout& b) {
    if (a.nodes.size() != b.nodes.size() || a.width != b.width || a.height != b.height) return false;
    for (size_t i = 0; i < a.nodes.size(); ++i) {
        const LinkNodeLayout& p = a.nodes[i];
        const LinkNodeLayout& q = b.nodes[i];
        if (p.entity != q.entity || p.x != q.x || p.y != q.y || p.layer != q.layer || p.component != q.component) {
            return false;
        }
    }
    return true;
}

}  // namespace

TEST(link_graph_layout_chain_layers) {
    const LinkGraphLayout l = layoutLinkGraph(graphOf({{1, 2}, {2, 3}}));
    CHECK(l.nodes.size() == 3);
    CHECK(nodeOf(l, 1) && nodeOf(l, 1)->layer == 0);
    CHECK(nodeOf(l, 2) && nodeOf(l, 2)->layer == 1);
    CHECK(nodeOf(l, 3) && nodeOf(l, 3)->layer == 2);
    CHECK(nodeOf(l, 1) && nodeOf(l, 2) && nodeOf(l, 3));
    if (nodeOf(l, 1) && nodeOf(l, 2) && nodeOf(l, 3)) {
        CHECK(nodeOf(l, 1)->y == nodeOf(l, 2)->y && nodeOf(l, 2)->y == nodeOf(l, 3)->y);
        CHECK(nodeOf(l, 1)->x == 0 && nodeOf(l, 2)->x == kColumnGap && nodeOf(l, 3)->x == 2 * kColumnGap);
    }
}

TEST(link_graph_layout_diamond_layers) {
    // a->b, a->c, b->d, c->d
    const LinkGraphLayout l = layoutLinkGraph(graphOf({{1, 2}, {1, 3}, {2, 4}, {3, 4}}));
    CHECK(nodeOf(l, 1) && nodeOf(l, 1)->layer == 0);
    CHECK(nodeOf(l, 2) && nodeOf(l, 2)->layer == 1);
    CHECK(nodeOf(l, 3) && nodeOf(l, 3)->layer == 1);
    CHECK(nodeOf(l, 4) && nodeOf(l, 4)->layer == 2);
    if (nodeOf(l, 2) && nodeOf(l, 3)) CHECK(nodeOf(l, 2)->y != nodeOf(l, 3)->y);
}

TEST(link_graph_layout_cycle_terminates) {
    // a->b->c->a: the back edge is dropped, positions stay distinct.
    const LinkGraphLayout l = layoutLinkGraph(graphOf({{1, 2}, {2, 3}, {3, 1}}));
    CHECK(l.nodes.size() == 3);
    bool distinct = true;
    for (size_t i = 0; i < l.nodes.size(); ++i) {
        for (size_t j = i + 1; j < l.nodes.size(); ++j) {
            if (l.nodes[i].x == l.nodes[j].x && l.nodes[i].y == l.nodes[j].y) distinct = false;
        }
    }
    CHECK(distinct);
    CHECK(nodeOf(l, 1) && nodeOf(l, 1)->layer == 0);
    CHECK(nodeOf(l, 2) && nodeOf(l, 2)->layer == 1);
    CHECK(nodeOf(l, 3) && nodeOf(l, 3)->layer == 2);
}

TEST(link_graph_layout_components_stack) {
    // Larger component (ids 20..22) on top, smaller one (ids 1..2) below with one empty row between them.
    const LinkGraphLayout l = layoutLinkGraph(graphOf({{20, 21}, {21, 22}, {1, 2}}));
    CHECK(l.nodes.size() == 5);
    CHECK(nodeOf(l, 20) && nodeOf(l, 20)->component == 0);
    CHECK(nodeOf(l, 22) && nodeOf(l, 22)->component == 0);
    CHECK(nodeOf(l, 1) && nodeOf(l, 1)->component == 1);
    CHECK(nodeOf(l, 2) && nodeOf(l, 2)->component == 1);
    double maxTop = -1, minBottom = 1e18;
    for (const LinkNodeLayout& nd : l.nodes) {
        if (nd.component == 0 && nd.y > maxTop) maxTop = nd.y;
        if (nd.component == 1 && (nd.y < minBottom)) minBottom = nd.y;
    }
    CHECK(minBottom > maxTop);
    CHECK(minBottom - maxTop >= 2 * kRowGap);  // one empty row between the components
    // No two nodes share a position.
    bool overlap = false;
    for (size_t i = 0; i < l.nodes.size(); ++i) {
        for (size_t j = i + 1; j < l.nodes.size(); ++j) {
            if (l.nodes[i].x == l.nodes[j].x && l.nodes[i].y == l.nodes[j].y) overlap = true;
        }
    }
    CHECK(!overlap);
}

TEST(link_graph_layout_self_loop_alone) {
    const LinkGraphLayout l = layoutLinkGraph(graphOf({{5, 5}}));
    CHECK(l.nodes.size() == 1);
    CHECK(nodeOf(l, 5) && nodeOf(l, 5)->layer == 0);
}

TEST(link_graph_layout_crossings_fixed) {
    // a->d, b->c with a above b (ids a=1, b=2, c=3, d=4). Id order alone crosses; barycenter sweeps remove it.
    const LinkGraph g = graphOf({{1, 4}, {2, 3}});
    const LinkGraphLayout l = layoutLinkGraph(g);
    CHECK(countLinkCrossings(g, l) == 0);
    CHECK(nodeOf(l, 1) && nodeOf(l, 2) && nodeOf(l, 1)->y < nodeOf(l, 2)->y);
}

TEST(link_graph_layout_hand_crossing_counted) {
    // Hand-made layout: 1->4 and 2->3 with the endpoints swapped, so exactly one crossing.
    const LinkGraph g = graphOf({{1, 4}, {2, 3}});
    LinkGraphLayout l;
    l.nodes = {
        LinkNodeLayout{1, 0, 0, 0, 0},
        LinkNodeLayout{2, 0, kRowGap, 0, 0},
        LinkNodeLayout{3, kColumnGap, 0, 1, 0},
        LinkNodeLayout{4, kColumnGap, kRowGap, 1, 0},
    };
    CHECK(countLinkCrossings(g, l) == 1);
}

TEST(link_graph_layout_deterministic) {
    const LinkGraph g = graphOf({{1, 2}, {1, 3}, {2, 4}, {3, 4}, {4, 1}, {7, 8}, {9, 9}, {10, 11}, {11, 12}});
    const LinkGraphLayout a = layoutLinkGraph(g);
    const LinkGraphLayout b = layoutLinkGraph(g);
    CHECK(samePositions(a, b));
}

TEST(link_graph_layout_empty) {
    const LinkGraph g;
    const LinkGraphLayout l = layoutLinkGraph(g);
    CHECK(l.nodes.empty());
    CHECK(l.width == 0);
    CHECK(l.height == 0);
    CHECK(countLinkCrossings(g, l) == 0);
}
