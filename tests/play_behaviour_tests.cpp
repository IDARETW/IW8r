#include "play_behaviour.h"
#include "test_harness.h"

#include <cmath>
#include <string>

using namespace iw8r;

// Research 131 gap 9: the play preview runs the map's scripted objects as the built map's script does.
namespace {

std::string brush(double x0, double y0, double z0, double x1, double y1, double z1, const char* mat = "custom/a") {
    char buf[1400];
    const std::string m = std::string(mat) + " 128 128 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0";
    std::snprintf(buf, sizeof buf,
                  "{\n ( %g %g %g ) ( %g %g %g ) ( %g %g %g ) %s\n ( %g %g %g ) ( %g %g %g ) ( %g %g %g ) %s\n"
                  " ( %g %g %g ) ( %g %g %g ) ( %g %g %g ) %s\n ( %g %g %g ) ( %g %g %g ) ( %g %g %g ) %s\n"
                  " ( %g %g %g ) ( %g %g %g ) ( %g %g %g ) %s\n ( %g %g %g ) ( %g %g %g ) ( %g %g %g ) %s\n}\n",
                  x1, y1, z0, x0, y1, z0, x0, y0, z0, m.c_str(), x0, y0, z1, x0, y1, z1, x1, y1, z1, m.c_str(),
                  x0, y0, z1, x1, y0, z1, x1, y0, z0, m.c_str(), x1, y1, z1, x0, y1, z1, x0, y1, z0, m.c_str(),
                  x1, y0, z1, x1, y1, z1, x1, y1, z0, m.c_str(), x0, y1, z1, x0, y0, z1, x0, y0, z0, m.c_str());
    return buf;
}

const Vec3 kMins{-15, -15, 0}, kMaxs{15, 15, 70};

bool blocked(const CollisionWorld& w, const Vec3& a, const Vec3& b) { return w.traceBox(a, b, kMins, kMaxs).fraction < 0.999; }

}  // namespace

TEST(play_behaviour_doors_lifts_triggers) {
    const std::string text =
        "iwmap 4\n{\n\"classname\" \"worldspawn\"\n" + brush(-1024, -1024, -16, 1024, 1024, 0) + "}\n" +
        // A sliding door across x = 0..8, y -64..64, opened by Use near it, sliding 128 up.
        "{\n\"classname\" \"script_brushmodel\"\n\"targetname\" \"gate\"\n\"iw8r_door\" \"slide 0 0 128 1 2\"\n" + brush(0, -64, 0, 8, 64, 112) + "}\n" +
        // A hinged door at x 300..308, y -48..48, toggled by Use.
        "{\n\"classname\" \"script_brushmodel\"\n\"targetname\" \"swing\"\n\"iw8r_door\" \"hinge 90\"\n" + brush(300, -48, 0, 308, 48, 112) + "}\n" +
        // A lift at x -300, 64 wide, 16 high, rising 200 over 2 s after a 1 s wait.
        "{\n\"classname\" \"script_brushmodel\"\n\"targetname\" \"lift\"\n\"iw8r_lift\" \"200 2 1\"\n" + brush(-332, -32, 0, -268, 32, 16) + "}\n" +
        // A teleporter at y 400 to a script_origin; a jump pad at y -400.
        "{\n\"classname\" \"trigger_multiple\"\n\"targetname\" \"tp\"\n\"iw8r_teleport\" \"tpdest\"\n" + brush(-32, 368, 0, 32, 432, 64, "tools/trigger") + "}\n" +
        "{\n\"classname\" \"script_origin\"\n\"targetname\" \"tpdest\"\n\"origin\" \"700 700 0\"\n\"angles\" \"0 180 0\"\n}\n" +
        "{\n\"classname\" \"trigger_multiple\"\n\"targetname\" \"jp\"\n\"iw8r_jump\" \"0 0 900\"\n" + brush(-32, -432, 0, 32, -368, 64, "tools/trigger") + "}\n";
    Id next = 1;
    ParseResult pr = parseMap(text, next);
    CHECK(pr.map.has_value());
    if (!pr.map) return;
    CollisionWorld world;
    PlayBehaviours pb;
    const std::vector<Id> moving = pb.build(*pr.map, world);
    CHECK(moving.size() == 3 && pb.doorCount() == 2 && pb.moverCount() == 1);
    CHECK(pb.warnings.empty());

    // The sliding door blocks the walk across x = 4 until it is used; then it rises and the way is free; it comes down
    // again after its hold time.
    const Vec3 standAt{-40, 0, 1};
    auto run = [&](double seconds, const Vec3& feet, bool useFirst) {
        std::vector<PlayBehaviours::Event> all;
        for (double t = 0; t < seconds; t += 1.0 / 60.0) {
            const auto ev = pb.tick(1.0 / 60.0, feet, kMins, kMaxs, useFirst && t == 0, world);
            all.insert(all.end(), ev.begin(), ev.end());
        }
        return all;
    };
    run(0.1, standAt, false);
    CHECK(blocked(world, standAt, Vec3{60, 0, 1}));
    const auto used = run(0.05, standAt, true);
    CHECK(used.size() == 1 && used[0].kind == PlayBehaviours::Event::DoorUsed && used[0].name == "gate");
    run(1.1, standAt, false);  // the 1 s move
    CHECK(!blocked(world, standAt, Vec3{60, 0, 1}));
    run(2.5, standAt, false);  // the 2 s hold, then it starts down
    run(1.1, standAt, false);
    CHECK(blocked(world, standAt, Vec3{60, 0, 1}));

    // The hinged door swings away from the user: used from -x, its free end swings toward +x; the gap opens.
    const Vec3 front{260, 0, 1};
    CHECK(blocked(world, front, Vec3{360, 0, 1}));
    run(0.05, front, true);
    run(0.8, front, false);
    CHECK(!blocked(world, front, Vec3{360, 0, 1}));
    run(3.0, front, false);  // a hinge door stays open
    CHECK(!blocked(world, front, Vec3{360, 0, 1}));
    run(0.05, front, true);  // used again: it closes
    run(0.8, front, false);
    CHECK(blocked(world, front, Vec3{360, 0, 1}));

    // The lift: a box dropped on it lands at its top, 16 at rest and 216 at the top of its travel (after 1 s wait and
    // 2 s move).
    CollisionWorld w2;
    PlayBehaviours lift;
    lift.build(*pr.map, w2);
    lift.tick(0.5, Vec3{0, 0, 0}, kMins, kMaxs, false, w2);
    auto landing = [&] { return w2.traceBox(Vec3{-300, 0, 400}, Vec3{-300, 0, -10}, kMins, kMaxs).end.z; };
    CHECK(std::abs(landing() - 16) < 0.5);
    for (int i = 0; i < 165; ++i) lift.tick(1.0 / 60.0, Vec3{0, 0, 0}, kMins, kMaxs, false, w2);  // t = 0.25 + 2.75 = 3.0 (one tick is at most 0.25 s)
    const double top = landing();
    if (std::abs(top - 216) >= 2.0) std::printf("  lift top landing %.3f at t %.3f\n", top, lift.time());
    CHECK(std::abs(top - 216) < 2.0);

    // The teleporter fires once on entry with the destination and its yaw; the jump pad gives its velocity.
    const auto tp = run(0.05, Vec3{0, 400, 1}, false);
    CHECK(tp.size() == 1 && tp[0].kind == PlayBehaviours::Event::Teleport && tp[0].position.x == 700 && tp[0].yaw == 180);
    CHECK(run(0.5, Vec3{0, 400, 1}, false).empty());  // still inside: nothing more
    const auto jp = run(0.05, Vec3{0, -400, 1}, false);
    CHECK(jp.size() == 1 && jp[0].kind == PlayBehaviours::Event::Launch && jp[0].velocity.z == 900);

    // Poses for drawing: one per door and mover.
    CHECK(pb.poses().size() == 3);
}
