// Game types of a map (src/core/map_gametypes.*): detection from spawn classes and mode objects, the worldspawn
// override, and the map.json "gameTypes" line the recompiled client reads (roadmap F.11, research 79).
#include <string>

#include "map_gametypes.h"
#include "map_text.h"
#include "test_harness.h"

using namespace iw8r;

namespace {

std::string ent(const std::string& cls, const std::string& keys = "") {
    return "{\n\"classname\" \"" + cls + "\"\n" + keys + "}\n";
}

std::string kv(const std::string& k, const std::string& v) { return "\"" + k + "\" \"" + v + "\"\n"; }

MapFile parse(const std::string& body) {
    Id next = 1;
    ParseResult pr = parseMap("iwmap 4\n" + body, next);
    CHECK(pr.map.has_value());
    return pr.map ? *pr.map : MapFile{};
}

const std::string kWorld = "{\n\"classname\" \"worldspawn\"\n}\n";
const std::string kTdm = ent("mp_tdm_spawn") + ent("mp_tdm_spawn") + ent("mp_tdm_spawn_allies_start") + ent("mp_tdm_spawn_axis_start");

const GameTypeCheck& find(const std::vector<GameTypeCheck>& v, const std::string& g) {
    for (const GameTypeCheck& c : v)
        if (c.gametype == g) return c;
    static GameTypeCheck none;
    return none;
}

}  // namespace

TEST(gametypes_detection) {
    // Nothing but the world: no game type is complete, the map is still listed with war.
    MapFile empty = parse(kWorld);
    auto checks = gameTypeChecks(empty);
    CHECK(checks.size() == 7 && checks[0].gametype == "war" && checks[1].gametype == "dm" && checks[4].gametype == "ctf" &&
          checks[5].gametype == "br" && !checks[5].supported && checks[6].gametype == "koth" && !checks[6].supported);
    CHECK(!find(checks, "war").supported && find(checks, "war").missing.size() == 3);
    MapGameTypes g = mapGameTypes(empty);
    CHECK(g.list == "war" && !g.fromKey && !g.notes.empty() && g.notes[0].find("no game type is complete") != std::string::npos);

    // TDM spawns only (what the test maps carry): war.
    g = mapGameTypes(parse(kWorld + kTdm));
    CHECK(g.list == "war" && g.notes.size() == 1 && g.notes[0] == "game types detected: war");

    // Free-for-all needs two mp_dm_spawn.
    g = mapGameTypes(parse(kWorld + kTdm + ent("mp_dm_spawn")));
    CHECK(g.list == "war");
    g = mapGameTypes(parse(kWorld + kTdm + ent("mp_dm_spawn") + ent("mp_dm_spawn")));
    CHECK(g.list == "war dm");

    // Domination: spawns, both starts and the three labelled flags. A started but incomplete set gets a note.
    const std::string domSpawns = ent("mp_dom_spawn") + ent("mp_dom_spawn") + ent("mp_dom_spawn_allies_start") + ent("mp_dom_spawn_axis_start");
    auto flag = [](const std::string& label) { return ent("trigger_radius", kv("targetname", "flag_primary") + kv("script_label", label)); };
    g = mapGameTypes(parse(kWorld + kTdm + domSpawns + flag("_a") + flag("_b")));
    CHECK(g.list == "war");
    bool noted = false;
    for (const std::string& n : g.notes) noted = noted || (n.find("dom (Domination) is not offered") != std::string::npos && n.find("_c") != std::string::npos);
    CHECK(noted);
    g = mapGameTypes(parse(kWorld + kTdm + domSpawns + flag("_a") + flag("_b") + flag("_c")));
    CHECK(g.list == "war dom");

    // Search and Destroy: both spawn classes, two site triggers, the bomb and its pickup trigger.
    const std::string sd = ent("mp_sd_spawn_attacker") + ent("mp_sd_spawn_defender") +
                           ent("trigger_use_touch", kv("targetname", "bombzone") + kv("script_label", "_a")) +
                           ent("trigger_use_touch", kv("targetname", "bombzone") + kv("script_label", "_b")) +
                           ent("script_model", kv("targetname", "sd_bomb")) + ent("trigger_multiple", kv("targetname", "sd_bomb_pickup_trig"));
    CHECK(find(gameTypeChecks(parse(kWorld + sd)), "sd").supported);
    // A script_model named bombzone is not a site trigger.
    CHECK(!find(gameTypeChecks(parse(kWorld + ent("mp_sd_spawn_attacker") + ent("mp_sd_spawn_defender") +
                                     ent("script_model", kv("targetname", "bombzone")) + ent("script_model", kv("targetname", "bombzone")) +
                                     ent("script_model", kv("targetname", "sd_bomb")) + ent("trigger_multiple", kv("targetname", "sd_bomb_pickup_trig")))),
                "sd").supported);

    // Capture the Flag: spawns, starts, and flag / trigger / zone for both teams.
    std::string ctf = ent("mp_ctf_spawn") + ent("mp_ctf_spawn") + ent("mp_ctf_spawn_allies_start") + ent("mp_ctf_spawn_axis_start");
    for (const char* n : {"ctf_flag_allies", "ctf_flag_axis", "ctf_trig_allies", "ctf_trig_axis", "ctf_zone_allies"}) ctf += ent("script_model", kv("targetname", n));
    CHECK(!find(gameTypeChecks(parse(kWorld + ctf)), "ctf").supported);
    ctf += ent("trigger_radius", kv("targetname", "ctf_zone_axis"));
    CHECK(find(gameTypeChecks(parse(kWorld + ctf)), "ctf").supported);
    g = mapGameTypes(parse(kWorld + kTdm + sd + ctf));
    CHECK(g.list == "war sd ctf");
}

TEST(gametypes_override_key) {
    const std::string world = "{\n\"classname\" \"worldspawn\"\n\"iw8r_gametypes\" \"WAR, conf  koth war\"\n}\n";
    MapGameTypes g = mapGameTypes(parse(world + kTdm));
    CHECK(g.fromKey && g.list == "war conf koth");
    // Listed but incomplete, where the editor can tell: one note.
    g = mapGameTypes(parse("{\n\"classname\" \"worldspawn\"\n\"iw8r_gametypes\" \"war dom\"\n}\n" + kTdm));
    CHECK(g.fromKey && g.list == "war dom");
    bool noted = false;
    for (const std::string& n : g.notes) noted = noted || n.find("dom (Domination) is listed but the map lacks") != std::string::npos;
    CHECK(noted);
    // A bad value falls back to detection, with a note.
    g = mapGameTypes(parse("{\n\"classname\" \"worldspawn\"\n\"iw8r_gametypes\" \"war; quit\\\"\"\n}\n" + kTdm));
    CHECK(!g.fromKey && g.list == "war");

    std::string out;
    CHECK(normalizeGameTypeList("war dm", out) && out == "war dm");
    CHECK(normalizeGameTypeList("  Dom,sd ;ctf ", out) && out == "dom sd ctf");
    CHECK(!normalizeGameTypeList("", out) && !normalizeGameTypeList("   ", out));
    CHECK(!normalizeGameTypeList("war \"x\"", out) && !normalizeGameTypeList("a-b", out));
    CHECK(!normalizeGameTypeList("abcdefghijkl", out));  // 12 characters: the client's names hold 11
    CHECK(normalizeGameTypeList("abcdefghijk", out));
    CHECK(!normalizeGameTypeList("a b c d e f g h i j k l m n o p q", out));  // 17 names
}

TEST(gametypes_map_json) {
    const std::string json = "{\n  \"id\": \"mp_iw8r_test\",\n  \"title\": \"Test\"\n}\n";
    const std::string with = withGameTypes(json, "war dm");
    CHECK(with == "{\n  \"id\": \"mp_iw8r_test\",\n  \"title\": \"Test\",\n  \"gameTypes\": \"war dm\"\n}\n");
    // Replacing keeps everything else.
    CHECK(withGameTypes(with, "war") == "{\n  \"id\": \"mp_iw8r_test\",\n  \"title\": \"Test\",\n  \"gameTypes\": \"war\"\n}\n");
    CHECK(withGameTypes(with, "war dm") == with);
    // CRLF text keeps CRLF; an empty object gets the one key; text that is not an object is left alone.
    CHECK(withGameTypes("{\r\n  \"id\": \"mp_a\"\r\n}\r\n", "war") == "{\r\n  \"id\": \"mp_a\",\r\n  \"gameTypes\": \"war\"\r\n}\r\n");
    CHECK(withGameTypes("{}", "war") == "{\n  \"gameTypes\": \"war\"\n}");
    CHECK(withGameTypes("not json", "war") == "not json");
    // The client's title reader scans for "title" and "name": the new key must not look like either.
    CHECK(with.find("\"name\"") == std::string::npos);
}
