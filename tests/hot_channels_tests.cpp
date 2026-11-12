// Hot channels (roadmap 7.7 / 7.9): payload encoders and decoders for the dvars iw8r_fx, iw8r_snd and iw8r_sun,
// their length limit and the generated GSC thread.
#include <cmath>
#include <string>
#include <vector>

#include "hot_channels.h"
#include "map_script.h"
#include "test_harness.h"

using namespace iw8r;

namespace {

bool near(double a, double b) { return std::fabs(a - b) < 1e-9; }

HotFx fxItem(const std::string& effect, bool play) {
    HotFx f;
    f.effect = effect;
    f.origin[0] = 1024.5;
    f.origin[1] = -312.25;
    f.origin[2] = 64;
    f.angles[0] = -90;
    f.angles[1] = 45.5;
    f.angles[2] = 0;
    f.play = play;
    return f;
}

bool sameFx(const HotFx& a, const HotFx& b) {
    if (a.effect != b.effect || a.play != b.play) return false;
    for (int i = 0; i < 3; ++i)
        if (!near(a.origin[i], b.origin[i]) || !near(a.angles[i], b.angles[i])) return false;
    return true;
}

size_t countOf(const std::string& s, const std::string& needle) {
    size_t n = 0;
    for (size_t p = s.find(needle); p != std::string::npos; p = s.find(needle, p + needle.size())) ++n;
    return n;
}

}  // namespace

TEST(hot_fx_round_trip_one_and_two) {
    const std::vector<HotFx> one = {fxItem("vfx/fire/torch_mp", true)};
    const std::string v1 = encodeHotFx(one, 7);
    CHECK(v1 == "7 fx 1 vfx/fire/torch_mp 1024.5 -312.25 64 -90 45.5 0 1");
    unsigned seq = 0;
    auto d1 = decodeHotFx(v1, &seq);
    CHECK(d1.has_value());
    CHECK(seq == 7);
    if (d1) {
        CHECK(d1->size() == 1);
        if (d1->size() == 1) CHECK(sameFx((*d1)[0], one[0]));
    }

    const std::vector<HotFx> two = {fxItem("vfx/smoke/a", true), fxItem("vfx/glass/b", false)};
    const std::string v2 = encodeHotFx(two, 8);
    CHECK(!v2.empty());
    CHECK(v2.rfind("8 fx 2 ", 0) == 0);
    auto d2 = decodeHotFx(v2, &seq);
    CHECK(d2.has_value());
    CHECK(seq == 8);
    if (d2 && d2->size() == 2) {
        CHECK(sameFx((*d2)[0], two[0]));
        CHECK(sameFx((*d2)[1], two[1]));
    }
}

TEST(hot_fx_default_seq_is_one) {
    CHECK(encodeHotFx({fxItem("vfx/a", true)}).rfind("1 fx 1 ", 0) == 0);
}

TEST(hot_fx_rejects_empty_unsafe_and_too_long) {
    CHECK(encodeHotFx({}, 1).empty());

    CHECK(encodeHotFx({fxItem("vfx/a b", true)}).empty());     // space would split the record
    CHECK(encodeHotFx({fxItem("vfx/a;b", true)}).empty());     // no ';' in a dvar value
    CHECK(encodeHotFx({fxItem("vfx/\"x\"", true)}).empty());   // no quotes
    CHECK(encodeHotFx({fxItem("", true)}).empty());

    HotFx nan = fxItem("vfx/a", true);
    nan.origin[0] = std::nan("");
    CHECK(encodeHotFx({nan}).empty());

    // A long name plus several records goes over 200 characters: nothing is sent, not a truncated value.
    const std::string longName(90, 'x');
    const std::vector<HotFx> many = {fxItem(longName, true), fxItem(longName, true)};
    CHECK(encodeHotFx(many, 3).empty());

    // One record with a 150-character name still fits.
    const std::string fits(150, 'y');
    const std::string v = encodeHotFx({fxItem(fits, true)}, 4);
    CHECK(!v.empty());
    CHECK(v.size() <= kHotChannelMaxLen);
}

TEST(hot_fx_decode_rejects_malformed) {
    CHECK(!decodeHotFx("").has_value());
    CHECK(!decodeHotFx("1 fx 1 vfx/a 0 0 0 0 0 0").has_value());          // missing play flag
    CHECK(!decodeHotFx("1 fx 2 vfx/a 0 0 0 0 0 0 1").has_value());        // count says 2, one record
    CHECK(!decodeHotFx("1 snd 1 vfx/a 0 0 0 0 0 0 1").has_value());       // wrong verb
    CHECK(!decodeHotFx("x fx 1 vfx/a 0 0 0 0 0 0 1").has_value());        // bad seq
    CHECK(!decodeHotFx("1 fx 1 vfx/a 0 0 nan 0 0 0 1").has_value());      // non-finite
    CHECK(!decodeHotFx("1 fx 1 vfx/a 0 0 0 0 0 0 2").has_value());        // flag not 0/1
    CHECK(!decodeHotFx("1  fx 1 vfx/a 0 0 0 0 0 0 1").has_value());       // double space
    CHECK(decodeHotFx("1 fx 0").has_value());                             // zero records is well formed
}

TEST(hot_sound_round_trip) {
    HotSound s;
    s.alias = "amb_wind_gust";
    s.origin[0] = -20;
    s.origin[1] = 300.125;
    s.origin[2] = 12;
    s.loop = true;
    s.stop = false;
    const std::string v = encodeHotSound(s, 21);
    CHECK(v == "21 snd amb_wind_gust -20 300.125 12 1 0");
    unsigned seq = 0;
    auto d = decodeHotSound(v, &seq);
    CHECK(d.has_value());
    CHECK(seq == 21);
    if (d) {
        CHECK(d->alias == s.alias);
        CHECK(d->loop == true);
        CHECK(d->stop == false);
        for (int i = 0; i < 3; ++i) CHECK(near(d->origin[i], s.origin[i]));
    }

    HotSound stop;
    stop.alias = "amb_wind_gust";
    stop.stop = true;
    auto d2 = decodeHotSound(encodeHotSound(stop, 22), &seq);
    CHECK(d2.has_value());
    if (d2) {
        CHECK(d2->stop == true);
        CHECK(d2->loop == false);
    }
}

TEST(hot_sound_rejects_bad_alias_and_malformed) {
    HotSound s;
    s.alias = "mp/wind";           // '/' is not allowed for sounds (map_script rule)
    CHECK(encodeHotSound(s, 1).empty());
    s.alias = "";
    CHECK(encodeHotSound(s, 1).empty());
    s.alias = "ok_alias";
    s.origin[2] = std::nan("");
    CHECK(encodeHotSound(s, 1).empty());

    CHECK(!decodeHotSound("1 snd ok_alias 0 0 0 1").has_value());
    CHECK(!decodeHotSound("1 snd ok_alias 0 0 0 2 0").has_value());
    CHECK(!decodeHotSound("1 fx ok_alias 0 0 0 1 0").has_value());
}

TEST(hot_sun_round_trip) {
    HotSun sun;
    sun.dir[0] = -40;
    sun.dir[1] = 120;
    sun.dir[2] = 0;
    sun.color[0] = 0.9f;
    sun.color[1] = 0.8f;
    sun.color[2] = 0.7f;
    sun.intensity = 2.5;
    const std::string v = encodeHotSun(sun, 13);
    CHECK(!v.empty());
    CHECK(v.rfind("13 sun -40 120 0 ", 0) == 0);
    unsigned seq = 0;
    auto d = decodeHotSun(v, &seq);
    CHECK(d.has_value());
    CHECK(seq == 13);
    if (d) {
        for (int i = 0; i < 3; ++i) {
            CHECK(near(d->dir[i], sun.dir[i]));
            CHECK(d->color[i] == sun.color[i]);   // float round trip is exact through the shortest text
        }
        CHECK(near(d->intensity, 2.5));
    }
    CHECK(v.size() <= kHotChannelMaxLen);
}

TEST(hot_sun_rejects_invalid) {
    HotSun sun;
    sun.intensity = -1;
    CHECK(encodeHotSun(sun, 1).empty());
    sun.intensity = std::nan("");
    CHECK(encodeHotSun(sun, 1).empty());
    sun.intensity = 1;
    sun.dir[1] = INFINITY;
    CHECK(encodeHotSun(sun, 1).empty());
    sun.dir[1] = 0;
    sun.color[2] = std::nanf("");
    CHECK(encodeHotSun(sun, 1).empty());

    CHECK(!decodeHotSun("1 sun 0 0 0 1 1 1").has_value());          // missing intensity
    CHECK(!decodeHotSun("1 sun 0 0 0 1 1 1 -2").has_value());       // negative intensity
    CHECK(!decodeHotSun("1 snd 0 0 0 1 1 1 2").has_value());        // wrong verb
}

TEST(hot_gsc_thread_and_builtins) {
    const std::string g = hotChannelsGsc();
    CHECK(!g.empty());
    CHECK(g.find("iw8r_hot_channels()") != std::string::npos);
    CHECK(g.find("getdvar(\"iw8r_fx\")") != std::string::npos);
    CHECK(g.find("getdvar(\"iw8r_snd\")") != std::string::npos);
    CHECK(g.find("iw8r_sun") == std::string::npos);  // removed: the sun builtins crashed Replay MP
    CHECK(g.find("loadfx(") == std::string::npos);  // effects come from the map's main(), never loaded late
    CHECK(g.find("playfx(fx, origin, anglestoforward(angles), anglestoup(angles))") != std::string::npos);
    CHECK(g.find("playsoundatpos(origin, alias)") != std::string::npos);
    CHECK(g.find("playloopsound(alias)") != std::string::npos);
    CHECK(g.find("stoploopsound(alias)") != std::string::npos);
    CHECK(g.find("lerpsunangles(") == std::string::npos && g.find("setsuncolorandintensity(") == std::string::npos);
    // A value is applied once: the last string is remembered per channel.
    CHECK(countOf(g, "v != lastFx") == 1);
    CHECK(countOf(g, "v != lastSnd") == 1);
    // Balanced braces and parentheses (cheap structural check; gsc-tool compiles the same text in the build).
    long braces = 0, parens = 0;
    for (char c : g) {
        if (c == '{') ++braces;
        if (c == '}') --braces;
        if (c == '(') ++parens;
        if (c == ')') --parens;
        CHECK(braces >= 0 && parens >= 0);
    }
    CHECK(braces == 0);
    CHECK(parens == 0);
}

TEST(hot_channels_in_map_script) {
    // The map script starts the channels and names every loaded effect for the hot fx channel.
    std::vector<FxPlacement> fx(1);
    fx[0].effect = "vfx/iw8/core/vfx_test";
    const std::string g = mapScriptGsc(std::nullopt, fx);
    CHECK(g.find("level thread iw8r_hot_channels();") != std::string::npos);
    CHECK(g.find("level.iw8r_hotfx[\"vfx/iw8/core/vfx_test\"] = level.iw8r_fx[0];") != std::string::npos);
    CHECK(g.find("iw8r_hot_channels()\n{") != std::string::npos);
    MapScriptExtras off;
    off.hotUpdate = false;
    const std::string g2 = mapScriptGsc(std::nullopt, fx, {}, off);
    CHECK(g2.find("iw8r_hot_channels") == std::string::npos);
}

TEST(hot_sun_from_worldspawn_keys) {
    const HotSun h = hotSunFromKeys("-90 0 0", "1 0.5 2", "");
    CHECK(h.dir[0] == -90 && h.dir[1] == 0 && h.dir[2] == 0);  // the key's angles, unchanged
    CHECK(h.color[0] == 1 && h.color[1] == 0.5f && h.color[2] == 1);  // clamped to 0..1
    CHECK(h.intensity == 1);
    const HotSun w = hotSunFromKeys("0 90 0", "bad", "1.5");
    CHECK(w.dir[1] == 90 && w.color[0] == 1 && w.intensity == 1.5);
    CHECK(!encodeHotSun(w, 3).empty());
}


TEST(map_script_sun_shadow_range) {
    const std::string g = mapScriptGsc(std::nullopt, {});
    CHECK(g.find("level thread iw8r_sun_shadow_range();") != std::string::npos);
    CHECK(g.find("player setclientdvar(\"sm_sunCascadeSizeMultiplier1\", \"3\");") != std::string::npos);
    MapScriptExtras off;
    off.sunShadowRange[0] = 0;
    CHECK(mapScriptGsc(std::nullopt, {}, {}, off).find("iw8r_sun_shadow_range") == std::string::npos);
}


TEST(map_script_stock_animations) {
    MapScriptExtras ex;
    ex.anims.push_back({"fan_a", "desktop_fan_fn_01_loop", 2.5});
    ex.anims.push_back({"bad name", "x", 0});  // unsafe targetname: skipped
    const std::string g = mapScriptGsc(std::nullopt, {}, {}, ex);
    CHECK(g.find("level thread iw8r_anim_0();") != std::string::npos);
    CHECK(g.find("ent scriptmodelplayanim(\"desktop_fan_fn_01_loop\");") != std::string::npos);  // literal: the converter names it
    CHECK(g.find("wait 2.5;") != std::string::npos);
    CHECK(g.find("iw8r_anim_1") == std::string::npos);
}


TEST(map_script_vision_volumes) {
    MapScriptExtras ex;
    ex.baseVision = "mp_test";
    ex.visionVolumes.push_back({"cave", "mp_test_v0"});
    const std::string g = mapScriptGsc(std::nullopt, {}, {}, ex);
    CHECK(g.find("level thread iw8r_vision_volumes();") != std::string::npos);
    CHECK(g.find("vols[0] = getent(\"cave\", \"targetname\");") != std::string::npos);
    CHECK(g.find("p visionsetnakedforplayer(\"mp_test\", 0.5);") != std::string::npos);
    CHECK(g.find("iw8r_vision_inside(p, vols[i], vnames[i])") != std::string::npos);
}


TEST(map_script_doors) {
    MapScriptExtras ex;
    MapScriptExtras::Door d;
    d.door = "gate1";
    d.trigger = "gate1_use";
    d.yaw = 90;
    ex.doors.push_back(d);
    MapScriptExtras::Door s2 = d;
    s2.door = "slider";
    s2.rotate = false;
    s2.offset[2] = 96;
    ex.doors.push_back(s2);
    const std::string g = mapScriptGsc(std::nullopt, {}, {}, ex);
    CHECK(g.find("level thread iw8r_door_0();") != std::string::npos && g.find("level thread iw8r_door_1();") != std::string::npos);
    CHECK(g.find("trig waittill(\"trigger\", user);") != std::string::npos);
    CHECK(g.find("door rotateyaw(90, 1);") != std::string::npos && g.find("door rotateyaw(-90, 1);") != std::string::npos);
    CHECK(g.find("door moveto(door.origin + (0, 0, 96), 1);") != std::string::npos);
}

// 6.12: inspector-only behaviours generate one thread each.
TEST(map_script_behaviours) {
    MapScriptExtras ex;
    ex.teleports.push_back({"tp_in", "tp_out"});
    ex.teleports.push_back({"bad name", "tp_out"});  // unsafe name: dropped
    MapScriptExtras::JumpPad j;
    j.trigger = "pad";
    j.velocity[0] = 400;
    j.velocity[2] = 650;
    ex.jumpPads.push_back(j);
    MapScriptExtras::DamageZone h;
    h.trigger = "acid";
    h.amount = 15;
    h.interval = 0.5;
    ex.damageZones.push_back(h);
    MapScriptExtras::Lift l;
    l.targetname = "elevator";
    l.dz = 256;
    l.moveTime = 3;
    l.waitTime = 1;
    ex.lifts.push_back(l);
    const std::string g = mapScriptGsc(std::nullopt, {}, {}, ex);
    CHECK(g.find("level thread iw8r_teleport_0();") != std::string::npos);
    CHECK(g.find("iw8r_teleport_1") == std::string::npos);
    CHECK(g.find("dest = getent(\"tp_out\", \"targetname\");") != std::string::npos);
    CHECK(g.find("user setorigin(dest.origin);") != std::string::npos && g.find("user setplayerangles(dest.angles);") != std::string::npos);
    CHECK(g.find("level thread iw8r_jump_0();") != std::string::npos);
    CHECK(g.find("user setvelocity((400, 0, 650));") != std::string::npos);
    CHECK(g.find("level thread iw8r_damage_0();") != std::string::npos);
    CHECK(g.find("user.iw8r_dz = gettime() + 500;") != std::string::npos && g.find("radiusdamage(user.origin, 32, 15, 15);") != std::string::npos);
    CHECK(g.find("level thread iw8r_lift_0();") != std::string::npos);
    CHECK(g.find("lift moveto(bottom + (0, 0, 256), 3);") != std::string::npos && g.find("lift moveto(bottom, 3);") != std::string::npos);
    long braces = 0, parens = 0;
    for (char c : g) {
        braces += c == '{' ? 1 : c == '}' ? -1 : 0;
        parens += c == '(' ? 1 : c == ')' ? -1 : 0;
        CHECK(braces >= 0 && parens >= 0);
    }
    CHECK(braces == 0 && parens == 0);
}
