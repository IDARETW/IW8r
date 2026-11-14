#include "mount_edges.h"
#include "test_harness.h"

#include <cmath>
#include <set>

using namespace iw8r;

// Research 125: the mount edges of a built map (wall corners and ledge tops of its brushes) and the edge list's
// octrees, written the way stock maps have them.
namespace {

MountSolid box(double x0, double y0, double z0, double x1, double y1, double z1, bool source = true) {
    MountSolid s;
    s.planes = {{{1, 0, 0}, x1}, {{-1, 0, 0}, -x0}, {{0, 1, 0}, y1}, {{0, -1, 0}, -y0}, {{0, 0, 1}, z1}, {{0, 0, -1}, -z0}};
    s.bounds.add({x0, y0, z0});
    s.bounds.add({x1, y1, z1});
    s.source = source;
    return s;
}

double edgeLength(const MapEdge& e) {
    return std::sqrt(std::pow(double(e.b[0]) - e.a[0], 2) + std::pow(double(e.b[1]) - e.a[1], 2) + std::pow(double(e.b[2]) - e.a[2], 2));
}
bool upright(const MapEdge& e) { return std::fabs(double(e.b[2]) - e.a[2]) > 0.9 * edgeLength(e); }

}  // namespace

TEST(mount_edges_box_on_ground) {
    // A crate on a ground slab: its four corners and four top edges; the slab's rim; nothing where the crate stands.
    const std::vector<MountSolid> solids{box(-256, -256, -16, 256, 256, 0), box(0, 0, 0, 64, 64, 64)};
    MountEdgeStats st;
    const std::vector<MapEdge> edges = findMountEdges(solids, {}, &st);
    CHECK(st.corners == 4);
    CHECK(st.ledges == 8);
    CHECK(st.short_ == 4);  // the slab's own corners are 16 units tall
    CHECK(edges.size() == 12);
    size_t crateCorners = 0, crateTops = 0, rim = 0;
    for (const MapEdge& e : edges) {
        CHECK(e.flags == kEdgeFlagMount);
        double adjacent[3], other[3];
        mapEdgeNormals(e, adjacent, other);
        const double d[3] = {double(e.b[0]) - e.a[0], double(e.b[1]) - e.a[1], double(e.b[2]) - e.a[2]};
        const double cx = adjacent[1] * other[2] - adjacent[2] * other[1], cy = adjacent[2] * other[0] - adjacent[0] * other[2],
                     cz = adjacent[0] * other[1] - adjacent[1] * other[0];
        CHECK(cx * d[0] + cy * d[1] + cz * d[2] > 0);  // the faces in the order the game expects
        CHECK(e.openAngle == 15944);                    // 90 degrees, the half float stock maps store
        if (upright(e)) {
            ++crateCorners;
            CHECK(std::fabs(edgeLength(e) - 64) < 1e-3);
            // Both faces are walls of the crate and point away from its middle.
            const double mx = (e.a[0] + e.b[0]) * 0.5 - 32, my = (e.a[1] + e.b[1]) * 0.5 - 32;
            CHECK(std::fabs(adjacent[2]) < 1e-4 && std::fabs(other[2]) < 1e-4);
            CHECK(adjacent[0] * mx + adjacent[1] * my > 0 && other[0] * mx + other[1] * my > 0);
            CHECK(std::fabs(adjacent[0] * other[0] + adjacent[1] * other[1]) < 2e-3);  // the open angle is a half float
        } else {
            // A ledge: the top face is the adjacent face, stored as the angle 0; the other face is the wall below.
            CHECK(e.adjacentNormal == 0.0f);
            CHECK(std::fabs(adjacent[2] - 1) < 1e-4);
            CHECK(std::fabs(other[2]) < 2e-3);
            if (std::fabs(e.a[2] - 64) < 1e-3) {
                ++crateTops;
                CHECK(std::fabs(edgeLength(e) - 64) < 1e-3);
                const double mx = (e.a[0] + e.b[0]) * 0.5 - 32, my = (e.a[1] + e.b[1]) * 0.5 - 32;
                CHECK(other[0] * mx + other[1] * my > 0);
            } else {
                ++rim;
                CHECK(std::fabs(e.a[2]) < 1e-3 && std::fabs(edgeLength(e) - 512) < 1e-3);
            }
        }
    }
    CHECK(crateCorners == 4 && crateTops == 4 && rim == 4);
}

TEST(mount_edges_joined_and_hidden) {
    // A wall of two brushes side by side: no edge at the joint, and the top edges are one edge over both.
    {
        MountEdgeStats st;
        const auto edges = findMountEdges({box(0, 0, 0, 64, 32, 64), box(64, 0, 0, 128, 32, 64)}, {}, &st);
        CHECK(st.corners == 4 && st.ledges == 4);
        CHECK(st.hidden == 6);
        size_t longTops = 0, endTops = 0;
        for (const MapEdge& e : edges) {
            if (upright(e)) {
                CHECK(e.a[0] == 0.0f || e.a[0] == 128.0f);  // never at the joint
                continue;
            }
            if (std::fabs(edgeLength(e) - 128) < 1e-3) ++longTops;
            if (std::fabs(edgeLength(e) - 32) < 1e-3) ++endTops;
        }
        CHECK(longTops == 2 && endTops == 2);
    }
    // A pillar of two stacked brushes, each too low for a corner on its own: four corners of the whole height.
    {
        MountEdgeStats st;
        const auto edges = findMountEdges({box(0, 0, 0, 32, 32, 24), box(0, 0, 24, 32, 32, 48)}, {}, &st);
        CHECK(st.corners == 4 && st.ledges == 4);
        for (const MapEdge& e : edges)
            if (upright(e)) CHECK(std::fabs(edgeLength(e) - 48) < 1e-3);
    }
    // Something that is no source still hides: a clip brush against one side takes that side's edges away.
    {
        MountEdgeStats st;
        const auto edges = findMountEdges({box(0, 0, 0, 64, 64, 64), box(64, -64, 0, 128, 128, 128, false)}, {}, &st);
        CHECK(st.corners == 2 && st.ledges == 3);
        for (const MapEdge& e : edges) CHECK(!(e.a[0] == 64.0f && e.b[0] == 64.0f));
    }
    // A low curb is no ledge to rest a weapon on: there is no room to stand in front of it below the edge.
    {
        MountEdgeStats st;
        findMountEdges({box(-256, -256, -16, 256, 256, 0), box(0, 0, 0, 128, 16, 6)}, {}, &st);
        CHECK(st.ledges == 4);  // the ground's rim only
    }
    // A cap of edges keeps the longest.
    {
        MountEdgeOptions o;
        o.maxEdges = 2;
        MountEdgeStats st;
        const auto edges = findMountEdges({box(0, 0, 0, 200, 40, 64)}, o, &st);
        CHECK(edges.size() == 2 && st.dropped == 6);
        for (const MapEdge& e : edges) CHECK(std::fabs(edgeLength(e) - 200) < 1e-3);
    }
    CHECK(mountEdgesJson({}) == "[]");
    MapEdge e;
    e.b[2] = 64, e.adjacentNormal = 1.5f, e.openAngle = 15944, e.flags = 1;
    CHECK(mountEdgesJson({e, e}) == "[[0,0,0,0,0,64,1.5,15944,1],[0,0,0,0,0,64,1.5,15944,1]]");
}

TEST(map_edges_metadata_rule) {
    // The half float of a right angle is the value on most stock edges.
    CHECK(edgeHalfFromFloat(1.5707964f) == 15944);
    CHECK(edgeHalfFromFloat(0.0f) == 0 && edgeHalfFromFloat(1.0f) == 0x3C00);
    CHECK(std::fabs(edgeFloatFromHalf(15944) - 1.5703125f) < 1e-6f);
    for (const float v : {0.25f, 0.7853982f, 2.3561945f, 3.1f}) CHECK(std::fabs(edgeFloatFromHalf(edgeHalfFromFloat(v)) - v) < 0.002f);
    // A pillar corner as research 125 section 8 found it on stock maps: the stored angle turns world X right-handed
    // about the edge to one wall, and the open angle turns that wall on to the other.
    const double a[3] = {8, 8, 0}, b[3] = {8, 8, 96};
    const double east[3] = {1, 0, 0}, north[3] = {0, 1, 0};
    MapEdge e;
    CHECK(makeMapEdge(a, b, north, east, kEdgeFlagMount, e, false));
    double adjacent[3], other[3];
    mapEdgeNormals(e, adjacent, other);
    CHECK(std::fabs(adjacent[0] - 1) < 1e-4 && std::fabs(other[1] - 1) < 1e-3);  // east first: east x north = up, the edge's way
    CHECK(e.adjacentNormal == 0.0f && e.openAngle == 15944);
    // The same corner walked downward: the faces change places.
    CHECK(makeMapEdge(b, a, north, east, kEdgeFlagMount, e, false));
    mapEdgeNormals(e, adjacent, other);
    CHECK(std::fabs(adjacent[1] - 1) < 1e-4 && std::fabs(other[0] - 1) < 1e-3);
    // A ledge keeps its top face first and turns the edge round instead.
    const double up[3] = {0, 0, 1}, south[3] = {0, -1, 0};
    const double p[3] = {0, 0, 40}, r[3] = {100, 0, 40};
    CHECK(makeMapEdge(p, r, up, south, kEdgeFlagMount, e, true));
    CHECK(e.adjacentNormal == 0.0f && e.a[0] == 0.0f && e.b[0] == 100.0f);  // the edge runs along up x wall = (1, 0, 0)
    CHECK(makeMapEdge(r, p, up, south, kEdgeFlagMount, e, true));
    CHECK(e.adjacentNormal == 0.0f && e.a[0] == 0.0f && e.b[0] == 100.0f);  // given the other way round: turned
    mapEdgeNormals(e, adjacent, other);
    CHECK(std::fabs(adjacent[2] - 1) < 1e-4 && std::fabs(other[1] + 1) < 1e-3);
    // Faces in one plane make no edge.
    CHECK(!makeMapEdge(a, b, east, east, kEdgeFlagMount, e));
}

TEST(map_edges_octree) {
    // Many short edges for mounting and a few ladder lines: two trees, the stock layout, and a walk that misses nothing.
    std::vector<MapEdge> edges;
    uint32_t seed = 12345;
    const auto rnd = [&seed] {
        seed = seed * 1664525u + 1013904223u;
        return float(seed >> 8) / float(1u << 24);
    };
    for (int i = 0; i < 3000; ++i) {
        MapEdge e;
        for (int k = 0; k < 3; ++k) e.a[k] = (rnd() - 0.5f) * (k == 2 ? 600.0f : 4000.0f);
        const int axis = int(rnd() * 3) % 3;
        for (int k = 0; k < 3; ++k) e.b[k] = e.a[k] + (k == axis ? 16.0f + rnd() * 200.0f : 0.0f);
        e.flags = kEdgeFlagMount;
        e.openAngle = 15944;
        edges.push_back(e);
    }
    for (int i = 0; i < 5; ++i) {
        MapEdge e;
        e.a[0] = e.b[0] = 100.0f * float(i);
        e.b[2] = 120;
        e.flags = kEdgeFlagLadder;
        edges.push_back(e);
    }
    const MapEdgeList list = buildMapEdgeList(edges);
    CHECK(std::string(validateMapEdgeList(list)).empty());
    CHECK(list.queryTypes == 5);
    CHECK(list.trees.size() == 2 && list.trees[0].bucket == kEdgeBucketMount && list.trees[1].bucket == kEdgeBucketLadder);
    // The mount tree: a root with sets, the first set 0, its children naming the root as stock maps do.
    const MapEdgeTree& mount = list.trees[0];
    CHECK(mount.root.flags == kEdgeNodeRoot && mount.root.a == 0xFFFFFFFFu && mount.root.b == 0 && mount.nodeSetCount == list.nodeSets.size());
    CHECK(mount.nodeSetCount > 50);
    for (const MapEdgeNode& n : list.nodeSets[0])
        if (!(n.flags & kEdgeNodeLeaf)) CHECK(n.a == kEdgeRootParent);
    // The ladder tree: one leaf that is the root.
    const MapEdgeTree& ladder = list.trees[1];
    CHECK(ladder.root.flags == (kEdgeNodeRoot | kEdgeNodeLeaf) && ladder.root.b == 5 && ladder.nodeSetCount == 0);
    for (uint32_t k = 0; k < 5; ++k) CHECK(list.indices[ladder.root.a + k] == 3000 + k);
    // Every leaf lists exactly the edges that cross its cube, and holds few of them unless it is at the last depth.
    size_t leaves = 0, wrong = 0, over = 0;
    for (const auto& set : list.nodeSets)
        for (const MapEdgeNode& n : set) {
            if (!(n.flags & kEdgeNodeLeaf)) continue;
            ++leaves;
            const double side = 2.0 * std::ldexp(double(mount.halfWidth), -int(n.depth));
            const double lo[3] = {mount.minExtent[0] + n.grid[0] * side, mount.minExtent[1] + n.grid[1] * side, mount.minExtent[2] + n.grid[2] * side};
            std::set<uint32_t> listed(list.indices.begin() + n.a, list.indices.begin() + n.a + n.b);
            if (n.b > 8 && n.depth < 10) ++over;
            for (uint32_t i = 0; i < 3000; ++i) {
                const bool in = edge_detail::segmentInCube(edges[i].a, edges[i].b, lo, side, 0.01);
                if (in != (listed.count(i) != 0)) ++wrong;
            }
        }
    CHECK(leaves > 400 && wrong == 0 && over == 0);
    // A query box gets every edge that touches it (it may get more: whole leaves).
    size_t missed = 0, total = 0;
    for (int qn = 0; qn < 60; ++qn) {
        float mins[3], maxs[3];
        for (int k = 0; k < 3; ++k) {
            mins[k] = (rnd() - 0.5f) * (k == 2 ? 600.0f : 4000.0f);
            maxs[k] = mins[k] + 30.0f + rnd() * 200.0f;
        }
        const std::vector<uint32_t> got = queryMapEdgeList(list, kEdgeBucketMount, mins, maxs);
        const std::set<uint32_t> have(got.begin(), got.end());
        total += got.size();
        for (uint32_t i = 0; i < 3000; ++i) {
            // The edges are axis-aligned: box against box.
            bool touch = true;
            for (int k = 0; k < 3; ++k) touch = touch && std::min(edges[i].a[k], edges[i].b[k]) <= maxs[k] && std::max(edges[i].a[k], edges[i].b[k]) >= mins[k];
            if (touch && !have.count(i)) ++missed;
        }
        for (const uint32_t i : got) CHECK(i < 3000);  // never a ladder line from the mount tree
    }
    CHECK(missed == 0);
    CHECK(total < 60u * 400u);  // and far from all of them
    const float all0[3] = {-1e6f, -1e6f, -1e6f}, all1[3] = {1e6f, 1e6f, 1e6f};
    CHECK(queryMapEdgeList(list, kEdgeBucketMount, all0, all1).size() == 3000);
    CHECK(queryMapEdgeList(list, kEdgeBucketLadder, all0, all1).size() == 5);
    CHECK(queryMapEdgeList(list, kEdgeBucketMantle, all0, all1).empty());
    // A few edges: the root is the only leaf. No edges: no tree.
    const MapEdgeList small = buildMapEdgeList({edges[0], edges[1], edges[2]});
    CHECK(small.trees.size() == 1 && small.trees[0].root.flags == (kEdgeNodeRoot | kEdgeNodeLeaf) && small.nodeSets.empty() && small.indices.size() == 3);
    CHECK(std::string(validateMapEdgeList(small)).empty());
    CHECK(buildMapEdgeList({}).trees.empty());
    // A broken list is told: an inner node that names the wrong parent.
    MapEdgeList broken = list;
    for (auto& set : broken.nodeSets)
        for (MapEdgeNode& n : set)
            if (!(n.flags & kEdgeNodeLeaf)) n.a = 7;
    CHECK(!std::string(validateMapEdgeList(broken)).empty());
}

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

TEST(mount_edges_model_hulls) {
    // A placed model's hull piece gives edges like a brush, but a round shape built from facets gives no corners: an
    // eight-sided post (45 degrees between neighbouring walls) is a corner source as a brush and none as a model.
    MountSolid post;
    for (int k = 0; k < 8; ++k) {
        const double a = (22.5 + 45.0 * k) * 3.14159265358979323846 / 180.0;
        post.planes.push_back({{std::cos(a), std::sin(a), 0}, 24.0});
    }
    post.planes.push_back({{0, 0, 1}, 96});
    post.planes.push_back({{0, 0, -1}, 0});
    post.bounds.add({-26, -26, 0});
    post.bounds.add({26, 26, 96});
    MountEdgeStats asBrush, asModel;
    findMountEdges({post}, {}, &asBrush);
    post.model = true;
    findMountEdges({post}, {}, &asModel);
    CHECK(asBrush.corners == 8 && asBrush.fromModels == 0);
    CHECK(asModel.corners == 0);
    // A crate that is a model: its corners and tops count, and are told apart in the numbers.
    MountSolid crate = box(0, 0, 0, 48, 48, 40);
    crate.model = true;
    MountEdgeStats st;
    const auto edges = findMountEdges({box(-200, -200, -16, 200, 200, 0), crate}, {}, &st);
    CHECK(st.corners == 4 && st.ledges == 8 && st.fromModels == 8);
    CHECK(edges.size() == 12);
    // A wall made of a brush and a model piece side by side: the joined top edge is not "from models" alone.
    MountSolid half = box(64, 0, 0, 128, 16, 48);
    half.model = true;
    MountEdgeStats mixed;
    findMountEdges({box(0, 0, 0, 64, 16, 48), half}, {}, &mixed);
    CHECK(mixed.ledges == 4 && mixed.corners == 4);
    CHECK(mixed.fromModels == 3);  // the model's own end: two corners and the short top edge
}
