import io, json
def edit(p, pairs):
    s = io.open(p, encoding='utf-8', newline='').read()
    crlf = '\r\n' in s
    s = s.replace('\r\n', '\n')
    for a, b in pairs:
        assert s.count(a) == 1, (p, s.count(a), a[:70])
        s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s.replace('\n', '\r\n') if crlf else s)

edit('tests/map_gametypes_tests.cpp', [
(r'''MapFile parse(const std::string& body) {
    return parseMapText("iwmap 4\n" + body);
}''', r'''MapFile parse(const std::string& body) {
    Id next = 1;
    ParseResult pr = parseMap("iwmap 4\n" + body, next);
    CHECK(pr.map.has_value());
    return pr.map ? *pr.map : MapFile{};
}'''),
])

edit('src/qtapp/compile_panel.h', [
(r'''    QByteArray metadataJson() const;  // what --metadata receives (id always set)''',
 r'''    QByteArray metadataJson() const;  // what --metadata receives (id always set)
    // Writes the map's game types into the built package's map.json ("gameTypes": "war dm"; core map_gametypes). The
    // converter writes map.json from fixed fields, so the editor adds this line after the zones stage. Returns the
    // list, or empty when there is no package yet.
    QString writePackageGameTypes(bool log = true);'''),
])

edit('src/qtapp/compile_panel.cpp', [
(r'''namespace {
const char* kInfoKeys[] = {"iw8r_title", "iw8r_description", "iw8r_compass_rotation", "iw8r_minimap_scale",
                           "iw8r_ladder_rung_distance", "iw8r_exposure_adjust"};
}  // namespace
''',
 r'''namespace {
const char* kInfoKeys[] = {"iw8r_title", "iw8r_description", "iw8r_compass_rotation", "iw8r_minimap_scale",
                           "iw8r_ladder_rung_distance", "iw8r_exposure_adjust", "iw8r_gametypes"};
}  // namespace

QString CompilePanel::writePackageGameTypes(bool log) {
    const QString path = zonesDir() + "/map.json";
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    const std::string before = f.readAll().toStdString();
    f.close();
    const MapGameTypes types = mapGameTypes(ctx_->doc.map());
    const std::string after = withGameTypes(before, types.list);
    if (after != before) {
        QFile out(path);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate) || out.write(after.data(), qint64(after.size())) < 0) {
            append("game types: cannot write " + path, true);
            return {};
        }
    }
    if (log)
        for (const std::string& n : types.notes) append("game types: " + QString::fromStdString(n));
    return QString::fromStdString(types.list);
}
'''),
(r'''        if (!pendingKey_.isEmpty()) {
            // Zones built and validated in this run: later runs with the same extraction can reuse them.''',
 r'''        // The package's map.json names the game types the map has spawns and objects for (the client lists a custom
        // map with Team Deathmatch only otherwise). Written on every finished run, also when the zones were reused.
        writePackageGameTypes();
        if (!pendingKey_.isEmpty()) {
            // Zones built and validated in this run: later runs with the same extraction can reuse them.'''),
(r'''    form->addRow("Exposure adjust (EV)", exposure);
    auto* buttons = new QDialogButtonBox(''',
 r'''    form->addRow("Exposure adjust (EV)", exposure);
    // Game types: blank = what the map has spawns and objects for (shown as the placeholder).
    auto* gametypes = new QLineEdit(value("iw8r_gametypes"), &dlg);
    gametypes->setObjectName("mapInfoGameTypes");
    {
        Id scratch = 1;
        MapFile probe = ctx_->doc.map();
        for (Entity& e : probe.entities)
            if (e.isWorldspawn()) e.removeKey("iw8r_gametypes");
        (void)scratch;
        gametypes->setPlaceholderText("detected: " + QString::fromStdString(mapGameTypes(probe).list));
    }
    gametypes->setToolTip("The game modes the game offers for this map (ui_gametype names with spaces: war dm dom sd ctf ...).\n"
                          "Leave blank to list the modes the map has spawns and objects for. Needs a client that reads map.json gameTypes.");
    form->addRow("Game modes", gametypes);
    auto* buttons = new QDialogButtonBox('''),
(r'''                              num(minimap), num(ladder), num(exposure)};''',
 r'''                              num(minimap), num(ladder), num(exposure), gametypes->text().trimmed()};'''),
(r'''    for (int i = 0; i < 6; ++i) {
        const std::string* cur = ctx_->doc.findEntity(wid)->value(kInfoKeys[i]);''',
 r'''    for (int i = 0; i < 7; ++i) {
        const std::string* cur = ctx_->doc.findEntity(wid)->value(kInfoKeys[i]);'''),
])
s = io.open('src/qtapp/compile_panel.cpp', encoding='utf-8', newline='').read()
if '#include "map_gametypes.h"' not in s:
    a = '#include "compile_panel.h"'
    assert s.count(a) == 1
    nl = '\r\n' if '\r\n' in s else '\n'
    s = s.replace(a, a + nl + nl + '#include "map_gametypes.h"', 1)
    io.open('src/qtapp/compile_panel.cpp', 'w', encoding='utf-8', newline='').write(s)

p = 'data/iw8_entity_defs_overrides.json'
raw = io.open(p, encoding='utf-8', newline='').read()
crlf = '\r\n' in raw
j = json.loads(raw)
j['classes']['worldspawn']['keys']['iw8r_gametypes'] = {
    "type": "string", "default": "",
    "description": "The game modes the game offers for this map, as ui_gametype names with spaces between them (war dm dom sd ctf ...). Left out, the editor lists the modes the map has spawns and objects for. Written to map.json as gameTypes; needs a client that reads it.",
    "required": False, "examples": ["war dm", "war dom sd"]}
out = json.dumps(j, indent=2, ensure_ascii=False) + '\n'
io.open(p, 'w', encoding='utf-8', newline='').write(out.replace('\n', '\r\n') if crlf else out)
print('ok')
