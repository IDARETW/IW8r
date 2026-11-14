import io

# One function emits a textured brush face for both camera geometry paths (the kept streams of camera_geometry.cpp and
# the fresh build of scene.cpp that the self-check compares them with): the face mesh when the item has one (core
# face_edges), else the plain fan. The fresh build also follows Game Lighting's "no brush outlines".


def patch(p, a, b, n=1):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


H = 'E:/IW8-Radiant/src/viewport/scene.h'
patch(H, '''class SceneCache {''', '''struct GameLightmap;
// The textured triangles of poly `i` of an item: its face mesh when the item has one (PrimGeom::polyEdges; nothing for
// an empty mesh, a face wholly under other faces), else the winding as a fan. `uv` are the winding's texture
// coordinates (the mesh's are the same affine function of the position), `centre` the winding's centre when gameLm is
// given (its lightmap UVs are kept per face corner).
void appendTexturedFace(const PrimGeom& g, size_t i, const Vec3& n, const Vec3& tan, uint32_t colour, const GameLightmap* gameLm,
                        const Vec3& centre, std::vector<GpuTexVertex>& out);

class SceneCache {''')

S = 'E:/IW8-Radiant/src/viewport/scene.cpp'
patch(S, '''void SceneCache::updateFaceEdges() {''', '''void appendTexturedFace(const PrimGeom& g, size_t i, const Vec3& n, const Vec3& tan, uint32_t colour, const GameLightmap* gameLm,
                        const Vec3& centre, std::vector<GpuTexVertex>& out) {
    const Winding& w = g.polys[i];
    const auto& uv = g.polyUV[i];
    const EdgeFaceMesh* em = i < g.polyEdges.size() ? &g.polyEdges[i] : nullptr;
    if (em && em->idx.empty()) return;  // wholly under coplanar faces of other brushes: not drawn
    if (!em) {
        const auto tv = [&](size_t k) {
            GpuTexVertex v{float(w[k].x), float(w[k].y), float(w[k].z), uv[k][0], uv[k][1], colour};
            v.nx = float(n.x), v.ny = float(n.y), v.nz = float(n.z);
            v.tx = float(tan.x), v.ty = float(tan.y), v.tz = float(tan.z);
            if (gameLm) {
                float luv[2];
                if (gameLm->uvFor(w[k], n, centre, luv)) v.lu = luv[0], v.lv = luv[1];
            }
            return v;
        };
        for (size_t k = 1; k + 1 < w.size(); ++k) {
            out.push_back(tv(0));
            out.push_back(tv(k));
            out.push_back(tv(k + 1));
        }
        return;
    }
    // The face as its mesh: the texture coordinates are the face's own, an affine function of the position, taken
    // from the corner pair that spans the face best.
    size_t far = 2;
    double best = -1;
    for (size_t k = 2; k < w.size(); ++k)
        if (const double a = length(cross(w[1] - w[0], w[k] - w[0])); a > best) best = a, far = k;
    const Vec3 e1 = w[1] - w[0], e2 = w[far] - w[0];
    const double d11 = dot(e1, e1), d12 = dot(e1, e2), d22 = dot(e2, e2);
    const double det = d11 * d22 - d12 * d12;
    // The preview bake's lightmap UVs are kept per face corner: the same affine blend gives the others.
    float l0[2], l1[2], l2[2];
    const bool haveLm = gameLm && gameLm->uvFor(w[0], n, centre, l0) && gameLm->uvFor(w[1], n, centre, l1) && gameLm->uvFor(w[far], n, centre, l2);
    for (const uint32_t vi : em->idx) {
        const EdgeVertex& q = em->verts[vi];
        const Vec3 d = q.pos - w[0];
        const double b1 = dot(d, e1), b2 = dot(d, e2);
        const double s = det > 1e-18 ? (b1 * d22 - b2 * d12) / det : 0.0, t = det > 1e-18 ? (b2 * d11 - b1 * d12) / det : 0.0;
        GpuTexVertex v{float(q.pos.x), float(q.pos.y), float(q.pos.z), float(uv[0][0] + s * (uv[1][0] - uv[0][0]) + t * (uv[far][0] - uv[0][0])),
                       float(uv[0][1] + s * (uv[1][1] - uv[0][1]) + t * (uv[far][1] - uv[0][1])), colour};
        v.nx = float(n.x), v.ny = float(n.y), v.nz = float(n.z);
        v.tx = float(tan.x), v.ty = float(tan.y), v.tz = float(tan.z);
        if (haveLm) {
            v.lu = float(l0[0] + s * (l1[0] - l0[0]) + t * (l2[0] - l0[0]));
            v.lv = float(l0[1] + s * (l1[1] - l0[1]) + t * (l2[1] - l0[1]));
        }
        v.edge = packEdge(q);
        out.push_back(v);
    }
}

void SceneCache::updateFaceEdges() {''')
# The fresh build.
s = io.open(S, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
a = s.index('                auto tv = [&](size_t k) {' + nl + '                    GpuTexVertex v{float(w[k].x), float(w[k].y), float(w[k].z), uv[k][0], uv[k][1], c};')
b_marker = ('                for (size_t k = 1; k + 1 < w.size(); ++k) {' + nl + '                    out.push_back(tv(0));' + nl +
            '                    out.push_back(tv(k));' + nl + '                    out.push_back(tv(k + 1));' + nl + '                }' + nl)
b = s.index(b_marker, a) + len(b_marker)
s = s[:a] + '                appendTexturedFace(*g, i, n, tan, c, gameLm, centre, out);' + nl + s[b:]
io.open(S, 'w', encoding='utf-8', newline='').write(s)
patch(S, '''            if (!g->edges.empty() || (g->meshPoly(i) && !sel && !wire)) continue;
            auto& dst = sel ? overlay : lines;''', '''            if (!g->edges.empty() || (g->meshPoly(i) && !sel && !wire)) continue;
            // Game Lighting: no editor outlines on brush faces (camera_geometry.cpp has the same rule).
            if (!sel && !wire && g->kind == PrimGeom::Brush && !g->volume && scene.gameLookActive() && !scene.gameOutlines) continue;
            auto& dst = sel ? overlay : lines;''')

G = 'E:/IW8-Radiant/src/viewport/camera_geometry.cpp'
s = io.open(G, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
a = s.index('            auto tv = [&](size_t k) {' + nl + '                GpuTexVertex v{float(w[k].x), float(w[k].y), float(w[k].z), uv[k][0], uv[k][1], c};')
b_marker = '        } else if (!wire) {'
b = s.index(b_marker, a)
s = s[:a] + '            appendTexturedFace(*g, i, n, tan, c, gameLm, centre, dst);' + nl + s[b:]
io.open(G, 'w', encoding='utf-8', newline='').write(s)
print('ok')
