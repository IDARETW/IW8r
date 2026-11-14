import io

# Viewport scene: brush items carry their face edge meshes (core face_edges) when a custom shader in use reads the
# edge inputs (SceneCache::faceEdges). Made after the items are synced, again only for a brush that changed or whose
# touching neighbours changed; the camera geometry draws the meshes instead of the plain face fans.


def patch(p, a, b, n=1):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


H = 'E:/IW8-Radiant/src/viewport/scene.h'
patch(H, '''    std::vector<Winding> polys;  // brush faces, tessellated patch triangles, entity box faces
''', '''    std::vector<Winding> polys;  // brush faces, tessellated patch triangles, entity box faces
    // Face edge data for custom surface shaders (core face_edges), parallel to polys; empty unless SceneCache::faceEdges
    // is on and the item is a drawn brush. A poly with a mesh is drawn as that mesh (the same surface, cut so that
    // every vertex knows its distance to the face's edges). edgeSet / edgeKey: SceneCache::updateFaceEdges.
    std::vector<EdgeFaceMesh> polyEdges;
    int edgeSet = 0;
    uint64_t edgeKey = 0;
''')
patch(H, '''    std::function<int(const std::string& material)> shaderFor;
''', '''    std::function<int(const std::string& material)> shaderFor;
    // A custom shader in use reads the edge inputs: brush faces get their edge meshes (PrimGeom::polyEdges). Call
    // invalidate() after changing it.
    bool faceEdges = false;
''')
patch(H, '''private:
    std::unordered_map<Id, PrimGeom> cache_;''', '''private:
    void updateFaceEdges();
    std::unordered_map<Id, PrimGeom> cache_;''')
s = io.open(H, encoding='utf-8').read()
if '#include "face_edges.h"' not in s:
    patch(H, '#include "collision_preview.h"\n', '#include "collision_preview.h"\n#include "face_edges.h"\n')

C = 'E:/IW8-Radiant/src/viewport/scene.cpp'
# The set of a brush: the static world (worldspawn and func_group) is one, every other brush entity its own.
patch(C, '''            pg->seen = visit_;
            entBounds.add(pg->bounds);
            list_.push_back(pg);
        }
        if (!hasPrim && !e.isWorldspawn() && e.origin()) {''', '''            pg->seen = visit_;
            pg->edgeSet = e.isWorldspawn() || cls == "func_group" ? 0 : int(e.id % 0x7fffffff) + 1;
            entBounds.add(pg->bounds);
            list_.push_back(pg);
        }
        if (!hasPrim && !e.isWorldspawn() && e.origin()) {''')
patch(C, '''    for (const auto& [from, name] : targets)
        if (auto it = named.find(name); it != named.end()) connections_.push_back({from, it->second});
''', '''    for (const auto& [from, name] : targets)
        if (auto it = named.find(name); it != named.end()) connections_.push_back({from, it->second});
    updateFaceEdges();
''')
patch(C, '''void SceneCache::sync(const Document& doc) {''', '''void SceneCache::updateFaceEdges() {
    // Brushes whose faces are drawn with a texture: the ones a surface shader can light.
    std::vector<PrimGeom*> items;
    for (const PrimGeom* cg : list_) {
        PrimGeom* g = const_cast<PrimGeom*>(cg);
        const bool brush = g->kind == PrimGeom::Brush && !g->volume && !g->prefab && g->polyMaterial.size() == g->polys.size() &&
                           g->faceNormals.size() == g->polys.size();
        if (faceEdges && brush) {
            items.push_back(g);
        } else if (!g->polyEdges.empty()) {
            g->polyEdges.clear();
            g->edgeKey = 0;
            g->stamp = ++stampCounter_;
        }
    }
    if (items.empty()) return;
    IW8R_PROF("s.face_edges");
    std::vector<EdgeBrush> brushes(items.size());
    for (size_t i = 0; i < items.size(); ++i) {
        const PrimGeom& g = *items[i];
        EdgeBrush& b = brushes[i];
        b.set = g.edgeSet;
        b.wins = g.polys;
        b.planes.resize(g.polys.size());
        b.drawn.resize(g.polys.size());
        for (size_t f = 0; f < g.polys.size(); ++f) {
            b.planes[f].n = g.faceNormals[f];
            b.planes[f].d = g.polys[f].empty() ? 0.0 : dot(g.faceNormals[f], g.polys[f][0]);
            b.drawn[f] = !isIw8ToolMaterial(g.polyMaterial[f]);
        }
        finishEdgeBrush(b);
    }
    const std::vector<std::vector<uint32_t>> near = edgeNeighbours(brushes);
    for (size_t i = 0; i < items.size(); ++i) {
        PrimGeom& g = *items[i];
        // The brush and the brushes touching it, as they are now: its edges change only when one of them does.
        uint64_t key = (1469598103934665603ull ^ g.rev) * 1099511628211ull;
        key = (key ^ uint64_t(g.edgeSet)) * 1099511628211ull;
        for (uint32_t n : near[i]) {
            key = (key ^ uint64_t(items[n]->ref.item)) * 1099511628211ull;
            key = (key ^ items[n]->rev) * 1099511628211ull;
        }
        if (key == 0) key = 1;
        if (g.edgeKey == key && g.polyEdges.size() == g.polys.size()) continue;
        g.polyEdges = edgeBrushMeshes(brushes, i, near[i]);
        g.polyEdges.resize(g.polys.size());
        g.edgeKey = key;
        g.stamp = ++stampCounter_;
    }
}

void SceneCache::sync(const Document& doc) {''')

G = 'E:/IW8-Radiant/src/viewport/camera_geometry.cpp'
patch(G, '''            for (size_t k = 1; k + 1 < w.size(); ++k) {
                dst.push_back(tv(0));
                dst.push_back(tv(k));
                dst.push_back(tv(k + 1));
            }
        } else if (!wire) {''', '''            const EdgeFaceMesh* em = i < g->polyEdges.size() && !g->polyEdges[i].idx.empty() ? &g->polyEdges[i] : nullptr;
            if (em) {
                // The face as its edge mesh (core face_edges): the same surface with the edge data on its vertices. The
                // texture coordinates are the face's own, an affine function of the position.
                size_t far = 2;
                double best = -1;
                for (size_t k = 2; k < w.size(); ++k)
                    if (const double a = length(cross(w[1] - w[0], w[k] - w[0])); a > best) best = a, far = k;
                const Vec3 e1 = w[1] - w[0], e2 = w[far] - w[0];
                const double d11 = dot(e1, e1), d12 = dot(e1, e2), d22 = dot(e2, e2);
                const double det = d11 * d22 - d12 * d12;
                auto ev = [&](const EdgeVertex& q) {
                    const Vec3 d = q.pos - w[0];
                    const double b1 = dot(d, e1), b2 = dot(d, e2);
                    const double s = det > 1e-18 ? (b1 * d22 - b2 * d12) / det : 0.0, t = det > 1e-18 ? (b2 * d11 - b1 * d12) / det : 0.0;
                    GpuTexVertex v{float(q.pos.x), float(q.pos.y), float(q.pos.z),
                                   float(uv[0][0] + s * (uv[1][0] - uv[0][0]) + t * (uv[far][0] - uv[0][0])),
                                   float(uv[0][1] + s * (uv[1][1] - uv[0][1]) + t * (uv[far][1] - uv[0][1])), c};
                    v.nx = float(n.x), v.ny = float(n.y), v.nz = float(n.z);
                    v.tx = float(tan.x), v.ty = float(tan.y), v.tz = float(tan.z);
                    if (gameLm) {
                        float luv[2];
                        if (gameLm->uvFor(q.pos, n, centre, luv)) v.lu = luv[0], v.lv = luv[1];
                    }
                    v.edge = packEdge(q);
                    return v;
                };
                for (const uint32_t vi : em->idx) dst.push_back(ev(em->verts[vi]));
            } else {
                for (size_t k = 1; k + 1 < w.size(); ++k) {
                    dst.push_back(tv(0));
                    dst.push_back(tv(k));
                    dst.push_back(tv(k + 1));
                }
            }
        } else if (!wire) {''')
print('ok')
