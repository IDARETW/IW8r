import io

p = 'docs/KEYS_REFERENCE.md'
s = io.open(p, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'


def rep(a, b):
    global s
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == 1, (s.count(a), a[:80])
    s = s.replace(a, b)


rep('''| `iw8r_sun_sprite` |''', '''| `iw8r_script` | GSC file, relative to the map folder | `mp_mymap_logic.gsc` | The map's own script. It must define `user_main()`, which runs after the load script. Edited in the Script dock. |
| `iw8r_mode_dvars` | `name=value; name=value` | `scr_br_teamsize=3; loot_table_name=mp/loot_item_defs_s3.csv` | Game mode settings the map's script sets at every start (Tools > Game Modes > Carry in this map). `loot_table_name` also picks the loot items a Warzone map carries. Not seen in game. |
| `iw8r_mode_rules` | `health=<1..1000>; speed=<0.2..3>; weapon=<name>; ammo=infinite` | `health=50; weapon=iw8_pi_golf21_mp` | Rules applied to every player at each spawn by the map's script, which runs after the game type's start (research 91). Not seen in game. |
| `iw8r_gametype_script` | GSC file beside the map; its file name is the game type name | `gametypes/teamkills.gsc` | A game type of the map's own (research 91 / 92). The build checks it and compiles it into the map zone; `map.json` lists the name. Not seen in game. |
| `iw8r_gametypes` | game type names, spaces between them | `war dm dom` | The game types `map.json` lists, instead of the detected ones. |
| `iw8r_br` | words `nocircle`, `nogulag`, `skipdrop` | `nocircle nogulag skipdrop` | What a small map switches off in the Warzone (br) game type; also marks the map as a Warzone map. |
| `iw8r_loot_zone` | lower-case name (default `map_quarry`) | `tutorial_scavenger` | The loot zone name of the map's loot points when no `iw8r_loot_zone` brush entity names one. The loot tables pick the loot by it. |
| `iw8r_no_loot` | 0 or 1 | `1` | 1 writes an empty loot list (all loot of a stock Warzone map is removed on Rebuild Stock Zone). |
| `iw8r_collision` | `auto`, `one` (or `full`) or `tiles` | `tiles` | How the build keeps the map's collision: in one piece or as the game's streamed tiles (research 90). `auto`: tiles for a map wider than 20,000 units. |
| `iw8r_sun_sprite` |''')

rep('''| `iw8r_mat:<from>` | material name |''', '''| `iw8r_pid` | placement id, set by the editor | `p7` | The placement id of a bundle placement: names local to the prefab become `<pid>_<name>` in the build, so two placements never share one. |
| `angles` | `pitch yaw roll` | `0 90 0` | Turns the placement. A wall bundle gets its yaw from the wall it is placed on; File > Rotate Selected Prefab adds the bundle's yaw step. |
| `iw8r_mat:<from>` | material name |''')

rep('''## Custom material definition (`.iw8mat.json`)''', '''## Warzone loot entities

| Class / key | Value format | Example | What it does |
|---|---|---|---|
| `iw8r_loot_point` `loot_cluster` | `free`, `cache`, a cluster number, or empty (auto) | `cache` | A loot point. `cache` makes a supply box point. On a stock map, a number keeps the point in that cluster while it stays inside its box. |
| `iw8r_loot_point` `loot_zone` | a zone number, or empty (auto) | `3` | The loot zone of a stock map's point (the first zone box that holds it when left empty). |
| `iw8r_loot_cluster` (brush entity) | its brushes give the box | | A new loot cluster: the points inside it join it (stock maps and own maps). |
| `iw8r_loot_zone` (brush entity) `name` | lower-case zone name | `map_quarry` | A new named loot zone; use a name the loot tables know. |

## Custom material definition (`.iw8mat.json`)''')

io.open(p, 'w', encoding='utf-8', newline='').write(s)
print('ok')
