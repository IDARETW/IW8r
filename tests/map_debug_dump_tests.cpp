// Editor side of the in-game map debug overlay (src/core/map_debug_dump.*, docs/research/78_map_debug_overlay.md):
// command texts, the parser of the client's entity dump, and the comparison with the map.
#include <string>

#include "devmap_plan.h"
#include "map_debug_dump.h"
#include "map_text.h"
#include "test_harness.h"

using namespace iw8r;

namespace {

MapFile parse(const std::string& body) {
    Id next = 1;
    ParseResult pr = parseMap("iwmap 4\n" + body, next);
    CHECK(pr.map.has_value());
    return pr.map ? *pr.map : MapFile{};
}

std::string ent(const std::string& cls, const std::string& name, const std::string& origin = "") {
    return "{\n\"classname\" \"" + cls + "\"\n" + (name.empty() ? "" : "\"targetname\" \"" + name + "\"\n") +
           (origin.empty() ? "" : "\"origin\" \"" + origin + "\"\n") + "}\n";
}

const char* kDump =
    "old IW8R_MAPDEBUG begin map=mp_old\n"
    "old IW8R_ENT 1|script_model|script_model||stale|||||crate|0.00 0.00 0.00|0.00 0.00 0.00|0.00 0.00 0.00|8.00 8.00 8.00|1|0|-1\n"
    "old IW8R_MAPDEBUG end entities=1\n"
    "12:00:01 IW8R_MAPDEBUG begin map=mp_iw8r_test gametype=war\r\n"
    "12:00:01 IW8R_VIEW 10.50 -20.25 124.00 5.00 90.00\r\n"
    "12:00:01 IW8R_FEET 10.50 -20.25 64.00 0\r\n"
    "12:00:01 IW8R_AUDIO zone=hall reverb=iw8_lg_tunnel\r\n"
    "12:00:01 IW8R_ENT_COLUMNS num|type|classname|code_classname|targetname|target|script_noteworthy|script_linkname|script_linkto|model|origin|angles|box_mid|box_half|model_type|linked|parent\r\n"
    "12:00:01 IW8R_ENT 0|player|player||||||||10.50 -20.25 64.00|0.00 90.00 0.00|0.00 0.00 36.00|16.00 16.00 36.00|0|0|-1\r\n"
    "12:00:01 IW8R_ENT 40|script_model|script_model||spin1||note1|||crate_wood|128.00 64.00 32.00|0.00 45.00 0.00|0.00 0.00 0.00|16.00 16.00 16.00|1|0|-1\r\n"
    "12:00:01 IW8R_ENT 41|script_brushmodel|script_brushmodel||door1|||||*3|0.00 180.00 48.00|0.00 100.00 0.00|0.00 0.00 0.00|32.00 4.00 48.00|2|1|44\r\n"
    "12:00:01 IW8R_ENT 42|trigger|trigger_multiple||tp_in|tp_out||||*4|-200.00 136.00 32.00|0.00 0.00 0.00|0.00 0.00 0.00|16.00 16.00 32.00|2|0|-1\r\n"
    "12:00:01 IW8R_ENT 43|script_model|script_model||cone||||||1.00 2.00 3.00|0.00 0.00 0.00|0.00 0.00 0.00|8.00 8.00 8.00|1|0|-1\r\n"
    "12:00:01 IW8R_ENT 44|script_model|script_model||cone||||||5.00 6.00 7.00|0.00 0.00 0.00|0.00 0.00 0.00|8.00 8.00 8.00|1|0|-1\r\n"
    "12:00:01 IW8R_ENT broken line\r\n"
    "12:00:01 IW8R_MAPDEBUG end entities=7\r\n";

}  // namespace

TEST(map_debug_commands) {
    const auto& presets = mapDebugPresets();
    CHECK(presets.size() == 5 && presets[0].mask == 0 && presets[1].mask == 4 && presets[2].mask == 7 && presets[3].mask == 15 && presets[4].mask == 63);
    auto c = mapDebugCommands(7, "");
    CHECK(c.size() == 2 && c[0] == "set iw8r_mapDebug 7" && c[1] == "set iw8r_mapDebugFilter \"\"");
    c = mapDebugCommands(999, "  trigger   spawn -tdm ");
    CHECK(c[0] == "set iw8r_mapDebug 255" && c[1] == "set iw8r_mapDebugFilter trigger spawn -tdm");
    CHECK(mapDebugCommands(-3, "")[0] == "set iw8r_mapDebug 0");
    // The mask command passes the F5 command filter; the filter text can never carry a second command.
    std::string joined;
    CHECK(devmap::joinCommands({c[0], c[1]}, joined));
    bool ok = true;
    CHECK(cleanMapDebugFilter("door1; quit", &ok).empty() && !ok);
    CHECK(cleanMapDebugFilter("a\"b", &ok).empty() && !ok);
    CHECK(cleanMapDebugFilter("Door_1 -x", &ok) == "Door_1 -x" && ok);
    CHECK(cleanMapDebugFilter(std::string(97, 'a'), &ok).empty() && !ok);
    CHECK(mapDebugCommands(1, "x; quit")[1] == "set iw8r_mapDebugFilter \"\"");
    CHECK(std::string(kMapDebugDumpCommand) == "iw8r_mapDebugDump");
}

TEST(map_debug_dump_parse) {
    MapDebugDump none = parseMapDebugDump("nothing here\n");
    CHECK(!none.found && !none.complete && none.entities.empty());
    MapDebugDump half = parseMapDebugDump("t IW8R_MAPDEBUG begin map=x\nt IW8R_VIEW 1 2 3 4 5\n");
    CHECK(half.found && !half.complete && half.hasView);

    const MapDebugDump d = parseMapDebugDump(kDump);
    CHECK(d.found && d.complete && d.declared == 7);
    CHECK(d.hasView && d.view.x == 10.5 && d.view.y == -20.25 && d.view.z == 124 && d.pitch == 5 && d.yaw == 90);
    CHECK(d.hasFeet && d.feet.z == 64);
    CHECK(d.audio == "zone=hall reverb=iw8_lg_tunnel");
    CHECK(d.entities.size() == 6);  // the last block only; the broken line is skipped
    if (d.entities.size() == 6) {
        const GameEntity& e = d.entities[1];
        CHECK(e.num == 40 && e.classname == "script_model" && e.targetname == "spin1" && e.noteworthy == "note1" && e.model == "crate_wood");
        CHECK(e.origin.x == 128 && e.origin.y == 64 && e.origin.z == 32 && e.angles.y == 45 && e.boxHalf.x == 16 && !e.linked && e.parent == -1);
        CHECK(d.entities[2].linked && d.entities[2].parent == 44 && d.entities[2].model == "*3");
        CHECK(d.entities[3].target == "tp_out");
    }
}

TEST(map_debug_dump_compare) {
    const MapDebugDump d = parseMapDebugDump(kDump);
    const MapFile map = parse("{\n\"classname\" \"worldspawn\"\n}\n" +
                              ent("script_model", "spin1", "128 64 32") +        // in place
                              ent("script_brushmodel", "door1") +                // found, no origin key to compare
                              ent("trigger_multiple", "tp_in", "0 0 0") +        // found, at another place
                              ent("script_origin", "lost1", "5 5 5") +           // not in the game
                              ent("script_model", "cone", "1 2 3") + ent("script_model", "cone", "5 6 7") +  // two copies, two found
                              ent("script_model", "pair", "0 0 0") + ent("script_model", "pair", "9 9 9") +  // two copies, none found
                              ent("light", "lamp1", "0 0 0") +                   // build-time only: not looked for
                              ent("script_model", "", "0 0 0") +                 // no name: not looked for
                              ent("mp_tdm_spawn", "") + ent("mp_tdm_spawn", ""));
    const DumpCompare r = compareDumpWithMap(d, map);
    CHECK(r.gameEntities == 6 && r.checked == 6);
    CHECK(r.matched == 3 && r.moved == 1 && r.missing == 2);
    std::string all;
    for (const std::string& l : r.lines) all += l + "\n";
    CHECK(all.find("6 entities in the game") != std::string::npos && all.find("the dump said 7") != std::string::npos);
    CHECK(all.find("MISSING  lost1 (script_origin)") != std::string::npos);
    CHECK(all.find("MISSING  pair (script_model x 2)") != std::string::npos);
    CHECK(all.find("MOVED    tp_in (trigger_multiple): map 0 0 0, game -200 136 32") != std::string::npos);
    CHECK(all.find("lamp1") == std::string::npos);
    CHECK(all.find("spawn points: 2 in the map, 0 entities") != std::string::npos && all.find("list of their own") != std::string::npos);
    CHECK(all.find("player feet at 10 -20 64; audio: zone=hall") != std::string::npos || all.find("player feet at 11 -20 64; audio: zone=hall") != std::string::npos);
    // The summary comes first.
    CHECK(r.lines.size() >= 4 && r.lines[1].find("6 named map entities looked for: 3 found in place, 1 at another place, 2 missing") == 0);

    // A different copy count is its own finding.
    const MapFile one = parse("{\n\"classname\" \"worldspawn\"\n}\n" + ent("script_model", "cone", "1 2 3"));
    const DumpCompare c = compareDumpWithMap(d, one);
    CHECK(c.checked == 1 && c.matched == 0 && c.lines.back().find("COUNT    cone: 1 in the map, 2 in the game") == 0);
}
