import io

# The build's light grid (what models, players and the viewmodel are lit by) bounces light in the surfaces' own
# colours too, as the lightmap bake does since tools/patches/bake_coloured_bounce.py: one lookup for both.

P = 'E:/IW8-Radiant/src/core/compile_iw8.cpp'
s = io.open(P, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'


def rep(a, b):
    global s
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == 1, (s.count(a), a[:80])
    s = s.replace(a, b)


# 1. The lookup moves out of the lightmap block, so the light grid can use it.
lookup = '''        // The bounce colour of a material: the average of its colour image (library materials: own ones, and the stock
        // ones the editor put there with their textures). Not known: the bake's grey.
        std::map<std::string, Vec3> bounceColour;
        const auto bounceOf = [&](const std::string& material) {
            auto it = bounceColour.find(material);
            if (it != bounceColour.end()) return it->second;
            Vec3 a{-1, -1, -1};
            if (const CustomMaterial* cm = opt.library ? opt.library->find(material) : nullptr; cm && !cm->colorImage.empty()) {
                int w = 0, h = 0;
                std::vector<uint8_t> px;
                std::string ierr;
                if (decodeImageFile(cm->colorImage, w, h, px, &ierr) && w > 0 && h > 0) {
                    a = bakeAlbedoFromRgba(px.data(), size_t(w) * size_t(h), std::max<size_t>(1, size_t(w) * size_t(h) / 4096));
                    if (cm->hasTint() && a.x >= 0)
                        a = Vec3{std::min(0.9, a.x * cm->tint[0]), std::min(0.9, a.y * cm->tint[1]), std::min(0.9, a.z * cm->tint[2])};
                }
            }
            return bounceColour.emplace(material, a).first->second;
        };
'''
rep('''        BakeScene scene;
''' + lookup, '''        BakeScene scene;
''')
moved = '\n'.join(line[4:] if line.startswith('    ') else line for line in lookup.split('\n'))
rep('''    std::vector<std::vector<std::array<float, 2>>> lightmapUv;
    std::vector<bool> lightmapped;''', moved + '''    std::vector<std::vector<std::array<float, 2>>> lightmapUv;
    std::vector<bool> lightmapped;''')

# 2. The light grid's rays: a triangle carries its surface's colour.
rep('''            std::vector<RayBvh::Triangle> tris;
            for (const RenderSurface& s : surfaces) {
                if (lower(s.editorMaterial).find("sky") != std::string::npos) continue;
                for (size_t t = 0; t + 2 < s.idx.size(); t += 3)
                    tris.push_back({s.verts[s.idx[t]].pos, s.verts[s.idx[t + 1]].pos, s.verts[s.idx[t + 2]].pos, 0});
            }
            gridBvh.build(std::move(tris));
        }''', '''            std::vector<RayBvh::Triangle> tris;
            for (const RenderSurface& s : surfaces) {
                if (lower(s.editorMaterial).find("sky") != std::string::npos) continue;
                // id: 0 = the bake's grey, else 1 + the entry of the surface's own bounce colour.
                uint32_t id = 0;
                if (const Vec3 a = bounceOf(s.editorMaterial); a.x >= 0) {
                    gridAlbedo.push_back(a);
                    id = uint32_t(gridAlbedo.size());
                }
                for (size_t t = 0; t + 2 < s.idx.size(); t += 3)
                    tris.push_back({s.verts[s.idx[t]].pos, s.verts[s.idx[t + 1]].pos, s.verts[s.idx[t + 2]].pos, id});
            }
            gridBvh.build(std::move(tris));
        }''')
rep('''        RayBvh gridBvh;
        {''', '''        RayBvh gridBvh;
        std::vector<Vec3> gridAlbedo;  // the bounce colours the triangles' ids name
        {''')
rep('''            lg.samplesAt = [&gridBvh, sunDir, sunC, gridSky, gridSkyI, reach](const Vec3& p, std::array<std::array<float, 3>, 56>& out) {''',
    '''            lg.samplesAt = [&gridBvh, &gridAlbedo, sunDir, sunC, gridSky, gridSkyI, reach](const Vec3& p, std::array<std::array<float, 3>, 56>& out) {''')
rep('''                        rad = e * (0.45 / kPi);''', '''                        const Vec3 rho = t.id > 0 && t.id <= gridAlbedo.size() ? gridAlbedo[t.id - 1] : Vec3{0.45, 0.45, 0.45};
                        rad = Vec3{e.x * rho.x, e.y * rho.y, e.z * rho.z} * (1.0 / kPi);''')
rep('''one bounce of the sun and the sky off what they hit (albedo 0.45, as the bake)''',
    '''one bounce of the sun and the sky off what they hit (in the surface's own colour, as the bake)''')
io.open(P, 'w', encoding='utf-8', newline='').write(s)
print('ok')
