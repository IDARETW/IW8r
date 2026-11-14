import io

# The camera view of a paint mesh on a caulked brush face (paint_mesh_on_faces.py): the flat patch and the tool face
# under it lie in one plane and would fight for every pixel. A tool face of a brush that lies wholly under a flat patch
# is not drawn. And the editor command: Brush > Paint Mesh on Faces.

ROOT = 'E:/IW8-Radiant/'


def patch(p, a, b, n=1):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


G = 'src/viewport/camera_geometry.cpp'
patch(G, '''    MatInfo& mat(const std::string& m) { return mats[m]; }
    void genItem(const PrimGeom* g, bool sel, ItemOut& out);''', '''    // Flat patches that touch a brush with tool faces (a paint mesh on a caulked face): a tool face wholly under one is
    // not drawn, or the two would fight for every pixel. Found again at every update (findCovers); their hash is part
    // of the global key, so a change makes every item again.
    struct FlatCover {
        Vec3 n;
        double d = 0;
        AABB box;
    };
    struct CoverInfo {
        uint64_t stamp = 0;
        bool flat = false, tool = false;
        FlatCover cover;
    };
    std::vector<FlatCover> covers;
    std::unordered_map<const PrimGeom*, CoverInfo> coverInfo;
    uint64_t findCovers(const std::vector<const PrimGeom*>& items);
    bool toolFaceCovered(const Winding& w, const Vec3& n) const;

    MatInfo& mat(const std::string& m) { return mats[m]; }
    void genItem(const PrimGeom* g, bool sel, ItemOut& out);''')
patch(G, '''void CameraGeometry::Impl::genItem(const PrimGeom* g, bool sel, ItemOut& out) {''', '''uint64_t CameraGeometry::Impl::findCovers(const std::vector<const PrimGeom*>& items) {
    covers.clear();
    if (coverInfo.size() > items.size() * 2 + 64) coverInfo.clear();  // items that are gone
    std::vector<const CoverInfo*> flats;
    std::vector<const PrimGeom*> toolBrushes;
    for (const PrimGeom* g : items) {
        if (g->kind == PrimGeom::PointEntity || g->volume) continue;
        CoverInfo& ci = coverInfo[g];
        if (ci.stamp != g->stamp) {
            ci = CoverInfo{};
            ci.stamp = g->stamp;
            if (g->kind == PrimGeom::Patch) {
                // Flat: every vertex in the plane of the first triangle that has an area.
                bool have = false;
                for (const Winding& w : g->polys) {
                    if (w.size() < 3) continue;
                    const Vec3 c = cross(w[1] - w[0], w[2] - w[0]);
                    if (!(length(c) > 1e-9)) continue;
                    ci.cover.n = normalize(c);
                    ci.cover.d = dot(ci.cover.n, w[0]);
                    have = true;
                    break;
                }
                bool flat = have;
                for (size_t i = 0; i < g->polys.size() && flat; ++i)
                    for (const Vec3& v : g->polys[i])
                        if (std::fabs(dot(ci.cover.n, v) - ci.cover.d) > 0.01) {
                            flat = false;
                            break;
                        }
                ci.flat = flat;
                ci.cover.box = g->bounds;
            } else {
                for (const std::string& m : g->polyMaterial)
                    if (isIw8ToolMaterial(m)) {
                        ci.tool = true;
                        break;
                    }
            }
        }
        if (ci.flat) flats.push_back(&ci);
        else if (ci.tool) toolBrushes.push_back(g);
    }
    if (flats.empty() || toolBrushes.empty()) return 0;
    uint64_t h = 1469598103934665603ull;
    for (const CoverInfo* f : flats) {
        const AABB& a = f->cover.box;
        bool touches = false;
        for (const PrimGeom* b : toolBrushes) {
            const AABB& o = b->bounds;
            if (a.mins.x <= o.maxs.x + 0.1 && a.maxs.x >= o.mins.x - 0.1 && a.mins.y <= o.maxs.y + 0.1 && a.maxs.y >= o.mins.y - 0.1 &&
                a.mins.z <= o.maxs.z + 0.1 && a.maxs.z >= o.mins.z - 0.1) {
                touches = true;
                break;
            }
        }
        if (!touches) continue;
        covers.push_back(f->cover);
        for (double v : {f->cover.n.x, f->cover.n.y, f->cover.n.z, f->cover.d, a.mins.x, a.mins.y, a.mins.z, a.maxs.x, a.maxs.y, a.maxs.z})
            h = mixDouble(h, v);
    }
    return covers.empty() ? 0 : h;
}

bool CameraGeometry::Impl::toolFaceCovered(const Winding& w, const Vec3& n) const {
    for (const FlatCover& c : covers) {
        if (std::fabs(dot(c.n, n)) < 0.999) continue;
        bool under = true;
        for (const Vec3& v : w) {
            if (std::fabs(dot(c.n, v) - c.d) > 0.05 || v.x < c.box.mins.x - 0.1 || v.x > c.box.maxs.x + 0.1 || v.y < c.box.mins.y - 0.1 ||
                v.y > c.box.maxs.y + 0.1 || v.z < c.box.mins.z - 0.1 || v.z > c.box.maxs.z + 0.1) {
                under = false;
                break;
            }
        }
        if (under) return true;
    }
    return false;
}

void CameraGeometry::Impl::genItem(const PrimGeom* g, bool sel, ItemOut& out) {''')
patch(G, '''            // Not in the game: no sun shadow falls on it either (sun share 0 in the lit encoding).
            if (noCast && encoded) c = (c & 0x00ffffffu) | 0x80000000u;
            for (size_t k = 1; k + 1 < w.size(); ++k) {''', '''            // Not in the game: no sun shadow falls on it either (sun share 0 in the lit encoding).
            if (noCast && encoded) c = (c & 0x00ffffffu) | 0x80000000u;
            // A tool face under a flat patch (a paint mesh on a caulked face) is left to the patch.
            const bool covered = tool && !sel && !wire && !covers.empty() && g->kind == PrimGeom::Brush && i < g->faceNormals.size() &&
                                 toolFaceCovered(w, g->faceNormals[i]);
            for (size_t k = 1; !covered && k + 1 < w.size(); ++k) {''')
patch(G, '''    const std::vector<const PrimGeom*>& items = scene.items();
    const size_t n = items.size();
    GeoStream<GpuVertex>* fs[kFlatStreams] = {&shadowOnly, &casters, &noCast, &lines};''', '''    const std::vector<const PrimGeom*>& items = scene.items();
    const size_t n = items.size();
    key = mixBits(key, d.findCovers(items));
    GeoStream<GpuVertex>* fs[kFlatStreams] = {&shadowOnly, &casters, &noCast, &lines};''')

# ---- the command
H = 'src/qtapp/main_window.h'
patch(H, '''    QByteArray webRenderFrame(const struct WebCamera& cam, int w, int h, int quality);''',
      '''    QByteArray webRenderFrame(const struct WebCamera& cam, int w, int h, int quality);
    // Brush > Paint Mesh on Faces (ROADMAP 5.9 / 8.11): mesh patches with a control point every `spacing` units on the
    // selected faces (or on every drawn face of the selected brushes); the covered faces become caulk. One undo step.
    // Returns the number of patches made.
    size_t addPaintMeshes(double spacing);''')
M = 'src/qtapp/main_window.cpp'
patch(M, '''    addCommand(brush, "ArbitraryRotation", "Arbitrary Rotation...", QKeySequence(), [this] {''',
      '''    addCommand(brush, "PaintMeshOnFaces", "Paint Mesh on Faces (for blend painting)...", QKeySequence(), [this] {
        // A brush face has only its corners: nothing to paint a blend on. This lays a mesh patch on it.
        bool ok = false;
        const int spacing = QInputDialog::getInt(this, "Paint Mesh on Faces", "A control point every (units):",
                                                 Settings::instance().value("paintMesh/spacing", 32).toInt(), 4, 512, 4, &ok);
        if (!ok) return;
        Settings::instance().setValue("paintMesh/spacing", spacing);
        if (!addPaintMeshes(spacing)) return;
        if (!ctx_.vertexPaint)
            if (QAction* vp = action("VertexPaintMode")) vp->trigger();  // asks for the colour and the brush, then paints
    });
    addCommand(brush, "ArbitraryRotation", "Arbitrary Rotation...", QKeySequence(), [this] {''')
patch(M, '''Vec3 MainWindow::followCameraPos() const {''', '''size_t MainWindow::addPaintMeshes(double spacing) {
    const size_t made = addPaintMeshesToBrushFaces(ctx_.doc, spacing);
    if (!made) {
        ctx_.setStatus("Paint mesh: select brush faces (Ctrl+Shift+click) or whole brushes first");
        return 0;
    }
    ctx_.scene.invalidate();
    ctx_.documentEdited(QString("Laid %1 paint mesh(es) on the faces; the faces under them are caulk now (one undo step). "
                                "Paint them with Patch > Vertex Paint Mode.").arg(made));
    return made;
}

Vec3 MainWindow::followCameraPos() const {''')
print('ok')
