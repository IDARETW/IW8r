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
 ('''#include "stock_loot.h"
''', '''#include "gametype_script.h"
#include "stock_loot.h"
'''),
 ('''    const BrLootItemTables* lootItems = nullptr;''', '''    const BrLootItemTables* lootItems = nullptr;
    // Stock script index (gametype_script.h): checks the far calls of a map's game type script (worldspawn
    // iw8r_gametype_script). Without it such a script is left out.
    const StockScriptIndex* stockScripts = nullptr;'''),
])

patch('src/core/compile_iw8.cpp', [
 ('''                if (!extras.userScript.empty()) warn.push_back("map script includes " + p.filename().string() + " (user_main runs after the load script)");
            }''', '''                if (!extras.userScript.empty()) warn.push_back("map script includes " + p.filename().string() + " (user_main runs after the load script)");
            }
            // 13.6 (research 91 / 92): a new game type script the map carries (worldspawn iw8r_gametype_script, relative
            // to the map folder). Checked here; the converter compiles it into the map zone as it is.
            if (const std::string* gp = worldspawn->value(kGameTypeScriptKey); gp && !gp->empty()) {
                fs::path p(*gp);
                if (p.is_relative() && !opt.mapDir.empty()) p = fs::path(opt.mapDir) / p;
                const std::string name = gameTypeNameOfScript(p.filename().string());
                std::string text;
                if (!readFile(p.string(), text)) {
                    warn.push_back(std::string(kGameTypeScriptKey) + " '" + *gp + "' cannot be read; no game type script is built");
                } else if (!opt.stockScripts) {
                    warn.push_back("game type script " + p.filename().string() + " left out: the stock script index "
                                   "(data/generated/iw8_stock_scripts.tsv) was not found, so its calls cannot be checked");
                } else {
                    const std::vector<std::string> problems = checkGameTypeScript(text, name, *opt.stockScripts);
                    if (!problems.empty()) {
                        warn.push_back("game type script " + p.filename().string() + " left out (" + std::to_string(problems.size()) +
                                       " problem(s); a match with it would end at load):");
                        for (const std::string& pr : problems) warn.push_back("  " + pr);
                    } else {
                        customFiles.push_back({root / "extracted" / "scripts" / "mp" / "gametypes" / (name + ".gsc"), text});
                        warn.push_back("game type '" + name + "' built into the map (scripts/mp/gametypes/" + name +
                                       ".gsc; start a match with ui_gametype " + name + "; research 91 / 92; not seen in game)");
                    }
                }
            }'''),
])

patch('src/core/map_gametypes.cpp', [
 ('''MapGameTypes mapGameTypes(const MapFile& map) {
    MapGameTypes r;''', '''namespace {
// 13.6: the game type of the map's own game type script (worldspawn iw8r_gametype_script) joins the list.
void addOwnGameType(const Entity* world, MapGameTypes& r) {
    const std::string* gp = world ? world->value(kGameTypeScriptKey) : nullptr;
    const std::string name = gp ? gameTypeNameOfScript(*gp) : std::string();
    if (name.empty()) return;
    if ((" " + r.list + " ").find(" " + name + " ") != std::string::npos) return;
    r.list += (r.list.empty() ? "" : " ") + name;
    r.notes.push_back("game type '" + name + "' of the map's own game type script is listed");
}
}  // namespace

MapGameTypes mapGameTypes(const MapFile& map) {
    MapGameTypes r;'''),
 ('''                                      (g.missing.size() > 1 ? " and " + std::to_string(g.missing.size() - 1) + " more" : ""));
            return r;''', '''                                      (g.missing.size() > 1 ? " and " + std::to_string(g.missing.size() - 1) + " more" : ""));
            addOwnGameType(world, r);
            return r;'''),
])
s = io.open('src/core/map_gametypes.cpp', encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
tail = '''                              (g.missing.size() > 1 ? " and " + std::to_string(g.missing.size() - 1) + " more" : ""));
    }
    return r;'''.replace('\n', nl)
assert s.count(tail) == 1, s.count(tail)
s = s.replace(tail, tail[:-len('    return r;')] + '    addOwnGameType(world, r);' + nl + '    return r;')
if '#include "gametype_script.h"' not in s:
    s = s.replace('#include "map_gametypes.h"' + nl, '#include "map_gametypes.h"' + nl + nl + '#include "gametype_script.h"' + nl, 1)
io.open('src/core/map_gametypes.cpp', 'w', encoding='utf-8', newline='').write(s)

patch('src/qtapp/compile_panel.cpp', [
 ('''        static BrLootItemTables lootItemTables;''', '''        static BrLootItemTables lootItemTables;
        static StockScriptIndex stockScriptIndex;'''),
 ('''                    lootItemTables.load(d.filePath("data/asset_catalog/iw8_br_loot_items.tsv").toStdString());''',
  '''                    lootItemTables.load(d.filePath("data/asset_catalog/iw8_br_loot_items.tsv").toStdString());
                    stockScriptIndex.load(d.filePath("data/generated/iw8_stock_scripts.tsv").toStdString());'''),
 ('''        if (lootItemTables.size()) opt.lootItems = &lootItemTables;''', '''        if (lootItemTables.size()) opt.lootItems = &lootItemTables;
        if (stockScriptIndex.size()) opt.stockScripts = &stockScriptIndex;'''),
])

patch('src/qtapp/main_window_game_modes.cpp', [
 ('''    auto* start = new QPushButton("Insert Warzone start entities", &dlg);''', '''    auto* start = new QPushButton("Insert Warzone start entities", &dlg);
    auto* newType = new QPushButton("New game type script...", &dlg);
    newType->setToolTip("A game type of your own under a new name, carried in the map (research 91 / 92): writes a "
                        "template beside the map and points worldspawn iw8r_gametype_script at it");'''),
 ('''    for (QPushButton* b : {useF5, bake, start, save, del}) buttons->addWidget(b);''',
  '''    for (QPushButton* b : {useF5, bake, start, newType, save, del}) buttons->addWidget(b);'''),
 ('''    connect(start, &QPushButton::clicked, &dlg, [this] { insertWarzoneStart(); });''', '''    connect(start, &QPushButton::clicked, &dlg, [this] { insertWarzoneStart(); });
    connect(newType, &QPushButton::clicked, &dlg, [this] {
        bool ok = false;
        const QString name = QInputDialog::getText(this, "New Game Type Script", "Game type name (1 to 11 of a-z, 0-9, _; not a stock one):",
                                                   QLineEdit::Normal, "teamkills", &ok).trimmed().toLower();
        if (!ok || name.isEmpty()) return;
        QString err;
        const QString file = newGameTypeScript(name, &err);
        if (file.isEmpty()) return void(QMessageBox::warning(this, "New Game Type Script", err));
        ctx_.setStatus("Game type script " + file + ": edit it, build, then start a match with game type " + name +
                       " (Use for F5 with that game type). The build checks its calls.");
    });'''),
 ('''int MainWindow::insertWarzoneStart() {''', '''QString MainWindow::newGameTypeScript(const QString& name, QString* err) {
    const std::string n = gameTypeNameOfScript(name.toStdString() + ".gsc");
    if (n.empty() || n != name.toStdString()) {
        if (err) *err = "The name must be 1 to 11 characters of a-z, 0-9 and _.";
        return {};
    }
    if (ctx_.doc.path().empty()) {
        if (err) *err = "Save the map first: the script is written beside it.";
        return {};
    }
    const QString dir = QFileInfo(qs(ctx_.doc.path())).absolutePath();
    const QString rel = "gametypes/" + name + ".gsc";
    const QString path = dir + "/" + rel;
    if (!QFileInfo::exists(path)) {
        QDir().mkpath(dir + "/gametypes");
        QFile f(path);
        const std::string text = gameTypeScriptTemplate(n);
        if (!f.open(QIODevice::WriteOnly) || f.write(text.data(), qint64(text.size())) != qint64(text.size())) {
            if (err) *err = "Cannot write " + path;
            return {};
        }
    }
    for (const Entity& e : ctx_.doc.map().entities)
        if (e.isWorldspawn()) {
            ctx_.doc.setKeyValue(e.id, kGameTypeScriptKey, rel.toStdString());
            ctx_.documentEdited("Game type script " + rel + " is carried by the map (worldspawn " + QString(kGameTypeScriptKey) + ")");
            return path;
        }
    if (err) *err = "The open document has no worldspawn.";
    return {};
}

int MainWindow::insertWarzoneStart() {'''),
])

patch('src/qtapp/main_window.h', [
 ('''    int insertWarzoneStart();''', '''    int insertWarzoneStart();
    // 13.6: writes the game type template <map folder>/gametypes/<name>.gsc (kept when it exists) and points worldspawn
    // iw8r_gametype_script at it. Returns the file, or "" with *err.
    QString newGameTypeScript(const QString& name, QString* err = nullptr);'''),
])
print('ok')
