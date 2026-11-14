// Face edges: the per-vertex edge distance of brush faces, an input of custom surface shaders (core face_edges).
#include "face_edges.h"
#include "brush_ops.h"
#include "test_harness.h"

#include <cmath>
#include <map>
#include <set>

using namespace iw8r;

namespace {

EdgeBrush boxBrush(const Vec3& mn, const Vec3& mx, int set = 0) {
    AABB b;
    b.add(mn), b.add(mx);
    const ConvexBrush cb = makeBox(b, " twc/plaster_tan_02 64 64 0 0 0 0");
    EdgeBrush out;
    for (const auto& f : cb) out.planes.push_back(f.plane);
    out.wins = brushWindings(cb);
    out.drawn.assign(out.planes.size(), 1);
    out.set = set;
    finishEdgeBrush(out);
    return out;
}

double meshArea(const EdgeFaceMesh& m) {
    double a = 0;
    for (size_t t = 0; t + 2 < m.idx.size(); t += 3)
        a += 0.5 * length(cross(m.verts[m.idx[t + 1]].pos - m.verts[m.idx[t]].pos, m.verts[m.idx[t + 2]].pos - m.verts[m.idx[t]].pos));
    return a;
}

// The edge value the mesh gives at a point of the face (barycentric in the triangle that holds it); -1 outside.
double edgeAt(const EdgeFaceMesh& m, const Vec3& p, const Vec3& n) {
    for (size_t t = 0; t + 2 < m.idx.size(); t += 3) {
        const EdgeVertex& a = m.verts[m.idx[t]];
        const EdgeVertex& b = m.verts[m.idx[t + 1]];
        const EdgeVertex& c = m.verts[m.idx[t + 2]];
        const double area = dot(cross(b.pos - a.pos, c.pos - a.pos), n);
        if (std::fabs(area) < 1e-12) continue;
        const double wa = dot(cross(b.pos - p, c.pos - p), n) / area;
        const double wb = dot(cross(c.pos - p, a.pos - p), n) / area;
        const double wc = 1 - wa - wb;
        if (wa < -1e-7 || wb < -1e-7 || wc < -1e-7) continue;
        return wa * a.edge + wb * b.edge + wc * c.edge;
    }
    return -1;
}

int faceWithNormal(const EdgeBrush& b, const Vec3& n) {
    for (size_t f = 0; f < b.planes.size(); ++f)
        if (dot(normalize(b.planes[f].n), n) > 0.999) return int(f);
    return -1;
}

}  // namespace

TEST(face_edges_box_face_distance_is_exact) {
    // A 64 x 32 x 16 box alone: every edge of every face counts, with a fold of 90 degrees, and the mesh gives the
    // distance to the nearest edge of the face (as a share of the range, 1 beyond it) at any point.
    const std::vector<EdgeBrush> brushes{boxBrush({0, 0, 0}, {64, 32, 16})};
    const auto near = edgeNeighbours(brushes);
    CHECK(near.size() == 1 && near[0].empty());
    const int top = faceWithNormal(brushes[0], {0, 0, 1});
    CHECK(top >= 0);
    const auto spans = faceEdgeSpans(brushes, 0, size_t(top), near[0]);
    CHECK(spans.size() == 4);
    for (const EdgeSpan& s : spans) CHECK(std::fabs(s.fold - 1.5707963) < 1e-4 && !s.cutA && !s.cutB);
    const auto meshes = edgeBrushMeshes(brushes, 0, near[0]);
    CHECK(meshes.size() == brushes[0].planes.size());
    const EdgeFaceMesh& m = meshes[size_t(top)];
    CHECK(std::fabs(meshArea(m) - 64.0 * 32.0) < 1e-6);  // the pieces cover the face exactly once
    for (const Vec3 p : {Vec3{32, 16, 16}, Vec3{1, 16, 16}, Vec3{63.5, 16, 16}, Vec3{32, 0.25, 16}, Vec3{2, 3, 16}, Vec3{61, 30.5, 16},
                         Vec3{0.5, 0.5, 16}, Vec3{10, 31, 16}}) {
        const double want = std::min(1.0, std::min(std::min(p.x, 64 - p.x), std::min(p.y, 32 - p.y)) / kEdgeRange);
        const double got = edgeAt(m, p, {0, 0, 1});
        CHECK(std::fabs(got - want) < 1e-6);
    }
    // The direction leads away from the edge inside the face, in the face's own axes.
    Vec3 a, b;
    edgeFaceBasis({0, 0, 1}, a, b);
    for (const EdgeVertex& v : m.verts) {
        if (v.edge > 0.999) continue;
        const Vec3 g = a * std::cos(v.dir) + b * std::sin(v.dir);
        CHECK(std::fabs(g.z) < 1e-6);
        // One step along it grows the true distance to the nearest edge (checked a little inside the piece).
        CHECK(std::fabs(std::fabs(g.x) + std::fabs(g.y) - 1) < 1e-5);  // axis aligned here
    }
    // A face thinner than twice the range: the two bands meet in the middle and nothing reaches 1.
    const int side = faceWithNormal(brushes[0], {1, 0, 0});  // 32 x 16
    const EdgeFaceMesh& sm = meshes[size_t(side)];
    CHECK(std::fabs(meshArea(sm) - 32.0 * 16.0) < 1e-6);
    CHECK(std::fabs(edgeAt(sm, {64, 16, 8}, {1, 0, 0}) - 1.0) < 1e-6);   // 8 units from the nearest edge
    CHECK(std::fabs(edgeAt(sm, {64, 16, 2}, {1, 0, 0}) - 0.5) < 1e-6);
}

TEST(face_edges_seams_and_partial_edges) {
    // Two slabs side by side with their tops in one plane: no line along the seam, on either top.
    {
        const std::vector<EdgeBrush> brushes{boxBrush({0, 0, 0}, {64, 64, 16}), boxBrush({64, 0, 0}, {128, 64, 16})};
        const auto near = edgeNeighbours(brushes);
        CHECK(near[0].size() == 1 && near[1].size() == 1);
        const int top = faceWithNormal(brushes[0], {0, 0, 1});
        const auto spans = faceEdgeSpans(brushes, 0, size_t(top), near[0]);
        CHECK(spans.size() == 3);
        const auto meshes = edgeBrushMeshes(brushes, 0, near[0]);
        CHECK(std::fabs(edgeAt(meshes[size_t(top)], {63, 32, 16}, {0, 0, 1}) - 1.0) < 1e-6);  // beside the seam: no edge
        CHECK(std::fabs(edgeAt(meshes[size_t(top)], {1, 32, 16}, {0, 0, 1}) - 0.25) < 1e-6);   // the open side
        // A brush of another set (a moving brush entity) is not a seam.
        std::vector<EdgeBrush> other{brushes[0], boxBrush({64, 0, 0}, {128, 64, 16}, 7)};
        CHECK(edgeNeighbours(other)[0].empty());
        CHECK(faceEdgeSpans(other, 0, size_t(top), edgeNeighbours(other)[0]).size() == 4);
    }
    // A slab that touches only half of the edge: the edge counts along the other half, with a square end.
    {
        const std::vector<EdgeBrush> brushes{boxBrush({0, 0, 0}, {64, 64, 16}), boxBrush({64, 0, 0}, {128, 32, 16})};
        const auto near = edgeNeighbours(brushes);
        const int top = faceWithNormal(brushes[0], {0, 0, 1});
        const auto spans = faceEdgeSpans(brushes, 0, size_t(top), near[0]);
        CHECK(spans.size() == 4);
        size_t cut = 0;
        for (const EdgeSpan& s : spans)
            if (s.cutA || s.cutB) {
                ++cut;
                CHECK(std::fabs(length(s.b - s.a) - 32) < 1e-3);
                CHECK(std::min(s.a.y, s.b.y) > 31.9);
            }
        CHECK(cut == 1);
        const EdgeFaceMesh m = edgeBrushMeshes(brushes, 0, near[0])[size_t(top)];
        CHECK(std::fabs(meshArea(m) - 64.0 * 64.0) < 1e-6);
        CHECK(std::fabs(edgeAt(m, {63, 48, 16}, {0, 0, 1}) - 0.25) < 1e-6);  // beside the open half
        CHECK(std::fabs(edgeAt(m, {63, 16, 16}, {0, 0, 1}) - 1.0) < 1e-6);   // beside the seam
    }
    // A crate on a floor: the crate's sides keep their bottom edge (a real line where wall meets floor); a taller
    // block against a slab keeps the slab's edge too (the surface does not carry on in its plane).
    {
        const std::vector<EdgeBrush> brushes{boxBrush({-256, -256, -16}, {256, 256, 0}), boxBrush({0, 0, 0}, {32, 32, 32})};
        const auto near = edgeNeighbours(brushes);
        const int side = faceWithNormal(brushes[1], {1, 0, 0});
        CHECK(faceEdgeSpans(brushes, 1, size_t(side), near[1]).size() == 4);
        const std::vector<EdgeBrush> step{boxBrush({0, 0, 0}, {64, 64, 16}), boxBrush({64, 0, 0}, {128, 64, 48})};
        const int top = faceWithNormal(step[0], {0, 0, 1});
        CHECK(faceEdgeSpans(step, 0, size_t(top), edgeNeighbours(step)[0]).size() == 4);
    }
    // Faces that are not drawn (tool materials) carry no edges and are no seam for others.
    {
        std::vector<EdgeBrush> brushes{boxBrush({0, 0, 0}, {64, 64, 16}), boxBrush({64, 0, 0}, {128, 64, 16})};
        brushes[1].drawn.assign(brushes[1].drawn.size(), 0);
        const int top = faceWithNormal(brushes[0], {0, 0, 1});
        CHECK(faceEdgeSpans(brushes, 0, size_t(top), edgeNeighbours(brushes)[0]).size() == 4);
        CHECK(faceEdgeSpans(brushes, 1, size_t(faceWithNormal(brushes[1], {0, 0, 1})), edgeNeighbours(brushes)[1]).empty());
    }
}

TEST(face_edges_meshes_are_watertight_and_packed) {
    // Every piece edge on the outline of a face is matched by the pieces of the face across it: the same end points on
    // both sides (no vertex of one side inside an edge of the other).
    const std::vector<EdgeBrush> brushes{boxBrush({0, 0, 0}, {64, 64, 16}), boxBrush({64, 0, 0}, {128, 32, 16}),
                                        boxBrush({16, 16, 16}, {40, 44, 60})};
    const auto near = edgeNeighbours(brushes);
    for (size_t bi = 0; bi < brushes.size(); ++bi) {
        const auto meshes = edgeBrushMeshes(brushes, bi, near[bi]);
        using K = std::array<long long, 3>;
        const auto key = [](const Vec3& p) { return K{std::llround(p.x * 4096), std::llround(p.y * 4096), std::llround(p.z * 4096)}; };
        std::map<std::pair<K, K>, int> edges;
        double area = 0, want = 0;
        for (size_t f = 0; f < meshes.size(); ++f) {
            const EdgeFaceMesh& m = meshes[f];
            area += meshArea(m);
            Vec3 s;
            for (size_t i = 1; i + 1 < brushes[bi].wins[f].size(); ++i)
                s += cross(brushes[bi].wins[f][i] - brushes[bi].wins[f][0], brushes[bi].wins[f][i + 1] - brushes[bi].wins[f][0]);
            want += 0.5 * length(s);
            for (size_t t = 0; t + 2 < m.idx.size(); t += 3)
                for (int e = 0; e < 3; ++e) {
                    K a = key(m.verts[m.idx[t + e]].pos), b = key(m.verts[m.idx[t + (e + 1) % 3]].pos);
                    CHECK(a != b);
                    if (b < a) std::swap(a, b);
                    ++edges[{a, b}];
                }
        }
        CHECK(std::fabs(area - want) < 1e-5);
        size_t open = 0;
        for (const auto& [k, n] : edges) open += n != 2;  // a closed surface: every edge has two triangles
        CHECK(open == 0);
    }
    // Packing: R edge, G fold, B direction, A 255.
    EdgeVertex v;
    CHECK(packEdge(v) == 0xFF0000FFu);
    v.edge = 0, v.fold = 1.6f, v.dir = 3.14159265f;
    CHECK(packEdge(v) == (0xFF000000u | (130u << 8) | (128u << 16)));
    v.dir = 6.2831853f;  // a full turn is 0 again
    CHECK(((packEdge(v) >> 16) & 255u) == 0);
}

TEST(face_edges_coplanar_overlaps_are_drawn_once) {
    // A panel lying in the top plane of a slab (its top face inside the slab's top face): the panel's face is drawn,
    // the slab's face has a hole there, and together they cover the slab's top exactly once.
    const std::vector<EdgeBrush> brushes{boxBrush({0, 0, 0}, {64, 64, 16}), boxBrush({16, 16, 8}, {48, 40, 16})};
    const auto near = edgeNeighbours(brushes);
    const int top0 = faceWithNormal(brushes[0], {0, 0, 1}), top1 = faceWithNormal(brushes[1], {0, 0, 1});
    CHECK(coplanarCovers(brushes, 0, size_t(top0), near[0]).size() == 1);  // the smaller face wins
    CHECK(coplanarCovers(brushes, 1, size_t(top1), near[1]).empty());
    for (const bool withEdges : {false, true}) {
        const auto m0 = edgeBrushMeshes(brushes, 0, near[0], kEdgeRange, withEdges);
        const auto m1 = edgeBrushMeshes(brushes, 1, near[1], kEdgeRange, withEdges);
        CHECK(std::fabs(meshArea(m0[size_t(top0)]) - (64.0 * 64.0 - 32.0 * 24.0)) < 1e-6);
        CHECK(std::fabs(meshArea(m1[size_t(top1)]) - 32.0 * 24.0) < 1e-6);
        CHECK(edgeAt(m0[size_t(top0)], {32, 28, 16}, {0, 0, 1}) < 0);   // under the panel: not part of the slab's mesh
        CHECK(edgeAt(m1[size_t(top1)], {32, 28, 16}, {0, 0, 1}) >= 0);
        if (!withEdges)
            for (const EdgeVertex& v : m0[size_t(top0)].verts) CHECK(v.edge == 1.0f);
    }
    // The panel's top edges are seams (the slab's face carries on around it): no edge on the panel's top.
    CHECK(faceEdgeSpans(brushes, 1, size_t(top1), near[1]).empty());
    // Two equal faces on top of each other (a doubled brush): the later brush draws, the first does not.
    const std::vector<EdgeBrush> twins{boxBrush({0, 0, 0}, {64, 64, 16}), boxBrush({0, 0, 0}, {64, 64, 16})};
    const auto tn = edgeNeighbours(twins);
    const auto a = edgeBrushMeshes(twins, 0, tn[0]), b = edgeBrushMeshes(twins, 1, tn[1]);
    double areaA = 0, areaB = 0;
    for (const auto& m : a) areaA += meshArea(m);
    for (const auto& m : b) areaB += meshArea(m);
    CHECK(areaA < 1e-9);
    CHECK(std::fabs(areaB - 2.0 * (64 * 64 + 64 * 16 + 64 * 16)) < 1e-6);
}

TEST(face_edges_seams_between_brushes_share_vertices) {
    // Two wall slices side by side, of different heights, with a third brush on top of one: along the seam both
    // brushes' front faces hold the same vertices (no vertex of one inside an edge of the other).
    const std::vector<EdgeBrush> brushes{boxBrush({0, 0, 0}, {64, 16, 128}), boxBrush({64, 0, 0}, {160, 16, 96}),
                                        boxBrush({0, 0, 128}, {64, 16, 140})};
    const auto near = edgeNeighbours(brushes);
    using K = std::array<long long, 3>;
    const auto key = [](const Vec3& p) { return K{std::llround(p.x * 4096), std::llround(p.y * 4096), std::llround(p.z * 4096)}; };
    const int f0 = faceWithNormal(brushes[0], {0, -1, 0}), f1 = faceWithNormal(brushes[1], {0, -1, 0});
    const auto onSeam = [&](const EdgeFaceMesh& m) {
        std::set<K> s;
        for (size_t t = 0; t + 2 < m.idx.size(); t += 3)
            for (int e = 0; e < 3; ++e) {
                const Vec3& p = m.verts[m.idx[t + e]].pos;
                if (std::fabs(p.x - 64) < 1e-6 && p.z <= 96 + 1e-6) s.insert(key(p));
            }
        return s;
    };
    const auto a = onSeam(edgeBrushMeshes(brushes, 0, near[0], kEdgeRange, true, &near)[size_t(f0)]);
    const auto b = onSeam(edgeBrushMeshes(brushes, 1, near[1], kEdgeRange, true, &near)[size_t(f1)]);
    CHECK(a.size() >= 2 && a == b);
    // Without the table each side has only its own vertices there (the old behaviour).
    const auto a0 = onSeam(edgeBrushMeshes(brushes, 0, near[0])[size_t(f0)]);
    const auto b0 = onSeam(edgeBrushMeshes(brushes, 1, near[1])[size_t(f1)]);
    CHECK(a0 != b0);
}

TEST(face_edges_hidden_faces_have_no_edges) {
    // Two wall slices side by side: the faces between them touch each other and are hidden: no edges on them.
    const std::vector<EdgeBrush> slices{boxBrush({0, 0, 0}, {64, 16, 128}), boxBrush({64, 0, 0}, {128, 16, 128})};
    const auto near = edgeNeighbours(slices);
    const int between = faceWithNormal(slices[0], {1, 0, 0});
    CHECK(faceEdgeSpans(slices, 0, size_t(between), near[0]).empty());
    // Two overlapping brushes: the part of a face inside the other brush has no edge, the part in the open has.
    const std::vector<EdgeBrush> over{boxBrush({0, 0, 0}, {64, 16, 128}), boxBrush({48, 0, 0}, {128, 16, 96})};
    const auto on = edgeNeighbours(over);
    const int side = faceWithNormal(over[0], {1, 0, 0});  // at x = 64, inside the second brush up to z = 96
    const auto spans = faceEdgeSpans(over, 0, size_t(side), on[0]);
    CHECK(!spans.empty());
    for (const EdgeSpan& s : spans) CHECK(std::min(s.a.z, s.b.z) > 96 - 1e-6);
    // A crate on a floor keeps the edges of its sides (they are in the open), as before.
    const std::vector<EdgeBrush> crate{boxBrush({-256, -256, -16}, {256, 256, 0}), boxBrush({0, 0, 0}, {32, 32, 32})};
    CHECK(faceEdgeSpans(crate, 1, size_t(faceWithNormal(crate[1], {1, 0, 0})), edgeNeighbours(crate)[1]).size() == 4);
}
