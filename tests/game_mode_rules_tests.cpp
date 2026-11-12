// Roadmap 13.6 (research 91): game mode rules carried by the map and applied by the map script at every spawn.
#include "game_modes.h"
#include "map_script.h"
#include "test_harness.h"

using namespace iw8r;

TEST(game_mode_rules_text) {
    std::vector<std::string> notes;
    ModeRules r = parseModeRules(" Health = 50 ; speed=1.25;weapon=IW8_pi_golf21_mp; ammo=infinite ;", &notes);
    CHECK(notes.empty());
    CHECK(r.health == 50 && r.speed == 1.25 && r.weapon == "iw8_pi_golf21_mp" && r.infiniteAmmo && !r.empty());
    CHECK(modeRulesText(r) == "health=50; speed=1.25; weapon=iw8_pi_golf21_mp; ammo=infinite");
    CHECK(parseModeRules(modeRulesText(r)) == r);
    CHECK(modeRulesText({}).empty() && parseModeRules("").empty());

    // Bad values and unknown rules are dropped with a note each; the good ones stay.
    notes.clear();
    r = parseModeRules("health=0; health=5000; speed=9; weapon=a b; ammo=lots; gravity=100; speed=0.5", &notes);
    CHECK(notes.size() == 6 && r.health == 0 && r.speed == 0.5 && r.weapon.empty() && !r.infiniteAmmo);
}

TEST(game_mode_rules_preset_json) {
    GameModePreset p;
    p.id = "pistols";
    p.title = "Pistols only";
    p.gametype = "war";
    p.rules.weapon = "iw8_pi_golf21_mp";
    p.rules.health = 150;
    const std::string json = gameModePresetJson(p);
    CHECK(json.find("\"rules\": \"health=150; weapon=iw8_pi_golf21_mp\"") != std::string::npos);
    GameModePreset back;
    std::string err;
    CHECK(parseGameModePreset(json, back, &err) && back.rules == p.rules);
    // A preset without rules writes no rules key (files of older editors read the same).
    GameModePreset plain = p;
    plain.rules = {};
    CHECK(gameModePresetJson(plain).find("rules") == std::string::npos);
    // A bad rule in a preset file is an error, as a bad dvar is.
    std::string bad = json;
    bad.replace(bad.find("health=150"), 10, "health=-1");
    CHECK(!parseGameModePreset(bad, back, &err) && err.find("health") != std::string::npos);
}

TEST(game_mode_rules_map_script) {
    MapScriptExtras extras;
    extras.modeRules.health = 50;
    extras.modeRules.speed = 1.2;
    extras.modeRules.weapon = "iw8_pi_golf21_mp";
    extras.modeRules.infiniteAmmo = true;
    const std::string g = mapScriptGsc(std::nullopt, {}, {}, extras);
    CHECK(g.find("    level thread iw8r_mode_rules();\n") != std::string::npos);
    CHECK(g.find("level waittill(\"connected\", player);") != std::string::npos);
    CHECK(g.find("self waittill(\"spawned_player\");\n        wait 0.05;") != std::string::npos);
    CHECK(g.find("self.maxhealth = 50;\n        self.health = 50;") != std::string::npos);
    CHECK(g.find("self setmovespeedscale(1.2);") != std::string::npos);
    CHECK(g.find("weapon = makeweapon(\"iw8_pi_golf21_mp\");\n        self takeallweapons();\n        self giveweapon(weapon);\n"
                 "        self switchtoweaponimmediate(weapon);") != std::string::npos);
    CHECK(g.find("self thread iw8r_mode_ammo();") != std::string::npos && g.find("iw8r_mode_ammo()\n{") != std::string::npos);
    CHECK(g.find("setweaponammoclip(weapon, weaponclipsize(weapon))") != std::string::npos);
    // Without rules nothing of this is generated.
    const std::string none = mapScriptGsc(std::nullopt, {}, {}, MapScriptExtras{});
    CHECK(none.find("iw8r_mode_rules") == std::string::npos);
    // Only the rules that are set.
    MapScriptExtras speedOnly;
    speedOnly.modeRules.speed = 0.8;
    const std::string s = mapScriptGsc(std::nullopt, {}, {}, speedOnly);
    CHECK(s.find("setmovespeedscale(0.8)") != std::string::npos && s.find("maxhealth") == std::string::npos &&
          s.find("makeweapon") == std::string::npos && s.find("iw8r_mode_ammo") == std::string::npos);
}
