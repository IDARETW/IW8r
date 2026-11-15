#include "own_decals.h"
#include "test_harness.h"

#include <cmath>
#include <string>

using namespace iw8r;

// Research 132 G6: the mapper's own decal volumes are projected for the viewport as the build makes them.
TEST(own_decals_projected_on_brushes) {
    const char* m = " custom/a 128 128 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0\n";
    // A floor (z -16..0) and a wall (x 64..80); a decal looking down onto the floor (angles 90 0 0: forward is -z), 64
    // wide; a decal looking +x into the wall; one with no material (left out).
    const std::string text = std::string("iwmap 4\n{\n\"classname\" \"worldspawn\"\n{\n") +
                             " ( 256 256 -16 ) ( -256 256 -16 ) ( -256 -256 -16 )" + m + " ( -256 -256 0 ) ( -256 256 0 ) ( 256 256 0 )" + m +
                             " ( -256 -256 0 ) ( 256 -256 0 ) ( 256 -256 -16 )" + m + " ( 256 256 0 ) ( -256 256 0 ) ( -256 256 -16 )" + m +
                             " ( 256 -256 0 ) ( 256 256 0 ) ( 256 256 -16 )" + m + " ( -256 256 0 ) ( -256 -256 0 ) ( -256 -256 -16 )" + m +
                             "}\n{\n" +
                             " ( 80 256 0 ) ( 64 256 0 ) ( 64 -256 0 )" + m + " ( 64 -256 128 ) ( 64 256 128 ) ( 80 256 128 )" + m +
                             " ( 64 -256 128 ) ( 80 -256 128 ) ( 80 -256 0 )" + m + " ( 80 256 128 ) ( 64 256 128 ) ( 64 256 0 )" + m +
                             " ( 80 -256 128 ) ( 80 256 128 ) ( 80 256 0 )" + m + " ( 64 256 128 ) ( 64 -256 128 ) ( 64 -256 0 )" + m + "}\n}\n" +
                             "{\n\"classname\" \"iw8r_decal\"\n\"origin\" \"0 0 2\"\n\"angles\" \"90 0 0\"\n\"size\" \"16 64 64\"\n\"material\" \"decal/puddle\"\n\"opacity\" \"0.5\"\n}\n"
                             "{\n\"classname\" \"iw8r_decal\"\n\"origin\" \"60 0 64\"\n\"angles\" \"0 0 0\"\n\"size\" \"16 32 32\"\n\"material\" \"decal/poster\"\n}\n"
                             "{\n\"classname\" \"iw8r_decal\"\n\"origin\" \"0 100 2\"\n}\n";
    Id next = 1;
    ParseResult pr = parseMap(text, next);
    CHECK(pr.map.has_value());
    if (!pr.map) return;
    std::vector<Id> ids;
    const std::vector<OwnDecal> decals = projectOwnDecals(*pr.map, &ids);
    CHECK(decals.size() == 2 && ids.size() == 2);
    if (decals.size() != 2) return;
    // The floor decal: pieces on the floor's top (z 0, lifted a little), within the 64 x 64 box, facing up.
    CHECK(decals[0].material == "decal/puddle" && decals[0].opacity == 0.5 && !decals[0].pieces.empty());
    double area = 0;
    bool onFloor = true;
    for (const OwnDecalPiece& p : decals[0].pieces) {
        area += 0.5 * length(cross(p.pos[1] - p.pos[0], p.pos[2] - p.pos[0]));
        for (const Vec3& v : p.pos) onFloor = onFloor && std::fabs(v.z - 0.1) < 1e-3 && std::fabs(v.x) <= 32.001 && std::fabs(v.y) <= 32.001;
        onFloor = onFloor && p.normal.z > 0.99;
    }
    CHECK(onFloor && std::fabs(area - 64.0 * 64.0) < 1.0);
    // Its material UV spans the box: 0..1 across 64 units.
    float umin = 1e9f, umax = -1e9f;
    for (const OwnDecalPiece& p : decals[0].pieces)
        for (const auto& uv : p.uv) umin = std::min(umin, uv[0]), umax = std::max(umax, uv[0]);
    CHECK(std::fabs(umin) < 1e-3f && std::fabs(umax - 1) < 1e-3f);
    // The wall decal: on the wall's face at x = 64 (lifted toward -x), 32 x 32.
    double wallArea = 0;
    bool onWall = true;
    for (const OwnDecalPiece& p : decals[1].pieces) {
        wallArea += 0.5 * length(cross(p.pos[1] - p.pos[0], p.pos[2] - p.pos[0]));
        for (const Vec3& v : p.pos) onWall = onWall && std::fabs(v.x - 63.9) < 1e-3;
    }
    CHECK(onWall && std::fabs(wallArea - 32.0 * 32.0) < 1.0);
    OwnDecalBox box;
    CHECK(!ownDecalBox(pr.map->entities[3], box));  // no material
}
