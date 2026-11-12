import io
p = 'src/viewport/scene.cpp'
s = io.open(p, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
def rep(a, b):
    global s
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == 1, (s.count(a), a[:70])
    s = s.replace(a, b)

rep('''    auto addPrim = [&](const Primitive& p) {
        if (p.kind == PrimKind::Brush) {
            const auto planes = p.planes();
            auto windings = buildBrushWindings(planes);
            for (size_t i = 0; i < windings.size(); ++i) {
                if (windings[i].size() < 3) continue;
                for (const auto& v : windings[i]) g.bounds.add(v);
                g.faceNormals.push_back(planes[i].n);
                g.polys.push_back(std::move(windings[i]));
            }
        } else if (p.kind == PrimKind::Patch) {
            const PatchMesh mesh = tessellatePrimitive(p, 4);
            for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
                const Vec3& a = mesh.points[mesh.indices[i]];
                const Vec3& b = mesh.points[mesh.indices[i + 1]];
                const Vec3& cc = mesh.points[mesh.indices[i + 2]];
                g.polys.push_back({a, b, cc});
                g.faceNormals.push_back(normalize(cross(b - a, cc - a)));
                g.bounds.add(a);
                g.bounds.add(b);
                g.bounds.add(cc);
            }
        }
    };
''', '''    // Faces carry their material and texture coordinates like a brush or patch of the map itself (buildPrim), so a
    // placed prefab is textured in the camera view. The primitives are already in world space with the placement's
    // material swaps applied (instantiatePrefab), which is what the build textures.
    auto addPrim = [&](const Primitive& p) {
        if (p.kind == PrimKind::Brush) {
            auto windings = faceWindingsOf(p);
            for (size_t i = 0; i < windings.size(); ++i) {
                if (windings[i].size() < 3) continue;
                const auto pl = facePlane(p, i);
                if (!pl) continue;
                const TexParams tp = texParamsOfFace(p, i).value_or(TexParams{});
                std::vector<std::array<float, 2>> uv;
                uv.reserve(windings[i].size());
                for (const auto& v : windings[i]) {
                    g.bounds.add(v);
                    const auto t = faceUV(pl->n, tp, v);
                    uv.push_back({float(t[0]), float(t[1])});
                }
                g.faceNormals.push_back(pl->n);
                g.polys.push_back(std::move(windings[i]));
                g.polyMaterial.push_back(p.faces[i].material());
                g.polyUV.push_back(std::move(uv));
            }
        } else if (p.kind == PrimKind::Patch) {
            const PatchMesh mesh = tessellatePrimitive(p, 4);
            const std::vector<std::array<double, 2>> stUv = tessellatePatchUVs(p, 4);
            for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
                const Vec3& a = mesh.points[mesh.indices[i]];
                const Vec3& b = mesh.points[mesh.indices[i + 1]];
                const Vec3& cc = mesh.points[mesh.indices[i + 2]];
                const Vec3 n = normalize(cross(b - a, cc - a));
                std::vector<std::array<float, 2>> uv;
                if (stUv.size() == mesh.points.size()) {
                    for (size_t k = 0; k < 3; ++k) {
                        const auto& t = stUv[mesh.indices[i + k]];
                        uv.push_back({float(t[0]), float(t[1])});
                    }
                } else {
                    const TexParams tp;
                    for (const Vec3* q : {&a, &b, &cc}) {
                        const auto t = faceUV(n, tp, *q);
                        uv.push_back({float(t[0]), float(t[1])});
                    }
                }
                g.polys.push_back({a, b, cc});
                g.faceNormals.push_back(n);
                g.polyMaterial.push_back(p.patchMaterial);
                g.polyUV.push_back(std::move(uv));
                g.bounds.add(a);
                g.bounds.add(b);
                g.bounds.add(cc);
            }
        }
    };
''')
rep('''    g.polys = std::move(parts.faces.polys);
    g.faceNormals = std::move(parts.faces.faceNormals);
    g.bounds = parts.faces.bounds;
    if (!parts.meshes.polys.empty()) {
        g.meshFrom = uint32_t(g.polys.size());
        if (!parts.meshes.polyMaterial.empty()) {
            // Materials and texture coordinates line up with the polys: none for the faces before the models.
            g.polyMaterial.resize(g.polys.size());
            g.polyUV.resize(g.polys.size());
            for (auto& m : parts.meshes.polyMaterial) g.polyMaterial.push_back(std::move(m));
            for (auto& uv : parts.meshes.polyUV) g.polyUV.push_back(std::move(uv));
        }
''', '''    g.polys = std::move(parts.faces.polys);
    g.faceNormals = std::move(parts.faces.faceNormals);
    g.bounds = parts.faces.bounds;
    // Materials and texture coordinates line up with the polys: the faces' own, then the models'.
    g.polyMaterial = std::move(parts.faces.polyMaterial);
    g.polyUV = std::move(parts.faces.polyUV);
    g.polyMaterial.resize(g.polys.size());
    g.polyUV.resize(g.polys.size());
    if (!parts.meshes.polys.empty()) {
        g.meshFrom = uint32_t(g.polys.size());
        if (!parts.meshes.polyMaterial.empty()) {
            for (auto& m : parts.meshes.polyMaterial) g.polyMaterial.push_back(std::move(m));
            for (auto& uv : parts.meshes.polyUV) g.polyUV.push_back(std::move(uv));
        }
''')
io.open(p, 'w', encoding='utf-8', newline='').write(s)
print('ok')
