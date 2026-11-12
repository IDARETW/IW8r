#include "map_doctor.h"
#include "test_harness.h"

#include <cmath>
#include <cstdio>
#include <string>

using namespace iw8r;

namespace {

// Axis-aligned box brush text (six faces, caulk), wound like the sample room.
std::string box(double x0, double y0, double z0, double x1, double y1, double z1) {
    auto n = [](double v) { return formatNumber(v); };
    auto f = [&](double ax, double ay, double az, double bx, double by, double bz, double cx, double cy, double cz) {
        return " ( " + n(ax) + " " + n(ay) + " " + n(az) + " ) ( " + n(bx) + " " + n(by) + " " + n(bz) + " ) ( " + n(cx) + " " + n(cy) +
               " " + n(cz) + " ) caulk 128 128 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0\n";
    };
    std::string s = "{\n";
    s += f(x1, y1, z0, x0, y1, z0, x0, y0, z0);  // bottom
    s += f(x0, y0, z1, x0, y1, z1, x1, y1, z1);  // top
    s += f(x0, y0, z1, x1, y0, z1, x1, y0, z0);  // -y
    s += f(x1, y1, z1, x0, y1, z1, x0, y1, z0);  // +y
    s += f(x1, y0, z1, x1, y1, z1, x1, y1, z0);  // +x
    s += f(x0, y1, z1, x0, y0, z1, x0, y0, z0);  // -x
    s += "}\n";
    return s;
}

std::string mapText() {
    std::string s = "iwmap 4\n// entity 0\n{\n\"classname\" \"worldspawn\"\n";
    s += box(-512, -512, -16, 512, 512, 0);   // floor, top at 0
    s += box(200, -64, 0, 328, 64, 96);       // a crate, top at 96
    s += "}\n";
    // 1: floating 300 above the floor
    s += "// entity 1\n{\n\"classname\" \"mp_tdm_spawn\"\n\"origin\" \"-300 0 300\"\n\"angles\" \"0 90 0\"\n}\n";
    // 2: inside the crate (feet at 40): lifted onto its top (96)
    s += "// entity 2\n{\n\"classname\" \"mp_dm_spawn\"\n\"origin\" \"264 0 40\"\n\"angles\" \"10 45 5\"\n}\n";
    // 3: fine (on the floor)
    s += "// entity 3\n{\n\"classname\" \"mp_dm_spawn\"\n\"origin\" \"-100 -300 0\"\n\"angles\" \"0 0 0\"\n}\n";
    // 4: dangling target2 and a good target
    s += "// entity 4\n{\n\"classname\" \"trigger_radius\"\n\"origin\" \"0 0 0\"\n\"target\" \"door\"\n\"target2\" \"gone\"\n}\n";
    s += "// entity 5\n{\n\"classname\" \"script_origin\"\n\"origin\" \"0 64 0\"\n\"targetname\" \"door\"\n}\n";
    return s;
}

}  // namespace

TEST(map_doctor_finds_and_fixes) {
    Document d;
    std::string err;
    CHECK(d.loadFromText(mapText(), "doctor.map", &err));
    const auto fixes = diagnoseMap(d.map());
    int floor = 0, angles = 0, dangling = 0;
    for (const DoctorFix& f : fixes) {
        floor += f.kind == DoctorFixKind::SpawnFloor;
        angles += f.kind == DoctorFixKind::SpawnAngles;
        dangling += f.kind == DoctorFixKind::DanglingTarget;
        CHECK(!f.description.empty());
    }
    CHECK(floor == 2);
    CHECK(angles == 1);
    CHECK(dangling == 1);
    const std::string before = d.text();
    CHECK(applyDoctorFixes(d, fixes) == fixes.size());
    // Spawn 1 is on the floor now, spawn 2 on the crate, with clean angles; the dangling key is gone, the good one kept.
    const auto& es = d.map().entities;
    CHECK(es.size() == 6);
    const auto o1 = es[1].origin(), o2 = es[2].origin();
    CHECK(o1 && std::fabs(o1->z - 0) < 0.25);
    CHECK(o2 && std::fabs(o2->z - 96) < 0.25);
    CHECK(es[2].value("angles") && *es[2].value("angles") == "0 45 0");
    CHECK(!es[4].value("target2"));
    CHECK(es[4].value("target") && *es[4].value("target") == "door");
    // Nothing left to fix; one undo step restores everything.
    CHECK(diagnoseMap(d.map()).empty());
    CHECK(d.undo());
    CHECK(d.text() == before);
}

TEST(map_doctor_clean_map_has_nothing) {
    Document d;
    std::string err;
    const std::string text = "iwmap 4\n// entity 0\n{\n\"classname\" \"worldspawn\"\n" + box(-512, -512, -16, 512, 512, 0) +
                             "}\n// entity 1\n{\n\"classname\" \"mp_dm_spawn\"\n\"origin\" \"0 0 0\"\n\"angles\" \"0 90 0\"\n}\n";
    CHECK(d.loadFromText(text, "clean.map", &err));
    CHECK(diagnoseMap(d.map()).empty());
    CHECK(applyDoctorFixes(d, {}) == 0);
}
