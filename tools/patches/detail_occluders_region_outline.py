import io

# Two small open points of the plan:
# - Occlusion bake, per-brush control: a detail brush (`contents detail;`, Brush > Make Detail) is not an occluder,
#   as detail never blocked sight in Radiant maps. Structural brushes are, as before.
# - The region outline: the active Region's box is drawn in the 2D views (dashed) and in the camera view.

ROOT = 'E:/IW8-Radiant/'


def read(p):
    return io.open(ROOT + p, encoding='utf-8', newline='').read()


def write(p, s):
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s)


def patch(p, a, b, n=1):
    s = read(p)
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    write(p, s.replace(a, b))


# ---- detail brushes
patch('src/core/face_edges.h', '''    bool opaque = false;
};''', '''    bool opaque = false;
    // `contents detail;`. The edge, cover and hidden-face rules do not read it; the occlusion bake leaves detail
    // brushes out of its occluders (detail does not block sight).
    bool detail = false;
};''')
K = 'src/core/compile_iw8.cpp'
patch(K, '''                edgeData.index.emplace(p, edgeData.brushes.size());''', '''                b.detail = brushIsDetail(*p);
                edgeData.index.emplace(p, edgeData.brushes.size());''')
patch(K, '''        size_t count = 0, skewed = 0, fitted = 0, open = 0;''', '''        size_t count = 0, skewed = 0, fitted = 0, open = 0, detail = 0;''')
patch(K, '''            bool isBox = b.planes.size() == 6 && b.bounds.valid();''', '''            if (b.detail) {  // the mapper's own switch: Brush > Make Detail takes a brush out of the occluders
                ++detail;
                continue;
            }
            bool isBox = b.planes.size() == 6 && b.bounds.valid();''')
patch(K, '''" opaque brush(es) are too thin or too slanted to hold a box and " + std::to_string(open) +''',
      '''" opaque brush(es) are too thin or too slanted to hold a box, " + std::to_string(detail) +
                       " detail brush(es) are left out and " + std::to_string(open) +''')
patch(K, '''    // side, which leaves out thin slanted walls). Without the key the converter writes its one-cell tome.''',
      '''    // side, which leaves out thin slanted walls). A detail brush (`contents detail;`) is never an occluder: that is
    // the mapper's switch per brush. Without the key the converter writes its one-cell tome.''')

T = 'tests/custom_materials_tests.cpp'
s = read(T)
nl = '\r\n' if '\r\n' in s else '\n'
test = '''
// The occlusion bake leaves detail brushes out: of two equal opaque boxes, the one marked `contents detail;` is not
// an occluder.
TEST(compile_occlusion_bake_leaves_detail_brushes_out) {
    const std::string dir = freshDir("occluders_detail");
    writeBytes(dir + "/library/custom/wall.png", pngBytes(8, 8, flatRgba(8, 8, 120, 120, 120, 255)));
    writeText(dir + "/library/custom/wall.iw8mat.json", "{\\"name\\":\\"custom/wall\\",\\"color\\":\\"wall.png\\",\\"surface\\":\\"concrete\\"}");
    MaterialLibrary library;
    std::string err;
    CHECK(library.load({dir + "/library"}, &err));
    const std::string suffix = " custom/wall 128 128 0 0 0 0";
    std::string detailBrush = makeBrushPrimitive(makeBox({{512, 0, 0}, {640, 256, 192}}, suffix), "\\n")->text();
    const size_t open = detailBrush.find("{\\n");
    CHECK(open != std::string::npos);
    if (open == std::string::npos) return;
    detailBrush.insert(open + 2, " contents detail;\\n");
    const std::string text = "iwmap 4\\n{\\n\\"classname\\" \\"worldspawn\\"\\n\\"iw8r_umbra_bake\\" \\"1\\"\\n" +
                             makeBrushPrimitive(makeBox({{0, 0, 0}, {128, 256, 192}}, suffix), "\\n")->text() + detailBrush + "}\\n";
    Id next = 1;
    ParseResult pr = parseMap(text, next);
    CHECK(pr.map.has_value());
    if (!pr.map) return;
    CompileOptions opt;
    opt.mapId = "mp_occd";
    opt.library = &library;
    CompileReport rep;
    CHECK(compileIw8Extraction(*pr.map, dir + "/out", opt, rep, &err));
    const std::string world = readFileOrEmpty(dir + "/out/extracted/maps/mp/mp_occd.d3dbsp.replay-world.json");
    CHECK(contains(world, "\\"umbra_occluders\\":[[0,0,0,128,256,192]]"));
    bool said = false;
    for (const std::string& w : rep.warnings) said = said || (contains(w, "occlusion bake: 1 occluder box(es)") && contains(w, "1 detail brush(es) are left out"));
    if (!said)
        for (const std::string& w : rep.warnings)
            if (contains(w, "occlusion")) std::printf("  %s\\n", w.c_str());
    CHECK(said);
}
'''.replace('\n', nl)
if 'compile_occlusion_bake_leaves_detail_brushes_out' not in s:
    write(T, s.rstrip() + nl + test)

# ---- the region outline
patch('src/viewport/scene.h', '''    std::set<Id> hiddenByRegion; // primitives and point entities outside the active Region (see core/region.h)''',
      '''    std::set<Id> hiddenByRegion; // primitives and point entities outside the active Region (see core/region.h)
    std::vector<std::pair<Vec3, Vec3>> regionLines;  // the active Region's box as 12 edges (empty: no region)''')
patch('src/qtapp/editor_context.cpp', '''    std::set<Id> out = region ? itemsOutside(doc.map(), *region) : std::set<Id>{};
    if (out != scene.hiddenByRegion) {
        scene.hiddenByRegion = std::move(out);
        scene.invalidate();
    }''', '''    std::set<Id> out = region ? itemsOutside(doc.map(), *region) : std::set<Id>{};
    if (out != scene.hiddenByRegion) {
        scene.hiddenByRegion = std::move(out);
        scene.invalidate();
    }
    // The region's own outline, for the camera view (the 2D views draw it from `region`).
    std::vector<std::pair<Vec3, Vec3>> lines;
    if (region && region->valid()) {
        const Vec3 a = region->mins, b = region->maxs;
        const Vec3 c[8] = {{a.x, a.y, a.z}, {b.x, a.y, a.z}, {b.x, b.y, a.z}, {a.x, b.y, a.z},
                           {a.x, a.y, b.z}, {b.x, a.y, b.z}, {b.x, b.y, b.z}, {a.x, b.y, b.z}};
        for (int k = 0; k < 4; ++k) {
            lines.push_back({c[k], c[(k + 1) % 4]});
            lines.push_back({c[4 + k], c[4 + (k + 1) % 4]});
            lines.push_back({c[k], c[4 + k]});
        }
    }
    const auto same = [](const std::vector<std::pair<Vec3, Vec3>>& x, const std::vector<std::pair<Vec3, Vec3>>& y) {
        if (x.size() != y.size()) return false;
        for (size_t k = 0; k < x.size(); ++k)
            if (length(x[k].first - y[k].first) > 1e-9 || length(x[k].second - y[k].second) > 1e-9) return false;
        return true;
    };
    if (!same(lines, scene.regionLines)) {
        scene.regionLines = std::move(lines);
        scene.invalidate();
    }''')
patch('src/viewport/camera_geometry.cpp', '''        for (const auto& [a, b] : scene.navLines) {  // the build's navmesh (6.23), green''',
      '''        for (const auto& [a, b] : scene.regionLines) {  // the active Region's box, amber
            d.mLines.push_back(gv(a, 0xff30b4ffu));
            d.mLines.push_back(gv(b, 0xff30b4ffu));
        }
        for (const auto& [a, b] : scene.navLines) {  // the build's navmesh (6.23), green''')
patch('src/qtapp/grid_view.cpp', '''    // Tool previews: new brush rectangle, resized brushes, clipper points and line.''',
      '''    // The active Region: its box, dashed amber (what lies outside it is hidden).
    if (ctx_->region && ctx_->region->valid()) {
        p.setPen(QPen(QColor(255, 180, 48), 1.5, Qt::DashDotLine));
        p.setBrush(Qt::NoBrush);
        p.drawRect(QRectF(worldToWidget(ctx_->region->mins[H], ctx_->region->maxs[V]), worldToWidget(ctx_->region->maxs[H], ctx_->region->mins[V])));
    }
    // Tool previews: new brush rectangle, resized brushes, clipper points and line.''')
patch('src/qtapp/qt_gui_test.cpp', '''        r.check(!ctx.scene.hiddenByRegion.count(pil.item), "the pillar itself stays visible in the region");''',
      '''        r.check(!ctx.scene.hiddenByRegion.count(pil.item), "the pillar itself stays visible in the region");
        r.check(ctx.region && ctx.scene.regionLines.size() == 12, "the region's box has its outline (12 edges) for the views");''')
patch('src/qtapp/qt_gui_test.cpp', '''        r.check(!ctx.region && ctx.scene.hiddenByRegion.empty() && ctx.scene.items().size() == drawnBefore, "Region > Off shows everything again");''',
      '''        r.check(!ctx.region && ctx.scene.hiddenByRegion.empty() && ctx.scene.items().size() == drawnBefore && ctx.scene.regionLines.empty(),
                "Region > Off shows everything again and takes the outline away");''')
print('ok')
