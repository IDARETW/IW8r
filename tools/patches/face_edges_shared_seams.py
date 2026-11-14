import io

# Watertight seams between brushes: the face meshes of touching brushes get each other's vertices along shared edges
# (dark pixels showed at the slices of a wall: the hidden side face behind a hairline crack). Hosts pass the whole
# neighbour table; the viewport's cache key covers the neighbours' neighbours, whose changes move those vertices.


def patch(p, a, b, n=1):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


H = 'E:/IW8-Radiant/src/core/face_edges.h'
patch(H, '''std::vector<EdgeFaceMesh> edgeBrushMeshes(const std::vector<EdgeBrush>& brushes, size_t self,
                                        const std::vector<uint32_t>& neighbours, double range = kEdgeRange, bool withEdges = true);''',
      '''// allNear (edgeNeighbours of `brushes`): when given, the vertices the touching brushes' own pieces put on this brush's
// edges are added here too, so the seam between two brushes is as tight as the edges inside one (both sides then hold
// the same vertices along it). The result then depends on the neighbours and on their neighbours.
std::vector<EdgeFaceMesh> edgeBrushMeshes(const std::vector<EdgeBrush>& brushes, size_t self,
                                        const std::vector<uint32_t>& neighbours, double range = kEdgeRange, bool withEdges = true,
                                        const std::vector<std::vector<uint32_t>>* allNear = nullptr);''')

C = 'E:/IW8-Radiant/src/core/face_edges.cpp'
patch(C, '''std::vector<EdgeFaceMesh> edgeBrushMeshes(const std::vector<EdgeBrush>& brushes, size_t self,
                                        const std::vector<uint32_t>& neighbours, double range, bool withEdges) {''',
      '''namespace {

// The pieces of one face before welding: its spans' bands, without the parts under coplanar faces of other brushes.
std::vector<Piece> brushFacePieces(const std::vector<EdgeBrush>& brushes, size_t self, size_t f, const std::vector<uint32_t>& neighbours,
                                   double range, bool withEdges, std::vector<Field>& fields, std::vector<Winding>* coversOut) {
    const EdgeBrush& B = brushes[self];
    const std::vector<EdgeSpan> spans = withEdges ? faceEdgeSpans(brushes, self, f, neighbours) : std::vector<EdgeSpan>();
    const Vec3 n = normalize(B.planes[f].n);
    std::vector<Piece> pieces = facePieces(B.wins[f], n, spans, range, fields);
    const std::vector<Winding> covers = coplanarCovers(brushes, self, f, neighbours);
    for (const Winding& cover : covers) pieces = withoutCover(std::move(pieces), cover, n);
    if (coversOut) *coversOut = covers;
    return pieces;
}

}  // namespace

std::vector<EdgeFaceMesh> edgeBrushMeshes(const std::vector<EdgeBrush>& brushes, size_t self,
                                        const std::vector<uint32_t>& neighbours, double range, bool withEdges,
                                        const std::vector<std::vector<uint32_t>>* allNear) {''')
patch(C, '''        const std::vector<EdgeSpan> spans = withEdges ? faceEdgeSpans(brushes, self, f, neighbours) : std::vector<EdgeSpan>();
        const Vec3 n = normalize(B.planes[f].n);
        std::vector<Piece> pieces = facePieces(B.wins[f], n, spans, range, fields[f]);
        // Coplanar faces of other brushes drawn instead of this one where they lie on it.
        const std::vector<Winding> covers = coplanarCovers(brushes, self, f, neighbours);
        for (const Winding& cover : covers) pieces = withoutCover(std::move(pieces), cover, n);
        for (const Piece& p : pieces) w.addPiece(p, f);''', '''        std::vector<Winding> covers;
        const std::vector<Piece> pieces = brushFacePieces(brushes, self, f, neighbours, range, withEdges, fields[f], &covers);
        for (const Piece& p : pieces) w.addPiece(p, f);''')
patch(C, '''    w.fixJunctions();
    for (const auto& q : w.polys) triangulate(w, q, fields[q.face], range, out[q.face]);
    return out;
}

void edgeFaceBasis''', '''    // The touching brushes' own piece corners, where they lie on this brush: fixJunctions puts them on the edges they
    // sit on, and they put this brush's on theirs.
    if (allNear && !w.polys.empty()) {
        AABB mine = B.bounds;
        mine.mins = mine.mins - Vec3{kWeld, kWeld, kWeld};
        mine.maxs = mine.maxs + Vec3{kWeld, kWeld, kWeld};
        std::vector<Field> scratch;
        for (uint32_t ni : neighbours) {
            if (ni == self || ni >= brushes.size() || ni >= allNear->size() || brushes[ni].set != B.set) continue;
            const EdgeBrush& N = brushes[ni];
            for (size_t f = 0; f < N.wins.size() && f < N.planes.size(); ++f) {
                if (N.wins[f].size() < 3) continue;
                for (const Piece& p : brushFacePieces(brushes, ni, f, (*allNear)[ni], range, withEdges, scratch, nullptr))
                    for (const Vec3& v : p.poly)
                        if (mine.contains(v)) w.add(v);
            }
        }
    }
    w.fixJunctions();
    for (const auto& q : w.polys) triangulate(w, q, fields[q.face], range, out[q.face]);
    return out;
}

void edgeFaceBasis''')

patch('E:/IW8-Radiant/src/core/compile_iw8.cpp', '''            edgeMeshes = edgeBrushMeshes(g.edges->brushes, it->second, g.edges->near[it->second], kEdgeRange, g.edgeColours);''',
      '''            edgeMeshes = edgeBrushMeshes(g.edges->brushes, it->second, g.edges->near[it->second], kEdgeRange, g.edgeColours, &g.edges->near);''')
S = 'E:/IW8-Radiant/src/viewport/scene.cpp'
patch(S, '''        g.polyEdges = edgeBrushMeshes(brushes, i, near[i], kEdgeRange, faceEdges);''',
      '''        g.polyEdges = edgeBrushMeshes(brushes, i, near[i], kEdgeRange, faceEdges, &near);''')
patch(S, '''        for (uint32_t n : near[i]) {
            key = (key ^ uint64_t(items[n]->ref.item)) * 1099511628211ull;
            key = (key ^ items[n]->rev) * 1099511628211ull;
        }''', '''        // ... and the neighbours' neighbours: they shape the neighbours' pieces, whose corners this brush's seams share.
        for (uint32_t n : near[i]) {
            key = (key ^ uint64_t(items[n]->ref.item)) * 1099511628211ull;
            key = (key ^ items[n]->rev) * 1099511628211ull;
            for (uint32_t m : near[n]) {
                key = (key ^ uint64_t(items[m]->ref.item)) * 1099511628211ull;
                key = (key ^ items[m]->rev) * 1099511628211ull;
            }
        }''')
print('ok')
