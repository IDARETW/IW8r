import io

def patch(p, pairs):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    for a, b in pairs:
        a = a.replace('\n', nl); b = b.replace('\n', nl)
        assert s.count(a) == 1, (p, s.count(a), a[:70])
        s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)

patch('src/core/compile_iw8.h', [
 ('#include "scriptable_catalog.h"\n', '#include "collision_layout.h"\n#include "scriptable_catalog.h"\n'),
 ('''    const ScriptableCatalog* scriptables = nullptr;
''', '''    const ScriptableCatalog* scriptables = nullptr;
    // How the collision is laid out (collision_layout.h): Auto cuts a map wider than 20000 units into the game's
    // streamed collision tiles and keeps a smaller one in one piece. Worldspawn iw8r_collision overrides this.
    CollisionLayout collisionLayout = CollisionLayout::Auto;
'''),
])
patch('src/core/compile_iw8.cpp', [
 ('''           (lootCount ? ",\\"loot_points\\":[" + lootJson + "]" : std::string()) + "}";
''', '''           (lootCount ? ",\\"loot_points\\":[" + lootJson + "]" : std::string()) + (collisionTiles ? ",\\"collision_tiles\\":true" : "") + "}";
'''),
 ('''    std::string col = "{\\"schema\\":1,\\"name\\":" + quote(mapName) + ",\\"submodel_count\\":" + std::to_string(brushModels.size() + 1) +
''', '''    // The collision layout (14.1, research 90): one piece, or the game's streamed tiles for a big map. The converter
    // cuts the tiles itself; the editor only asks for them.
    bool collisionTiles = false;
    {
        CollisionLayout wanted = opt.collisionLayout;
        if (worldspawn)
            if (const std::string* key = worldspawn->value("iw8r_collision"); key && !key->empty()) {
                bool known = true;
                const CollisionLayout fromKey = parseCollisionLayout(*key, &known);
                if (known) wanted = fromKey;
                else warn.push_back("worldspawn iw8r_collision '" + *key + "' not understood (auto, one, tiles); using " + collisionLayoutName(wanted));
            }
        const CollisionLayoutChoice choice = chooseCollisionLayout(wanted, bounds);
        collisionTiles = choice.tiles;
        for (const std::string& n : choice.notes) warn.push_back(n);
    }
    std::string col = "{\\"schema\\":1,\\"name\\":" + quote(mapName) + ",\\"submodel_count\\":" + std::to_string(brushModels.size() + 1) +
'''),
])
patch('src/qtapp/compile_panel.cpp', [
 ('''        opt.carryScriptables = PrefabBrowser::realScriptables();
''', '''        opt.carryScriptables = PrefabBrowser::realScriptables();
        // 14.1: the collision layout for maps that do not say it themselves (worldspawn iw8r_collision).
        opt.collisionLayout = parseCollisionLayout(Settings::instance().value("build/collisionLayout", "auto").toString().toStdString());
'''),
 ('''    // Optional: the map-select preview the converter makes from the loadscreen image (1.17).
''', '''    // A map built with streamed collision (14.1) also has its 1,024 collision tile zones and the combined package:
    // they belong to the package, all of them.
    if (QFileInfo(zonesDir() + "/" + qs(collisionPackageZoneName(id.toStdString())) + ".ff").isFile()) {
        for (const std::string& zone : collisionTileZoneNames(id.toStdString())) {
            const QString f = zonesDir() + "/" + qs(zone) + ".ff";
            if (!QFileInfo(f).isFile()) return {};
            out << f;
        }
    }
    // Optional: the map-select preview the converter makes from the loadscreen image (1.17).
'''),
 ('''        for (const QFileInfo& fi : zones.entryInfoList({"*.ff"}, QDir::Files, QDir::Name)) ffs << fi.absoluteFilePath();
''', '''        // The 1,024 collision tile zones of a tiled map are not tested one by one: the combined package holds the
        // same tiles and is tested.
        static const QRegularExpression tileZone("_cg_\\\\d{5}_tr\\\\.ff$");
        for (const QFileInfo& fi : zones.entryInfoList({"*.ff"}, QDir::Files, QDir::Name))
            if (!tileZone.match(fi.fileName()).hasMatch()) ffs << fi.absoluteFilePath();
'''),
 ('''            if (code != 0) return fail(QString("%1 exited with %2").arg(procTitle_).arg(code));
''', '''            if (code != 0 && procTitle_.startsWith("ACTS --test ")) {
                // The loader test cannot check the streaming info of a map with streamed collision (it fails the same
                // way on the stock mp_br_quarry and mp_donetsk): that one failure does not fail the build.
                const QString zone = QFileInfo(procTitle_.mid(12)).completeBaseName();
                QString journal;
                QDirIterator it(buildRoot() + "/validate/" + zone, {"assets.jsonl"}, QDir::Files, QDirIterator::Subdirectories);
                if (it.hasNext()) {
                    QFile f(it.next());
                    if (f.open(QIODevice::ReadOnly)) journal = QString::fromUtf8(f.readAll());
                }
                if (actsOnlyStreamingInfoFailed(journal.toStdString())) {
                    append(procTitle_ + ": every asset passes except the streaming info, which the loader test cannot check on a map "
                                        "with streamed collision (the same on the stock big maps)");
                    return next();
                }
            }
            if (code != 0) return fail(QString("%1 exited with %2").arg(procTitle_).arg(code));
'''),
])
print('ok')
