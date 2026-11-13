import io


def patch(p, a, b, n=1):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


RH = 'src/viewport/render.h'
patch(RH, '''#include <cstdint>
#include <string>''', '''#include <cstdint>
#include <memory>
#include <string>''')
patch(RH, '''struct InstancedTexMesh {
    std::vector<GpuTexVertex> tris;
    std::vector<float> rows;  // 12 floats per instance''', '''struct InstancedTexMesh {
    std::vector<GpuTexVertex> tris;
    // The same triangles held by the caller (a stock prop's converted mesh): used instead of `tris` when set, so a
    // prop rebuild does not copy every model's vertices again (Verdansk: about a gigabyte per copy, 2026-11-13).
    std::shared_ptr<const std::vector<GpuTexVertex>> shared;
    const std::vector<GpuTexVertex>& verts() const { return shared ? *shared : tris; }
    std::vector<float> rows;  // 12 floats per instance''')

R = 'src/viewport/render.cpp'
patch(R, '''        GpuTexInstanced g;  // kept even when empty: the index is the caller's mesh index (textures are set by it)
        if (!m.tris.empty() && count > 0) {
            D3D11_BUFFER_DESC bd{};
            bd.Usage = D3D11_USAGE_IMMUTABLE;
            bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
            bd.ByteWidth = UINT(m.tris.size() * sizeof(GpuTexVertex));
            D3D11_SUBRESOURCE_DATA init{m.tris.data(), 0, 0};''', '''        GpuTexInstanced g;  // kept even when empty: the index is the caller's mesh index (textures are set by it)
        const std::vector<GpuTexVertex>& tris = m.verts();
        if (!tris.empty() && count > 0) {
            D3D11_BUFFER_DESC bd{};
            bd.Usage = D3D11_USAGE_IMMUTABLE;
            bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
            bd.ByteWidth = UINT(tris.size() * sizeof(GpuTexVertex));
            D3D11_SUBRESOURCE_DATA init{tris.data(), 0, 0};''')
patch(R, '''                    g.vertices = unsigned(m.tris.size());
                    g.instances = unsigned(count);''', '''                    g.vertices = unsigned(tris.size());
                    g.instances = unsigned(count);''')
patch(R, '''                    for (const GpuTexVertex& v : m.tris) {
                        const float p[3] = {v.x, v.y, v.z};''', '''                    for (const GpuTexVertex& v : tris) {
                        const float p[3] = {v.x, v.y, v.z};''')

MH = 'src/qtapp/main_window.h'
patch(MH, '''        std::vector<std::pair<std::string, std::vector<app::GpuTexVertex>>> tex;     // material, its triangles''',
      '''        // Material, its triangles (shared with the camera's instanced meshes: no copy per rebuild).
        std::vector<std::pair<std::string, std::shared_ptr<const std::vector<app::GpuTexVertex>>>> tex;''')
patch(MH, '''            std::vector<std::pair<std::string, std::vector<app::GpuTexVertex>>> tex;
        };''', '''            std::vector<std::pair<std::string, std::shared_ptr<const std::vector<app::GpuTexVertex>>>> tex;
        };''')

M = 'src/qtapp/main_window.cpp'
patch(M, '''                const auto toTex = [&](const ImportedModel& src, std::vector<std::pair<std::string, std::vector<app::GpuTexVertex>>>& outTex,''',
      '''                const auto toTex = [&](const ImportedModel& src,
                                       std::vector<std::pair<std::string, std::shared_ptr<const std::vector<app::GpuTexVertex>>>>& outTex,''')
patch(M, '''                        if (!texTris.empty()) outTex.push_back({ms.material, std::move(texTris)});''',
      '''                        if (!texTris.empty())
                            outTex.push_back({ms.material, std::make_shared<const std::vector<app::GpuTexVertex>>(std::move(texTris))});''')
patch(M, '''                    tm.tris = tris;''', '''                    tm.shared = tris;''')
patch(M, '''                        tm.tris = tris;''', '''                        tm.shared = tris;''')
patch(M, '''        for (const auto& [mat, tris] : it->second->tex)
            for (const auto& v : tris) pb.add(place(v));''', '''        for (const auto& [mat, tris] : it->second->tex)
            for (const auto& v : *tris) pb.add(place(v));''')
patch(M, '''        for (const auto& [mat, tris] : it->second->tex) {
            // Cut-out and blended materials (foliage, cards, glass) receive no decal volume (research 99: their''',
      '''        for (const auto& [mat, trisPtr] : it->second->tex) {
            const std::vector<app::GpuTexVertex>& tris = *trisPtr;
            // Cut-out and blended materials (foliage, cards, glass) receive no decal volume (research 99: their''')
print('ok')
