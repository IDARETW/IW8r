// View > Game Mode: which entities a game mode keeps (core game_mode_view, research 40).
#include "game_mode_view.h"
#include "test_harness.h"

using namespace iw8r;

TEST(game_mode_view_gameobject_tokens) {
    const GameModeView* dom = findGameModeView("dom");
    const GameModeView* sd = findGameModeView("sd");
    const GameModeView* koth = findGameModeView("koth");
    const GameModeView* hq = findGameModeView("hq");
    const GameModeView* war = findGameModeView("war");
    CHECK(dom && sd && koth && hq && war);
    CHECK(!findGameModeView("") && !findGameModeView("all"));
    const std::string flag = "dom onslaught", bomb = "bombzone", hardpoint = "hardpoint", radio = "hqloc", ctf = "ctf";
    // Domination's flags stay in dom only; "onslaught" is not a mode shown here.
    CHECK(gameModeKeepsEntity(*dom, "trigger_radius", &flag));
    CHECK(!gameModeKeepsEntity(*sd, "trigger_radius", &flag));
    CHECK(!gameModeKeepsEntity(*war, "trigger_radius", &flag));
    // Bomb sites in sd (and its crates, a script_model), not in dom.
    CHECK(gameModeKeepsEntity(*sd, "script_model", &bomb));
    CHECK(gameModeKeepsEntity(*sd, "trigger_use_touch", &bomb));
    CHECK(!gameModeKeepsEntity(*dom, "script_brushmodel", &bomb));
    // Hardpoint zones in koth and hq; the hq radios in hq only (research 40 section 7).
    CHECK(gameModeKeepsEntity(*koth, "trigger_multiple", &hardpoint));
    CHECK(gameModeKeepsEntity(*hq, "trigger_multiple", &hardpoint));
    CHECK(!gameModeKeepsEntity(*koth, "script_model", &radio));
    CHECK(gameModeKeepsEntity(*hq, "script_model", &radio));
    CHECK(!gameModeKeepsEntity(*war, "script_model", &ctf));
    // No gameobject name: every mode keeps it.
    CHECK(gameModeKeepsEntity(*war, "script_model", nullptr));
    const std::string empty;
    CHECK(gameModeKeepsEntity(*war, "script_brushmodel", &empty));
    // A leading '!' inverts the test.
    const std::string notDom = "!dom";
    CHECK(!gameModeKeepsEntity(*dom, "script_model", &notDom));
    CHECK(gameModeKeepsEntity(*sd, "script_model", &notDom));
}

TEST(game_mode_view_structs_and_spawns) {
    const GameModeView* dom = findGameModeView("dom");
    const GameModeView* war = findGameModeView("war");
    const GameModeView* sd = findGameModeView("sd");
    CHECK(dom && war && sd);
    // Structs: by substring of the gametype (load.gsc filterstructs).
    const std::string many = "dom sd";
    CHECK(gameModeKeepsEntity(*dom, "script_struct", &many));
    CHECK(gameModeKeepsEntity(*sd, "script_struct", &many));
    CHECK(!gameModeKeepsEntity(*war, "script_struct", &many));
    // Spawns by class: mp_<token>_spawn*.
    CHECK(gameModeKeepsSpawn(*war, "mp_tdm_spawn_allies_start"));
    CHECK(!gameModeKeepsSpawn(*war, "mp_dom_spawn"));
    CHECK(gameModeKeepsSpawn(*dom, "mp_dom_spawn_secondary"));
    CHECK(gameModeKeepsSpawn(*sd, "mp_sd_spawn_attacker"));
    CHECK(!gameModeKeepsSpawn(*sd, "mp_tdm_spawn"));
    CHECK(gameModeKeepsSpawn(*sd, "mp_global_intermission"));
    CHECK(!gameModeKeepsEntity(*sd, "mp_dom_spawn", nullptr));
    CHECK(gameModeKeepsEntity(*sd, "info_player_start", nullptr));
}
