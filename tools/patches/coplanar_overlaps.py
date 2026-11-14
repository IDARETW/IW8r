import io

# Coplanar overlapping faces (owner 2026-11-13: z-fighting on the square structures of Cinderrail: 2,636 pairs of brush
# faces lying in one plane on top of each other). Where two drawn faces of one set overlap in the same plane facing the
# same way, exactly one is drawn there: the smaller face (a panel on a wall), the later brush when they are the same
# size; the other is cut away under it. Core face_edges does it for the viewport and the build alike, always (with or
# without edge data).


def patch(p, a, b, n=1):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


H = 'E:/IW8-Radiant/src/core/face_edges.h'
patch(H, '''// Every face of brushes[self]: meshes[face] (empty for a face without a winding). Vertices the pieces of one face put
// on an edge are added to the other pieces along it, across the brush's faces too, so no seam can open between them.
std::vector<EdgeFaceMesh> edgeBrushMeshes(const std::vector<EdgeBrush>& brushes, size_t self,
                                        const std::vector<uint32_t>& neighbours, double range = kEdgeRange);''',
      '''// The faces of other brushes that are drawn instead of face `face` of brushes[self] where they lie on it: drawn faces
// of the same set in the same plane, facing the same way, that overlap it and win (the smaller face; of two equal ones
// the later brush). Without this both would be drawn in one plane and fight for every pixel.
std::vector<Winding> coplanarCovers(const std::vector<EdgeBrush>& brushes, size_t self, size_t face,
                                    const std::vector<uint32_t>& neighbours);

// Every face of brushes[self]: meshes[face] (empty for a face without a winding, or wholly under coplanar faces of
// other brushes: coplanarCovers). Vertices the pieces of one face put on an edge are added to the other pieces along
// it, across the brush's faces too, so no seam can open between them. withEdges false: no edge data (edge 1
// everywhere), only the faces themselves with the covered parts cut away.
std::vector<EdgeFaceMesh> edgeBrushMeshes(const std::vector<EdgeBrush>& brushes, size_t self,
                                        const std::vector<uint32_t>& neighbours, double range = kEdgeRange, bool withEdges = true);''')

C = 'E:/IW8-Radiant/src/core/face_edges.cpp'
patch(C, '''EdgeFaceMesh edgeFaceMesh(const Winding& face, const Vec3& normal, const std::vector<EdgeSpan>& spans, double range) {''',
      '''std::vector<Winding> coplanarCovers(const std::vector<EdgeBrush>& brushes, size_t self, size_t face,
                                    const std::vector<uint32_t>& neighbours) {
    std::vector<Winding> out;
    if (self >= brushes.size()) return out;
    const EdgeBrush& B = brushes[self];
    if (face >= B.wins.size() || face >= B.planes.size() || B.wins[face].size() < 3) return out;
    if (face < B.drawn.size() && !B.drawn[face]) return out;
    const Winding& A = B.wins[face];
    const Vec3 nA = normalize(B.planes[face].n);
    const double dA = B.planes[face].d / std::max(1e-12, length(B.planes[face].n));
    const double areaA = polyArea(A);
    AABB ab;
    for (const Vec3& p : A) ab.add(p);
    for (uint32_t ci : neighbours) {
        if (ci == self || ci >= brushes.size()) continue;
        const EdgeBrush& C = brushes[ci];
        if (C.set != B.set) continue;
        if (C.bounds.mins.x > ab.maxs.x || C.bounds.maxs.x < ab.mins.x || C.bounds.mins.y > ab.maxs.y || C.bounds.maxs.y < ab.mins.y ||
            C.bounds.mins.z > ab.maxs.z || C.bounds.maxs.z < ab.mins.z)
            continue;
        for (size_t g = 0; g < C.planes.size() && g < C.wins.size(); ++g) {
            const Winding& F = C.wins[g];
            if (F.size() < 3 || (g < C.drawn.size() && !C.drawn[g])) continue;
            const Vec3 nF = normalize(C.planes[g].n);
            if (dot(nF, nA) < 0.99999) continue;
            if (std::fabs(C.planes[g].d / std::max(1e-12, length(C.planes[g].n)) - dA) > 0.02) continue;
            // Who is drawn where they overlap: the smaller face, the later brush of two equal ones.
            const double areaF = polyArea(F);
            const double tol = 1e-6 * std::max(1.0, std::max(areaA, areaF));
            const bool wins = areaF < areaA - tol || (std::fabs(areaF - areaA) <= tol && ci > self);
            if (!wins) continue;
            // Do they overlap at all (more than touching)?
            Winding common = A;
            Vec3 fc;
            for (const Vec3& p : F) fc += p;
            fc = fc * (1.0 / double(F.size()));
            for (size_t k = 0; k < F.size() && !common.empty(); ++k) {
                Vec3 m = cross(F[(k + 1) % F.size()] - F[k], nF);
                if (length(m) < 1e-12) continue;
                m = normalize(m);
                if (dot(m, fc - F[k]) > 0) m = m * -1.0;  // outward of F in the plane
                common = clipKeep(common, m, dot(m, F[k]));
            }
            if (common.empty() || polyArea(common) < 0.05) continue;
            out.push_back(F);
        }
    }
    return out;
}

namespace {

// The pieces without the part under a convex polygon of their plane.
std::vector<Piece> withoutCover(std::vector<Piece> pieces, const Winding& cover, const Vec3& n) {
    Vec3 cc;
    for (const Vec3& p : cover) cc += p;
    cc = cc * (1.0 / double(cover.size()));
    std::vector<Piece> out;
    for (Piece& pc : pieces) {
        Winding cur = std::move(pc.poly);
        for (size_t k = 0; k < cover.size() && !cur.empty(); ++k) {
            Vec3 m = cross(cover[(k + 1) % cover.size()] - cover[k], n);
            if (length(m) < 1e-12) continue;
            m = normalize(m);
            if (dot(m, cc - cover[k]) > 0) m = m * -1.0;
            const double c = dot(m, cover[k]);
            Winding outside = clipKeep(cur, m * -1.0, -c);
            if (!outside.empty()) out.push_back({std::move(outside), pc.span});
            cur = clipKeep(cur, m, c);
        }
        // What is left of cur lies under the cover: not drawn.
    }
    return out;
}

}  // namespace

EdgeFaceMesh edgeFaceMesh(const Winding& face, const Vec3& normal, const std::vector<EdgeSpan>& spans, double range) {''')
patch(C, '''std::vector<EdgeFaceMesh> edgeBrushMeshes(const std::vector<EdgeBrush>& brushes, size_t self,
                                        const std::vector<uint32_t>& neighbours, double range) {''',
      '''std::vector<EdgeFaceMesh> edgeBrushMeshes(const std::vector<EdgeBrush>& brushes, size_t self,
                                        const std::vector<uint32_t>& neighbours, double range, bool withEdges) {''')
patch(C, '''        const std::vector<EdgeSpan> spans = faceEdgeSpans(brushes, self, f, neighbours);
        for (const Piece& p : facePieces(B.wins[f], normalize(B.planes[f].n), spans, range, fields[f])) w.addPiece(p, f);
    }''', '''        const std::vector<EdgeSpan> spans = withEdges ? faceEdgeSpans(brushes, self, f, neighbours) : std::vector<EdgeSpan>();
        const Vec3 n = normalize(B.planes[f].n);
        std::vector<Piece> pieces = facePieces(B.wins[f], n, spans, range, fields[f]);
        // Coplanar faces of other brushes drawn instead of this one where they lie on it.
        const std::vector<Winding> covers = coplanarCovers(brushes, self, f, neighbours);
        for (const Winding& cover : covers) pieces = withoutCover(std::move(pieces), cover, n);
        for (const Piece& p : pieces) w.addPiece(p, f);
        // The covers' corners become vertices of the pieces' edges they lie on (fixJunctions): no gap beside them.
        if (!pieces.empty())
            for (const Winding& cover : covers)
                for (const Vec3& p : cover) w.add(p);
    }''')
print('ok')
