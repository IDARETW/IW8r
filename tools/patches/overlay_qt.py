import io

def patch(p, pairs):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    for a, b in pairs:
        a = a.replace('\n', nl); b = b.replace('\n', nl)
        assert s.count(a) == 1, (p, s.count(a), a[:70])
        s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)

patch('src/core/stock_zone_edit.h', [
 ('''struct StockEditCarry {
    std::string zone, type, name;
''', '''struct StockEditCarry {
    std::string zone, type, name;
    // The folder that holds `zone` ("" = the game's zone folder): a zone the editor built, such as the brushes added
    // to a stock map (research 88).
    std::string folder;
'''),
])
patch('src/core/stock_zone_edit.cpp', [
 (r'''                    ",\"name\":" + jsonText(carry[i].name) + "}";
        json += "]";
''', r'''                    ",\"name\":" + jsonText(carry[i].name) + (carry[i].folder.empty() ? std::string() : ",\"folder\":" + jsonText(carry[i].folder)) + "}";
        json += "]";
'''),
])
patch('src/core/stock_spawns.cpp', [
 (r'''" brush(es) or patch(es) of the document are not built into a stock map; left out\n";''',
  r'''" brush(es) or patch(es) of the document are not part of the entity text (File > Rebuild Stock Zone builds the visible ones as models)\n";'''),
])
patch('tests/stock_spawns_tests.cpp', [
 ('''    // The text parses again as two entities without primitives.''', '''    CHECK(err.find("not part of the entity text") != std::string::npos);
    // The text parses again as two entities without primitives.'''),
])
patch('src/qtapp/main_window.h', [
 ('''    bool prepareStockEdit(const QString& work, QString* program, QStringList* args, QString* err, QStringList* notes = nullptr);
''', '''    // 11.4: the brushes added to a stock map are built first as models of a small helper map (the editor's own
    // headless build), then carried into the stock zone. `overlay` (optional) gets that build: the helper map and the
    // folder for its result; without it brushes are left out with a note.
    struct StockOverlayPlan {
        QString map;        // the helper map (empty: no brushes to build)
        QString resultDir;  // --out of the headless build (compile-result.txt names the package folder)
        QStringList models;
    };
    bool prepareStockEdit(const QString& work, QString* program, QStringList* args, QString* err, QStringList* notes = nullptr,
                          StockOverlayPlan* overlay = nullptr);
'''),
])
patch('src/qtapp/main_window_stock_edit.cpp', [
 ('''bool MainWindow::prepareStockEdit(const QString& work, QString* program, QStringList* args, QString* err, QStringList* notes) {
''', '''namespace {

// The placeholder for the helper map's package folder in edits.json: known only after its build.
const char* const kOverlayFolder = "@IW8R_OVERLAY_PACKAGE@";
const char* const kOverlayMapId = "mp_iw8rovl";

// A closed room with a floor, two spawns and one script_model per added model: the smallest map the build accepts,
// built only for the models it places.
std::string overlayMapText(const std::vector<std::string>& models) {
    std::string t = "iwmap 4\\n// entity 0\\n{\\n\\"classname\\" \\"worldspawn\\"\\n\\"iw8r_title\\" \\"stock map brushes\\"\\n";
    const auto box = [&](int x0, int y0, int z0, int x1, int y1, int z1) {
        const auto v = [](int a, int b, int c) { return "( " + std::to_string(a) + " " + std::to_string(b) + " " + std::to_string(c) + " )"; };
        const std::string m = " iw8r_overlay_room 64 64 0 0 0 0 lightmap_gray 16384 16384 0 0 0 0\\n";
        t += "{\\n";
        t += " " + v(x1, y1, z0) + " " + v(x0, y1, z0) + " " + v(x0, y0, z0) + m;
        t += " " + v(x0, y0, z1) + " " + v(x0, y1, z1) + " " + v(x1, y1, z1) + m;
        t += " " + v(x0, y0, z1) + " " + v(x1, y0, z1) + " " + v(x1, y0, z0) + m;
        t += " " + v(x1, y1, z1) + " " + v(x0, y1, z1) + " " + v(x0, y1, z0) + m;
        t += " " + v(x1, y0, z1) + " " + v(x1, y1, z1) + " " + v(x1, y1, z0) + m;
        t += " " + v(x0, y1, z1) + " " + v(x0, y0, z1) + " " + v(x0, y0, z0) + m;
        t += "}\\n";
    };
    box(-256, -256, -16, 256, 256, 0);
    box(-256, -256, 256, 256, 256, 272);
    box(-272, -256, 0, -256, 256, 256);
    box(256, -256, 0, 272, 256, 256);
    box(-256, -272, 0, 256, -256, 256);
    box(-256, 256, 0, 256, 272, 256);
    t += "}\\n";
    t += "{\\n\\"classname\\" \\"mp_tdm_spawn\\"\\n\\"origin\\" \\"-128 0 0\\"\\n}\\n";
    t += "{\\n\\"classname\\" \\"mp_dm_spawn\\"\\n\\"origin\\" \\"128 0 0\\"\\n}\\n";
    for (const std::string& m : models)
        t += "{\\n\\"classname\\" \\"script_model\\"\\n\\"model\\" \\"" + m + "\\"\\n\\"origin\\" \\"0 0 64\\"\\n}\\n";
    return t;
}

}  // namespace

bool MainWindow::prepareStockEdit(const QString& work, QString* program, QStringList* args, QString* err, QStringList* notes,
                                  StockOverlayPlan* overlay) {
'''),
 ('''    QDir().mkpath(work);
    const QString entsPath = work + "/edit.ents";
''', '''    // 11.4: brushes added to the stock map become models of a helper map (built first), carried into the zone and
    // placed by script_model entities at their place.
    std::vector<StockEditCarry> overlayCarry;
    {
        std::vector<std::string> overlayNotes;
        const std::vector<StockOverlayModel> models = stockOverlayModels(ctx_.doc.map(), 4, &overlayNotes);
        if (notes)
            for (const std::string& n : overlayNotes) *notes << qs(n);
        if (!models.empty() && !overlay) {
            if (notes) *notes << QString("%1 group(s) of added brushes are left out: only File > Rebuild Stock Zone builds them").arg(models.size());
        } else if (!models.empty()) {
            const QString dir = work + "/overlay";
            QDir().mkpath(dir + "/models");
            std::vector<std::string> names;
            std::string placed;
            for (const StockOverlayModel& m : models) {
                const QByteArray hash = QCryptographicHash::hash(QByteArray::fromStdString(stockZone_.toStdString() + "|" + m.obj),
                                                                 QCryptographicHash::Sha1).toHex().left(12);
                const std::string name = "iw8r_ovl_" + hash.toStdString();
                QFile of(dir + "/models/" + qs(name) + ".obj");
                if (!of.open(QIODevice::WriteOnly) || of.write(m.obj.data(), qint64(m.obj.size())) != qint64(m.obj.size())) {
                    if (err) *err = "Cannot write " + of.fileName();
                    return false;
                }
                names.push_back(name);
                overlayCarry.push_back({kOverlayMapId, "xmodel", name, kOverlayFolder});
                // The game's own form of the entity text (numeric keys: classname 212, model 652, origin 709).
                placed += "{\\n212 \\"script_model\\"\\n652 \\"" + name + "\\"\\n709 \\"" + formatNumber(m.centre.x) + " " + formatNumber(m.centre.y) + " " +
                          formatNumber(m.centre.z) + "\\"\\n}\\n";
                if (notes)
                    *notes << QString("added brushes (entity %1): %2 triangles, %3 material(s), built as model %4")
                                  .arg(m.entity).arg(m.triangles).arg(m.materials.size()).arg(qs(name));
            }
            const std::string mapText = overlayMapText(names);
            QFile mf(dir + "/" + kOverlayMapId + ".map");
            if (!mf.open(QIODevice::WriteOnly) || mf.write(mapText.data(), qint64(mapText.size())) != qint64(mapText.size())) {
                if (err) *err = "Cannot write " + mf.fileName();
                return false;
            }
            mf.close();
            if (!ents.empty() && ents.back() != '\\n') ents += "\\n";
            ents += placed;
            overlay->map = mf.fileName();
            overlay->resultDir = dir + "/result";
            for (const std::string& n : names) overlay->models << qs(n);
        }
    }
    QDir().mkpath(work);
    const QString entsPath = work + "/edit.ents";
'''),
 ('''    const std::vector<StockEditCarry> carry =
        catalogue.size() ? stockEditCarryList(ctx_.doc.map(), stockZone_.toStdString(), catalogue, &carryNotes, &stockModelNames_)
                         : std::vector<StockEditCarry>{};
''', '''    std::vector<StockEditCarry> carry =
        catalogue.size() ? stockEditCarryList(ctx_.doc.map(), stockZone_.toStdString(), catalogue, &carryNotes, &stockModelNames_)
                         : std::vector<StockEditCarry>{};
    // The added brushes' models are not in the catalogue: they come from the helper map.
    carry.insert(carry.end(), overlayCarry.begin(), overlayCarry.end());
'''),
 ('''    QString program, err;
    QStringList args, notes;
    if (!prepareStockEdit(work, &program, &args, &err, &notes)) {
        QMessageBox::warning(this, "Rebuild Stock Zone", err);
        return;
    }
    for (const QString& n : notes) EditorLog::instance().add(LogLevel::Info, "stock", n);
    const QString zone = stockZone_;
    auto* proc = new QProcess(this);
''', '''    QString program, err;
    QStringList args, notes;
    StockOverlayPlan overlay;
    if (!prepareStockEdit(work, &program, &args, &err, &notes, &overlay)) {
        QMessageBox::warning(this, "Rebuild Stock Zone", err);
        return;
    }
    for (const QString& n : notes) EditorLog::instance().add(LogLevel::Info, "stock", n);
    if (!overlay.map.isEmpty()) {
        // 11.4: the helper map with the added brushes is built first with the editor's own headless build (stock
        // textures, models, collision), then the stock rebuild carries its models from that package.
        auto* build = new QProcess(this);
        build->setProgram(QCoreApplication::applicationFilePath());
        build->setArguments({"--compile", overlay.map, "--out", overlay.resultDir});
        build->setProcessChannelMode(QProcess::MergedChannels);
        connect(build, &QProcess::finished, this, [this, build, overlay, work, program, args](int code) {
            build->deleteLater();
            QFile rf(overlay.resultDir + "/compile-result.txt");
            const QString result = rf.open(QIODevice::ReadOnly) ? QString::fromUtf8(rf.readAll()).trimmed() : QString();
            const int at = result.indexOf("package=");
            if (code != 0 || !result.startsWith("ok") || at < 0) {
                ctx_.setStatus("Rebuild Stock Zone: the build of the added brushes failed; see " + overlay.resultDir + "/compile-log.txt");
                return;
            }
            const QString package = QDir::fromNativeSeparators(result.mid(at + 8).trimmed());
            QFile jf(work + "/edits.json");
            QByteArray json = jf.open(QIODevice::ReadOnly) ? jf.readAll() : QByteArray();
            jf.close();
            json.replace(kOverlayFolder, package.toUtf8());
            if (!jf.open(QIODevice::WriteOnly | QIODevice::Truncate) || jf.write(json) != json.size()) {
                ctx_.setStatus("Rebuild Stock Zone: cannot write " + jf.fileName());
                return;
            }
            jf.close();
            EditorLog::instance().add(LogLevel::Info, "stock", QString("the added brushes are built (%1 model(s)): %2").arg(overlay.models.size()).arg(package));
            runStockEdit(work, program, args);
        });
        build->start();
        ctx_.setStatus(QString("Rebuilding %1: building the added brushes first (%2 model(s), a minute or two)").arg(stockZone_).arg(overlay.models.size()));
        return;
    }
    runStockEdit(work, program, args);
}

void MainWindow::runStockEdit(const QString& work, const QString& program, const QStringList& args) {
    const QString zone = stockZone_;
    auto* proc = new QProcess(this);
'''),
 ('#include <QDir>\n', '#include <QCryptographicHash>\n#include <QDir>\n'),
 ('#include "spawnlist.h"\n', '#include "compile_iw8.h"\n#include "spawnlist.h"\n'),
])
patch('src/qtapp/main_window.h', [
 ('''    void rebuildStockZoneFast();
''', '''    void rebuildStockZoneFast();
    void runStockEdit(const QString& work, const QString& program, const QStringList& args);  // the editzone step
'''),
])
print('ok')
