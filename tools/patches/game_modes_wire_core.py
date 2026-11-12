import io

def patch(p, pairs):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    for a, b in pairs:
        a = a.replace('\n', nl); b = b.replace('\n', nl)
        assert s.count(a) == 1, (p, s.count(a), a[:70])
        s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)

# ---- match start: the mode's dvars are set first
patch('src/core/devmap_plan.h', [(
 'std::vector<std::string> startMatchCommands(const std::string& mapId, const std::string& gametype, const GameSnapshot& s);\n',
 'std::vector<std::string> startMatchCommands(const std::string& mapId, const std::string& gametype, const GameSnapshot& s);\n'
 '// The same with the dvars of a game mode preset set first ("set <name> <value>", game_modes.h): a private match does\n'
 '// not apply playlist dvars, so a Warzone variant has to bring its own (research 87, 4.4). Pairs that are not a plain\n'
 '// name and value are left out.\n'
 'std::vector<std::string> startMatchCommands(const std::string& mapId, const std::string& gametype, const GameSnapshot& s,\n'
 '                                            const std::vector<std::pair<std::string, std::string>>& modeDvars);\n')])
patch('src/core/devmap_plan.cpp', [(
 'std::string backToLobbyCommand() { return "xpartybackout"; }\n',
 'std::vector<std::string> startMatchCommands(const std::string& mapId, const std::string& gametype, const GameSnapshot& s,\n'
 '                                            const std::vector<std::pair<std::string, std::string>>& modeDvars) {\n'
 '    std::vector<std::string> out;\n'
 '    for (const auto& [name, value] : modeDvars)\n'
 '        if (validModeDvarName(name) && validModeDvarValue(value)) out.push_back("set " + name + " " + value);\n'
 '    for (std::string& line : startMatchCommands(mapId, gametype, s)) out.push_back(std::move(line));\n'
 '    return out;\n'
 '}\n\n'
 'std::string backToLobbyCommand() { return "xpartybackout"; }\n')])
s = io.open('src/core/devmap_plan.cpp', encoding='utf-8', newline='').read()
if '#include "game_modes.h"' not in s:
    nl = '\r\n' if '\r\n' in s else '\n'
    a = '#include "devmap_plan.h"'
    assert s.count(a) == 1
    s = s.replace(a, a + nl + '#include "game_modes.h"')
    io.open('src/core/devmap_plan.cpp', 'w', encoding='utf-8', newline='').write(s)

# ---- map script: baked mode dvars and the br switches
patch('src/core/map_script.h', [(
 '    bool stockDoors = false;\n',
 '    bool stockDoors = false;\n'
 '    // Game mode settings carried by the map (worldspawn iw8r_mode_dvars, game_modes.h): set at the start of main(), as\n'
 '    // the stock Warzone tutorial map sets its own; and what a small map switches off in the br game type (worldspawn\n'
 '    // iw8r_br: nocircle, nogulag, skipdrop; research 87 section 6).\n'
 '    std::vector<std::pair<std::string, std::string>> modeDvars;\n'
 '    bool brNoCircle = false, brNoGulag = false, brSkipDrop = false;\n')])
patch('src/core/map_script.cpp', [(
 '    if (extras.stockDoors) s += "    scripts\\\\engine\\\\scriptable_door::system_init();\\n";\n',
 '    if (extras.stockDoors) s += "    scripts\\\\engine\\\\scriptable_door::system_init();\\n";\n'
 '    for (const auto& [name, value] : extras.modeDvars)\n'
 '        if (validModeDvarName(name) && validModeDvarValue(value)) s += "    setdvar(\\"" + name + "\\", \\"" + value + "\\");\\n";\n'
 '    if (extras.brNoCircle) s += "    level.br_circle_disabled = 1;\\n    scripts\\\\mp\\\\gametypes\\\\br_gametypes::disablefeature(\\"circle\\");\\n";\n'
 '    if (extras.brNoGulag) s += "    level.usegulag = 0;\\n";\n'
 '    if (extras.brSkipDrop) s += "    level.skipprematchdropspawn = 1;\\n";\n')])
s = io.open('src/core/map_script.cpp', encoding='utf-8', newline='').read()
if '#include "game_modes.h"' not in s:
    nl = '\r\n' if '\r\n' in s else '\n'
    a = '#include "map_script.h"'
    assert s.count(a) == 1
    s = s.replace(a, a + nl + '#include "game_modes.h"')
    io.open('src/core/map_script.cpp', 'w', encoding='utf-8', newline='').write(s)

patch('src/core/compile_iw8.cpp', [(
 '        extras.stockDoors = stockDoorPlaced;\n',
 '        extras.stockDoors = stockDoorPlaced;\n'
 '        if (worldspawn) {\n'
 '            // Game mode settings the map carries (game_modes.h).\n'
 '            if (const std::string* dv = worldspawn->value("iw8r_mode_dvars")) {\n'
 '                std::vector<std::string> notes;\n'
 '                extras.modeDvars = parseModeDvars(*dv, &notes);\n'
 '                for (const std::string& n : notes) warn.push_back("iw8r_mode_dvars: " + n);\n'
 '                if (!extras.modeDvars.empty())\n'
 '                    warn.push_back(std::to_string(extras.modeDvars.size()) + " game mode dvar(s) are set by the map script at start (" +\n'
 '                                   modeDvarsText(extras.modeDvars) + ")");\n'
 '            }\n'
 '            if (const std::string* br = worldspawn->value("iw8r_br")) {\n'
 '                parseModeBr(*br, extras.brNoCircle, extras.brNoGulag, extras.brSkipDrop);\n'
 '                if (extras.brNoCircle || extras.brNoGulag || extras.brSkipDrop)\n'
 '                    warn.push_back(std::string("Warzone (br) switches set by the map script:") + (extras.brNoCircle ? " no circle" : "") +\n'
 '                                   (extras.brNoGulag ? " no gulag" : "") + (extras.brSkipDrop ? " no drop from the plane" : "") +\n'
 '                                   " (research 87; not seen in game)");\n'
 '            }\n'
 '        }\n')])
s = io.open('src/core/compile_iw8.cpp', encoding='utf-8', newline='').read()
if '#include "game_modes.h"' not in s:
    nl = '\r\n' if '\r\n' in s else '\n'
    a = '#include "compile_iw8.h"'
    assert s.count(a) == 1
    s = s.replace(a, a + nl + '#include "game_modes.h"')
    io.open('src/core/compile_iw8.cpp', 'w', encoding='utf-8', newline='').write(s)

# ---- game type detection: br
patch('src/core/map_gametypes.cpp', [(
 '    for (GameTypeCheck& g : out) g.supported = g.missing.empty();\n    return out;\n',
 '    {\n'
 '        // Warzone (research 87, section 6): the br script takes its starting spawns from mp_dm_spawn_start and its\n'
 '        // prematch areas from script_struct entities named br_prematch_insertion_point. Circle, gulag and plane data\n'
 '        // are switched off by the map (worldspawn iw8r_br) or come from a game mode preset.\n'
 '        GameTypeCheck& g = add("br", "Warzone (battle royale)");\n'
 '        needClass(g, c, "mp_dm_spawn_start", 1);\n'
 '        needName(g, c, "br_prematch_insertion_point");\n'
 '    }\n'
 '    for (GameTypeCheck& g : out) g.supported = g.missing.empty();\n    return out;\n')])
patch('src/core/map_gametypes.h', [(
 '// One entry per detected game type, in menu order (war, dm, dom, sd, ctf).\n',
 '// One entry per detected game type, in menu order (war, dm, dom, sd, ctf, br).\n')])
print('ok')
