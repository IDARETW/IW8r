// Layered node-view layout of the entity link graph. See link_graph_layout.h for the rules.
#include "link_graph_layout.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <map>
#include <numeric>
#include <queue>
#include <set>
#include <utility>
#include <vector>

namespace iw8r {
namespace {

// Distinct directed pairs with from != to, ascending.
std::vector<std::pair<Id, Id>> distinctPairs(const LinkGraph& graph) {
    std::vector<std::pair<Id, Id>> pairs;
    pairs.reserve(graph.edges.size());
    for (const LinkEdge& e : graph.edges) {
        if (e.from != e.to) pairs.emplace_back(e.from, e.to);
    }
    std::sort(pairs.begin(), pairs.end());
    pairs.erase(std::unique(pairs.begin(), pairs.end()), pairs.end());
    return pairs;
}

size_t findRoot(std::vector<size_t>& parent, size_t i) {
    while (parent[i] != i) {
        parent[i] = parent[parent[i]];
        i = parent[i];
    }
    return i;
}

}  // namespace

LinkGraphLayout layoutLinkGraph(const LinkGraph& graph) {
    LinkGraphLayout out;

    // Participating entities in ascending Id order. Dense index i stands for ids[i], so index order is Id order.
    std::set<Id> idSet;
    for (const LinkEdge& e : graph.edges) {
        idSet.insert(e.from);
        idSet.insert(e.to);
    }
    if (idSet.empty()) return out;

    const std::vector<Id> ids(idSet.begin(), idSet.end());
    const size_t n = ids.size();
    std::map<Id, size_t> dense;
    for (size_t i = 0; i < n; ++i) dense[ids[i]] = i;

    // Adjacency: outAdj is directed, nbrAdj ignores direction. Both ascending and distinct.
    std::vector<std::vector<size_t>> outAdj(n);
    std::vector<std::vector<size_t>> nbrAdj(n);
    std::vector<size_t> parent(n);
    std::iota(parent.begin(), parent.end(), size_t{0});
    for (const auto& [a, b] : distinctPairs(graph)) {
        const size_t u = dense[a];
        const size_t v = dense[b];
        outAdj[u].push_back(v);
        nbrAdj[u].push_back(v);
        nbrAdj[v].push_back(u);
        const size_t ru = findRoot(parent, u);
        const size_t rv = findRoot(parent, v);
        if (ru != rv) parent[std::max(ru, rv)] = std::min(ru, rv);
    }
    for (auto& nb : nbrAdj) {
        std::sort(nb.begin(), nb.end());
        nb.erase(std::unique(nb.begin(), nb.end()), nb.end());
    }

    // 1. Cycle breaking: iterative DFS from nodes in ascending Id order. An edge into a node still on the stack is a
    //    back edge and is dropped; every other edge is kept in the acyclic remainder.
    std::vector<std::vector<size_t>> dagOut(n);
    {
        std::vector<int> state(n, 0);  // 0 unvisited, 1 on stack, 2 finished
        std::vector<size_t> next(n, 0);
        for (size_t s = 0; s < n; ++s) {
            if (state[s] != 0) continue;
            std::vector<size_t> stack{s};
            state[s] = 1;
            while (!stack.empty()) {
                const size_t u = stack.back();
                if (next[u] < outAdj[u].size()) {
                    const size_t v = outAdj[u][next[u]++];
                    if (state[v] == 1) continue;  // back edge
                    dagOut[u].push_back(v);
                    if (state[v] == 0) {
                        state[v] = 1;
                        stack.push_back(v);
                    }
                } else {
                    state[u] = 2;
                    stack.pop_back();
                }
            }
        }
    }

    // 2. Layer = longest path from a source over the acyclic remainder (Kahn order).
    std::vector<size_t> layer(n, 0);
    {
        std::vector<int> indeg(n, 0);
        for (size_t u = 0; u < n; ++u) {
            for (size_t v : dagOut[u]) ++indeg[v];
        }
        std::queue<size_t> ready;
        for (size_t i = 0; i < n; ++i) {
            if (indeg[i] == 0) ready.push(i);
        }
        while (!ready.empty()) {
            const size_t u = ready.front();
            ready.pop();
            for (size_t v : dagOut[u]) {
                layer[v] = std::max(layer[v], layer[u] + 1);
                if (--indeg[v] == 0) ready.push(v);
            }
        }
    }

    // 1 (continued). Weakly connected components, largest first, ties by smallest Id.
    std::map<size_t, std::vector<size_t>> byRoot;
    for (size_t i = 0; i < n; ++i) byRoot[findRoot(parent, i)].push_back(i);
    std::vector<std::vector<size_t>> comps;
    comps.reserve(byRoot.size());
    for (auto& entry : byRoot) comps.push_back(std::move(entry.second));
    std::stable_sort(comps.begin(), comps.end(), [](const std::vector<size_t>& a, const std::vector<size_t>& b) {
        if (a.size() != b.size()) return a.size() > b.size();
        return a.front() < b.front();  // members are ascending, so front() is the smallest Id
    });

    std::vector<size_t> compOf(n, 0);
    std::vector<size_t> rowOf(n, 0);
    std::vector<size_t> posInLayer(n, 0);
    size_t globalMaxLayer = 0;
    size_t rowBase = 0;

    for (size_t c = 0; c < comps.size(); ++c) {
        const std::vector<size_t>& members = comps[c];

        size_t maxLayer = 0;
        for (size_t v : members) maxLayer = std::max(maxLayer, layer[v]);
        globalMaxLayer = std::max(globalMaxLayer, maxLayer);

        // 3. Initial order: ascending Id within each layer.
        std::vector<std::vector<size_t>> layers(maxLayer + 1);
        for (size_t v : members) {
            compOf[v] = c;
            layers[layer[v]].push_back(v);
        }
        for (const auto& row : layers) {
            for (size_t i = 0; i < row.size(); ++i) posInLayer[row[i]] = i;
        }

        // Barycenter reorder of layer L against its neighbours in layer `ref`. Stable: ties keep the current order.
        auto reorder = [&](size_t L, size_t ref) {
            std::vector<std::pair<double, size_t>> keyed;  // (barycenter or current index, current index)
            keyed.reserve(layers[L].size());
            for (size_t i = 0; i < layers[L].size(); ++i) {
                const size_t v = layers[L][i];
                double sum = 0;
                size_t count = 0;
                for (size_t u : nbrAdj[v]) {
                    if (layer[u] == ref) {
                        sum += static_cast<double>(posInLayer[u]);
                        ++count;
                    }
                }
                const double key = count ? sum / static_cast<double>(count) : static_cast<double>(i);
                keyed.emplace_back(key, i);
            }
            std::stable_sort(keyed.begin(), keyed.end(),
                             [](const std::pair<double, size_t>& a, const std::pair<double, size_t>& b) {
                                 return a.first < b.first;
                             });
            std::vector<size_t> sorted;
            sorted.reserve(keyed.size());
            for (const auto& k : keyed) sorted.push_back(layers[L][k.second]);
            layers[L] = std::move(sorted);
            for (size_t i = 0; i < layers[L].size(); ++i) posInLayer[layers[L][i]] = i;
        };

        for (int sweep = 0; sweep < 4; ++sweep) {
            for (size_t L = 1; L <= maxLayer; ++L) reorder(L, L - 1);  // down
            for (size_t L = maxLayer; L-- > 0;) reorder(L, L + 1);     // up
        }

        // 4. Rows: index within the layer plus the component's row offset.
        size_t rowsUsed = 0;
        for (const auto& row : layers) rowsUsed = std::max(rowsUsed, row.size());
        for (const auto& row : layers) {
            for (size_t i = 0; i < row.size(); ++i) rowOf[row[i]] = rowBase + i;
        }
        rowBase += rowsUsed + 1;  // one empty row between components
    }

    const size_t totalRows = rowBase > 0 ? rowBase - 1 : 0;

    out.nodes.reserve(n);
    for (size_t v = 0; v < n; ++v) {
        LinkNodeLayout nd;
        nd.entity = ids[v];
        nd.layer = static_cast<int>(layer[v]);
        nd.component = static_cast<int>(compOf[v]);
        nd.x = static_cast<double>(layer[v]) * kColumnGap;
        nd.y = static_cast<double>(rowOf[v]) * kRowGap;
        out.nodes.push_back(nd);
    }
    out.width = static_cast<double>(globalMaxLayer + 1) * kColumnGap;
    out.height = static_cast<double>(totalRows) * kRowGap;
    return out;
}

size_t countLinkCrossings(const LinkGraph& graph, const LinkGraphLayout& layout) {
    std::map<Id, const LinkNodeLayout*> at;
    for (const LinkNodeLayout& nd : layout.nodes) at[nd.entity] = &nd;

    struct Span {
        int layer;   // upper layer index of the two
        long top;    // row of the endpoint in the upper layer
        long bottom; // row of the endpoint in the lower layer
    };
    std::vector<Span> spans;
    for (const LinkEdge& e : graph.edges) {
        if (e.from == e.to) continue;
        const auto ia = at.find(e.from);
        const auto ib = at.find(e.to);
        if (ia == at.end() || ib == at.end()) continue;
        const LinkNodeLayout* a = ia->second;
        const LinkNodeLayout* b = ib->second;
        if (std::abs(a->layer - b->layer) != 1) continue;
        const LinkNodeLayout* upper = a->layer < b->layer ? a : b;
        const LinkNodeLayout* lower = upper == a ? b : a;
        Span s;
        s.layer = std::min(a->layer, b->layer);
        s.top = std::lround(upper->y / kRowGap);
        s.bottom = std::lround(lower->y / kRowGap);
        spans.push_back(s);
    }

    size_t count = 0;
    for (size_t i = 0; i < spans.size(); ++i) {
        for (size_t j = i + 1; j < spans.size(); ++j) {
            if (spans[i].layer != spans[j].layer) continue;
            const long dt = spans[i].top - spans[j].top;
            const long db = spans[i].bottom - spans[j].bottom;
            if (dt * db < 0) ++count;
        }
    }
    return count;
}

}  // namespace iw8r
