import io, json

p = 'data/iw8_entity_defs_overrides.json'
d = json.loads(io.open(p, encoding='utf-8').read())
ws = d['classes']['worldspawn']['keys']


def key(name, typ, default, desc, examples):
    assert name not in ws, name
    ws[name] = {'type': typ, 'default': default, 'description': desc, 'required': False, 'examples': examples}


key('iw8r_script', 'string', '', "The map's own GSC file (relative to the map folder). It must define user_main(), which runs "
    "after the load script. Edited in the Script dock.", ['mp_mymap_logic.gsc'])
key('iw8r_mode_dvars', 'string', '', "Game mode settings the map's script sets at every start, 'name=value; name=value' "
    "(Tools > Game Modes > Carry in this map). Not seen in game.", ['scr_br_teamsize=3; loot_table_name=mp/loot_item_defs_s3.csv'])
key('iw8r_mode_rules', 'string', '', "Game mode rules applied to every player at each spawn by the map's script, which runs "
    "after the game type's own start (research 91): health=<1..1000>; speed=<0.2..3>; weapon=<name>; ammo=infinite. "
    "Not seen in game.", ['health=50; speed=1.2; weapon=iw8_pi_golf21_mp; ammo=infinite'])
key('iw8r_gametype_script', 'string', '', "A game type of the map's own (research 91 / 92): a GSC file beside the map whose file "
    "name is the game type name. The build checks it (a new name, main(), calls only into stock scripts stored by "
    "number) and compiles it into the map zone; map.json lists the name. Tools > Game Modes > New game type script "
    "writes a template. Not seen in game.", ['gametypes/teamkills.gsc'])
key('iw8r_br', 'string', '', "What a small map switches off in the Warzone (br) game type: nocircle, nogulag, skipdrop "
    "(Tools > Game Modes). Also marks the map as a Warzone map for scriptables and vehicles.", ['nocircle nogulag skipdrop'])
key('iw8r_loot_zone', 'string', 'map_quarry', "The loot zone name of the map's loot points when no iw8r_loot_zone brush "
    "entity names one; the loot tables pick the loot by it (research 87).", ['map_quarry', 'tutorial_scavenger'])
key('iw8r_no_loot', 'int', '0', "1 writes an empty loot list (removes all loot points of a stock Warzone map on Rebuild "
    "Stock Zone).", ['1'])
key('iw8r_collision', 'string', 'auto', "How the build keeps the map's collision: auto (streamed tiles for a map wider than "
    "20000 units), one (one piece; also full) or tiles (the game's streamed collision tiles, research 90). Overrides Edit > "
    "Preferences > Build: collision.", ['auto', 'one', 'tiles'])
io.open(p, 'w', encoding='utf-8', newline='\n').write(json.dumps(d, indent=2, ensure_ascii=False) + '\n')
print('ok')
