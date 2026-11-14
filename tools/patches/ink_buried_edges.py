import io

# Ink outlines: skip edges that are not a visible crease, where a face next to the edge lies against another brush
# (slab-to-slab seams, wall bottoms on floors, edges buried in neighbours). Cinderrail: 21023 strips before.


def patch(p, a, b, n=1):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


H = 'E:/IW8-Radiant/src/core/toon_outline.h'
patch(H, '''    Vec3 bisector;  // the mean of the two faces' outward normals (the strip's orientation)
};''', '''    Vec3 bisector;  // the mean of the two faces' outward normals (the strip's orientation)
    Vec3 n1, n2;    // the two faces' outward normals
};''')
patch(H, '''    bool skipHiddenFaces = true;
};''', '''    bool skipHiddenFaces = true;
    // uniqueInkEdges: an edge is inked only when both faces are open to the air beside it (a point just off each face,
    // next to the edge, is inside no other drawn brush): seams between flush brushes and edges against or inside
    // neighbours are not creases anyone sees.
    bool skipBuried = true;
};''')
patch(H, '''// The edges of many brushes with the duplicates (the same segment from two touching brushes, either direction)
// removed.''', '''// The edges of many brushes with the duplicates (the same segment from two touching brushes, either direction)
// removed, and (skipBuried) the edges that are not a visible crease.''')

C = 'E:/IW8-Radiant/src/core/toon_outline.cpp'
patch(C, '''                out.push_back({it->second.second.first, it->second.second.second, bis});''',
      '''                out.push_back({it->second.second.first, it->second.second.second, bis, n1, n2});''')
patch(C, '''std::vector<InkEdge> uniqueInkEdges(const std::vector<ConvexBrush>& brushes, const InkOptions& opt) {
    std::vector<InkEdge> out;''', '''std::vector<InkEdge> uniqueInkEdges(const std::vector<ConvexBrush>& brushes, const InkOptions& opt) {
    std::vector<InkEdge> out;
    // The solids that can cover a face: drawn brushes (any face not a tool material), with their bounds.
    std::vector<std::pair<AABB, const ConvexBrush*>> solids;
    if (opt.skipBuried)
        for (const ConvexBrush& b : brushes) {
            bool drawn = false;
            for (const auto& f : b) drawn |= !isIw8ToolMaterial(inkFaceMaterial(f.suffix));
            if (drawn && isValidBrush(b)) solids.push_back({brushBounds(b), &b});
        }
    const auto inside = [&](const Vec3& p) {
        for (const auto& [box, b] : solids) {
            if (p.x < box.mins.x || p.y < box.mins.y || p.z < box.mins.z || p.x > box.maxs.x || p.y > box.maxs.y ||
                p.z > box.maxs.z)
                continue;
            bool in = true;
            for (const auto& f : *b)
                if (dot(f.plane.n, p) - f.plane.d > -1e-3) {
                    in = false;
                    break;
                }
            if (in) return true;
        }
        return false;
    };
    // Points a little off each face beside the edge, at a quarter and three quarters of it (a short neighbour may
    // cover only part of a long edge: the edge is kept when either face is open at either point).
    const auto visible = [&](const InkEdge& e) {
        constexpr double eps = 0.5;
        for (double t : {0.25, 0.5, 0.75}) {
            const Vec3 m = e.a + (e.b - e.a) * t;
            if (!inside(m + e.n1 * eps - e.n2 * eps) && !inside(m + e.n2 * eps - e.n1 * eps)) return true;
        }
        return false;
    };''')
patch(C, '''            if (seen.emplace(k, true).second) out.push_back(e);''',
      '''            if (seen.emplace(k, true).second && (!opt.skipBuried || visible(e))) out.push_back(e);''')
print('ok')
