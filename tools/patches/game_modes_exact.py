import io

def patch(p, pairs):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    for a, b in pairs:
        a = a.replace('\n', nl); b = b.replace('\n', nl)
        assert s.count(a) == 1, (p, s.count(a), a[:70])
        s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)

patch('src/core/game_modes.cpp', [
 ('''        // Research 87, 4.3: the private server's playlist catalogue. All four are the script "br".
        make("br_trios", "Warzone: Battle Royale Trios", "br",
             {{"loot_table_name", "mp/loot_item_defs_s3.csv"}, {"scr_br_teamsize", "3"}, {"scriptable_force_large_mode", "1"}},
             "The Warzone battle royale in teams of three, as the Trios playlist sets it. Meant for the stock Warzone maps."),
        make("br_quads", "Warzone: Battle Royale Quads", "br",
             {{"loot_table_name", "mp/loot_item_defs_s3.csv"}, {"scr_br_teamsize", "4"}, {"scriptable_force_large_mode", "1"}},
             "Teams of four."),
        make("br_solos_stim", "Warzone: Stimulus Solos", "br",
             {{"loot_table_name", "mp/loot_item_defs_cash.csv"}, {"scr_br_teamsize", "1"}, {"scriptable_force_large_mode", "1"}},
             "Solo battle royale with the cash loot table (buy back with money)."),
        make("blood_money", "Warzone: Blood Money (Plunder)", "br",
             {{"scr_br_gametype", "dmz"}, {"br_kiosk_items_filename", "mp/brKioskPurchases_DMZ.csv"}, {"scr_br_timelimit", "1800"},
              {"ui_blood_money", "1"}, {"scr_br_teamsize", "3"}},
             "The cash-collecting variant: the br script with its dmz sub-mode."),
''',
  '''        // Research 87, 4.3: the dvars of the private server's playlist catalogue (data/playlists/5800/catalog.json of
        // the owner's server project), entry for entry. All four are the script "br". RKMMNSQKO is the name the br
        // scripts read the loot table from; loot_table_name is the engine's.
        make("br_trios", "Warzone: Battle Royale Trios", "br",
             {{"OKTSLRSQMS", "-1"}, {"RKMMNSQKO", "mp/loot_item_defs_s3.csv"},
              {"br_mission_reward_groups_filename", "mp/brmission_reward_groups.csv"}, {"loot_table_name", "mp/loot_item_defs_s3.csv"},
              {"scr_br_teamsize", "3"}, {"scriptable_force_large_mode", "1"}, {"sv_forceNetfieldLoD", "-1"}},
             "The Warzone battle royale in teams of three, as the Trios playlist sets it. Meant for the stock Warzone maps."),
        make("br_quads", "Warzone: Battle Royale Quads", "br",
             {{"OKTSLRSQMS", "-1"}, {"RKMMNSQKO", "mp/loot_item_defs_s3.csv"},
              {"br_mission_reward_groups_filename", "mp/brmission_reward_groups.csv"}, {"loot_table_name", "mp/loot_item_defs_s3.csv"},
              {"scr_br_teamsize", "4"}, {"scriptable_force_large_mode", "1"}, {"sv_forceNetfieldLoD", "-1"}},
             "Teams of four."),
        make("br_solos_stim", "Warzone: Stimulus Solos", "br",
             {{"OKTSLRSQMS", "-1"}, {"RKMMNSQKO", "mp/loot_item_defs_cash.csv"},
              {"br_mission_reward_groups_filename", "mp/brmission_reward_groups.csv"}, {"loot_table_name", "mp/loot_item_defs_cash.csv"},
              {"scr_br_teamsize", "1"}, {"scriptable_force_large_mode", "1"}, {"sv_forceNetfieldLoD", "-1"}},
             "Solo battle royale with the cash loot table (buy back with money)."),
        make("blood_money", "Warzone: Blood Money (Plunder)", "br",
             {{"OKTSLRSQMS", "-1"}, {"br_kiosk_items_filename", "mp/brKioskPurchases_DMZ.csv"},
              {"br_mission_reward_filename", "mp/brmission_rewards_dmz.csv"},
              {"br_mission_reward_groups_filename", "mp/brmission_reward_groups_dmz.csv"}, {"scr_br_gametype", "dmz"},
              {"scr_br_teamsize", "3"}, {"scr_br_timelimit", "1800"}, {"scr_dmz_lc_active", "1"}, {"sv_forceNetfieldLoD", "-1"},
              {"ui_blood_money", "1"}},
             "The cash-collecting variant: the br script with its dmz sub-mode."),
'''),
])
patch('tests/game_modes_tests.cpp', [
 ('CHECK(trios && trios->gametype == "br" && trios->dvars.size() == 3 && !trios->brNoCircle);\n'
  '    if (trios) CHECK(modeDvarsText(trios->dvars) == "loot_table_name=mp/loot_item_defs_s3.csv; scr_br_teamsize=3; scriptable_force_large_mode=1");',
  'CHECK(trios && trios->gametype == "br" && trios->dvars.size() == 7 && !trios->brNoCircle);\n'
  '    // Entry for entry the Trios playlist of the private server\'s catalogue (both names of the loot table).\n'
  '    if (trios)\n'
  '        CHECK(modeDvarsText(trios->dvars) ==\n'
  '              "OKTSLRSQMS=-1; RKMMNSQKO=mp/loot_item_defs_s3.csv; br_mission_reward_groups_filename=mp/brmission_reward_groups.csv; "\n'
  '              "loot_table_name=mp/loot_item_defs_s3.csv; scr_br_teamsize=3; scriptable_force_large_mode=1; sv_forceNetfieldLoD=-1");\n'
  '    const GameModePreset* money = findGameMode(modes, "blood_money");\n'
  '    CHECK(money && money->dvars.size() == 10);'),
])
print('ok')
