import io

# Second review (2026-11-15), first start / new map / file flow and the main window's own dialogs:
#  - File > New Map forgets the stock map that was open (zone, placed objects, loot, spawns, selection sets, effects);
#  - a map that fails to open leaves the open map and its state as they were (the resets run after a good load);
#  - the missing-file message mentions Recent only when the entry was there; a dropped file that is not a map says so;
#  - Recent: one entry per file (slash direction, case, relative paths), numbered names with the folder, full path
#    as the tooltip, Clear Recent;
#  - Save on an untitled map suggests untitled.map in the last folder; Save As remembers its folder; the title says
#    when a new map has never been saved;
#  - start-up says when ACTS is not set; First-Run Setup asks before saving without ACTS;
#  - Tool Paths: Browse... buttons, a found / missing note per row, the gsc-tool empty rule as it really is;
#  - Relight Stock Map and Rebuild Stock Map With These Entities only take a zone name made of a-z 0-9 _ (a typo used
#    to fail minutes into the rebuild); the rebuild offers the stock map list.

ROOT = 'E:/IW8-Radiant/'


def patch(p, a, b, count=1):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    if b in s:  # applied already (b can contain a: never apply twice)
        return
    assert s.count(a) == count, (p, s.count(a), a[:80])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


M = 'src/qtapp/main_window.cpp'
patch(M, '''    addCommand(file, "NewMap", "&New Map", QKeySequence("Ctrl+N"), [this] {
        if (!confirmDiscard()) return;
        ctx_.scene.clear();
        prefabCache_.clear();
        std::string err;''', '''    addCommand(file, "NewMap", "&New Map", QKeySequence("Ctrl+N"), [this] {
        if (!confirmDiscard()) return;
        resetForNewDocument();  // a stock map open before must not stay the target of stock commands
        prefabCache_.clear();
        std::string err;''')
patch(M, '''void MainWindow::loadDocument(const QString& path) {
    std::string err;
    const std::string p = path.toStdString();
    ctx_.scene.clear();
    stockZone_.clear();  // openStockZone sets it again after loading its dump
    stockSpawnsEditable_ = false;
    stockLoot_ = StockLoot{};
    stockLootEditable_ = false;
    stockModelNames_.clear();
    stockPlaced_.clear();
    selectionSets_.clear();  // ids belong to the previous document
    if (selectionSetsMenu_) selectionSetsMenu_->clear();
    if (path.endsWith(".json", Qt::CaseInsensitive)) {''', '''void MainWindow::resetForNewDocument() {
    // What belongs to the document that was open: the scene caches, the stock map's state, selection sets (their ids
    // are the old document's) and the stock effects the camera plays.
    ctx_.scene.clear();
    stockZone_.clear();  // openStockZone sets it again after loading its dump
    stockSpawnsEditable_ = false;
    stockLoot_ = StockLoot{};
    stockLootEditable_ = false;
    stockModelNames_.clear();
    stockPlaced_.clear();
    selectionSets_.clear();
    if (selectionSetsMenu_) selectionSetsMenu_->clear();
    if (camera_) camera_->setStockEffects({});
}

void MainWindow::loadDocument(const QString& path) {
    std::string err;
    const std::string p = path.toStdString();
    // The open map and its state are reset only after the new one has loaded: a file that fails leaves them as they were.
    if (path.endsWith(".json", Qt::CaseInsensitive)) {''')
patch(M, '''        Iw8MapEntsDump dump;
        if (!loadActsMapEnts(p, strings, dump, &err) || !ctx_.doc.loadFromText(dump.entityString, "", &err)) {
            QMessageBox::critical(this, "Open IW8 MapEnts", qs(err));
            return;
        }
        ctx_.scene.spawns = std::move(dump.spawns);''', '''        Iw8MapEntsDump dump;
        if (!loadActsMapEnts(p, strings, dump, &err) || !ctx_.doc.loadFromText(dump.entityString, "", &err)) {
            QMessageBox::critical(this, "Open IW8 MapEnts", qs(err));
            return;
        }
        resetForNewDocument();
        ctx_.scene.spawns = std::move(dump.spawns);''')
patch(M, '''        if (!ctx_.doc.load(p, &err)) {
            QMessageBox::critical(this, "Open", qs(err));
            return;
        }
        displayName_ = QFileInfo(path).fileName();''', '''        if (!ctx_.doc.load(p, &err)) {
            QMessageBox::critical(this, "Open", qs(err) + "\\n\\nThe map that was open stays open.");
            return;
        }
        resetForNewDocument();
        displayName_ = QFileInfo(path).fileName();''')
patch(M, '''        QStringList recent = Settings::instance().recentFiles();
        if (recent.removeAll(path) > 0) {
            Settings::instance().setValue("recentFiles", recent);
            rebuildRecentMenu();
        }
        QMessageBox::warning(this, "Open", path + "\\n\\nThis file no longer exists (moved or deleted). It was removed from File > Recent.");
        return;''', '''        QStringList recent = Settings::instance().recentFiles();
        const bool wasRecent = recent.removeAll(path) > 0;
        if (wasRecent) {
            Settings::instance().setValue("recentFiles", recent);
            rebuildRecentMenu();
        }
        QMessageBox::warning(this, "Open", path + "\\n\\nThis file does not exist (moved or deleted)." +
                                               (wasRecent ? QString(" It was removed from File > Recent.") : QString()));
        return;''')
patch(M, '''void MainWindow::dropEvent(QDropEvent* e) {
    const auto urls = e->mimeData()->urls();
    if (!urls.isEmpty() && urls.front().isLocalFile()) openPath(urls.front().toLocalFile());
}''', '''void MainWindow::dropEvent(QDropEvent* e) {
    const auto urls = e->mimeData()->urls();
    if (urls.isEmpty() || !urls.front().isLocalFile()) return;
    // Only what Open takes (a map, an entity string, an ACTS MapEnts dump); one file at a time.
    const QString f = urls.front().toLocalFile();
    const QString suffix = QFileInfo(f).suffix().toLower();
    if (suffix != "map" && suffix != "ents" && suffix != "json")
        return ctx_.setStatus(QFileInfo(f).fileName() + " is not a map (.map, .ents or an ACTS MapEnts .json); nothing was opened");
    if (urls.size() > 1) ctx_.setStatus(QString("Opening %1 (one map at a time: the other %2 dropped file(s) were left)").arg(QFileInfo(f).fileName()).arg(urls.size() - 1));
    openPath(f);
}''')
patch(M, '''void MainWindow::rebuildRecentMenu() {
    if (!recentMenu_) return;
    recentMenu_->clear();
    const QStringList recent = Settings::instance().recentFiles();
    recentMenu_->setEnabled(!recent.isEmpty());
    for (const QString& r : recent) {
        QAction* a = recentMenu_->addAction(r);
        connect(a, &QAction::triggered, this, [this, r] { openPath(r); });
    }
}''', '''void MainWindow::rebuildRecentMenu() {
    if (!recentMenu_) return;
    recentMenu_->clear();
    recentMenu_->setToolTipsVisible(true);
    const QStringList recent = Settings::instance().recentFiles();
    recentMenu_->setEnabled(!recent.isEmpty());
    // Numbered file names with their folder (shortened in the middle); the full path is the tooltip.
    int n = 0;
    for (const QString& r : recent) {
        const QFileInfo fi(r);
        const QString folder = recentMenu_->fontMetrics().elidedText(QDir::toNativeSeparators(fi.absolutePath()), Qt::ElideMiddle, 360);
        QString name = fi.fileName();
        name.replace('&', "&&");
        QAction* a = recentMenu_->addAction(QString("%1%2   %3").arg(++n <= 9 ? QString("&%1  ").arg(n) : QString("    "), name, folder));
        a->setToolTip(QDir::toNativeSeparators(r));
        connect(a, &QAction::triggered, this, [this, r] { openPath(r); });
    }
    if (!recent.isEmpty()) {
        recentMenu_->addSeparator();
        recentMenu_->addAction("Clear Recent", this, [this] {
            Settings::instance().setValue("recentFiles", QStringList());
            rebuildRecentMenu();
        });
    }
}''')
patch(M, '''bool MainWindow::saveTo(const QString& path, QString* err) {
    std::string e;
    if (!ctx_.doc.saveAs(path.toStdString(), &e)) {
        if (err) *err = qs(e);
        return false;
    }
    displayName_ = QFileInfo(path).fileName();''', '''bool MainWindow::saveTo(const QString& path, QString* err) {
    std::string e;
    if (!ctx_.doc.saveAs(path.toStdString(), &e)) {
        if (err) *err = qs(e);
        return false;
    }
    Settings::instance().setValue("lastDir", QFileInfo(path).absolutePath());  // the next Save As / Open starts here
    displayName_ = QFileInfo(path).fileName();''')
patch(M, '''    const QString start = ctx_.doc.path().empty() ? Settings::instance().value("lastDir").toString() : qs(ctx_.doc.path());''',
      '''    // An untitled map: untitled.map in the last folder used (Open, Save As), so Enter saves it straight away.
    QString start = qs(ctx_.doc.path());
    if (start.isEmpty()) {
        const QString dir = Settings::instance().value("lastDir").toString();
        start = (dir.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) : dir) + "/" +
                (displayName_.endsWith(".map") ? displayName_ : QString("untitled.map"));
    }''')
patch(M, '''    setWindowTitle(QString("IW8 Radiant Suite - %1%2  [MW2019 Replay 1.20.4.7623265]%3")
                       .arg(displayName_, ctx_.doc.modified() ? " *" : "", qEnvironmentVariableIsSet("IW8R_SAFE_MODE") ? "  (Safe Mode)" : ""));''',
      '''    // A new map that was never saved says so (it has no file yet, even when nothing was edited).
    const bool neverSaved = ctx_.doc.path().empty() && displayName_ == "untitled.map";
    setWindowTitle(QString("IW8 Radiant Suite - %1%2%3  [MW2019 Replay 1.20.4.7623265]%4")
                       .arg(displayName_, neverSaved ? " (not saved yet)" : "", ctx_.doc.modified() ? " *" : "",
                            qEnvironmentVariableIsSet("IW8R_SAFE_MODE") ? "  (Safe Mode)" : ""));''')
patch(M, '''void MainWindow::showFirstRunSetup(bool onlyIfReplayMissing) {
    if (onlyIfReplayMissing && QFileInfo::exists(toolPath("replay"))) return;''', '''void MainWindow::showFirstRunSetup(bool onlyIfReplayMissing) {
    if (onlyIfReplayMissing && QFileInfo::exists(toolPath("replay"))) {
        // The game is found; ACTS is the other tool most features need (stock maps, stock assets, validation).
        if (!QFileInfo::exists(toolPath("acts")))
            ctx_.setStatus("ACTS (acts.exe) is not set: stock maps, stock assets and build validation need it (Tools > Tool Paths)");
        return;
    }''')
patch(M, '''    const QList<QPair<QString, QString>> rows = {{"acts", "ACTS (acts.exe)"}, {"replay", "Replay 1.20 game_dx12_ship_replay.exe"}, {"oodle", "Oodle (oo2core_7_win64.dll)"}, {"zonetool", "x64-zt (zonetool.exe)"}, {"gsc_tool", "gsc-tool (compiles the map script; empty = none)"}, {"x64zt_output", "x64-zt output folder (empty = app data)"},''',
      '''    const QList<QPair<QString, QString>> rows = {{"acts", "ACTS (acts.exe)"}, {"replay", "Replay 1.20 game_dx12_ship_replay.exe"}, {"oodle", "Oodle (oo2core_7_win64.dll)"}, {"zonetool", "x64-zt (zonetool.exe)"}, {"gsc_tool", "gsc-tool (compiles the map script; empty = the default location)"}, {"x64zt_output", "x64-zt output folder (empty = app data)"},''')
patch(M, '''        auto* edit = new QLineEdit(toolPath(key), &dlg);
        edit->setMinimumWidth(480);
        auto* browse = new QPushButton("...", &dlg);
        const bool folder = key.endsWith("_output");''', '''        auto* edit = new QLineEdit(toolPath(key), &dlg);
        edit->setMinimumWidth(480);
        auto* browse = new QPushButton("Browse...", &dlg);
        browse->setToolTip("Locate it on disk");
        const bool folder = key.endsWith("_output");
        // Found / missing, as the field changes (a typo used to show up only in a build).
        auto* state = new QLabel(&dlg);
        state->setMinimumWidth(70);
        const auto check = [state, edit, folder] {
            const QString t = edit->text().trimmed();
            const bool ok = t.isEmpty() || (folder ? QFileInfo(t).isDir() : QFileInfo(t).isFile());
            state->setText(t.isEmpty() ? QString("default") : ok ? QString("found") : QString("not found"));
            state->setStyleSheet(t.isEmpty() ? "color: gray" : ok ? "color: #6c6" : "color: #e66");
        };
        check();
        connect(edit, &QLineEdit::textChanged, &dlg, check);''')
patch(M, '''        row->addWidget(edit, 1);
        row->addWidget(browse);
        form->addRow(label, row);
        edits.insert(key, edit);''', '''        row->addWidget(edit, 1);
        row->addWidget(browse);
        row->addWidget(state);
        form->addRow(label, row);
        edits.insert(key, edit);''')
patch(M, '''    auto* zone = new QLineEdit(stockZone().isEmpty() ? QString("mp_shipment") : stockZone(), &dlg);
    auto* start = new QDoubleSpinBox(&dlg);''', '''    auto* zone = new QLineEdit(stockZone().isEmpty() ? QString("mp_shipment") : stockZone(), &dlg);
    // A zone name only (a typo or a path used to fail minutes into the rebuild).
    zone->setValidator(new QRegularExpressionValidator(QRegularExpression("[a-z0-9_]+"), zone));
    auto* start = new QDoubleSpinBox(&dlg);''')
patch(M, '''    box->button(QDialogButtonBox::Ok)->setText("Rebuild Zone");
    connect(box, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);''', '''    box->button(QDialogButtonBox::Ok)->setText("Rebuild Zone");
    connect(zone, &QLineEdit::textChanged, box, [box, zone] { box->button(QDialogButtonBox::Ok)->setEnabled(!zone->text().isEmpty()); });
    connect(box, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);''')
patch(M, '''        bool ok = false;
        zone = QInputDialog::getText(this, "Rebuild Stock Map", "Stock zone to rebuild with the open entities:", QLineEdit::Normal,
                                     zone.startsWith("mp_") ? zone : QString("mp_shipment"), &ok).trimmed();
        if (!ok || zone.isEmpty()) return;''', '''        // The stock maps of the catalogue to pick from (a name can still be typed); a zone name only.
        QStringList ids;
        {
            QDir d(QCoreApplication::applicationDirPath());
            for (int up = 0; up < 4 && !QFileInfo::exists(d.filePath("data/asset_catalog/iw8_stock_maps.tsv")); ++up) d.cdUp();
            QFile f(d.filePath("data/asset_catalog/iw8_stock_maps.tsv"));
            if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
                QTextStream in(&f);
                while (!in.atEnd()) {
                    const QStringList c = in.readLine().split('\\t');
                    if (c.size() >= 7 && !c[0].startsWith('#') && c[0] != "map_id" && c[6] == "yes") ids << c[0];
                }
            }
        }
        const QString guess = zone.startsWith("mp_") ? zone : QString("mp_shipment");
        if (!ids.contains(guess)) ids.prepend(guess);
        bool ok = false;
        zone = QInputDialog::getItem(this, "Rebuild Stock Map", "Stock zone to rebuild with the open entities:", ids, int(ids.indexOf(guess)), true, &ok)
                   .trimmed();
        if (!ok || zone.isEmpty()) return;
        if (!QRegularExpression("^[a-z0-9_]+$").match(zone).hasMatch())
            return ctx_.setStatus("Rebuild Stock Map: \\"" + zone + "\\" is not a zone name (lower-case letters, digits and _ only)");''')

patch(M, '''#include <QStandardPaths>
''', '''#include <QStandardPaths>
#include <QRegularExpressionValidator>
''')

H = 'src/qtapp/main_window.h'
patch(H, '''    bool discardConfirmed_ = false;  // the close in progress has already asked about unsaved changes''',
      '''    bool discardConfirmed_ = false;  // the close in progress has already asked about unsaved changes
    void resetForNewDocument();      // forgets the open document's scene caches and stock map state''')

S = 'src/qtapp/settings.cpp'
patch(S, '''#include <QDir>
''', '''#include <QDir>
#include <QFileInfo>
#include <algorithm>
''')
patch(S, '''void Settings::addRecentFile(const QString& path) {
    if (volatile_) return;
    QStringList r = recentFiles();
    r.removeAll(path);
    r.prepend(path);''', '''void Settings::addRecentFile(const QString& path) {
    if (volatile_) return;
    // One entry per file: an absolute path in one spelling (E:/a/b.map and e:/A/B.map are one file on Windows).
    const auto norm = [](const QString& p) { return p.startsWith("stock:") ? p : QDir::cleanPath(QFileInfo(p).absoluteFilePath()); };
    const QString n = norm(path);
    QStringList r = recentFiles();
    r.erase(std::remove_if(r.begin(), r.end(), [&](const QString& o) { return norm(o).compare(n, Qt::CaseInsensitive) == 0; }), r.end());
    r.prepend(n);''')

W = 'src/qtapp/setup_wizard.cpp'
patch(W, '''        const auto answer = QMessageBox::question(this, "First-run setup",
                                                  "The Replay exe is not set or is missing. Save anyway?");
        if (answer != QMessageBox::Yes) return;
    }''', '''        const auto answer = QMessageBox::question(this, "First-run setup",
                                                  "The Replay exe is not set or is missing. Save anyway?");
        if (answer != QMessageBox::Yes) return;
    }
    if (!isExistingFile(actsPath().toStdString()) &&
        QMessageBox::question(this, "First-run setup",
                              "ACTS (acts.exe) is not set or is missing: stock maps, stock assets and build validation will not work "
                              "until it is (Tools > Tool Paths). Save anyway?") != QMessageBox::Yes)
        return;''')
print('ok')
