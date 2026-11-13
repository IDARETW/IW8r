import io


def patch(p, a, b, n=1):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


H = 'src/viewport/render.h'
patch(H, '''struct InstancedTexMesh {
    std::vector<GpuTexVertex> tris;
    std::vector<float> rows;  // 12 floats per instance
};''', '''struct InstancedTexMesh {
    std::vector<GpuTexVertex> tris;
    std::vector<float> rows;  // 12 floats per instance
    // Stock foliage: the model's last LOD distance (inches). The game stops drawing a placement beyond it (the grass
    // dissolves there with a dither); 0 = always drawn.
    float lodOut = 0;
};''')
patch(H, '''        float centre[3] = {0, 0, 0}, radius = 0;
        ID3D11Buffer* visInst = nullptr;''', '''        float centre[3] = {0, 0, 0}, radius = 0;
        float lodOut = 0;  // InstancedTexMesh::lodOut
        ID3D11Buffer* visInst = nullptr;''')

R = 'src/viewport/render.cpp'
patch(R, '''                out = dist > rad && rad < dist * pixelAngle * k;  // radius under k pixels: the game drops or LODs these''',
      '''                out = dist > rad && rad < dist * pixelAngle * k;  // radius under k pixels: the game drops or LODs these
                // Stock foliage past its last LOD distance: the game does not draw it (research 100 section 3.3).
                static const bool noLodOut = std::getenv("IW8R_NO_FOLIAGE_LOD_OUT") != nullptr;
                if (!noLodOut && g.lodOut > 0 && dist > g.lodOut) out = true;''')
patch(R, '''                    g.visible = g.instances;
                    g.rows = m.rows;''', '''                    g.visible = g.instances;
                    g.rows = m.rows;
                    g.lodOut = m.lodOut;''')

MH = 'src/qtapp/main_window.h'
patch(MH, '''        std::vector<std::pair<std::string, std::vector<app::GpuTexVertex>>> tex;     // material, its triangles
        size_t tris = 0;''', '''        std::vector<std::pair<std::string, std::vector<app::GpuTexVertex>>> tex;     // material, its triangles
        size_t tris = 0;
        double lodOut = 0;  // foliage: the last LOD distance (not drawn beyond it, as in game); 0 = always''')

M = 'src/qtapp/main_window.cpp'
patch(M, '''                for (const auto& ms : im->meshes) fresh->tris += ms.idx.size() / 3;
                {''', '''                for (const auto& ms : im->meshes) fresh->tris += ms.idx.size() / 3;
                // Foliage (grass, weeds, plants, trees) stops drawing past its last LOD distance in game. Other props
                // keep drawing: the map can override their LOD-out per placement, which the editor does not read.
                {
                    const QString lower = qs(in.model).toLower();
                    if (lower.contains("foliage") || lower.contains("grass") || lower.contains("weed"))
                        if (const auto detail = modelCacheStock_->loadDetail(qs(in.model)); detail && !detail->lods.empty())
                            fresh->lodOut = std::max(0.0, detail->lods.back().distance);
                }
                {''')
patch(M, '''                    app::InstancedTexMesh tm;
                    tm.tris = tris;''', '''                    app::InstancedTexMesh tm;
                    tm.tris = tris;
                    tm.lodOut = float(built->lodOut);''')
print('ok')
