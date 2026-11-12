// Roadmap 13.6 (research 91 / 92): a new game type script carried by a map, its checks and the template.
#include "gametype_script.h"
#include "map_gametypes.h"
#include "map_text.h"
#include "test_harness.h"

using namespace iw8r;

namespace {
const char* kIndex =
    "# comment\n"
    "scripts/mp/globallogic\tnumbered\t1857\tcommon_base_mp\n"
    "scripts/mp/gameobjects\tnumbered\t1851\tcommon_base_mp\n"
    "scripts/mp/gamescore\tnumbered\t1852\tcommon_base_mp\n"
    "scripts/mp/spawnlogic\tnumbered\t1912\tcommon_base_mp\n"
    "scripts/mp/utility/game\tnumbered\t2398\tcommon_base_mp\n"
    "scripts/mp/gametypes/common\tnamed\t0\tcommon_base_mp\n"
    "scripts/mp/gametypes/war\tnamed\t0\tcommon_base_mp\n";
}

TEST(gametype_script_index_and_names) {
    StockScriptIndex idx;
    CHECK(idx.parse(kIndex) && idx.size() == 7);
    CHECK(idx.form("scripts\\mp\\globallogic") == StockScriptIndex::Form::Numbered);
    CHECK(idx.form("Scripts/MP/gametypes/war.gsc") == StockScriptIndex::Form::Named);
    CHECK(idx.form("scripts\\mp\\nothing") == StockScriptIndex::Form::Unknown);
    CHECK(normalizedScriptPath("Scripts\\Mp\\Utility\\Game.gsc") == "scripts/mp/utility/game");
    CHECK(gameTypeNameOfScript("gametypes/Team_Kills.gsc") == "team_kills" && gameTypeNameOfScript("a\\b\\gg.gsc") == "gg");
    CHECK(gameTypeNameOfScript("toolongname12.gsc").empty() && gameTypeNameOfScript("bad-name.gsc").empty() && gameTypeNameOfScript(".gsc").empty());
}

TEST(gametype_script_far_calls) {
    const std::string gsc = "main()\n{\n    scripts\\mp\\globallogic::init();\n    // scripts\\mp\\gametypes\\common::x();\n"
                            "    s = \"scripts\\\\mp\\\\fake::y\";\n    level.f = ::local;\n    thread scripts\\mp\\gamescore::give( 1 );\n}\n";
    const auto calls = scriptFarCalls(gsc);
    CHECK(calls.size() == 2);
    if (calls.size() == 2) {
        CHECK(calls[0].line == 3 && calls[0].path == "scripts\\mp\\globallogic" && calls[0].function == "init");
        CHECK(calls[1].line == 7 && calls[1].function == "give");
    }
}

TEST(gametype_script_checks) {
    StockScriptIndex idx;
    CHECK(idx.parse(kIndex));
    // The template passes its own checks and names its game type.
    const std::string tmpl = gameTypeScriptTemplate("teamkills");
    CHECK(tmpl.find("Game type teamkills") != std::string::npos && tmpl.find("scr_teamkills_scorelimit") != std::string::npos);
    CHECK(tmpl.find("@NAME@") == std::string::npos);
    const auto ok = checkGameTypeScript(tmpl, "teamkills", idx);
    CHECK(ok.empty());
    for (const auto& p : ok) std::printf("    unexpected: %s\n", p.c_str());
    // A stock name, a bad name, no main, a named and an unknown far call.
    CHECK(!checkGameTypeScript(tmpl, "war", idx).empty());
    CHECK(!checkGameTypeScript(tmpl, "Bad Name", idx).empty());
    const std::string bad = "init()\n{\n    scripts\\mp\\gametypes\\common::oncommonnormaldeath();\n    scripts\\mp\\nothing::f();\n"
                            "    scripts\\mp\\gametypes\\mine::local();\n}\n";
    const auto problems = checkGameTypeScript(bad, "mine", idx);
    CHECK(problems.size() == 3);
    bool named = false, unknown = false, noMain = false;
    for (const auto& p : problems) {
        named |= p.find("line 3") != std::string::npos && p.find("stored by its name") != std::string::npos;
        unknown |= p.find("line 4") != std::string::npos && p.find("no stock script") != std::string::npos;
        noMain |= p.find("no main()") != std::string::npos;
    }
    CHECK(named && unknown && noMain);
    // "main" inside a call or a comment is not a definition.
    CHECK(!checkGameTypeScript("x()\n{\n    // main()\n    scripts\\mp\\gameobjects::main( a );\n}\n", "abc", idx).empty());
}

TEST(gametype_script_listed_in_map_json) {
    const std::string text = "iwmap 4\n{\n\"classname\" \"worldspawn\"\n\"iw8r_gametype_script\" \"gametypes/teamkills.gsc\"\n}\n";
    Id next = 1;
    ParseResult pr = parseMap(text, next);
    CHECK(pr.map.has_value());
    if (!pr.map) return;
    const MapGameTypes types = mapGameTypes(*pr.map);
    CHECK((" " + types.list + " ").find(" teamkills ") != std::string::npos);
    // With a hand-written list too.
    const std::string listed = "iwmap 4\n{\n\"classname\" \"worldspawn\"\n\"iw8r_gametypes\" \"war dm\"\n"
                               "\"iw8r_gametype_script\" \"gametypes/teamkills.gsc\"\n}\n";
    Id next2 = 1;
    ParseResult pr2 = parseMap(listed, next2);
    CHECK(pr2.map.has_value());
    if (pr2.map) CHECK(mapGameTypes(*pr2.map).list == "war dm teamkills");
}
