// Research 131 (scripted objects audit), build side: brush-model spin pivots, trigger_damage, door triggers without a
// name, iw8r_anim liveness, duplicated names, teleport destinations written into the script.
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "brush_ops.h"
#include "compile_iw8.h"
#include "map_script.h"
#include "map_text.h"
#include "test_harness.h"

using namespace iw8r;
namespace fs = std::filesystem;

namespace {

constexpr const char* kSampleId = "mp_iw8_sample";

std::string scratch(const std::string& name) {
    const fs::path p = fs::path(test::tmpDir()) / "scripted_objects" / name;
    std::error_code ec;
    fs::remove_all(p, ec);
    return p.string();
}

std::string samplePath() {
    fs::path p = fs::current_path();
    for (int up = 0; up < 6; ++up, p = p.parent_path()) {
        const fs::path cand = p / "tests" / "data" / "iw8_sample_room.map";
        if (fs::exists(cand)) return cand.string();
    }
    return {};
}

std::string readAll(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

bool has(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

bool hasWarning(const CompileReport& rep, const std::string& part) {
    for (const auto& w : rep.warnings)
        if (has(w, part)) return true;
    return false;
}

std::string box(const Vec3& mn, const Vec3& mx, const std::string& eol, const char* material = "caulk") {
    return makeBrushPrimitive(makeBox({mn, mx}, std::string(" ") + material + " 64 64 0 0 0 0"), eol)->text();
}

// An entity: keys, then brushes (already text).
std::string ent(std::initializer_list<std::pair<const char*, const char*>> kv, const std::string& eol, const std::string& brushes = "") {
    std::string s = "{" + eol;
    for (const auto& [k, v] : kv) s += std::string("\"") + k + "\" \"" + v + "\"" + eol;
    return s + brushes + "}" + eol;
}

// The sample room with `extra` appended, built; returns the generated map script ("" when the build fails).
std::string buildWith(const std::string& name, const std::string& extraFn(const std::string& eol), CompileReport& rep) {
    std::string text;
    if (!readFile(samplePath(), text)) return {};
    const std::string eol = text.find("\r\n") != std::string::npos ? "\r\n" : "\n";
    text += extraFn(eol);
    Id next = 1;
    ParseResult pr = parseMap(text, next);
    if (!pr.map) return {};
    const std::string out = scratch(name);
    CompileOptions opt;
    opt.mapId = kSampleId;
    std::string err;
    if (!compileIw8Extraction(*pr.map, out, opt, rep, &err)) return {};
    return readAll((fs::path(out) / "extracted" / "maps" / "mp" / (std::string(kSampleId) + ".gsc")).string());
}

MapFile parse(const std::string& text) {
    Id next = 1;
    ParseResult pr = parseMap(text, next);
    return pr.map ? std::move(*pr.map) : MapFile{};
}

}  // namespace

// G1: a spinning script_brushmodel turns about the centre of its brushes (a pivot it is linked to); models keep
// turning themselves.
TEST(scripted_brush_spinner_turns_about_its_centre) {
    CompileReport rep;
    const std::string gsc = buildWith("spin_pivot", [](const std::string& eol) -> const std::string& {
        static std::string s;
        s = ent({{"classname", "script_brushmodel"}, {"targetname", "fan"}, {"iw8r_spin", "4"}}, eol, box({100, 200, 0}, {164, 264, 32}, eol)) +
            ent({{"classname", "script_brushmodel"}, {"targetname", "wheel"}, {"iw8r_spin", "2 roll"}, {"iw8r_bob", "16 2"}}, eol,
                box({-300, -300, 64}, {-296, -236, 128}, eol)) +
            ent({{"classname", "script_model"}, {"model", "iw8r_nomodel"}, {"origin", "8 8 8"}, {"targetname", "sign"}, {"iw8r_spin", "3"}}, eol);
        return s;
    }, rep);
    CHECK(!gsc.empty());
    CHECK(has(gsc, "level thread iw8r_spin_pivot(\"fan\", (132, 232, 16), 4, 0);"));
    CHECK(has(gsc, "level thread iw8r_spin_pivot(\"wheel\", (-298, -268, 96), 2, 2);"));
    CHECK(has(gsc, "pivot = spawn(\"script_origin\", centre);") && has(gsc, "        ent linkto(pivot);") &&
          has(gsc, "pivot rotateyaw(360, seconds);") && has(gsc, "pivot rotateroll(360, seconds);"));
    // The model turns about its own origin, as before.
    CHECK(has(gsc, "level thread iw8r_spin_model(\"sign\", 3, 0);") && has(gsc, "ent rotateyaw(360, seconds);"));
    // A bob on a spinning brush model cannot move it (it is linked to the pivot): left out with a warning.
    CHECK(!has(gsc, "iw8r_bob_model(\"wheel\""));
    CHECK(hasWarning(rep, "iw8r_bob and iw8r_spin together are not built"));
    // The shared centre function: brushes and patch control points.
    const MapFile m = parse("iwmap 4\n{\n\"classname\" \"worldspawn\"\n}\n{\n\"classname\" \"script_brushmodel\"\n" +
                            box({-10, 0, 0}, {30, 8, 4}, "\n") + "}\n");
    CHECK(m.entities.size() == 2);
    if (m.entities.size() == 2) {
        const auto c = brushModelCentre(m.entities[1]);
        CHECK(c && c->x == 10 && c->y == 4 && c->z == 2);
        CHECK(!brushModelCentre(m.entities[0]).has_value());
    }
}

// G4: trigger_damage fires when it is damaged; it is not a presence volume and does not hurt.
TEST(scripted_trigger_damage_fires_when_shot) {
    const std::string eol = "\n";
    const MapFile m = parse("iwmap 4\n" + ent({{"classname", "worldspawn"}}, eol) +
                            ent({{"classname", "trigger_damage"}, {"targetname", "shoot_me"}, {"dmg", "50"}}, eol,
                                box({0, 0, 0}, {32, 32, 32}, eol, "trigger")) +
                            ent({{"classname", "trigger_damage"}, {"script_exploder", "3"}}, eol, box({64, 0, 0}, {96, 32, 32}, eol, "trigger")) +
                            ent({{"classname", "trigger_multiple"}, {"targetname", "shoot_me"}}, eol, box({200, 0, 0}, {232, 32, 32}, eol, "trigger")) +
                            ent({{"classname", "trigger_hurt"}, {"targetname", "lava"}, {"dmg", "20"}, {"spawnflags", "16"}}, eol,
                                box({300, 0, 0}, {332, 32, 32}, eol, "trigger")));
    const auto vols = readTriggerVolumes(m);
    // The named trigger_damage is its own volume (not merged with the trigger_multiple of the same name); the nameless
    // one is left to the stock exploder code.
    CHECK(vols.size() == 3);
    size_t shot = 0;
    for (const auto& v : vols) {
        if (v.shot) {
            ++shot;
            CHECK(v.targetname == "shoot_me" && v.damage == 0);
        }
        if (v.targetname == "lava") CHECK(v.damage == 20 && v.damageIntervalMs == 1000 && !v.shot);
    }
    CHECK(shot == 1);
    MapScriptExtras ex;
    ex.triggerVolumes = vols;
    const std::string g = mapScriptGsc(std::nullopt, {}, {}, ex);
    CHECK(has(g, "t = iw8r_trigger_add(\"shoot_me\", 0, 0, 50);\n    t.shot = 1;\n    level thread iw8r_trigger_shot(t);"));
    CHECK(has(g, "self waittill(\"damage\", amount, attacker);") && has(g, "t.proxy notify(\"trigger\", attacker);"));
    CHECK(has(g, "if (isdefined(t.shot) || !iw8r_trigger_holds(t, p.origin))"));
    CHECK(has(g, "if (t.name == name && !isdefined(t.shot) && iw8r_trigger_holds(t, player.origin))"));
    // Volumes of one name share what iw8r_trigger returns.
    CHECK(has(g, "if (name != \"\" && o.name == name)\n            t.proxy = o.proxy;"));

    // The build: a teleporter on a shot trigger, and the warning for a trigger_damage without script_exploder.
    CompileReport rep;
    const std::string gsc = buildWith("shot", [](const std::string& e2) -> const std::string& {
        static std::string s;
        s = ent({{"classname", "trigger_damage"}, {"targetname", "target_tp"}, {"iw8r_teleport", "far_spot"}}, e2,
                box({0, 0, 0}, {32, 32, 32}, e2, "trigger")) +
            ent({{"classname", "script_origin"}, {"targetname", "far_spot"}, {"origin", "128 0 16"}}, e2);
        return s;
    }, rep);
    CHECK(has(gsc, "t = iw8r_trigger_add(\"target_tp\", 0, 0, 50);\n    t.shot = 1;"));
    CHECK(has(gsc, "trig = iw8r_trigger(\"target_tp\");") && has(gsc, "user setorigin((128, 0, 16));"));
    CHECK(hasWarning(rep, "trigger_damage 'target_tp' has no script_exploder"));
}

// G7: a trigger that targets a door but has no targetname is reported; the door falls back to its own use area.
TEST(scripted_door_trigger_without_name_warns) {
    CompileReport rep;
    const std::string gsc = buildWith("door_unnamed", [](const std::string& eol) -> const std::string& {
        static std::string s;
        s = ent({{"classname", "script_brushmodel"}, {"targetname", "gate"}, {"iw8r_door", "rotate 90"}}, eol, box({0, 28, 0}, {64, 36, 96}, eol)) +
            ent({{"classname", "trigger_use"}, {"target", "gate"}}, eol, box({-48, -48, 0}, {48, 48, 96}, eol, "trigger"));
        return s;
    }, rep);
    CHECK(hasWarning(rep, "trigger_use targets the door 'gate' but has no targetname"));
    CHECK(has(gsc, "trig = iw8r_trigger(\"gate_use\");"));
    // The default move time of a hinged / rotate door without a time: 0.6 s.
    CHECK(has(gsc, "pivot rotateyaw(90 * dir, 0.6);"));
}

// G11: a named model with iw8r_anim is animated whether or not something targets it, and a misc_model with it is
// built as a named script_model; the build log says so.
TEST(scripted_anim_needs_only_a_name) {
    CompileReport rep;
    const std::string gsc = buildWith("anim", [](const std::string& eol) -> const std::string& {
        static std::string s;
        s = ent({{"classname", "script_model"}, {"model", "iw8r_nomodel"}, {"origin", "0 0 0"}, {"targetname", "fan_m"},
                 {"iw8r_anim", "fan_spin_loop"}, {"iw8r_anim_loop", "2"}}, eol) +
            ent({{"classname", "misc_model"}, {"model", "iw8r_nomodel"}, {"origin", "16 0 0"}, {"iw8r_anim", "flag_wave"}}, eol);
        return s;
    }, rep);
    CHECK(has(gsc, "ents = getentarray(\"fan_m\", \"targetname\");") && has(gsc, "ent scriptmodelplayanim(\"fan_spin_loop\");"));
    CHECK(has(gsc, "ent scriptmodelplayanim(\"flag_wave\");"));
    CHECK(has(gsc, "level thread iw8r_anim_1();"));
    CHECK(has(gsc, "iw8r_make_solid(\"fan_m\");"));
    CHECK(hasWarning(rep, "2 model(s) play a stock animation (iw8r_anim)"));
    CHECK(hasWarning(rep, "moving model(s) (iw8r_spin / iw8r_bob / iw8r_lift / iw8r_anim): 1 misc_model(s) built as script_model"));
    CHECK(!hasWarning(rep, "is not live"));
}

// G14 / G15: a teleport destination of any class is written into the script; triggers that share a name (two
// placements of one bare prefab) each use their own destination / velocity; repeated names start one thread.
TEST(scripted_shared_names_and_struct_destinations) {
    CompileReport rep;
    const std::string gsc = buildWith("shared", [](const std::string& eol) -> const std::string& {
        static std::string s;
        s = ent({{"classname", "trigger_multiple"}, {"targetname", "tp"}, {"iw8r_teleport", "spot"}}, eol, box({0, 0, 0}, {32, 32, 32}, eol, "trigger")) +
            ent({{"classname", "trigger_multiple"}, {"targetname", "tp"}, {"iw8r_teleport", "spot"}}, eol,
                box({1000, 0, 0}, {1032, 32, 32}, eol, "trigger")) +
            ent({{"classname", "script_struct"}, {"targetname", "spot"}, {"origin", "100 0 8"}, {"angles", "0 90 0"}}, eol) +
            ent({{"classname", "script_struct"}, {"targetname", "spot"}, {"origin", "1100 0 8"}, {"angles", "0 180 0"}}, eol) +
            ent({{"classname", "trigger_multiple"}, {"targetname", "pad"}, {"iw8r_jump", "0 0 700"}}, eol, box({0, 200, 0}, {32, 232, 32}, eol, "trigger")) +
            ent({{"classname", "trigger_multiple"}, {"targetname", "pad"}, {"iw8r_jump", "300 0 500"}}, eol,
                box({1000, 200, 0}, {1032, 232, 32}, eol, "trigger")) +
            ent({{"classname", "script_brushmodel"}, {"targetname", "lift"}, {"iw8r_lift", "64"}}, eol, box({0, 400, 0}, {32, 432, 8}, eol)) +
            ent({{"classname", "script_brushmodel"}, {"targetname", "lift"}, {"iw8r_lift", "64"}}, eol, box({1000, 400, 0}, {1032, 432, 8}, eol)) +
            ent({{"classname", "script_brushmodel"}, {"targetname", "door"}, {"iw8r_door", "hinge 90"}}, eol, box({0, 600, 0}, {64, 608, 96}, eol)) +
            ent({{"classname", "script_brushmodel"}, {"targetname", "door"}, {"iw8r_door", "hinge 90"}}, eol, box({1000, 600, 0}, {1064, 608, 96}, eol));
        return s;
    }, rep);
    CHECK(!gsc.empty());
    // One thread per name.
    CHECK(has(gsc, "level thread iw8r_teleport_0();") && !has(gsc, "iw8r_teleport_1"));
    CHECK(has(gsc, "level thread iw8r_jump_0();") && !has(gsc, "iw8r_jump_1"));
    CHECK(has(gsc, "level thread iw8r_lift_0();") && !has(gsc, "iw8r_lift_1"));
    CHECK(has(gsc, "level thread iw8r_door_0();") && !has(gsc, "iw8r_door_1"));
    // Each teleporter uses the struct nearest to it; the player gets the one of the trigger they are at.
    CHECK(has(gsc, "from[0] = (16, 16, 16);\n    to[0] = (100, 0, 8);\n    ang[0] = (0, 90, 0);"));
    CHECK(has(gsc, "from[1] = (1016, 16, 16);\n    to[1] = (1100, 0, 8);\n    ang[1] = (0, 180, 0);"));
    CHECK(has(gsc, "k = iw8r_nearest(from, user.origin);") && has(gsc, "user setorigin(to[k]);"));
    CHECK(has(gsc, "vel[1] = (300, 0, 500);") && has(gsc, "user setvelocity(vel[iw8r_nearest(from, user.origin)]);"));
    CHECK(has(gsc, "iw8r_nearest(points, pos)\n{"));
    // Both lifts run, each from its own start; the shared door name is reported.
    CHECK(has(gsc, "lifts = getentarray(\"lift\", \"targetname\");") && has(gsc, "lift.iw8r_bottom = lift.origin;"));
    CHECK(has(gsc, "doors = getentarray(\"door\", \"targetname\");") && has(gsc, "door = doors[0];"));
    CHECK(hasWarning(rep, "2 entities with iw8r_door share the targetname 'door'"));
}

// Entries with one name given directly to the generator start one thread; a single teleport spot is written as is.
TEST(scripted_map_script_dedupes_names) {
    MapScriptExtras ex;
    MapScriptExtras::Lift l;
    l.targetname = "elevator";
    ex.lifts.push_back(l);
    ex.lifts.push_back(l);
    MapScriptExtras::Spinner sp;
    sp.targetname = "fan";
    ex.spinners.push_back(sp);
    ex.spinners.push_back(sp);
    MapScriptExtras::DamageZone h;
    h.trigger = "acid";
    ex.damageZones.push_back(h);
    ex.damageZones.push_back(h);
    MapScriptExtras::Teleport t{"tp_in", "tp_out", {}};
    MapScriptExtras::Teleport::Spot spot;
    spot.from = Vec3{0, 0, 0};
    spot.to = Vec3{1, 2, 3};
    spot.angles = Vec3{0, 45, 0};
    t.spots.push_back(spot);
    ex.teleports.push_back(t);
    const std::string g = mapScriptGsc(std::nullopt, {}, {}, ex);
    CHECK(has(g, "iw8r_lift_0") && !has(g, "iw8r_lift_1"));
    CHECK(has(g, "iw8r_damage_0") && !has(g, "iw8r_damage_1"));
    size_t spins = 0;
    for (size_t at = g.find("level thread iw8r_spin_model(\"fan\""); at != std::string::npos; at = g.find("level thread iw8r_spin_model(\"fan\"", at + 1))
        ++spins;
    CHECK(spins == 1);
    CHECK(has(g, "user setorigin((1, 2, 3));\n        user setplayerangles((0, 45, 0));"));
    CHECK(!has(g, "iw8r_nearest") && !has(g, "iw8r_spin_pivot"));
}
