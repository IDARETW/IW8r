import io

def patch(p, pairs):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    for a, b in pairs:
        a = a.replace('\n', nl); b = b.replace('\n', nl)
        assert s.count(a) == 1, (p, s.count(a), a[:70])
        s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)

patch('src/qtapp/devmap_launch.h', [
 ('    static QString gametypeSetting();\n',
  '    static QString gametypeSetting();\n'
  '    // The dvars of the game mode preset chosen for F5 (setting devmap/modeDvars, "name=value; ..."; game_modes.h).\n'
  '    // They are sent as console lines before the map and game type when the editor starts the match itself.\n'
  '    static std::vector<std::pair<std::string, std::string>> modeDvarsSetting();\n'
  '    static void setModePreset(const QString& id, const QString& gametype, const QString& dvarsText);\n'
  '    static QString modePresetSetting();\n'),
])
s = io.open('src/qtapp/devmap_launch.h', encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
for inc in ('<string>', '<utility>', '<vector>'):
    if '#include ' + inc not in s:
        first = s.index('#include <')
        s = s[:first] + '#include ' + inc + nl + s[first:]
io.open('src/qtapp/devmap_launch.h', 'w', encoding='utf-8', newline='').write(s)

patch('src/qtapp/devmap_launch.cpp', [
 ('bool DevmapRunner::reloadSetting() { return Settings::instance().value("devmap/reload", true).toBool(); }\n',
  'std::vector<std::pair<std::string, std::string>> DevmapRunner::modeDvarsSetting() {\n'
  '    return parseModeDvars(Settings::instance().value("devmap/modeDvars").toString().toStdString());\n'
  '}\n'
  'QString DevmapRunner::modePresetSetting() { return Settings::instance().value("devmap/modePreset").toString(); }\n'
  'void DevmapRunner::setModePreset(const QString& id, const QString& gametype, const QString& dvarsText) {\n'
  '    Settings& s = Settings::instance();\n'
  '    s.setValue("devmap/modePreset", id);\n'
  '    s.setValue("devmap/modeDvars", QString::fromStdString(modeDvarsText(parseModeDvars(dvarsText.toStdString()))));\n'
  '    PlayOptions o = playOptions();\n'
  '    o.gametype = gametype;\n'
  '    setPlayOptions(o);\n'
  '}\n'
  'bool DevmapRunner::reloadSetting() { return Settings::instance().value("devmap/reload", true).toBool(); }\n'),
 ('    cfg.startRequest = startRequestSetting();\n    cfg.bots = botsSetting();\n',
  '    // A mode with dvars is started by the editor (it sends them as console lines); the client\'s own start request\n'
  '    // names only the map and the game type.\n'
  '    cfg.startRequest = startRequestSetting() && modeDvarsSetting().empty();\n    cfg.bots = botsSetting();\n'),
 ('            if (!send(devmap::startMatchCommands(c.mapId, c.gametype, snap), &err)) return;\n',
  '            if (!send(devmap::startMatchCommands(c.mapId, c.gametype, snap, modeDvarsSetting()), &err)) return;\n'),
])
s = io.open('src/qtapp/devmap_launch.cpp', encoding='utf-8', newline='').read()
if '#include "game_modes.h"' not in s:
    a = '#include "devmap_launch.h"'
    assert s.count(a) == 1
    s = s.replace(a, a + ('\r\n' if '\r\n' in s else '\n') + '#include "game_modes.h"')
    io.open('src/qtapp/devmap_launch.cpp', 'w', encoding='utf-8', newline='').write(s)

# live link quick starts send the mode dvars too
s = io.open('src/qtapp/live_link_panel.cpp', encoding='utf-8', newline='').read()
n = 0
for a, b in (
    ('devmap::startMatchCommands(mapId.toStdString(), DevmapRunner::gametypeSetting().toStdString(),', None),
):
    pass
import re
s2, n = re.subn(r'devmap::startMatchCommands\(([^;]*?), snap\)', lambda m: 'devmap::startMatchCommands(' + m.group(1) + ', snap, DevmapRunner::modeDvarsSetting())', s)
assert n >= 3, n
io.open('src/qtapp/live_link_panel.cpp', 'w', encoding='utf-8', newline='').write(s2)

patch('src/qtapp/main_window.h', [(
 '    void installEditedStockMap();\n',
 '    void installEditedStockMap();\n'
 '    // Map > Game Modes: presets (game type + dvars) for F5, baked into the map, and the Warzone start entities.\n'
 '    void showGameModes();\n'
 '    // Applies a preset to the open map (worldspawn iw8r_mode_dvars / iw8r_br, and the game type joins iw8r_gametypes\n'
 '    // when that key is set). Returns false without a worldspawn.\n'
 '    bool bakeGameMode(const struct GameModePreset& preset);\n'
 '    // Adds what the br game type needs to start on a small map: spawn points and one prematch insertion point around\n'
 '    // the 2D view centre. Returns the number of entities added.\n'
 '    int insertWarzoneStart();\n')])
print('ok')
