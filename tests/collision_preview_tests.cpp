// Build preview: collision hulls read back from the extraction JSON (src/core/collision_preview.*).
#include "collision_preview.h"
#include "test_harness.h"

using namespace iw8r;

TEST(collision_preview_box_and_wedge) {
    // A box given only by mins/maxs (12 edges), and a wedge: the box cut by a slanted plane, marked as a ladder.
    const std::string text = R"({"schema":1,"brushes":[
        {"contents":1,"mins":[0,0,0],"maxs":[64,64,64],"planes":[]},
        {"contents":65536,"mins":[100,0,0],"maxs":[164,64,64],"planes":[[0.7071067811865476,0,0.7071067811865476,161.2]],"ladder_planes":[]},
        {"contents":1,"mins":[0,0,0],"maxs":[1,1,1],"planes":[[1,0,0,-5]]}]})";
    std::vector<CollisionHullEdges> hulls;
    CHECK(readCollisionHulls(text, hulls));
    CHECK(hulls.size() == 2);  // the empty-volume brush is skipped
    CHECK(hulls[0].edges.size() == 12 && hulls[0].contents == 1 && !hulls[0].ladder);
    CHECK(hulls[1].contents == 65536 && hulls[1].ladder && hulls[1].edges.size() > 12);
    std::vector<CollisionHullEdges> none;
    CHECK(!readCollisionHulls("{}", none));
    // The build's mount edges come as one entry after the hulls.
    std::vector<CollisionHullEdges> withMount;
    CHECK(readCollisionHulls("{\"brushes\":[],\"mount_edges\":[[0,0,0,0,0,64,0,15944,1],[8,0,40,72,0,40,0,15944,1]]}", withMount));
    CHECK(withMount.size() == 1 && withMount[0].mount && withMount[0].edges.size() == 2);
    if (withMount.size() == 1 && withMount[0].edges.size() == 2) {
        CHECK(withMount[0].edges[0].second.z == 64 && withMount[0].edges[1].first.x == 8 && withMount[0].edges[1].second.x == 72);
    }
}

// Compiled world preview (3.10): triangle edges of world.json surfaces, shared edges once.
TEST(compiled_surfaces_edges) {
    const std::string text = R"({"surfaces":[{"vertices":[{"position":[0,0,0]},{"position":[64,0,0]},{"position":[64,64,0]},{"position":[0,64,0]}],
        "indices":[0,1,2,0,2,3]}]})";
    std::vector<std::pair<Vec3, Vec3>> edges;
    size_t tris = 0;
    std::string err;
    CHECK(readCompiledSurfaces(text, edges, &tris, &err));
    CHECK(tris == 2 && edges.size() == 5);  // a quad's 4 sides + the shared diagonal once
}
