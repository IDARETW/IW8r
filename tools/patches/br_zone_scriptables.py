import io

def patch(p, pairs):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    for a, b in pairs:
        a = a.replace('\n', nl); b = b.replace('\n', nl)
        assert s.count(a) == 1, (p, s.count(a), a[:70])
        s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)

patch('src/core/scriptable_catalog.cpp', [
 ('''const ScriptableCatalog& ScriptableCatalog::shared(const std::string& startDir) {
''', '''bool ScriptableDefinition::brZone() const {
    for (size_t at = 0; at < zones.size();) {
        const size_t comma = zones.find(',', at);
        if (zones.substr(at, comma == std::string::npos ? std::string::npos : comma - at) == "common_br_mp") return true;
        if (comma == std::string::npos) break;
        at = comma + 1;
    }
    return false;
}

const ScriptableCatalog& ScriptableCatalog::shared(const std::string& startDir) {
'''),
])
patch('src/core/compile_iw8.cpp', [
 ('''    {
        std::map<std::string, size_t> carried;  // stock zone -> instances
        std::map<std::string, size_t> leftOut;  // reason -> count
''', '''    {
        // A map made for the Warzone game type (worldspawn iw8r_br, or the prematch area the br script needs): the
        // game loads the battle royale zone (common_br_mp) for such a match, so its definitions (buy station, loot
        // caches) are placed by name. Whether it does so for an own map is not seen in game (research 89).
        bool brMap = worldspawn && worldspawn->value("iw8r_br");
        for (const Entity* pe : points)
            if (const std::string* tn = pe->value("targetname"); tn && *tn == "br_prematch_insertion_point") brMap = true;
        size_t brPlaced = 0;
        std::map<std::string, size_t> carried;  // stock zone -> instances
        std::map<std::string, size_t> leftOut;  // reason -> count
'''),
 ('''            std::string carryZone;
            if (!def->alwaysLoaded) {
                carryZone = def->carryZone();
''', '''            std::string carryZone;
            const bool fromBrZone = !def->alwaysLoaded && def->brZone();
            if (fromBrZone && !brMap) {
                ++leftOut["'" + def->name + "' is in the Warzone zone (common_br_mp), which only a Warzone match loads; make the map a "
                          "Warzone map first (Tools > Game Modes > Carry in this map, Insert Warzone start entities)"];
                continue;
            }
            if (fromBrZone) ++brPlaced;
            if (!def->alwaysLoaded && !fromBrZone) {
                carryZone = def->carryZone();
'''),
 ('''        for (const auto& [reason, n] : leftOut)
            warn.push_back(std::to_string(n) + " scriptable entit" + (n == 1 ? "y" : "ies") + " left out: " + reason);
''', '''        if (brPlaced)
            warn.push_back(std::to_string(brPlaced) + " scriptable instance(s) name a definition of the Warzone zone (common_br_mp): they exist "
                           "only in a Warzone (br) match; whether the game loads that zone for an own map is not seen in game");
        for (const auto& [reason, n] : leftOut)
            warn.push_back(std::to_string(n) + " scriptable entit" + (n == 1 ? "y" : "ies") + " left out: " + reason);
'''),
])
print('ok')
