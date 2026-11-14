import io

# Build: when the map's surface shader reads the edge inputs, the world's brush faces are written as their edge meshes
# (core face_edges) with the edge data in the vertex colour, and IW8R_EDGES tells the converter's shader to read it.


def patch(p, a, b, n=1):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


K = 'E:/IW8-Radiant/src/core/compile_iw8.cpp'
patch(K, '#include "sky_sun.h"\n', '#include "sky_sun.h"\n#include "face_edges.h"\n')
patch(K, '''struct Gather {
    int subdiv = 4;''', '''// The brushes of the static world for the face edge data (core face_edges), by primitive.
struct EdgeData {
    std::vector<EdgeBrush> brushes;
    std::vector<std::vector<uint32_t>> near;
    std::map<const Primitive*, size_t> index;
};

struct Gather {
    const EdgeData* edges = nullptr;  // set: brush faces are written as their edge meshes, the data in the colour
    size_t paintedPatches = 0;        // patches whose painted colours were left out because the colour holds edge data
    int subdiv = 4;''')
patch(K, '''    // Render (doc 15 §2.2): one fan per contributing face, indices [0,k,k+1] (CCW seen from outside).
    for (size_t fi = 0; fi < cb.size() && fi < wins.size(); ++fi) {''', '''    // Face edge data for the map's surface shader: every face as its edge mesh (the same surface, cut so that each
    // vertex knows its distance to the face's edges).
    std::vector<EdgeFaceMesh> edgeMeshes;
    if (g.edges)
        if (const auto it = g.edges->index.find(&p); it != g.edges->index.end())
            edgeMeshes = edgeBrushMeshes(g.edges->brushes, it->second, g.edges->near[it->second]);

    // Render (doc 15 §2.2): one fan per contributing face, indices [0,k,k+1] (CCW seen from outside).
    for (size_t fi = 0; fi < cb.size() && fi < wins.size(); ++fi) {''')
patch(K, '''        for (const Vec3& P : w) {
            const auto uv = faceUV(n, tp, P);
            c.verts.push_back({P, n, faceTan, uv[0], uv[1]});
            g.bounds.add(P);
        }
        for (size_t k = 1; k + 1 < w.size(); ++k) {
            c.idx.push_back(0);
            c.idx.push_back(uint32_t(k));
            c.idx.push_back(uint32_t(k + 1));
        }
        g.chunks.push_back(std::move(c));''', '''        if (fi < edgeMeshes.size() && !edgeMeshes[fi].idx.empty()) {
            for (const EdgeVertex& q : edgeMeshes[fi].verts) {
                const auto uv = faceUV(n, tp, q.pos);
                c.verts.push_back({q.pos, n, faceTan, uv[0], uv[1]});
                const uint32_t packed = packEdge(q);
                c.verts.back().hasColor = true;
                c.verts.back().color = {std::uint8_t(packed), std::uint8_t(packed >> 8), std::uint8_t(packed >> 16), std::uint8_t(packed >> 24)};
                g.bounds.add(q.pos);
            }
            c.idx = edgeMeshes[fi].idx;
            g.chunks.push_back(std::move(c));
            continue;
        }
        for (const Vec3& P : w) {
            const auto uv = faceUV(n, tp, P);
            c.verts.push_back({P, n, faceTan, uv[0], uv[1]});
            g.bounds.add(P);
        }
        for (size_t k = 1; k + 1 < w.size(); ++k) {
            c.idx.push_back(0);
            c.idx.push_back(uint32_t(k));
            c.idx.push_back(uint32_t(k + 1));
        }
        g.chunks.push_back(std::move(c));''')
patch(K, '''    const std::vector<Rgba8> colours = patchHasColours(p) ? tessellatePatchColours(p, g.subdiv) : std::vector<Rgba8>{};''',
      '''    // With edge data the world's vertex colour is read as that data: a painted patch keeps white (no edge).
    if (g.edges && patchHasColours(p)) ++g.paintedPatches;
    const std::vector<Rgba8> colours = patchHasColours(p) && !g.edges ? tessellatePatchColours(p, g.subdiv) : std::vector<Rgba8>{};''')
patch(K, '''    g.warnings = &warn;
    g.library = opt.library;
    const Entity* worldspawn = nullptr;
    std::vector<const Entity*> points;''', '''    g.warnings = &warn;
    g.library = opt.library;
    // The map's surface shader (worldspawn iw8r_surface_shader, core custom_shader), found before the geometry is
    // gathered: when it reads the edge inputs, the static world's brush faces carry the edge data.
    CustomShader surfaceShader;
    bool haveSurfaceShader = false;
    std::string surfaceShaderError;
    for (const auto& we : map.entities) {
        if (!we.isWorldspawn()) continue;
        if (const std::string* sname = we.value("iw8r_surface_shader"); sname && !sname->empty()) {
            std::vector<std::string> dirs;
            if (!opt.mapDir.empty()) dirs.push_back(opt.mapDir + "/shaders");
            dirs.insert(dirs.end(), opt.shaderDirs.begin(), opt.shaderDirs.end());
            haveSurfaceShader = findCustomShader(*sname, dirs, surfaceShader, &surfaceShaderError);
        }
        break;
    }
    EdgeData edgeData;
    const bool surfaceEdges = haveSurfaceShader && customShaderUsesEdges(surfaceShader.code);
    if (surfaceEdges) {
        for (const auto& ee : map.entities) {
            const std::string c = lower(ee.classname());
            if (c != "worldspawn" && c != "func_group") continue;
            for (const auto& it : ee.items) {
                const Primitive* p = it.prim();
                if (!p || p->kind != PrimKind::Brush) continue;
                const ConvexBrush cb = brushFromPrimitive(*p);
                if (!isValidBrush(cb)) continue;
                EdgeBrush b;
                b.wins = brushWindings(cb);
                for (const auto& f : cb) {
                    b.planes.push_back(f.plane);
                    b.drawn.push_back(!isIw8ToolMaterial(firstToken(f.suffix)));
                }
                finishEdgeBrush(b);
                edgeData.index.emplace(p, edgeData.brushes.size());
                edgeData.brushes.push_back(std::move(b));
            }
        }
        edgeData.near = edgeNeighbours(edgeData.brushes);
        g.edges = &edgeData;
    }
    const Entity* worldspawn = nullptr;
    std::vector<const Entity*> points;''')
patch(K, '''    std::string surfaceShaderKey;
    if (const std::string* sname = worldspawn ? worldspawn->value("iw8r_surface_shader") : nullptr; sname && !sname->empty()) {
        std::vector<std::string> dirs;
        if (!opt.mapDir.empty()) dirs.push_back(opt.mapDir + "/shaders");
        dirs.insert(dirs.end(), opt.shaderDirs.begin(), opt.shaderDirs.end());
        CustomShader shader;
        std::string serr;
        if (findCustomShader(*sname, dirs, shader, &serr)) {
            surfaceShaderKey = ",\\"iw8r_surface_shader\\":" + quote(customShaderBuildSource(shader));
            warn.push_back("surface shader '" + shader.name + "' lights the map's own surfaces (custom shader; in-game look not verified)");
        } else {
            warn.push_back("surface shader not used: " + serr);
        }
    }''', '''    std::string surfaceShaderKey;
    if (haveSurfaceShader) {
        surfaceShaderKey = ",\\"iw8r_surface_shader\\":" + quote(customShaderBuildSource(surfaceShader, surfaceEdges));
        warn.push_back("surface shader '" + surfaceShader.name + "' lights the map's own surfaces" +
                       (surfaceEdges ? " and reads the brush faces' edge data (written into their vertex colour)" : "") +
                       " (custom shader; in-game look not verified)");
        if (g.paintedPatches)
            warn.push_back(std::to_string(g.paintedPatches) + " painted patch(es) built without their vertex colours: with a "
                           "shader that reads edges the world's vertex colour holds the edge data");
    } else if (!surfaceShaderError.empty()) {
        warn.push_back("surface shader not used: " + surfaceShaderError);
    }''')
print('ok')
