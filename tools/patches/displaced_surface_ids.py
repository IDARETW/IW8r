import io

# Plan 2.2: displaced terrain keeps its GfxWorld surface ids, so the Umbra view culls it like every other surface.
# The subdivision already knows each triangle's source triangle (SubTri::origin).

ROOT = 'E:/IW8-Radiant/'


def read(p):
    return io.open(ROOT + p, encoding='utf-8', newline='').read()


def write(p, s):
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s)


def patch(p, a, b, count=1):
    s = read(p)
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == count, (p, s.count(a), a[:70])
    write(p, s.replace(a, b))


patch('src/core/stock_displacement.h', '''    std::vector<float> offset;  // per vertex: how far it moved along the displacement direction, inches (signed)''',
      '''    std::vector<float> offset;  // per vertex: how far it moved along the displacement direction, inches (signed)
    std::vector<uint32_t> triOrigin;  // per triangle of mesh.idx: the source triangle it was cut from''')
patch('src/core/stock_displacement.cpp', '''    out.mesh.idx.reserve(tris.size() * 3);
    for (const SubTri& s : tris)
        for (int c = 0; c < 3; ++c) out.mesh.idx.push_back(s.v[c]);''', '''    out.mesh.idx.reserve(tris.size() * 3);
    out.triOrigin.clear();
    out.triOrigin.reserve(tris.size());
    for (const SubTri& s : tris) {
        for (int c = 0; c < 3; ++c) out.mesh.idx.push_back(s.v[c]);
        out.triOrigin.push_back(s.origin);
    }''')
patch('src/core/stock_displacement.cpp', '''    world.meshes[mesh] = std::move(m.mesh);
    if (mesh < world.triSurface.size()) world.triSurface[mesh].clear();  // new triangles: of no known surface''',
      '''    // A new triangle is of the surface its source triangle was of (the Umbra view culls by it). Without the origins,
    // or when the source had none: of no known surface.
    if (mesh < world.triSurface.size()) {
        std::vector<uint32_t>& surf = world.triSurface[mesh];
        std::vector<uint32_t> next;
        if (!surf.empty() && surf.size() * 3 == world.meshes[mesh].idx.size() && m.triOrigin.size() * 3 == m.mesh.idx.size()) {
            next.reserve(m.triOrigin.size());
            for (const uint32_t o : m.triOrigin) {
                if (o >= surf.size()) {
                    next.clear();
                    break;
                }
                next.push_back(surf[o]);
            }
        }
        surf = std::move(next);
    }
    world.meshes[mesh] = std::move(m.mesh);''')
patch('src/core/stock_world.h', '''    // parallel to meshes: triSurface[m][t] for triangle t of meshes[m]. A mesh rebuilt later (displaced terrain) has
    // none (size 0): its triangles are of no known surface.''', '''    // parallel to meshes: triSurface[m][t] for triangle t of meshes[m]. A mesh rebuilt later (displaced terrain) keeps
    // each new triangle's surface (replaceStockWorldMesh); size 0 = its triangles are of no known surface.''')
patch('tests/stock_displacement_tests.cpp', '''    const size_t nv = out.mesh.pos.size();
    CHECK(nv > wm.pos.size());
    replaceStockWorldMesh(world, data.meshes[0].mesh, std::move(out));
    const size_t mi = data.meshes[0].mesh;''', '''    const size_t nv = out.mesh.pos.size();
    CHECK(nv > wm.pos.size());
    // Every new triangle names the source triangle it was cut from (plan 2.2: displaced terrain keeps its surface
    // ids, so the Umbra view culls it).
    const size_t srcTris = wm.idx.size() / 3, newTris = out.mesh.idx.size() / 3;
    CHECK(out.triOrigin.size() == newTris && newTris > srcTris);
    bool originsOk = true;
    std::vector<uint8_t> originSeen(srcTris, 0);
    for (const uint32_t o : out.triOrigin) {
        if (o >= srcTris) originsOk = false;
        else originSeen[o] = 1;
    }
    CHECK(originsOk);
    for (const uint8_t seen : originSeen) CHECK(seen == 1);  // no source triangle is lost
    const std::vector<uint32_t> origins = out.triOrigin;
    const size_t meshIndex = data.meshes[0].mesh;
    // Give the source triangles distinct surfaces; the displaced mesh must carry them over.
    if (world.triSurface.size() <= meshIndex) world.triSurface.resize(meshIndex + 1);
    world.triSurface[meshIndex].resize(srcTris);
    for (size_t t = 0; t < srcTris; ++t) world.triSurface[meshIndex][t] = uint32_t(100 + t);
    replaceStockWorldMesh(world, data.meshes[0].mesh, std::move(out));
    CHECK(world.triSurface[meshIndex].size() == newTris);
    bool carried = world.triSurface[meshIndex].size() == origins.size();
    for (size_t t = 0; carried && t < origins.size(); ++t) carried = world.triSurface[meshIndex][t] == 100 + origins[t];
    CHECK(carried);
    const size_t mi = data.meshes[0].mesh;''')
print('ok')
