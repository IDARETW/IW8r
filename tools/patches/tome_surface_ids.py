import io

# Umbra visibility view (plan M2.2): every stock world triangle knows its GfxWorld surface (the Umbra tome's class 0
# user ID), and the reference vertices carry it in the spare vertex attribute for the tome cull.


def patch(p, a, b, n=1):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


H = 'E:/IW8-Radiant/src/core/stock_world.h'
patch(H, '''    std::vector<std::vector<int>> lightmapIndex;
''', '''    std::vector<std::vector<int>> lightmapIndex;
    // The GfxWorld surface (its index in the surface table: the Umbra tome's class 0 user ID) of each triangle,
    // parallel to meshes: triSurface[m][t] for triangle t of meshes[m]. A mesh rebuilt later (displaced terrain) has
    // none (size 0): its triangles are of no known surface.
    std::vector<std::vector<uint32_t>> triSurface;
''')
C = 'E:/IW8-Radiant/src/core/stock_world.cpp'
patch(C, '''            out.lightmapUv.emplace_back();
            out.lightmapIndex.emplace_back();
            out.layerCount.push_back(std::max(1, materialLayers[name]));''', '''            out.lightmapUv.emplace_back();
            out.lightmapIndex.emplace_back();
            out.triSurface.emplace_back();
            out.layerCount.push_back(std::max(1, materialLayers[name]));''')
patch(C, '''            m.idx.push_back(corner[0]);
            m.idx.push_back(corner[2]);
            m.idx.push_back(corner[1]);
''', '''            m.idx.push_back(corner[0]);
            m.idx.push_back(corner[2]);
            m.idx.push_back(corner[1]);
            out.triSurface[mi].push_back(uint32_t(si));
''')
D = 'E:/IW8-Radiant/src/core/stock_displacement.cpp'
patch(D, '''    world.meshes[mesh] = std::move(m.mesh);
''', '''    world.meshes[mesh] = std::move(m.mesh);
    if (mesh < world.triSurface.size()) world.triSurface[mesh].clear();  // new triangles: of no known surface
''')

S = 'E:/IW8-Radiant/src/viewport/scene.cpp'
patch(S, '''        const std::vector<uint32_t>* vcolHi =
            layered && mi < world.vertexColorHigh.size() && world.vertexColorHigh[mi].size() == m.pos.size() ? &world.vertexColorHigh[mi] : nullptr;
''', '''        const std::vector<uint32_t>* vcolHi =
            layered && mi < world.vertexColorHigh.size() && world.vertexColorHigh[mi].size() == m.pos.size() ? &world.vertexColorHigh[mi] : nullptr;
        // The triangle's GfxWorld surface, for the Umbra tome cull (referenceSurfaceTag; 0 = of no known surface).
        const std::vector<uint32_t>* triSurf =
            mi < world.triSurface.size() && world.triSurface[mi].size() * 3 == m.idx.size() ? &world.triSurface[mi] : nullptr;
''')
patch(S, '''                GpuTexVertex v{float(m.pos[k].x), float(m.pos[k].y), float(m.pos[k].z), m.uv[k][0], 1.0f - m.uv[k][1], rgba};
                v.nx = float(n.x), v.ny = float(n.y), v.nz = float(n.z);
                {
                    const uint32_t col = vcol ? (*vcol)[k] : 0xFFFFFFFFu;''', '''                GpuTexVertex v{float(m.pos[k].x), float(m.pos[k].y), float(m.pos[k].z), m.uv[k][0], 1.0f - m.uv[k][1], rgba};
                v.nx = float(n.x), v.ny = float(n.y), v.nz = float(n.z);
                v.edge = referenceSurfaceTag(triSurf ? int64_t((*triSurf)[i / 3]) : -1);
                {
                    const uint32_t col = vcol ? (*vcol)[k] : 0xFFFFFFFFu;''')
R = 'E:/IW8-Radiant/src/viewport/render.h'
patch(R, '''// 3.20 instanced static models: one model's triangles''', '''// A stock reference vertex's GpuTexVertex::edge: the triangle's GfxWorld surface index + 1 in the low 24 bits (0: of no
// known surface) and a zero top byte (edge data and "none" have 255 there). The vertex shader reads it for the Umbra
// tome cull (SceneRenderer::setReferenceSurfaceVisibility).
inline uint32_t referenceSurfaceTag(int64_t surface) {
    return surface >= 0 && surface < 0xFFFFFE ? uint32_t(surface) + 1u : 0u;
}

// 3.20 instanced static models: one model's triangles''')
print('ok')
