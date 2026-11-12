import io


def patch(p, pairs):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    for a, b in pairs:
        a = a.replace('\n', nl); b = b.replace('\n', nl)
        assert s.count(a) == 1, (p, s.count(a), a[:70])
        s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


patch('src/core/compile_iw8.cpp', [
 ('''            if (fromBrZone) ++brPlaced;
            if (!def->alwaysLoaded && !fromBrZone) {
                carryZone = def->carryZone();''', '''            if (fromBrZone) ++brPlaced;
            if (fromBrZone) {
                // Research 93 (read from the game exe): the extra zone of a map is chosen by the map's name
                // (mp/levelZoneParents.csv), not by the game type, so a custom map gets common_core_mp and never
                // common_br_mp, even in a br match. The definition is carried from common_br_mp instead.
                carryZone = "common_br_mp";
                if (!opt.carryScriptables) {
                    ++leftOut["'" + def->name + "' lives in the Warzone zone (common_br_mp), which a custom map does not load; turn on "
                              "Edit > Preferences > Real stock scripted objects to carry it into the map"];
                    continue;
                }
            }
            if (!def->alwaysLoaded && !fromBrZone) {
                carryZone = def->carryZone();'''),
 ('''            warn.push_back(std::to_string(brPlaced) + " scriptable instance(s) name a definition of the Warzone zone (common_br_mp): they exist "''',
  '''            warn.push_back(std::to_string(brPlaced) + " scriptable instance(s) use a definition of the Warzone zone (common_br_mp), carried into the map "
                           "(a custom map does not load that zone, research 93); "''' + '\n' + '''                           "'''),
])
print('ok')
