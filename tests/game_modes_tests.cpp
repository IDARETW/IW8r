#include "game_modes.h"
#include "test_harness.h"

#include <filesystem>
#include <fstream>

using namespace iw8r;
namespace fs = std::filesystem;

// Research 87: a mode variant is a game type script plus dvars; presets carry them.
TEST(game_modes_builtin_presets) {
    const auto& modes = builtinGameModes();
    CHECK(modes.size() >= 12);
    for (const auto& m : modes) {
        CHECK(validGameModeId(m.id) && !m.title.empty() && !m.gametype.empty() && m.builtin);
        for (const auto& [name, value] : m.dvars) CHECK(validModeDvarName(name) && validModeDvarValue(value));
    }
    const GameModePreset* trios = findGameMode(modes, "br_trios");
    CHECK(trios && trios->gametype == "br" && trios->dvars.size() == 7 && !trios->brNoCircle);
    // Entry for entry the Trios playlist of the private server's catalogue (both names of the loot table).
    if (trios)
        CHECK(modeDvarsText(trios->dvars) ==
              "OKTSLRSQMS=-1; RKMMNSQKO=mp/loot_item_defs_s3.csv; br_mission_reward_groups_filename=mp/brmission_reward_groups.csv; "
              "loot_table_name=mp/loot_item_defs_s3.csv; scr_br_teamsize=3; scriptable_force_large_mode=1; sv_forceNetfieldLoD=-1");
    const GameModePreset* money = findGameMode(modes, "blood_money");
    CHECK(money && money->dvars.size() == 10);
    const GameModePreset* small = findGameMode(modes, "br_small_map");
    CHECK(small && small->gametype == "br" && small->brNoCircle && small->brNoGulag && small->brSkipDrop);
    if (small) CHECK(modeBrText(*small) == "nocircle nogulag skipdrop");
    const GameModePreset* war = findGameMode(modes, "war");
    CHECK(war && war->dvars.empty() && modeBrText(*war).empty());
    CHECK(!findGameMode(modes, "nothing"));
}

TEST(game_modes_dvar_text_and_rules) {
    CHECK(validModeDvarName("scr_br_teamsize") && !validModeDvarName("") && !validModeDvarName("a b") && !validModeDvarName("x;y"));
    CHECK(validModeDvarValue("mp/loot_item_defs_s3.csv") && validModeDvarValue("3") && validModeDvarValue("-1.5"));
    CHECK(!validModeDvarValue("") && !validModeDvarValue("a b") && !validModeDvarValue("a\"b") && !validModeDvarValue("a;b"));
    CHECK(validGameModeId("my_mode_2") && !validGameModeId("My Mode") && !validGameModeId(""));

    std::vector<std::string> notes;
    const auto dvars = parseModeDvars(" scr_a = 1 ; bad name=2; scr_b=mp/x.csv;scr_a=5; novalue ; scr_c=has space", &notes);
    CHECK(dvars.size() == 2);
    if (dvars.size() == 2) {
        CHECK(dvars[0].first == "scr_a" && dvars[0].second == "5");  // the later value wins, the place stays
        CHECK(dvars[1].first == "scr_b" && dvars[1].second == "mp/x.csv");
    }
    CHECK(notes.size() == 3);
    CHECK(parseModeDvars("").empty());

    bool a = true, b = true, c = true;
    parseModeBr("NoCircle skipdrop other", a, b, c);
    CHECK(a && !b && c);
    parseModeBr("", a, b, c);
    CHECK(!a && !b && !c);
}

TEST(game_modes_json_round_trip_and_user_folder) {
    GameModePreset p;
    p.id = "gun_rush";
    p.title = "Gun rush";
    p.gametype = "dm";
    p.dvars = {{"scr_dm_scorelimit", "50"}, {"scr_dm_timelimit", "5"}};
    p.description = "Short free-for-all";
    p.brSkipDrop = true;
    GameModePreset q;
    std::string err;
    CHECK(parseGameModePreset(gameModePresetJson(p), q, &err));
    CHECK(q.id == p.id && q.title == p.title && q.gametype == "dm" && q.dvars == p.dvars && q.description == p.description &&
          q.brSkipDrop && !q.brNoCircle && !q.builtin);

    CHECK(!parseGameModePreset("[]", q, &err));
    CHECK(!parseGameModePreset("{\"id\":\"Bad Id\",\"gametype\":\"war\"}", q, &err) && err.find("id") != std::string::npos);
    CHECK(!parseGameModePreset("{\"id\":\"x\",\"gametype\":\"WAR!\"}", q, &err));
    CHECK(!parseGameModePreset("{\"id\":\"x\",\"gametype\":\"war\",\"dvars\":{\"a b\":\"1\"}}", q, &err));
    CHECK(!parseGameModePreset("{\"id\":\"x\",\"gametype\":\"war\",\"dvars\":{\"a\":\"1;quit\"}}", q, &err));
    CHECK(parseGameModePreset("{\"id\":\"x\",\"gametype\":\"war\",\"dvars\":{\"a\":7}}", q, &err) && q.dvars.size() == 1 && q.dvars[0].second == "7" &&
          q.title == "x");

    const fs::path dir = fs::path(iw8r::test::tmpDir()) / "game_modes_user";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    std::ofstream(dir / "gun_rush.iw8mode.json", std::ios::binary) << gameModePresetJson(p);
    GameModePreset over = p;
    over.id = "war";
    over.title = "My Team Deathmatch";
    over.gametype = "war";
    std::ofstream(dir / "war.iw8mode.json", std::ios::binary) << gameModePresetJson(over);
    std::ofstream(dir / "broken.iw8mode.json", std::ios::binary) << "{ not json";
    std::ofstream(dir / "ignored.txt", std::ios::binary) << "x";
    std::vector<std::string> notes;
    const auto modes = loadGameModes(dir.string(), &notes);
    CHECK(modes.size() == builtinGameModes().size() + 1);
    const GameModePreset* mine = findGameMode(modes, "gun_rush");
    CHECK(mine && !mine->builtin && mine->dvars.size() == 2);
    const GameModePreset* war = findGameMode(modes, "war");
    CHECK(war && war->title == "My Team Deathmatch" && !war->builtin);  // a user preset replaces the built-in one
    CHECK(notes.size() == 1 && notes[0].find("broken.iw8mode.json") != std::string::npos);
    CHECK(loadGameModes((dir / "missing").string()).size() == builtinGameModes().size());
    fs::remove_all(dir, ec);
}
