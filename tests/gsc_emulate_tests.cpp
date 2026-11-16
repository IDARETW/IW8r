#include "gsc_emulate.h"
#include "play_behaviour.h"
#include "test_harness.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using namespace iw8r;

// Map script emulation (core gsc_emulate): the map's own GSC moves its entities in the editor as it does in game.
namespace {

namespace fs = std::filesystem;

bool near(double a, double b, double eps = 1e-6) { return std::fabs(a - b) <= eps; }
double wrap360(double a) {
    a = std::fmod(a, 360.0);
    return a < 0 ? a + 360.0 : a;
}

std::string findUp(const std::string& rel) {
    fs::path p = fs::current_path();
    for (int up = 0; up < 6; ++up, p = p.parent_path())
        if (fs::exists(p / rel)) return (p / rel).string();
    return "";
}

std::string readText(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// The 1-based line of the first `what` in `text`.
int lineOf(const std::string& text, const std::string& what) {
    const size_t at = text.find(what);
    if (at == std::string::npos) return -1;
    return 1 + int(std::count(text.begin(), text.begin() + std::ptrdiff_t(at), '\n'));
}

std::optional<MapFile> parse(const std::string& text) {
    Id next = 1;
    ParseResult pr = parseMap(text, next);
    return pr.map;
}

std::string brush(double x0, double y0, double z0, double x1, double y1, double z1) {
    char buf[1400];
    const char* m = "custom/a 128 128 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0";
    std::snprintf(buf, sizeof buf,
                  "{\n ( %g %g %g ) ( %g %g %g ) ( %g %g %g ) %s\n ( %g %g %g ) ( %g %g %g ) ( %g %g %g ) %s\n"
                  " ( %g %g %g ) ( %g %g %g ) ( %g %g %g ) %s\n ( %g %g %g ) ( %g %g %g ) ( %g %g %g ) %s\n"
                  " ( %g %g %g ) ( %g %g %g ) ( %g %g %g ) %s\n ( %g %g %g ) ( %g %g %g ) ( %g %g %g ) %s\n}\n",
                  x1, y1, z0, x0, y1, z0, x0, y0, z0, m, x0, y0, z1, x0, y1, z1, x1, y1, z1, m, x0, y0, z1, x1, y0, z1, x1, y0, z0, m,
                  x1, y1, z1, x0, y1, z1, x0, y1, z0, m, x1, y0, z1, x1, y1, z1, x1, y1, z0, m, x0, y1, z1, x0, y0, z1, x0, y0, z0, m);
    return buf;
}

// The compact showcase's row 4 script (samples/dev/iw8r_compact_showcase.gsc, as written for the owner 2026-11-15).
const char* const kShowcaseScript = R"(// showcase
user_main()
{
    level thread showcase_spin("scripted_0");
    level thread showcase_bob("scripted_1");
    level thread showcase_blink("scripted_2");
}

showcase_spin(name)
{
    ent = getent(name, "targetname");
    if (!isdefined(ent))
        return;
    for (;;)
    {
        ent rotateyaw(360, 4);
        wait 4;
    }
}

showcase_bob(name)
{
    ent = getent(name, "targetname");
    if (!isdefined(ent))
        return;
    for (;;)
    {
        ent movez(32, 1.5, 0.5, 0.5);
        wait 1.5;
        ent movez(-32, 1.5, 0.5, 0.5);
        wait 1.5;
    }
}

showcase_blink(name)
{
    ent = getent(name, "targetname");
    if (!isdefined(ent))
        return;
    for (;;)
    {
        wait 2;
        ent hide();
        wait 1;
        ent show();
    }
}
)";

const char* const kShowcaseMap =
    "iwmap 4\n{\n\"classname\" \"worldspawn\"\n\"iw8r_script\" \"iw8r_compact_showcase.gsc\"\n}\n"
    "{\n\"classname\" \"script_model\"\n\"model\" \"military_hq_crate_01_proxy\"\n\"targetname\" \"scripted_0\"\n\"origin\" \"-608.0 -96 0\"\n}\n"
    "{\n\"classname\" \"script_model\"\n\"model\" \"tm/container_barrel_plastic_01_closed\"\n\"targetname\" \"scripted_1\"\n\"origin\" \"-441.6 -96 0\"\n}\n"
    "{\n\"classname\" \"script_model\"\n\"model\" \"tm/furniture_cafe_chair_01\"\n\"targetname\" \"scripted_2\"\n\"origin\" \"-275.2 -96 0\"\n}\n";

// The showcase's three behaviours at a few times: a turn every 4 s, a 32-unit bob in 1.5 s each way with 0.5 s of
// speeding up and slowing down, hidden from 2 to 3 s of every 3 s.
void checkShowcase(GscEmulation& emu, const char* what) {
    std::printf("  showcase checks: %s\n", what);
    const auto spin = [&](double t) {
        emu.advanceTo(t);
        const GscPose* p = emu.pose("scripted_0");
        return p ? p->angles.y : -1.0;
    };
    const auto bob = [&](double t) {
        emu.advanceTo(t);
        const GscPose* p = emu.pose("scripted_1");
        return p ? p->origin.z : -1.0;
    };
    const auto shown = [&](double t) {
        emu.advanceTo(t);
        const GscPose* p = emu.pose("scripted_2");
        return !p || p->visible;
    };
    CHECK(near(spin(0), 0));
    CHECK(near(spin(1), 90));
    CHECK(near(spin(2), 180));
    CHECK(near(wrap360(spin(5)), 90));
    CHECK(near(wrap360(spin(13)), 90));
    // The turn is about the model's own origin, which stays put.
    if (const GscPose* p = emu.pose("scripted_0")) {
        CHECK(near(p->origin.x, -608) && near(p->origin.y, -96) && near(p->origin.z, 0));
        CHECK(!p->brush && p->visible);
        // R takes +x to +y at a quarter turn (13 s: 3 1/4 turns).
        CHECK(near(p->R[1][0], 1, 1e-9) && near(p->R[0][0], 0, 1e-9));
    }
    CHECK(near(bob(0.25), 2));      // 0.5 s speeding up to 1 unit/1.5 s: x^2 / 2 of the way in the first quarter
    CHECK(near(bob(0.75), 16));     // half way at half time
    CHECK(near(bob(1.5), 32));      // at the top
    CHECK(near(bob(2.25), 16));     // half way down
    CHECK(near(bob(3), 0));         // back
    CHECK(near(bob(3.75), 16));     // up again
    CHECK(near(bob(31.5), 32));     // 10 1/2 cycles in
    if (const GscPose* p = emu.pose("scripted_1")) CHECK(near(p->origin.x, -441.6) && near(p->origin.y, -96) && near(p->move.z, 32));
    CHECK(shown(1));
    CHECK(!shown(2.5));
    CHECK(shown(3.5));
    CHECK(!shown(5.5));
    CHECK(shown(6.5));
    // Back in time: the same answers (it starts again from 0).
    CHECK(near(bob(0.75), 16));
    CHECK(near(spin(1), 90));
    CHECK(!shown(2.5));
    CHECK(emu.threadCount() == 3);  // user_main ended; the three loops wait
}

}  // namespace

TEST(gsc_emulate_showcase_script) {
    const auto map = parse(kShowcaseMap);
    CHECK(map.has_value());
    if (!map) return;
    GscEmulation emu;
    CHECK(emu.load(kShowcaseScript, *map));
    CHECK(emu.functionCount() == 4);
    for (const GscNote& n : emu.notes()) std::printf("  note line %d: %s\n", n.line, n.text.c_str());
    CHECK(emu.notes().empty());
    checkShowcase(emu, "inline copy");
}

TEST(gsc_emulate_showcase_files) {
    const std::string mapPath = findUp("samples/dev/iw8r_compact_showcase.map");
    if (mapPath.empty()) {
        iw8r::test::skipf("  SKIP: samples/dev/iw8r_compact_showcase.map not found\n");
        return;
    }
    const auto map = parse(readText(mapPath));
    CHECK(map.has_value());
    if (!map) return;
    std::string scriptPath;
    const std::string src = readMapScript(*map, mapPath, &scriptPath);
    CHECK(!src.empty() && fs::path(scriptPath).filename() == "iw8r_compact_showcase.gsc");
    GscEmulation emu;
    CHECK(emu.load(src, *map));
    for (const GscNote& n : emu.notes()) std::printf("  note line %d: %s\n", n.line, n.text.c_str());
    CHECK(emu.notes().empty());
    checkShowcase(emu, "samples/dev files");

    // The runner a view uses: loads it from the map's key, runs it to the asked time, hands out notes once.
    MapScriptRunner run;
    run.update(*map, mapPath, 1, 0.75);
    CHECK(run.active() && run.emulation().pose("scripted_1") && near(run.emulation().pose("scripted_1")->origin.z, 16));
    CHECK(run.takeNewNotes().empty());
    run.update(*map, mapPath, 1, 1.0);
    CHECK(run.emulation().pose("scripted_0") && near(run.emulation().pose("scripted_0")->angles.y, 90));
    // Without the map's path the relative script name finds nothing: no script runs.
    MapScriptRunner none;
    none.update(*map, "", 1, 1.0);
    CHECK(!none.active() && none.poses().empty());
}

TEST(gsc_emulate_language_subset) {
    const std::string mapText = std::string("iwmap 4\n{\n\"classname\" \"worldspawn\"\n}\n") +
                                "{\n\"classname\" \"script_model\"\n\"targetname\" \"a\"\n\"origin\" \"0 0 0\"\n\"angles\" \"0 90 0\"\n}\n"
                                "{\n\"classname\" \"script_model\"\n\"targetname\" \"b\"\n\"origin\" \"100 0 0\"\n}\n"
                                "{\n\"classname\" \"script_model\"\n\"targetname\" \"crate\"\n\"script_noteworthy\" \"row\"\n\"origin\" \"0 200 0\"\n}\n"
                                "{\n\"classname\" \"script_model\"\n\"targetname\" \"crate\"\n\"script_noteworthy\" \"row\"\n\"origin\" \"0 300 0\"\n}\n"
                                "{\n\"classname\" \"script_model\"\n\"targetname\" \"gone\"\n\"origin\" \"0 400 0\"\n}\n"
                                "{\n\"classname\" \"script_model\"\n\"targetname\" \"kid\"\n\"origin\" \"64 0 0\"\n}\n"
                                "{\n\"classname\" \"script_origin\"\n\"targetname\" \"hub\"\n\"origin\" \"0 0 0\"\n}\n"
                                "{\n\"classname\" \"script_brushmodel\"\n\"targetname\" \"plat\"\n" + brush(-32, -32, 0, 32, 32, 16) + "}\n";
    const auto map = parse(mapText);
    CHECK(map.has_value());
    if (!map) return;
    const char* const script = R"(
#include scripts\mp\utility;
/#
    devonly(); // never parsed
#/
user_main()
{
    level.count = 0;
    level endon("game_ended");
    thread go_a();
    level thread go_b("b", (0, 0, 64));
    crates = getentarray("row", "script_noteworthy");
    foreach (i, c in crates)
        c thread lift(i);
    level thread killer();
    level thread pivot();
    level thread counter();
    level thread mystery();
    level thread runaway();
    level thread brushes();
    level thread events();
}

go_a()
{
    a = getent("a", "targetname");
    a rotateto((0, 180, 0), 2);     // from yaw 90: +90 in 2 s
    a waittill("rotatedone");
    level.a_done = gettime();
    a moveto((10, 20, 30), 1, 0.25, 0.25);
    a waittill("movedone");
    a hide();
}

go_b(name, offset)
{
    b = getent(name, "targetname");
    for (i = 0; i < 3; i++)
    {
        b moveto(b.origin + offset, 1);
        wait 1;
    }
    while (true)
    {
        b rotatevelocity((0, 0, 90), 2);   // roll 90 deg/s
        wait 2;
    }
}

lift(i)
{
    level endon("stop_lifts");
    for (;;)
    {
        self movez(10 * (i + 1), 1);
        wait 1;
        self movez(-10 * (i + 1), 1);
        wait 1;
    }
}

killer()
{
    wait 3.5;
    level notify("stop_lifts");
    g = getent("gone", "targetname");
    g delete();
    if (!isdefined(g))
        level.gone_ok = 1;
}

pivot()
{
    hub = getent("hub", "targetname");
    kid = getent("kid", "targetname");
    kid linkto(hub);
    hub rotateyaw(90, 1);
}

counter()
{
    n = 0;
    s = "";
    while (1)
    {
        n++;
        n += 1;
        s = s + n + ",";
        switch (n)
        {
        case 4:
            level.four = s;
            break;
        case 6:
            level.six = 1;
            break;
        default:
            break;
        }
        if (n >= 10)
            return;
        wait 0.5;
    }
}

mystery()
{
    self thisisnotreal(1, 2);
    x = ;
    playfx(level._effect["none"], (0, 0, 0));
    level.mystery_done = 1;
}

runaway()
{
    for (;;)
        level.spin = 1;
}

brushes()
{
    p = getent("plat", "targetname");
    p rotateyaw(90, 1);
}

events()
{
    level waittill("ping", who);
    level.who = who;
}
)";
    GscEmulation emu;
    CHECK(emu.load(script, *map));
    // Notes: the unknown method and the statement that does not parse are skipped, each once, with their lines.
    bool unknown = false, broken = false;
    for (const GscNote& n : emu.notes()) {
        std::printf("  note line %d: %s\n", n.line, n.text.c_str());
        unknown = unknown || (n.text.find("thisisnotreal") != std::string::npos && n.line == lineOf(script, "thisisnotreal"));
        broken = broken || (n.line == lineOf(script, "x = ;") && n.text.find("skipped") != std::string::npos);
    }
    CHECK(unknown && broken);

    emu.advanceTo(1);
    const GscPose* a = emu.pose("a");
    CHECK(a && near(a->angles.y, 135));            // half way from 90 to 180
    const GscPose* b = emu.pose("b");
    CHECK(b && near(b->origin.z, 64));
    const GscPose* kid = emu.pose("kid");
    CHECK(kid && near(kid->origin.x, 0, 1e-6) && near(kid->origin.y, 64, 1e-6));  // a quarter turn about the hub
    const GscPose* plat = emu.pose("plat");
    CHECK(plat && plat->brush && near(plat->pivot.x, 0) && near(plat->R[1][0], 1, 1e-9));  // about the map origin
    // Crates: lifted 10 and 20 (foreach key i = 0, 1).
    int crates = 0;
    for (const GscPose& p : emu.poses())
        if (p.targetname == "crate") {
            ++crates;
            CHECK(near(p.origin.z, p.origin.y == 200 ? 10 : 20));
        }
    CHECK(crates == 2);

    emu.advanceTo(2.5);
    a = emu.pose("a");
    CHECK(a && near(a->angles.y, 180) && near(a->origin.x, 5) && a->visible);  // rotatedone at 2, then moving (eased)
    emu.advanceTo(3.25);
    a = emu.pose("a");
    CHECK(a && near(a->origin.y, 20) && near(a->origin.z, 30) && !a->visible);  // movedone at 3 -> hidden
    b = emu.pose("b");
    CHECK(b && near(b->origin.z, 192) && near(b->angles.z, 22.5));              // three moves done, rolling
    // stop_lifts at 3.5, while they come down (3 .. 4): the move goes on, the loop does not (at 5.5 they would be
    // half way up again).
    emu.advanceTo(4.5);
    for (const GscPose& p : emu.poses())
        if (p.targetname == "crate") CHECK(near(p.origin.z, 0));
    emu.advanceTo(5.5);
    for (const GscPose& p : emu.poses())
        if (p.targetname == "crate") CHECK(near(p.origin.z, 0));
    const GscPose* gone = emu.pose("gone");
    CHECK(gone && gone->deleted && !gone->visible);
    emu.advanceTo(10);
    // The loop without a wait was stopped, with a line of its loop.
    bool runaway = false;
    for (const GscNote& n : emu.notes())
        if (n.text.find("runaway() stopped") != std::string::npos)
            runaway = n.line == lineOf(script, "runaway()\n{\n    for (;;)") + 2 || n.line == lineOf(script, "level.spin = 1");
    CHECK(runaway);
    // Determinism: a fresh run to 3.25 gives what the first one gave.
    GscEmulation again;
    CHECK(again.load(script, *map));
    again.advanceTo(3.25);
    const GscPose* b2 = again.pose("b");
    CHECK(b2 && near(b2->origin.z, 192) && near(b2->angles.z, 22.5));
}

TEST(gsc_emulate_values_and_fields) {
    const auto map = parse(
        "iwmap 4\n{\n\"classname\" \"worldspawn\"\n}\n{\n\"classname\" \"script_model\"\n\"targetname\" \"probe\"\n\"origin\" \"0 0 0\"\n}\n");
    CHECK(map.has_value());
    if (!map) return;
    // The script reports what it computed by moving the probe: x = a sum, y = 1 when the string is right, z = the
    // switch's answer; then a notify with an argument reaches a waittill.
    const char* const script = R"(
user_main()
{
    p = getent("probe", "targetname");
    sum = 0;
    arr = [];
    arr[arr.size] = 5;
    arr[arr.size] = 7;
    arr["k"] = 30;
    foreach (v in arr)
        sum += v;
    s = "n=" + 3 + "/" + 1.5;
    ok = 0;
    if (s == "n=3/1.5" && arr.size == 3 && !isdefined(arr[9]))
        ok = 1;
    v = (1, 2, 3) * 2;
    z = v[2] + int(7.9) % 4;   // 6 + 3
    switch (z)
    {
    case 8:
        z = 100;
        break;
    case 9:
        z += 0;
    case 10:
        z += 0;
        break;
    default:
        z = -1;
    }
    level.items = [];
    level.items[0] = 1;
    level.items[1] += 0;       // undefined + 0: noted, gives undefined
    level thread listen(p);
    wait 0.5;
    level notify("ping", 4);
    p moveto((sum, ok, z), 1);
}

listen(p)
{
    level waittill("ping", amount);
    p.answer = amount;
    p movex(amount, 0);
}
)";
    GscEmulation emu;
    CHECK(emu.load(script, *map));
    emu.advanceTo(2);
    const GscPose* p = emu.pose("probe");
    CHECK(p && near(p->origin.x, 42) && near(p->origin.y, 1) && near(p->origin.z, 9));
    bool undefinedNoted = false;
    for (const GscNote& n : emu.notes()) undefinedNoted = undefinedNoted || n.text.find("undefined") != std::string::npos;
    CHECK(undefinedNoted);
    // At 0.5 the notify ran the listener at once (its movex(4, 0) first), then the moveto started from there.
    emu.advanceTo(0.5);
    p = emu.pose("probe");
    CHECK(p && near(p->origin.x, 4));
}

TEST(gsc_emulate_bad_scripts_never_throw) {
    const auto map = parse("iwmap 4\n{\n\"classname\" \"worldspawn\"\n}\n");
    CHECK(map.has_value());
    if (!map) return;
    GscEmulation emu;
    CHECK(!emu.load("", *map));
    CHECK(!emu.load("foo() { wait 1; }", *map));  // no user_main
    CHECK(!emu.notes().empty());
    const char* const broken[] = {
        "user_main() {", "user_main() { for (;;) {} }", "user_main() { a = (1, 2; }", "user_main() { x = \"open; }",
        "user_main( { }", "user_main() { [[ level.f ]](); self waittill(); undefined_thing movez(1, 1); }",
        "user_main() { while (1) { level notify(\"a\"); level waittill(\"a\"); } }",
        "user_main() { thread user_main(); }", "user_main() { user_main(); }", "}}}{{{ user_main() { wait -1; wait \"x\"; }",
        "user_main() { a = []; a[99999999] = 1; b = 1 / 0; c = 5 % 0; d = (1,2,3) + 1; }",
    };
    for (const char* s : broken) {
        GscEmulation e;
        e.load(s, *map);
        e.advanceTo(5);
        e.advanceTo(1);
        CHECK(true);  // reached: nothing threw or hung
    }
}

TEST(gsc_emulate_play_behaviours) {
    // The play preview runs the script on its clock: a script_brushmodel the script names is a mover (it collides
    // where the script has it), and models get poses too.
    const std::string text = std::string("iwmap 4\n{\n\"classname\" \"worldspawn\"\n") + brush(-1024, -1024, -16, 1024, 1024, 0) + "}\n" +
                             "{\n\"classname\" \"script_brushmodel\"\n\"targetname\" \"elevator\"\n" + brush(-32, -32, 0, 32, 32, 16) + "}\n" +
                             "{\n\"classname\" \"script_model\"\n\"targetname\" \"blinker\"\n\"origin\" \"200 0 0\"\n}\n";
    const auto map = parse(text);
    CHECK(map.has_value());
    if (!map) return;
    PlayBehaviours pb;
    pb.setScriptSource("user_main() { e = getent(\"elevator\", \"targetname\"); e movez(100, 1); m = getent(\"blinker\", \"targetname\"); wait 0.5; m hide(); }");
    CollisionWorld world;
    const std::vector<Id> moving = pb.build(*map, world);
    CHECK(moving.size() == 1 && pb.scriptMoverCount() == 1 && pb.script().loaded());
    const Vec3 mins{-15, -15, 0}, maxs{15, 15, 70};
    for (int k = 0; k < 120; ++k) pb.tick(1.0 / 60.0, Vec3{500, 500, 1}, mins, maxs, false, world);
    bool elevator = false, blinker = false;
    for (const auto& p : pb.poses()) {
        if (p.entity == map->entities[1].id) elevator = near(p.move.z, 100, 1e-6) && p.visible;
        if (p.entity == map->entities[2].id) blinker = !p.visible;
    }
    CHECK(elevator && blinker);
    // The elevator's brushes are up at 100..116: a box falling at x 0 lands on top of them.
    const auto tr = world.traceBox(Vec3{0, 0, 300}, Vec3{0, 0, 50}, mins, maxs);
    CHECK(tr.fraction < 1 && near(Vec3{0, 0, 300}.z + (50 - 300) * tr.fraction, 116, 0.5));
    CHECK(pb.takeScriptNotes().empty());
}
