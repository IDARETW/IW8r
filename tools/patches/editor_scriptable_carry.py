import io

def load(p):
    return io.open(p, encoding='utf-8', newline='').read()
def save(p, s):
    io.open(p, 'w', encoding='utf-8', newline='').write(s)
def rep(s, a, b, count=1):
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == count, (s.count(a), a[:80])
    return s.replace(a, b)

# ---------------- catalogue
p = 'src/core/scriptable_catalog.h'
s = load(p)
s = rep(s, '''    // What a map build accepts today: the definition is always loaded and every number is known.
    bool placeable() const { return alwaysLoaded && verified; }
};
''', '''    // What a map build accepts without carrying anything: the definition is always loaded and every number is known.
    bool placeable() const { return alwaysLoaded && verified; }
    // The stock map zone the converter takes the definition from when the map carries it ("" when it is always
    // loaded or no map zone holds it): the first "mp_" zone of the list.
    std::string carryZone() const;
    // Buildable when the map may carry stock definitions (CompileOptions::carryScriptables).
    bool carryable() const { return verified && !alwaysLoaded && !carryZone().empty(); }
    // A stock door (usable, sparse occluder): the stock door script must be started for it.
    bool door() const { return sparseOccluder && usableParts > 0; }
};
''')
s = rep(s, '''    std::string targetname, target, linkname, linkto, noteworthy;
};

// The JSON object of one placement (no trailing comma).
''', '''    std::string targetname, target, linkname, linkto, noteworthy;
    std::string carryZone;  // "" = the definition is always loaded (a name reference)
};

// The JSON object of one placement (no trailing comma).
''')
save(p, s)

p = 'src/core/scriptable_catalog.cpp'
s = load(p)
s = rep(s, '''const ScriptableCatalog& ScriptableCatalog::shared(const std::string& startDir) {
''', '''std::string ScriptableDefinition::carryZone() const {
    if (alwaysLoaded) return {};
    for (size_t at = 0; at < zones.size();) {
        const size_t comma = zones.find(',', at);
        const std::string zone = zones.substr(at, comma == std::string::npos ? std::string::npos : comma - at);
        if (zone.rfind("mp_", 0) == 0) return zone;
        if (comma == std::string::npos) break;
        at = comma + 1;
    }
    return {};
}

const ScriptableCatalog& ScriptableCatalog::shared(const std::string& startDir) {
''')
s = rep(s, '''    text("script_noteworthy", p.noteworthy);
''', '''    text("script_noteworthy", p.noteworthy);
    text("carry_zone", p.carryZone);
''')
save(p, s)

# ---------------- map script
p = 'src/core/map_script.h'
s = load(p)
s = rep(s, '''struct MapScriptExtras {
    std::vector<std::string> solidTargetnames;
''', '''struct MapScriptExtras {
    // A stock door scriptable is placed (6.14, research 83 / 86): main() starts the stock door script, as stock map
    // scripts and scripts/mp/load do. The converter lets the map script call it.
    bool stockDoors = false;
    std::vector<std::string> solidTargetnames;
''')
save(p, s)
p = 'src/core/map_script.cpp'
s = load(p)
s = rep(s, '''                    "entities).\\nmain()\\n{\\n    maps\\\\mp\\\\_load::main();\\n";
''', '''                    "entities).\\nmain()\\n{\\n    maps\\\\mp\\\\_load::main();\\n";
    if (extras.stockDoors) s += "    scripts\\\\engine\\\\scriptable_door::system_init();\\n";
''')
save(p, s)

# ---------------- compile
p = 'src/core/compile_iw8.h'
s = load(p)
s = rep(s, '''    const ScriptableCatalog* scriptables = nullptr;
''', '''    const ScriptableCatalog* scriptables = nullptr;
    // Carry stock definitions (doors, cones, cars: definitions that live in stock map zones): the instance names the
    // stock zone and the converter copies the definition with everything it uses into the map. Off: such an entity
    // is left out with a warning. Built like the stock map's own data, not seen in game.
    bool carryScriptables = false;
''')
save(p, s)

p = 'src/core/compile_iw8.cpp'
s = load(p)
s = rep(s, '''    std::string scriptablesJson;
    size_t scriptableCount = 0;
    {
''', '''    std::string scriptablesJson;
    size_t scriptableCount = 0;
    bool stockDoorPlaced = false;
    {
        std::map<std::string, size_t> carried;  // stock zone -> instances
''')
s = rep(s, '''            if (!def->alwaysLoaded) {
                ++leftOut["'" + def->name + "' lives only in stock map zones (" + def->zones + "); a map cannot carry a definition yet"];
                continue;
            }
            if (!def->verified) {
                ++leftOut["'" + def->name + "' is always loaded, but no stock map shows all the numbers a build must write for it"];
                continue;
            }
''', '''            if (!def->verified) {
                ++leftOut["'" + def->name + "': no stock map shows all the numbers a build must write for it"];
                continue;
            }
            std::string carryZone;
            if (!def->alwaysLoaded) {
                carryZone = def->carryZone();
                if (carryZone.empty()) {
                    ++leftOut["'" + def->name + "' is in no always-loaded zone and no stock map zone (" + def->zones + ")"];
                    continue;
                }
                if (!opt.carryScriptables) {
                    ++leftOut["'" + def->name + "' lives only in stock map zones (" + def->zones +
                              "); turn on Edit > Preferences > Real stock scripted objects to carry it into the map"];
                    continue;
                }
            }
''')
s = rep(s, '''            place.linkto = text("script_linkto");
            scriptablesJson += std::string(scriptableCount ? "," : "") + scriptableInstanceJson(place);
            ++scriptableCount;
        }
''', '''            place.linkto = text("script_linkto");
            place.carryZone = carryZone;
            if (!carryZone.empty()) ++carried[carryZone];
            stockDoorPlaced |= def->door();
            scriptablesJson += std::string(scriptableCount ? "," : "") + scriptableInstanceJson(place);
            ++scriptableCount;
        }
        for (const auto& [zone, n] : carried)
            warn.push_back(std::to_string(n) + " scriptable instance(s) carry their definition from the stock zone " + zone +
                           " (the converter copies the definition, its models, collision and materials; needs the game "
                           "folder; not seen in game)");
''')
s = rep(s, '''        MapScriptExtras extras;
        if (worldspawn)
''', '''        MapScriptExtras extras;
        extras.stockDoors = stockDoorPlaced;
        if (worldspawn)
''')
save(p, s)

# ---------------- qtapp: setting -> options and prefab features
p = 'src/qtapp/compile_panel.cpp'
s = load(p)
s = rep(s, '''        opt.scriptables = &ScriptableCatalog::shared(QCoreApplication::applicationDirPath().toStdString());
''', '''        opt.scriptables = &ScriptableCatalog::shared(QCoreApplication::applicationDirPath().toStdString());
        opt.carryScriptables = Settings::instance().value("build/realScriptables", false).toBool();
''')
save(p, s)

p = 'src/qtapp/prefab_browser.cpp'
s = load(p)
s = rep(s, '''PrefabBuildSupport PrefabBrowser::buildSupport() {
    PrefabBuildSupport s;
    s.stock = stockCatalog();
    return s;
}
''', '''PrefabBuildSupport PrefabBrowser::buildSupport() {
    PrefabBuildSupport s;
    s.stock = stockCatalog();
    // Edit > Preferences > Real stock scripted objects: the build writes scriptable instances, carries their stock
    // definitions and starts the stock door script, so a bundle that asks for them builds its real contents.
    if (Settings::instance().value("build/realScriptables", false).toBool()) {
        s.features.insert(kPrefabFeatureScriptableInstances);
        s.features.insert(kPrefabFeatureScriptableDefCarry);
        s.features.insert(kPrefabFeatureStockScriptCalls);
    }
    return s;
}
''')
if 'settings.h' not in s:
    import re
    m = re.search(r'#include "prefab_browser\.h"', s)
    assert m
    s = s.replace(m.group(0), m.group(0) + ('\r\n' if '\r\n' in s else '\n') + '#include "settings.h"', 1)
save(p, s)

# ---------------- Preferences
p = 'src/qtapp/main_window.cpp'
s = load(p)
s = rep(s, '''    form->addRow("F5: game mode", playMode);
''', '''    auto* realScriptables = new QCheckBox("Real stock scripted objects: carry stock doors, cones and cars into the map (experimental)", &dlg);
    realScriptables->setObjectName("prefsRealScriptables");
    realScriptables->setChecked(Settings::instance().value("build/realScriptables", false).toBool());
    realScriptables->setToolTip("On: a scriptable entity whose definition lives in a stock map (a door, a traffic cone, a car) is built by copying "
                                "the stock definition with its models, collision and materials into your map, and the stock door prefab builds "
                                "the game's real door. Built exactly like the stock map's own data, but not seen in game yet. "
                                "Off: such objects are left out and the door prefab builds its scripted stand-in.");
    form->addRow("Build", realScriptables);
    form->addRow("F5: game mode", playMode);
''')
s = rep(s, '''    applyPreferences(p);
    DevmapRunner::PlayOptions po;
''', '''    applyPreferences(p);
    Settings::instance().setValue("build/realScriptables", realScriptables->isChecked());
    DevmapRunner::PlayOptions po;
''')
save(p, s)

# ---------------- Check Map wording
p = 'src/qtapp/find_dialog.cpp'
s = load(p)
s = rep(s, '''                else if (!def->alwaysLoaded)
                    why = QString("names '%1', which lives only in stock map zones (%2); a map cannot carry a definition yet")
                              .arg(qs(def->name), qs(def->zones));
                else if (!def->verified)
                    why = QString("names '%1', whose build numbers are not all known").arg(qs(def->name));
''', '''                else if (!def->verified)
                    why = QString("names '%1', whose build numbers are not all known").arg(qs(def->name));
                else if (!def->alwaysLoaded && !def->carryable())
                    why = QString("names '%1', which no stock map zone holds (%2)").arg(qs(def->name), qs(def->zones));
                else if (!def->alwaysLoaded && !Settings::instance().value("build/realScriptables", false).toBool())
                    why = QString("names '%1', which lives only in stock map zones (%2); turn on Edit > Preferences > Real stock "
                                  "scripted objects to carry it into the map")
                              .arg(qs(def->name), qs(def->zones));
''')
if '"settings.h"' not in s:
    s = rep(s, '#include "scriptable_catalog.h"', '#include "scriptable_catalog.h"\n#include "settings.h"')
save(p, s)

# ---------------- CLI flag
p = 'src/cli/compile_cmd.cpp'
s = load(p)
s = rep(s, '''    opt.scriptables = &iw8r::ScriptableCatalog::shared(std::filesystem::current_path().string());
''', '''    opt.scriptables = &iw8r::ScriptableCatalog::shared(std::filesystem::current_path().string());
    opt.carryScriptables = std::getenv("IW8R_CARRY_SCRIPTABLES") != nullptr;  // carry stock definitions into the map
''')
save(p, s)

# ---------------- entity class description
for p in ('data/iw8_entity_defs_overrides.json',):
    s = load(p)
    a = "Today a map can place the definitions the game always has loaded and whose build numbers are known: hardpoint_chevron (the Hardpoint zone marker). Doors, traffic cones and cars live in stock map zones and cannot be carried into a map yet; they are left out of the build with a warning."
    assert s.count(a) == 1
    s = s.replace(a, "A definition the game always has loaded is placed by name: hardpoint_chevron (the Hardpoint zone marker). Doors, traffic cones, cars and other world objects live in stock map zones: with Edit > Preferences > Real stock scripted objects turned on the build copies the stock definition with its models, collision and materials into your map; with it off they are left out with a warning.")
    a = '"scriptable": key("string"'
    save(p, s)
print('ok')
