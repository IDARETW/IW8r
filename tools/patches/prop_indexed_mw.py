import io


def patch(p, a, b, n=1):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


MH = 'src/qtapp/main_window.h'
patch(MH, '''        // Material, its triangles (shared with the camera's instanced meshes: no copy per rebuild).
        std::vector<std::pair<std::string, std::shared_ptr<const std::vector<app::GpuTexVertex>>>> tex;''',
      '''        // Material, its indexed mesh (shared with the camera's instanced meshes: no copy per rebuild).
        std::vector<std::pair<std::string, std::shared_ptr<const app::PropMeshData>>> tex;''')
patch(MH, '''            std::vector<std::pair<std::string, std::shared_ptr<const std::vector<app::GpuTexVertex>>>> tex;
        };''', '''            std::vector<std::pair<std::string, std::shared_ptr<const app::PropMeshData>>> tex;
        };''')

M = 'src/qtapp/main_window.cpp'
patch(M, '''                const auto toTex = [&](const ImportedModel& src,
                                       std::vector<std::pair<std::string, std::shared_ptr<const std::vector<app::GpuTexVertex>>>>& outTex,
                                       std::vector<app::GpuVertex>* plainOut) {
                    std::vector<app::GpuVertex> ignored;
                    std::vector<app::GpuVertex>& plain = plainOut ? *plainOut : ignored;
                    const auto zup = [](const Vec3& p) { return Vec3{p.x, -p.z, p.y}; };  // glTF Y-up -> Z-up
                    for (const auto& ms : src.meshes) {
                        const bool textured = !ms.material.empty() && ms.uv.size() == ms.pos.size();
                        std::vector<app::GpuTexVertex> texTris;
                        for (size_t i = 0; i + 2 < ms.idx.size(); i += 3) {
                            const Vec3 a = zup(ms.pos[ms.idx[i]]), b = zup(ms.pos[ms.idx[i + 1]]), c = zup(ms.pos[ms.idx[i + 2]]);
                            const double k = 0.55 + 0.45 * std::fabs(dot(normalize(cross(b - a, c - a)), light));
                            if (textured) {
                                const uint32_t g = uint32_t(std::clamp(k * 255.0, 0.0, 255.0));
                                const uint32_t col = 0xff000000u | (g << 16) | (g << 8) | g;
                                const Vec3* v[3] = {&a, &b, &c};
                                for (int n = 0; n < 3; ++n) {
                                    const auto& uv = ms.uv[ms.idx[i + size_t(n)]];
                                    app::GpuTexVertex tv;
                                    tv.x = float(v[n]->x);
                                    tv.y = float(v[n]->y);
                                    tv.z = float(v[n]->z);
                                    tv.u = float(uv[0]);
                                    tv.v = float(uv[1]);
                                    tv.rgba = col;
                                    // The model's own vertex normals (tree cards bend theirs outward from the crown): Game
                                    // Lighting shades with them (vs_tex_inst turns them by the instance).
                                    if (ms.normal.size() == ms.pos.size()) {
                                        const Vec3 nn = zup(ms.normal[ms.idx[i + size_t(n)]]);
                                        if (length(nn) > 1e-6) {
                                            const Vec3 un = normalize(nn);
                                            tv.nx = float(un.x), tv.ny = float(un.y), tv.nz = float(un.z);
                                        }
                                    }
                                    texTris.push_back(tv);
                                }
                            } else {
                                const uint32_t g = uint32_t(std::clamp(k * 175.0, 0.0, 255.0));
                                const uint32_t col = 0xff000000u | (uint32_t(g * 0.92) << 16) | (g << 8) | uint32_t(g * 0.85);
                                for (const Vec3* v : {&a, &b, &c}) plain.push_back({float(v->x), float(v->y), float(v->z), col});
                            }
                        }
                        if (!texTris.empty())
                            outTex.push_back({ms.material, std::make_shared<const std::vector<app::GpuTexVertex>>(std::move(texTris))});
                    }
                };''', '''                const auto toTex = [&](const ImportedModel& src,
                                       std::vector<std::pair<std::string, std::shared_ptr<const app::PropMeshData>>>& outTex,
                                       std::vector<app::GpuVertex>* plainOut) {
                    std::vector<app::GpuVertex> ignored;
                    std::vector<app::GpuVertex>& plain = plainOut ? *plainOut : ignored;
                    const auto zup = [](const Vec3& p) { return Vec3{p.x, -p.z, p.y}; };  // glTF Y-up -> Z-up
                    for (const auto& ms : src.meshes) {
                        const bool textured = !ms.material.empty() && ms.uv.size() == ms.pos.size();
                        if (!textured) {
                            for (size_t i = 0; i + 2 < ms.idx.size(); i += 3) {
                                const Vec3 a = zup(ms.pos[ms.idx[i]]), b = zup(ms.pos[ms.idx[i + 1]]), c = zup(ms.pos[ms.idx[i + 2]]);
                                const double k = 0.55 + 0.45 * std::fabs(dot(normalize(cross(b - a, c - a)), light));
                                const uint32_t g = uint32_t(std::clamp(k * 175.0, 0.0, 255.0));
                                const uint32_t col = 0xff000000u | (uint32_t(g * 0.92) << 16) | (g << 8) | uint32_t(g * 0.85);
                                for (const Vec3* v : {&a, &b, &c}) plain.push_back({float(v->x), float(v->y), float(v->z), col});
                            }
                            continue;
                        }
                        // Indexed as stored (28 bytes a vertex). The model's own vertex normals (tree cards bend theirs
                        // outward from the crown): Game Lighting shades with them (vs_tex_inst turns them by the
                        // instance); none (zero) = the pixel shader derives one. The plain view's facing shade is per
                        // vertex, from that normal or, without one, from the faces around the vertex.
                        const size_t nv = ms.pos.size();
                        const bool ownNormals = ms.normal.size() == nv;
                        std::vector<Vec3> shadeN(nv, Vec3{0, 0, 0});
                        auto pm = std::make_shared<app::PropMeshData>();
                        pm->indices.reserve(ms.idx.size());
                        for (size_t i = 0; i + 2 < ms.idx.size(); i += 3) {
                            const uint32_t ia = ms.idx[i], ib = ms.idx[i + 1], ic = ms.idx[i + 2];
                            if (ia >= nv || ib >= nv || ic >= nv) continue;
                            pm->indices.push_back(ia), pm->indices.push_back(ib), pm->indices.push_back(ic);
                            if (!ownNormals) {
                                const Vec3 a = zup(ms.pos[ia]), b = zup(ms.pos[ib]), c = zup(ms.pos[ic]);
                                const Vec3 fn = cross(b - a, c - a);
                                shadeN[ia] = shadeN[ia] + fn, shadeN[ib] = shadeN[ib] + fn, shadeN[ic] = shadeN[ic] + fn;
                            }
                        }
                        if (pm->indices.empty()) continue;
                        pm->vertices.resize(nv);
                        for (size_t v = 0; v < nv; ++v) {
                            const Vec3 p = zup(ms.pos[v]);
                            app::PropVertex& pv = pm->vertices[v];
                            pv.x = float(p.x), pv.y = float(p.y), pv.z = float(p.z);
                            pv.u = float(ms.uv[v][0]), pv.v = float(ms.uv[v][1]);
                            pv.normal = 0;
                            Vec3 sn = shadeN[v];
                            if (ownNormals) {
                                const Vec3 nn = zup(ms.normal[v]);
                                if (length(nn) > 1e-6) {
                                    const Vec3 un = normalize(nn);
                                    pv.normal = app::packPropNormal(float(un.x), float(un.y), float(un.z));
                                    sn = un;
                                }
                            }
                            const double k = length(sn) > 1e-12 ? 0.55 + 0.45 * std::fabs(dot(normalize(sn), light)) : 0.775;
                            const uint32_t g = uint32_t(std::clamp(k * 255.0, 0.0, 255.0));
                            pv.rgba = 0xff000000u | (g << 16) | (g << 8) | g;
                        }
                        outTex.push_back({ms.material, std::move(pm)});
                    }
                };''')
patch(M, '''tm.shared = tris;''', '''tm.mesh = tris;''', 2)
# projectPropDecals: the indexed mesh
patch(M, '''        const auto place = [&](const app::GpuTexVertex& v) {
            const Vec3 p{v.x, v.y, v.z};
            return in.origin + (in.axis[0] * p.x + in.axis[1] * p.y + in.axis[2] * p.z) * in.scale;
        };
        AABB pb;
        for (const auto& [mat, tris] : it->second->tex)
            for (const auto& v : *tris) pb.add(place(v));''', '''        const auto place = [&](const app::PropVertex& v) {
            const Vec3 p{v.x, v.y, v.z};
            return in.origin + (in.axis[0] * p.x + in.axis[1] * p.y + in.axis[2] * p.z) * in.scale;
        };
        AABB pb;
        for (const auto& [mat, pm] : it->second->tex)
            for (const auto& v : pm->vertices) pb.add(place(v));''')
patch(M, '''        for (const auto& [mat, trisPtr] : it->second->tex) {
            const std::vector<app::GpuTexVertex>& tris = *trisPtr;
            if (!materialReceives(mat)) continue;
            for (size_t i = 0; i + 2 < tris.size(); i += 3) {
                const Vec3 a = place(tris[i]), b = place(tris[i + 1]), c = place(tris[i + 2]);''', '''        for (const auto& [mat, pm] : it->second->tex) {
            const std::vector<app::PropVertex>& pv = pm->vertices;
            const std::vector<uint32_t>& ix = pm->indices;
            if (!materialReceives(mat)) continue;
            for (size_t i = 0; i + 2 < ix.size(); i += 3) {
                const Vec3 a = place(pv[ix[i]]), b = place(pv[ix[i + 1]]), c = place(pv[ix[i + 2]]);''')
# bench memory diagnosis
patch(M, '''            for (const auto& t : b->tex) keptMb += t.second->size() * sizeof(app::GpuTexVertex);
            for (const auto& l : b->lods)
                for (const auto& t : l.tex) keptLodMb += t.second->size() * sizeof(app::GpuTexVertex);''', '''            for (const auto& t : b->tex) keptMb += t.second->vertices.size() * sizeof(app::PropVertex) + t.second->indices.size() * 4;
            for (const auto& l : b->lods)
                for (const auto& t : l.tex) keptLodMb += t.second->vertices.size() * sizeof(app::PropVertex) + t.second->indices.size() * 4;''')
patch(M, '''        for (const auto& m : texInstanced) drawMb += m.verts().size() * sizeof(app::GpuTexVertex), rowsMb += (m.rows.size() + m.light.size()) * 4;''',
      '''        for (const auto& m : texInstanced)
            drawMb += m.mesh ? m.mesh->vertices.size() * sizeof(app::PropVertex) + m.mesh->indices.size() * 4 : 0,
                rowsMb += (m.rows.size() + m.light.size()) * 4;''')
print('ok')
