import io

# Coplanar overlaps in the hosts: the build and the viewport always draw brush faces as their face meshes (core
# face_edges edgeBrushMeshes), with edge data only when a shader reads it. A face wholly under other faces is not drawn.


def patch(p, a, b, n=1):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


K = 'E:/IW8-Radiant/src/core/compile_iw8.cpp'
patch(K, '''    const EdgeData* edges = nullptr;  // set: brush faces are written as their edge meshes, the data in the colour
    size_t paintedPatches = 0;        // patches whose painted colours were left out because the colour holds edge data''',
      '''    const EdgeData* edges = nullptr;  // set: brush faces are written as their face meshes (coplanar overlaps cut away)
    bool edgeColours = false;         // ... with the edge data in the vertex colour (a surface shader reads it)
    size_t coveredFaces = 0;          // faces not written: wholly under coplanar faces of other brushes
    size_t paintedPatches = 0;        // patches whose painted colours were left out because the colour holds edge data''')
patch(K, '''    std::vector<EdgeFaceMesh> edgeMeshes;
    if (g.edges)
        if (const auto it = g.edges->index.find(&p); it != g.edges->index.end())
            edgeMeshes = edgeBrushMeshes(g.edges->brushes, it->second, g.edges->near[it->second]);
''', '''    std::vector<EdgeFaceMesh> edgeMeshes;
    bool haveMeshes = false;
    if (g.edges)
        if (const auto it = g.edges->index.find(&p); it != g.edges->index.end()) {
            edgeMeshes = edgeBrushMeshes(g.edges->brushes, it->second, g.edges->near[it->second], kEdgeRange, g.edgeColours);
            haveMeshes = true;
        }
''')
patch(K, '''        if (fi < edgeMeshes.size() && !edgeMeshes[fi].idx.empty()) {
            for (const EdgeVertex& q : edgeMeshes[fi].verts) {
                const auto uv = faceUV(n, tp, q.pos);
                c.verts.push_back({q.pos, n, faceTan, uv[0], uv[1]});
                const uint32_t packed = packEdge(q);
                c.verts.back().hasColor = true;
                c.verts.back().color = {std::uint8_t(packed), std::uint8_t(packed >> 8), std::uint8_t(packed >> 16), std::uint8_t(packed >> 24)};
                g.bounds.add(q.pos);
            }''', '''        if (haveMeshes && fi < edgeMeshes.size()) {
            if (edgeMeshes[fi].idx.empty()) {
                ++g.coveredFaces;  // wholly under coplanar faces of other brushes
                continue;
            }
            for (const EdgeVertex& q : edgeMeshes[fi].verts) {
                const auto uv = faceUV(n, tp, q.pos);
                c.verts.push_back({q.pos, n, faceTan, uv[0], uv[1]});
                if (g.edgeColours) {
                    const uint32_t packed = packEdge(q);
                    c.verts.back().hasColor = true;
                    c.verts.back().color = {std::uint8_t(packed), std::uint8_t(packed >> 8), std::uint8_t(packed >> 16), std::uint8_t(packed >> 24)};
                }
                g.bounds.add(q.pos);
            }''')
patch(K, '''    if (g.edges && patchHasColours(p)) ++g.paintedPatches;
    const std::vector<Rgba8> colours = patchHasColours(p) && !g.edges ? tessellatePatchColours(p, g.subdiv) : std::vector<Rgba8>{};''',
      '''    if (g.edgeColours && patchHasColours(p)) ++g.paintedPatches;
    const std::vector<Rgba8> colours = patchHasColours(p) && !g.edgeColours ? tessellatePatchColours(p, g.subdiv) : std::vector<Rgba8>{};''')
patch(K, '''    const bool surfaceEdges = haveSurfaceShader && customShaderUsesEdges(surfaceShader.code);
    if (surfaceEdges) {
        for (const auto& ee : map.entities) {''', '''    const bool surfaceEdges = haveSurfaceShader && customShaderUsesEdges(surfaceShader.code);
    {
        // Always: the static world's brush faces go through core face_edges, which also cuts away the part of a face
        // that lies under a coplanar face of another brush (two faces in one plane fight for every pixel in game).
        for (const auto& ee : map.entities) {''')
patch(K, '''        edgeData.near = edgeNeighbours(edgeData.brushes);
        g.edges = &edgeData;
    }''', '''        edgeData.near = edgeNeighbours(edgeData.brushes);
        g.edges = &edgeData;
        g.edgeColours = surfaceEdges;
    }''')
patch(K, '''    } else if (!surfaceShaderError.empty()) {
        warn.push_back("surface shader not used: " + surfaceShaderError);
    }''', '''    } else if (!surfaceShaderError.empty()) {
        warn.push_back("surface shader not used: " + surfaceShaderError);
    }
    if (g.coveredFaces)
        warn.push_back(std::to_string(g.coveredFaces) + " brush face(s) not built: each lies wholly under faces of other brushes in the "
                       "same plane (overlapping faces are drawn once: the smaller one where they overlap)");''')

S = 'E:/IW8-Radiant/src/viewport/scene.cpp'
patch(S, '''        if (faceEdges && brush) {
            items.push_back(g);''', '''        if (brush) {
            items.push_back(g);''')
patch(S, '''        uint64_t key = (1469598103934665603ull ^ g.rev) * 1099511628211ull;
        key = (key ^ uint64_t(g.edgeSet)) * 1099511628211ull;''', '''        uint64_t key = (1469598103934665603ull ^ g.rev) * 1099511628211ull;
        key = (key ^ uint64_t(g.edgeSet)) * 1099511628211ull;
        key = (key ^ uint64_t(faceEdges ? 2 : 1)) * 1099511628211ull;''')
patch(S, '''        g.polyEdges = edgeBrushMeshes(brushes, i, near[i]);''', '''        g.polyEdges = edgeBrushMeshes(brushes, i, near[i], kEdgeRange, faceEdges);''')
patch('E:/IW8-Radiant/src/viewport/scene.h', '''    // Face edge data for custom surface shaders (core face_edges), parallel to polys; empty unless SceneCache::faceEdges
    // is on and the item is a drawn brush. A poly with a mesh is drawn as that mesh (the same surface, cut so that
    // every vertex knows its distance to the face's edges). edgeSet / edgeKey: SceneCache::updateFaceEdges.''',
      '''    // The faces as their meshes (core face_edges), parallel to polys, for brushes (empty for other items). A textured
    // poly is drawn as its mesh: the same surface without the parts that lie under a coplanar face of another brush,
    // and (SceneCache::faceEdges) with every vertex knowing its distance to the face's edges. An empty mesh: the face
    // is wholly under other faces and not drawn. edgeSet / edgeKey: SceneCache::updateFaceEdges.''')
G = 'E:/IW8-Radiant/src/viewport/camera_geometry.cpp'
patch(G, '''            const EdgeFaceMesh* em = i < g->polyEdges.size() && !g->polyEdges[i].idx.empty() ? &g->polyEdges[i] : nullptr;
            if (em) {''', '''            const EdgeFaceMesh* em = i < g->polyEdges.size() ? &g->polyEdges[i] : nullptr;
            if (em && em->idx.empty()) {
                // Wholly under coplanar faces of other brushes: not drawn (core face_edges coplanarCovers).
            } else if (em) {''')
print('ok')
