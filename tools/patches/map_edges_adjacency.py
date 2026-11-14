import io

# Mount adjacency (research 125 section 11): the pairs of mount edges a mounted player can move between: the two edges
# of one narrow face (a thin wall's end, a thin wall's top). The rule reproduces the stock tables of mp_m_speedball
# (174 of 174), mp_rust (190 of 190) and mp_shipment (80 of 80 between static edges).

p = 'E:/IW8-Radiant/src/core/map_edges.h'
s = io.open(p, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'


def rep(a, b):
    global s
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == 1, (s.count(a), a[:70])
    s = s.replace(a, b)


rep('''#include <cstring>
#include <vector>
''', '''#include <cstring>
#include <unordered_map>
#include <utility>
#include <vector>
''')

rep('''struct MapEdgeList {
    std::vector<MapEdge> edges;''', '''// One neighbour of a mount edge (EdgeAdjacency, 12 bytes): the part of this edge (0..1 from a to b, half floats) that
// lies beside the part adjStart..adjEnd of the edge adjEdge. A mounted player can move from one to the other.
struct MapEdgeAdjacency {
    uint16_t start = 0, end = 0;
    uint32_t adjEdge = 0;
    uint16_t adjStart = 0, adjEnd = 0;
};

struct MapEdgeList {
    std::vector<MapEdge> edges;''')
rep('''    std::vector<uint32_t> indices;                      // the leaves' edge lists
''', '''    std::vector<uint32_t> indices;                      // the leaves' edge lists
    // Mount adjacency: empty, or one word per edge (first record in the low 23 bits, the number of records in bits 23
    // and 24) and the records.
    std::vector<uint32_t> adjacencyMeta;
    std::vector<MapEdgeAdjacency> adjacency;
''')

rep('''// Builds the list: the box, and one tree per bucket that has edges.''', '''// The mount adjacency of a list's edges, as stock maps have it (research 125 section 11): two mount edges are
// neighbours when they are the two long sides of one narrow face: parallel (within about 2.5 degrees), 0.5 to 16
// units apart, one face of each in the same plane between them, their other faces turned away from each other, and at
// least 4 units of their lengths side by side. Each names the other with the parts that lie side by side. An edge
// keeps at most three neighbours (the count has two bits; stock has at most two), the nearest first.
inline void buildMapEdgeAdjacency(MapEdgeList& list) {
    using namespace edge_detail;
    list.adjacencyMeta.clear();
    list.adjacency.clear();
    struct Info {
        double d[3], adjacent[3], other[3], length;
    };
    std::vector<Info> info(list.edges.size());
    std::vector<uint32_t> mount;
    // Edges by grid cell: an edge is put into every cell a point of it (one every 16 units) lies in or near.
    constexpr double kCell = 64.0, kReach = 16.0;
    std::unordered_map<uint64_t, std::vector<uint32_t>> cells;
    const auto cellKey = [](long long x, long long y, long long z) {
        return (uint64_t(x & 0x1FFFFF) << 42) | (uint64_t(y & 0x1FFFFF) << 21) | uint64_t(z & 0x1FFFFF);
    };
    for (uint32_t i = 0; i < list.edges.size(); ++i) {
        const MapEdge& e = list.edges[i];
        if (!(e.flags & kEdgeFlagMount)) continue;
        Info& in = info[i];
        for (int k = 0; k < 3; ++k) in.d[k] = double(e.b[k]) - e.a[k];
        in.length = std::sqrt(dot(in.d, in.d));
        if (in.length < 1e-6) continue;
        for (int k = 0; k < 3; ++k) in.d[k] /= in.length;
        mapEdgeNormals(e, in.adjacent, in.other);
        mount.push_back(i);
        const int steps = std::max(1, int(std::ceil(in.length / 16.0)));
        for (int sIdx = 0; sIdx <= steps; ++sIdx) {
            const double t = in.length * sIdx / steps;
            long long c[3];
            for (int k = 0; k < 3; ++k) c[k] = static_cast<long long>(std::floor((e.a[k] + in.d[k] * t) / kCell));
            auto& cell = cells[cellKey(c[0], c[1], c[2])];
            if (cell.empty() || cell.back() != i) cell.push_back(i);
        }
    }
    struct Neighbour {
        uint32_t edge;
        double distance, start, end, adjStart, adjEnd;
    };
    std::vector<std::vector<Neighbour>> found(list.edges.size());
    const auto consider = [&](uint32_t i, uint32_t j) {
        const Info &a = info[i], &b = info[j];
        if (std::fabs(dot(a.d, b.d)) < 0.999) return;
        double off[3], across[3];
        for (int k = 0; k < 3; ++k) off[k] = double(list.edges[j].a[k]) - list.edges[i].a[k];
        const double along = dot(off, a.d);
        for (int k = 0; k < 3; ++k) across[k] = off[k] - a.d[k] * along;
        const double apart = std::sqrt(dot(across, across));
        if (apart < 0.5 || apart > kReach) return;
        // The face they share, and each edge's other face.
        const double* shared = nullptr;
        const double* otherA = nullptr;
        const double* otherB = nullptr;
        const double* facesA[2] = {a.adjacent, a.other};
        const double* facesB[2] = {b.adjacent, b.other};
        for (int x = 0; x < 2 && !shared; ++x)
            for (int y = 0; y < 2 && !shared; ++y)
                if (dot(facesA[x], facesB[y]) > 0.99) shared = facesA[x], otherA = facesA[1 - x], otherB = facesB[1 - y];
        if (!shared || std::fabs(dot(across, shared)) > 0.5) return;
        if (dot(otherA, across) > -0.1 * apart || dot(otherB, across) < 0.1 * apart) return;
        // The parts side by side, in each edge's own measure.
        double endB[3];
        for (int k = 0; k < 3; ++k) endB[k] = double(list.edges[j].b[k]) - list.edges[i].a[k];
        const double t0 = along / a.length, t1 = dot(endB, a.d) / a.length;
        const double lo = std::max(0.0, std::min(t0, t1)), hi = std::min(1.0, std::max(t0, t1));
        if ((hi - lo) * a.length < 4.0) return;
        double backA[3], backB[3];
        for (int k = 0; k < 3; ++k) backA[k] = double(list.edges[i].a[k]) - list.edges[j].a[k], backB[k] = double(list.edges[i].b[k]) - list.edges[j].a[k];
        const double u0 = dot(backA, b.d) / b.length, u1 = dot(backB, b.d) / b.length;
        const double lo2 = std::max(0.0, std::min(u0, u1)), hi2 = std::min(1.0, std::max(u0, u1));
        if (hi2 <= lo2) return;
        found[i].push_back({j, apart, lo, hi, lo2, hi2});
        found[j].push_back({i, apart, lo2, hi2, lo, hi});
    };
    for (const uint32_t i : mount) {
        const MapEdge& e = list.edges[i];
        const Info& in = info[i];
        std::vector<uint32_t> near;
        const int steps = std::max(1, int(std::ceil(in.length / 16.0)));
        for (int sIdx = 0; sIdx <= steps; ++sIdx) {
            const double t = in.length * sIdx / steps;
            long long lo[3], hi[3];
            for (int k = 0; k < 3; ++k) {
                const double v = e.a[k] + in.d[k] * t;
                lo[k] = static_cast<long long>(std::floor((v - kReach) / kCell));
                hi[k] = static_cast<long long>(std::floor((v + kReach) / kCell));
            }
            for (long long x = lo[0]; x <= hi[0]; ++x)
                for (long long y = lo[1]; y <= hi[1]; ++y)
                    for (long long z = lo[2]; z <= hi[2]; ++z) {
                        const auto it = cells.find(cellKey(x, y, z));
                        if (it == cells.end()) continue;
                        for (const uint32_t j : it->second)
                            if (j > i) near.push_back(j);  // each pair once
                    }
        }
        std::sort(near.begin(), near.end());
        near.erase(std::unique(near.begin(), near.end()), near.end());
        for (const uint32_t j : near) consider(i, j);
    }
    bool any = false;
    for (const auto& f : found) any = any || !f.empty();
    if (!any) return;
    list.adjacencyMeta.assign(list.edges.size(), 0);
    for (uint32_t i = 0; i < list.edges.size(); ++i) {
        auto& f = found[i];
        if (f.empty()) continue;
        std::stable_sort(f.begin(), f.end(), [](const Neighbour& x, const Neighbour& y) { return x.distance < y.distance; });
        if (f.size() > 3) f.resize(3);
        list.adjacencyMeta[i] = uint32_t(list.adjacency.size()) | (uint32_t(f.size()) << 23);
        for (const Neighbour& n : f)
            list.adjacency.push_back({edgeHalfFromFloat(float(n.start)), edgeHalfFromFloat(float(n.end)), n.edge,
                                      edgeHalfFromFloat(float(n.adjStart)), edgeHalfFromFloat(float(n.adjEnd))});
    }
}

// Builds the list: the box, and one tree per bucket that has edges.''')

rep('''        list.queryTypes |= 1u << bucket;
    }
    return list;
}''', '''        list.queryTypes |= 1u << bucket;
    }
    buildMapEdgeAdjacency(list);
    return list;
}''')

rep('''    if (nextSet != list.nodeSets.size()) return "node sets no tree owns";
    return "";''', '''    if (nextSet != list.nodeSets.size()) return "node sets no tree owns";
    if (list.adjacencyMeta.empty() != list.adjacency.empty()) return "adjacency records without their index, or the index without records";
    if (!list.adjacencyMeta.empty()) {
        if (list.adjacencyMeta.size() != list.edges.size()) return "the adjacency index does not have one word per edge";
        if (list.adjacency.size() >= (1u << 23)) return "too many adjacency records";
        for (uint32_t i = 0; i < list.edges.size(); ++i) {
            const uint32_t first = list.adjacencyMeta[i] & 0x7FFFFFu, count = (list.adjacencyMeta[i] >> 23) & 3u;
            if (list.adjacencyMeta[i] >> 25) return "an adjacency word uses bits beyond the mount count";
            if (size_t(first) + count > list.adjacency.size()) return "an edge's adjacency records lie outside the table";
            for (uint32_t k = 0; k < count; ++k) {
                const MapEdgeAdjacency& r = list.adjacency[first + k];
                if (r.adjEdge >= list.edges.size() || r.adjEdge == i) return "an adjacency record names no other edge";
                if (!(list.edges[i].flags & kEdgeFlagMount) || !(list.edges[r.adjEdge].flags & kEdgeFlagMount)) return "an adjacency record joins an edge that is no mount edge";
                // The neighbour names this edge back.
                const uint32_t back = list.adjacencyMeta[r.adjEdge] & 0x7FFFFFu, backCount = (list.adjacencyMeta[r.adjEdge] >> 23) & 3u;
                bool named = false;
                for (uint32_t q = 0; q < backCount && size_t(back) + q < list.adjacency.size(); ++q) named = named || list.adjacency[back + q].adjEdge == i;
                if (!named && backCount < 3) return "an adjacency record is not answered by its neighbour";
            }
        }
    }
    return "";''')
io.open(p, 'w', encoding='utf-8', newline='').write(s)

# Tests.
p = 'E:/IW8-Radiant/tests/mount_edges_tests.cpp'
s = io.open(p, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
s = s.rstrip('\r\n') + nl + '''
TEST(map_edges_adjacency) {
    // A thin wall (8 thick) standing free: at each end its two corners are neighbours, and so are its two top edges.
    // A crate (64 wide) has no neighbours: its edges are too far apart. Two crates 10 apart: the edges that face each
    // other across the gap are no neighbours either (no face lies between them).
    const std::vector<MountSolid> boxed{box(0, 0, 0, 200, 8, 48), box(500, 0, 0, 564, 64, 64), box(574, 0, 0, 638, 64, 64)};
    const std::vector<MapEdge> edges = findMountEdges(boxed);
    const MapEdgeList list = buildMapEdgeList(edges);
    CHECK(std::string(validateMapEdgeList(list)).empty());
    CHECK(list.adjacencyMeta.size() == list.edges.size());
    size_t wallPairs = 0, crateRecords = 0, fullRange = 0;
    for (uint32_t i = 0; i < list.edges.size(); ++i) {
        const uint32_t first = list.adjacencyMeta[i] & 0x7FFFFFu, count = (list.adjacencyMeta[i] >> 23) & 3u;
        const MapEdge& e = list.edges[i];
        if (e.a[0] >= 499.0f || e.b[0] >= 499.0f) {
            crateRecords += count;
            continue;
        }
        for (uint32_t k = 0; k < count; ++k) {
            const MapEdgeAdjacency& r = list.adjacency[first + k];
            const MapEdge& o = list.edges[r.adjEdge];
            ++wallPairs;
            // The neighbour is 8 units away and as long: the whole of both edges lies side by side.
            if (r.start == 0 && r.end == 0x3C00 && r.adjStart == 0 && r.adjEnd == 0x3C00) ++fullRange;
            const double mx = (double(e.a[0]) + e.b[0]) * 0.5 - (double(o.a[0]) + o.b[0]) * 0.5, my = (double(e.a[1]) + e.b[1]) * 0.5 - (double(o.a[1]) + o.b[1]) * 0.5,
                         mz = (double(e.a[2]) + e.b[2]) * 0.5 - (double(o.a[2]) + o.b[2]) * 0.5;
            CHECK(std::fabs(std::sqrt(mx * mx + my * my + mz * mz) - 8.0) < 1e-3);
        }
    }
    // The wall: two corner pairs (one per end) and one pair of top edges along it: 3 pairs, 6 records. Its short top
    // edges at the ends (8 long) are no mount edges (below the shortest ledge).
    CHECK(wallPairs == 6 && fullRange == 6);
    CHECK(crateRecords == 0);
    // No mount edges close together: no tables at all.
    const MapEdgeList lone = buildMapEdgeList(findMountEdges({boxed[1]}));
    CHECK(lone.adjacencyMeta.empty() && lone.adjacency.empty() && std::string(validateMapEdgeList(lone)).empty());
    // A record that is not answered is told.
    MapEdgeList broken = list;
    for (uint32_t i = 0; i < broken.adjacencyMeta.size(); ++i)
        if ((broken.adjacencyMeta[i] >> 23) & 3u) {
            broken.adjacencyMeta[i] = 0;
            break;
        }
    CHECK(!std::string(validateMapEdgeList(broken)).empty());
}
'''.replace('\n', nl)
io.open(p, 'w', encoding='utf-8', newline='').write(s)
print('ok')
