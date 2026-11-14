import io

# Mount edges from placed models too (research 125 section 10): the convex pieces of a placed model's collision hull
# give edges like brushes do, with a stricter corner rule (a round shape built from facets is no corner). The solid
# box a model gets when it has no hull gives none. Worldspawn iw8r_mount_edges: 1 (default) brushes and models,
# "brushes" brushes only, 0 none.

ROOT = 'E:/IW8-Radiant/'


def patch(p, a, b, count=1):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == count, (p, s.count(a), a[:70])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


patch('src/core/mount_edges.h', '''    bool source = true;  // false: it only hides edges of others (a placed model's collision, a clip brush)
};''', '''    bool source = true;  // false: it only hides edges of others (a clip brush, a model's stand-in box)
    // A piece of a placed model's collision hull: a wall corner needs its two walls at least 60 degrees apart (45 for
    // a brush), so a barrel or a pipe built from facets gives no corners.
    bool model = false;
};''')
patch('src/core/mount_edges.h', '''struct MountEdgeStats {
    size_t corners = 0, ledges = 0;''', '''struct MountEdgeStats {
    size_t corners = 0, ledges = 0;
    size_t fromModels = 0;  // of those, the edges that come from placed models' hulls''')
patch('src/core/mount_edges.h', '''//   - ledge tops: a (nearly) level edge where a top face meets a wall below it.''',
      '''//   - ledge tops: a (nearly) level edge where a top face meets a wall below it.
// The same on the convex pieces of a placed model's collision hull (a crate, a barrier, a car's bonnet).''')

M = 'src/core/mount_edges.cpp'
patch(M, '''    bool ledge = false;
    std::vector<std::pair<double, double>> parts;
};''', '''    bool ledge = false;
    bool model = false;  // every part comes from a placed model's hull
    std::vector<std::pair<double, double>> parts;
};''')
patch(M, '''                    if (c > 0.71 || c < -0.71) continue;''', '''                    if (c > (s.model ? 0.5 : 0.71) || c < -0.71) continue;''')
patch(M, '''                if (line.parts.empty()) line.dir = d, line.origin = origin, line.first = n1, line.second = n2, line.ledge = ledge;''',
      '''                if (line.parts.empty()) line.dir = d, line.origin = origin, line.first = n1, line.second = n2, line.ledge = ledge, line.model = s.model;
                line.model = line.model && s.model;''')
patch(M, '''    struct Found {
        MapEdge edge;
        double length;
        bool ledge;
    };''', '''    struct Found {
        MapEdge edge;
        double length;
        bool ledge, model;
    };''')
patch(M, '''            found.push_back({e, to - from, line.ledge});''', '''            found.push_back({e, to - from, line.ledge, line.model});''')
patch(M, '''        (f.ledge ? st.ledges : st.corners)++;''', '''        (f.ledge ? st.ledges : st.corners)++;
        if (f.model) ++st.fromModels;''')

C = 'src/core/compile_iw8.cpp'
patch(C, '''    bool mountSource = false;   // a solid brush of the map itself: its open corners and ledge tops become mount edges
};''', '''    bool mountSource = false;   // a solid brush of the map itself: its open corners and ledge tops become mount edges
    bool modelHull = false;     // a convex piece of a placed model's collision hull (mount edges with the model rule)
};''')
patch(C, '''                    hb.surfaceType = surfaceOfModel(*mv);''', '''                    hb.surfaceType = surfaceOfModel(*mv);
                    hb.modelHull = true;''')
patch(C, '''        if (off && *off == "0") {
            warn.push_back("mount edges off (worldspawn iw8r_mount_edges 0): weapons cannot be mounted on this map");
        } else {''', '''        const bool brushesOnly = off && *off == "brushes";
        if (off && *off == "0") {
            warn.push_back("mount edges off (worldspawn iw8r_mount_edges 0): weapons cannot be mounted on this map");
        } else {''')
patch(C, '''                solids.push_back({b.planes, b.bounds, b.mountSource});''',
      '''                MountSolid solid{b.planes, b.bounds, b.mountSource || (b.modelHull && !brushesOnly), b.modelHull};
                if (b.modelHull) {
                    // A hull piece carries its whole model's box: its own box, from its corners, makes the probes cheap.
                    AABB own;
                    for (const Winding& w : buildBrushWindings(b.planes))
                        for (const Vec3& v : w) own.add(v);
                    if (!own.valid()) continue;
                    solid.bounds = own;
                }
                solids.push_back(std::move(solid));''')
patch(C, '''" ledge top(s) where a weapon can be mounted (from the world's solid brushes; " + std::to_string(st.hidden) +''',
      '''" ledge top(s) where a weapon can be mounted (" + std::to_string(st.corners + st.ledges - st.fromModels) +
                               " on brushes, " + std::to_string(st.fromModels) + " on placed models" +
                               (brushesOnly ? " (worldspawn iw8r_mount_edges brushes)" : "") + "; " + std::to_string(st.hidden) +''')

T = 'tests/mount_edges_tests.cpp'
s = io.open(ROOT + T, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
s = s.rstrip('\r\n') + nl + '''
TEST(mount_edges_model_hulls) {
    // A placed model's hull piece gives edges like a brush, but a round shape built from facets gives no corners: an
    // eight-sided post (45 degrees between neighbouring walls) is a corner source as a brush and none as a model.
    MountSolid post;
    for (int k = 0; k < 8; ++k) {
        const double a = (22.5 + 45.0 * k) * 3.14159265358979323846 / 180.0;
        post.planes.push_back({{std::cos(a), std::sin(a), 0}, 24.0});
    }
    post.planes.push_back({{0, 0, 1}, 96});
    post.planes.push_back({{0, 0, -1}, 0});
    post.bounds.add({-26, -26, 0});
    post.bounds.add({26, 26, 96});
    MountEdgeStats asBrush, asModel;
    findMountEdges({post}, {}, &asBrush);
    post.model = true;
    findMountEdges({post}, {}, &asModel);
    CHECK(asBrush.corners == 8 && asBrush.fromModels == 0);
    CHECK(asModel.corners == 0);
    // A crate that is a model: its corners and tops count, and are told apart in the numbers.
    MountSolid crate = box(0, 0, 0, 48, 48, 40);
    crate.model = true;
    MountEdgeStats st;
    const auto edges = findMountEdges({box(-200, -200, -16, 200, 200, 0), crate}, {}, &st);
    CHECK(st.corners == 4 && st.ledges == 8 && st.fromModels == 8);
    CHECK(edges.size() == 12);
    // A wall made of a brush and a model piece side by side: the joined top edge is not "from models" alone.
    MountSolid half = box(64, 0, 0, 128, 16, 48);
    half.model = true;
    MountEdgeStats mixed;
    findMountEdges({box(0, 0, 0, 64, 16, 48), half}, {}, &mixed);
    CHECK(mixed.ledges == 4 && mixed.corners == 4);
    CHECK(mixed.fromModels == 3);  // the model's own end: two corners and the short top edge
}
'''.replace('\n', nl)
io.open(ROOT + T, 'w', encoding='utf-8', newline='').write(s)
print('ok')
