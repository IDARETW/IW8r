#include "play_world.h"
#include "collision_world.h"

#include <cmath>
#include "test_harness.h"

#include <filesystem>
#include <fstream>

using namespace iw8r;
namespace fs = std::filesystem;

// Research 131 gap 9: the play preview collides with what the built map collides with: the map's brushes, the
// contents of prefabs and placed models.
namespace {

const char* kFloor =
    "{\n"
    " ( 512 512 -16 ) ( -512 512 -16 ) ( -512 -512 -16 ) custom/a 128 128 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0\n"
    " ( -512 -512 0 ) ( -512 512 0 ) ( 512 512 0 ) custom/a 128 128 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0\n"
    " ( -512 -512 0 ) ( 512 -512 0 ) ( 512 -512 -16 ) custom/a 128 128 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0\n"
    " ( 512 512 0 ) ( -512 512 0 ) ( -512 512 -16 ) custom/a 128 128 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0\n"
    " ( 512 -512 0 ) ( 512 512 0 ) ( 512 512 -16 ) custom/a 128 128 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0\n"
    " ( -512 512 0 ) ( -512 -512 0 ) ( -512 -512 -16 ) custom/a 128 128 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0\n"
    "}\n";

// A cube model, 32 units, in file (Y-up) coordinates from 0 to 32 on every axis.
std::shared_ptr<ImportedModel> cube() {
    auto m = std::make_shared<ImportedModel>();
    m->name = "cube";
    ImportedMesh mesh;
    for (int i = 0; i < 8; ++i) mesh.pos.push_back(Vec3{double(i & 1) * 32, double((i >> 1) & 1) * 32, double((i >> 2) & 1) * 32});
    const int faces[6][4] = {{0, 1, 3, 2}, {4, 6, 7, 5}, {0, 4, 5, 1}, {2, 3, 7, 6}, {0, 2, 6, 4}, {1, 5, 7, 3}};
    for (const auto& f : faces) {
        for (const int k : {0, 1, 2, 0, 2, 3}) mesh.idx.push_back(uint32_t(f[k]));
    }
    m->meshes.push_back(mesh);
    for (const Vec3& p : mesh.pos) m->bounds.add(p);
    return m;
}

// Does a straight walk at knee height from `from` to `to` hit something before the end?
bool blocked(const CollisionWorld& w, const Vec3& from, const Vec3& to) {
    return w.traceBox(from, to, Vec3{-15, -15, 0}, Vec3{15, 15, 60}).fraction < 0.999;
}

}  // namespace

TEST(play_world_models_and_prefabs) {
    const fs::path dir = fs::temp_directory_path() / "iw8r_play_world";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    // A prefab: one wall brush from x 0..16, y -64..64, z 0..96 (prefab-local).
    {
        std::ofstream f(dir / "wall.map");
        f << "iwmap 4\n{\n\"classname\" \"worldspawn\"\n{\n"
             " ( 16 64 0 ) ( 0 64 0 ) ( 0 -64 0 ) custom/a 128 128 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0\n"
             " ( 0 -64 96 ) ( 0 64 96 ) ( 16 64 96 ) custom/a 128 128 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0\n"
             " ( 0 -64 96 ) ( 16 -64 96 ) ( 16 -64 0 ) custom/a 128 128 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0\n"
             " ( 16 64 96 ) ( 0 64 96 ) ( 0 64 0 ) custom/a 128 128 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0\n"
             " ( 16 -64 96 ) ( 16 64 96 ) ( 16 64 0 ) custom/a 128 128 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0\n"
             " ( 0 64 96 ) ( 0 -64 96 ) ( 0 -64 0 ) custom/a 128 128 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0\n"
             "}\n}\n";
    }
    const std::string text = std::string("iwmap 4\n{\n\"classname\" \"worldspawn\"\n") + kFloor +
                             "}\n"
                             "{\n\"classname\" \"misc_model\"\n\"model\" \"cube\"\n\"origin\" \"100 16 0\"\n}\n"                  // x 100..132, y -16..16 (file z is map -y)
                             "{\n\"classname\" \"script_model\"\n\"model\" \"cube\"\n\"origin\" \"200 16 0\"\n\"targetname\" \"spinner\"\n\"iw8r_spin\" \"4\"\n}\n"
                             "{\n\"classname\" \"script_model\"\n\"model\" \"cube\"\n\"origin\" \"-232 16 0\"\n\"iw8r_no_collision\" \"1\"\n\"targetname\" \"ghost\"\n}\n"
                             "{\n\"classname\" \"misc_model\"\n\"model\" \"cube\"\n\"origin\" \"0 200 0\"\n\"modelscale\" \"2\"\n\"angles\" \"0 45 0\"\n}\n"
                             "{\n\"classname\" \"misc_prefab\"\n\"model\" \"wall.map\"\n\"origin\" \"-100 0 0\"\n}\n";
    Id next = 1;
    ParseResult pr = parseMap(text, next);
    CHECK(pr.map.has_value());
    if (!pr.map) return;
    const auto model = cube();
    PlayWorldOptions opt;
    opt.mapPath = (dir / "test.map").string();
    opt.modelOf = [&](const Entity& e) -> std::shared_ptr<const ImportedModel> {
        const std::string* m = e.value("model");
        return m && *m == "cube" ? model : nullptr;
    };
    CollisionWorld plain;
    plain.build(*pr.map);
    CollisionWorld world;
    PlayWorldStats st;
    buildPlayWorld(*pr.map, opt, world, &st);
    CHECK(st.prefabs == 1 && st.models == 2 && st.hullPieces == 2 && st.boxes == 0);
    // The map's own brushes alone: nothing stops a walk along y = 0 at x 40..160 or x -160..-60.
    CHECK(!blocked(plain, Vec3{40, 0, 1}, Vec3{160, 0, 1}));
    CHECK(!blocked(plain, Vec3{-60, 0, 1}, Vec3{-160, 0, 1}));
    // With the built map's collision: the crate (x 100..132) and the prefab's wall (x -100..-84) stop the player.
    CHECK(blocked(world, Vec3{40, 0, 1}, Vec3{160, 0, 1}));
    CHECK(blocked(world, Vec3{-60, 0, 1}, Vec3{-160, 0, 1}));
    // The crate's top is 32 high: the player stands on it.
    const auto down = world.traceBox(Vec3{116, 0, 80}, Vec3{116, 0, -10}, Vec3{-15, -15, 0}, Vec3{15, 15, 60});
    CHECK(down.fraction < 1 && std::abs(down.end.z - 32) < 0.5);
    // The spinning model is a live entity and the iw8r_no_collision one has none: the player walks through both.
    CHECK(!blocked(world, Vec3{170, 0, 1}, Vec3{260, 0, 1}));
    CHECK(!blocked(world, Vec3{-170, 0, 1}, Vec3{-260, 0, 1}));
    // The scaled, turned cube (64 units, turned 45 degrees about its corner at 0 200): its far corner is about 90 units
    // out along the diagonal; a walk along x through y 245 hits it.
    CHECK(blocked(world, Vec3{-80, 245, 1}, Vec3{80, 245, 1}));
    // The rule for which models collide.
    CHECK(playModelCollides(pr.map->entities[1], *pr.map));
    CHECK(!playModelCollides(pr.map->entities[2], *pr.map));
    CHECK(!playModelCollides(pr.map->entities[3], *pr.map));
    // A model with no source gives nothing; the plain world is unchanged by an empty option set.
    CollisionWorld bare;
    buildPlayWorld(*pr.map, PlayWorldOptions{}, bare, &st);
    CHECK(st.models == 0 && !blocked(bare, Vec3{40, 0, 1}, Vec3{160, 0, 1}));
    fs::remove_all(dir, ec);
}

// A stock map's world in the play preview (owner checklist 2026-11-15: walking a stock map fell back to noclip, its
// world gave no collision): triangles added as facets, and the grid broad phase giving the same traces as a plain
// pass over every solid.
TEST(collision_triangles_and_grid) {
    CollisionWorld small, big;
    // A floor of 2 x 64 x 64 triangles (32-unit squares) at z = 0, and a wall at x = 512.
    const auto floor = [](CollisionWorld& w, int n) {
        for (int i = 0; i < n; ++i)
            for (int j = 0; j < n; ++j) {
                const double x = i * 32.0 - 1024, y = j * 32.0 - 1024;
                w.addTriangle(Vec3{x, y, 0}, Vec3{x + 32, y, 0}, Vec3{x + 32, y + 32, 0});
                w.addTriangle(Vec3{x, y, 0}, Vec3{x + 32, y + 32, 0}, Vec3{x, y + 32, 0});
            }
        w.addTriangle(Vec3{512, -1024, 0}, Vec3{512, 1024, 0}, Vec3{512, 1024, 512});
        w.addTriangle(Vec3{512, -1024, 0}, Vec3{512, 1024, 512}, Vec3{512, -1024, 512});
    };
    floor(big, 64);  // 8,194 solids: the grid is used
    CHECK(big.solids().size() == 8194);
    CHECK(!big.addTriangle(Vec3{0, 0, 0}, Vec3{1, 0, 0}, Vec3{2, 0, 0}));  // degenerate
    const auto down = big.traceCapsule(Vec3{100, 37, 200}, Vec3{100, 37, -200}, 15, 70);
    CHECK(down.fraction < 1.0 && std::fabs(down.end.z) < 1.5);  // lands on the floor
    const auto across = big.traceCapsule(Vec3{0, 0, 10}, Vec3{900, 0, 10}, 15, 70);
    CHECK(across.fraction < 1.0 && across.end.x < 512 && across.end.x > 480);  // stopped by the wall
    // The same traces with a plain pass (a world below the grid threshold, cut to the same area).
    floor(small, 16);
    const auto down2 = small.traceCapsule(Vec3{-900, -900, 200}, Vec3{-900, -900, -200}, 15, 70);
    const auto down3 = big.traceCapsule(Vec3{-900, -900, 200}, Vec3{-900, -900, -200}, 15, 70);
    CHECK(std::fabs(down2.fraction - down3.fraction) < 1e-9);
}
