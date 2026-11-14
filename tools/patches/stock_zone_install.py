import io

# Edited stock maps install as whole unsigned zones (research 116): editor wiring and the edits file.

ROOT = 'E:/IW8-Radiant/'


def read(p):
    return io.open(ROOT + p, encoding='utf-8', newline='').read()


def write(p, s):
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s)


def patch(p, a, b, n=1):
    s = read(p)
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    write(p, s.replace(a, b))


def between(p, start, end, new):
    s = read(p)
    nl = '\r\n' if '\r\n' in s else '\n'
    assert s.count(start) == 1 and s.count(end) == 1, (p, s.count(start), s.count(end))
    i, j = s.index(start), s.index(end)
    assert i < j
    write(p, s[:i] + new.replace('\n', nl) + s[j:])


# --- core: the edits file -------------------------------------------------------------------------------------------
H = 'src/core/stock_zone_edit.h'
patch(H, '''// `stockPatches` (stock_install.h): the saved stock patches of the edits that are installed in the game; the
// converter then reads those zones as the stock game has them, not as the installed edit left them.
''', '''// `stockPatches` (stock_install.h): the saved stock patches of the edits that are installed in the game; the
// converter then reads those zones as the stock game has them, not as the installed edit left them.
// `stockZones`: the same for edits installed as whole zones (editzone "stock_zones": zone file name -> the kept stock
// file). `patchFiles` false: the converter writes no <zone>.fp ("patch": false); the game does not load them on its
// signed zones (research 116).
''')
patch(H, '''                          const std::string* removeWorld = nullptr, const std::string* litFilter = nullptr);
''', '''                          const std::string* removeWorld = nullptr, const std::string* litFilter = nullptr,
                          const std::vector<StockPatchOverride>* stockZones = nullptr, bool patchFiles = true);
''')
C = 'src/core/stock_zone_edit.cpp'
patch(C, '''                          const std::string* removeWorld, const std::string* litFilter) {
    std::string json = "{";''', '''                          const std::string* removeWorld, const std::string* litFilter,
                          const std::vector<StockPatchOverride>* stockZones, bool patchFiles) {
    std::string json = "{";''')
patch(C, '''            json += std::string(i ? "," : "") + jsonText((*stockPatches)[i].name) + ":" + jsonText((*stockPatches)[i].stockFile);
        json += "}";
    }
    return json + "}";
''', '''            json += std::string(i ? "," : "") + jsonText((*stockPatches)[i].name) + ":" + jsonText((*stockPatches)[i].stockFile);
        json += "}";
    }
    if (stockZones && !stockZones->empty()) {
        sep();
        json += "\\"stock_zones\\":{";
        for (size_t i = 0; i < stockZones->size(); ++i)
            json += std::string(i ? "," : "") + jsonText((*stockZones)[i].name) + ":" + jsonText((*stockZones)[i].stockFile);
        json += "}";
    }
    if (!patchFiles) {
        sep();
        json += "\\"patch\\":false";
    }
    return json + "}";
''')

# --- editor ---------------------------------------------------------------------------------------------------------
MH = 'src/qtapp/main_window.h'
patch(MH, '''    QString stockBackupDir(const QString& zone) const;
''', '''    QString stockBackupDir(const QString& zone) const;
    // Where the game's stock zone files wait while an edit is installed in their place: beside the game's zone folder.
    QString stockKeepDir() const;
    // Installs the edited zones of a folder (a rebuild's out folder) for the map `zone`; asks first when `ask`.
    bool installEditedStockZones(const QString& outDir, const QString& zone, bool ask);
''')

E = 'src/qtapp/main_window_stock_edit.cpp'
patch(E, '''    const std::vector<StockPatchOverride> stockPatches = installedStockPatchOverrides(
        (QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/stock_backup").toStdString());
    if (notes && !stockPatches.empty()) {
        QStringList names;
        for (const StockPatchOverride& o : stockPatches) names << qs(o.name);
        *notes << QString("installed edit(s) in the game: %1. This rebuild starts from the stock map (the stock patch files saved at install)")
                      .arg(names.join(", "));
    }
''', '''    const std::string backupRoot = (QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/stock_backup").toStdString();
    const std::vector<StockPatchOverride> stockPatches = installedStockPatchOverrides(backupRoot);
    // An edit installed as a whole zone took the place of the stock zone file, which waits in stockKeepDir().
    const std::vector<StockPatchOverride> stockZones = installedStockZoneOverrides(backupRoot, stockKeepDir().toStdString());
    if (notes && (!stockPatches.empty() || !stockZones.empty())) {
        QStringList names;
        for (const StockPatchOverride& o : stockZones) names << qs(o.name);
        for (const StockPatchOverride& o : stockPatches) names << qs(o.name);
        *notes << QString("installed edit(s) in the game: %1. This rebuild starts from the stock map (the stock files kept at install)")
                      .arg(names.join(", "));
    }
''')
patch(E, '''                                           &removeWorld, &litFilter);''', '''                                           &removeWorld, &litFilter, &stockZones, false);''')

between(E, 'QString MainWindow::stockZoneForReading(', 'int MainWindow::loadStreamedWorldAroundCamera() {', '''QString MainWindow::stockZoneForReading(const QString& zone, QString* tempDir, QString* err) {
    if (tempDir) tempDir->clear();
    const QString name = QFileInfo(zone).completeBaseName();
    const QString backup = stockBackupDir(name);
    // An edit installed as a whole zone: the stock zone (and its stock patch) waits in the keep folder.
    const std::vector<std::string> zones = installedStockZones(backup.toStdString());
    if (std::find(zones.begin(), zones.end(), (name + ".ff").toStdString()) != zones.end()) {
        const QString kept = stockKeepDir() + "/" + name + ".ff";
        if (!QFileInfo::exists(kept)) {
            if (err) *err = "An edit of " + name + " is installed in the game, but the stock zone is not in " + QDir::toNativeSeparators(stockKeepDir()) +
                            ". Put the stock " + name + ".ff back there (or into the game's zone folder), then open the map again.";
            return {};
        }
        EditorLog::instance().add(LogLevel::Info, "stock", "an edit of " + name + " is installed in the game: the stock map is read from " +
                                                               QDir::toNativeSeparators(kept));
        return kept;
    }
    // An edit installed by the old method (a patch file): the stock map is read from a copy with the stock patch.
    const std::vector<std::string> installed = installedStockPatches(backup.toStdString());
    if (std::find(installed.begin(), installed.end(), (name + ".fp").toStdString()) == installed.end()) return zone;
    const QString view = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/stock_view/" + name;
    QDir(view).removeRecursively();
    QDir().mkpath(view);
    const QString copy = view + "/" + name + ".ff";
    const QString saved = backup + "/" + name + ".fp.stock";
    if (!QFile::copy(zone, copy) || (QFileInfo::exists(saved) && !QFile::copy(saved, view + "/" + name + ".fp"))) {
        QDir(view).removeRecursively();
        if (err) *err = "An edit of " + name + " is installed in the game, and the stock map could not be copied to " + view +
                        " to read it (disk space?). File > Restore Stock Map In Game, then open the map again.";
        return {};
    }
    if (tempDir) *tempDir = view;
    EditorLog::instance().add(LogLevel::Info, "stock", "an edit of " + name + " is installed in the game: the stock map is read from a copy with "
                                                           "the stock patch saved at install");
    return copy;
}

''')

between(E, '// The edited map goes into the game as patch files', 'int MainWindow::hideStockPropsInSelection() {', '''QString MainWindow::stockKeepDir() const {
    const QString replay = toolPath("replay");
    return replay.isEmpty() ? QString() : QFileInfo(replay).absolutePath() + "/iw8r_stock";
}

// The edited map goes into the game as whole unsigned zone files in place of the stock ones (research 116: the game
// reads a patch of a signed zone only with its own signature). The stock files are moved to stockKeepDir() and put
// back by Restore. Only these two actions write to the game folder, and only after the owner confirms the list.
void MainWindow::installEditedStockMap() {
    QString outDir = lastStockRebuild_.isEmpty() ? QString() : lastStockRebuild_ + "/out";
    QString zone = lastStockRebuildZone_;
    if (outDir.isEmpty()) {
        // No rebuild in this session: the out folder of an earlier rebuild can be installed.
        if (QMessageBox::question(this, "Install Edited Stock Map",
                                  "No stock map was rebuilt in this session (File > Rebuild Stock Zone).\\n\\n"
                                  "Choose a folder with edited zone files (the out folder of an earlier rebuild) to install instead?",
                                  QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes)
            return;
        outDir = QFileDialog::getExistingDirectory(this, "Folder with the edited zone files");
        if (outDir.isEmpty()) return;
        const std::vector<std::string> files = stockZoneFiles(outDir.toStdString());
        if (!files.empty()) {
            zone = QFileInfo(qs(files.front())).completeBaseName();
            if (zone.startsWith("techsets_")) zone = zone.mid(9);
        }
    }
    installEditedStockZones(outDir, zone, true);
}

bool MainWindow::installEditedStockZones(const QString& outDir, const QString& zone, bool ask) {
    const QString replay = toolPath("replay");
    const QString zoneDir = QFileInfo(replay).absolutePath() + "/zone";
    const std::vector<std::string> files = stockZoneFiles(outDir.toStdString());
    if (replay.isEmpty() || !QFileInfo(zoneDir).isDir() || files.empty() || zone.isEmpty()) {
        QMessageBox::warning(this, "Install Edited Stock Map",
                             files.empty() ? "There is no edited zone file (.ff) in " + QDir::toNativeSeparators(outDir) + "."
                                           : "The game folder is not set (Tools > Tool Paths).");
        return false;
    }
    QStringList names;
    qint64 bytes = 0;
    for (const std::string& f : files) {
        names << qs(f);
        bytes += QFileInfo(outDir + "/" + qs(f)).size();
    }
    const QString backup = stockBackupDir(zone), keep = stockKeepDir();
    if (ask &&
        QMessageBox::question(this, "Install Edited Stock Map",
                              QString("Install %1 edited zone file(s) into the game?\\n\\n%2\\n\\nTo: %3\\n\\nThe stock files of the same "
                                      "name, and their stock patch files, are moved to\\n%4\\nFile > Restore Stock Map In Game moves them "
                                      "back. The copy needs about %5 MB free on the game's drive.\\n\\nClose the game before you continue.")
                                  .arg(names.size()).arg(names.join("\\n"), QDir::toNativeSeparators(zoneDir), QDir::toNativeSeparators(keep))
                                  .arg(bytes / (1024 * 1024) + 1),
                              QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes)
        return false;
    StockInstallReport report;
    std::string err;
    const bool ok = installStockZones(outDir.toStdString(), zoneDir.toStdString(), keep.toStdString(), backup.toStdString(), report, &err);
    for (const std::string& f : report.removed) EditorLog::instance().add(LogLevel::Info, "stock", "removed " + qs(f) + " from " + zoneDir);
    for (const std::string& f : report.backedUp) EditorLog::instance().add(LogLevel::Info, "stock", "moved the stock " + qs(f) + " to " + keep);
    for (const std::string& f : report.installed) EditorLog::instance().add(LogLevel::Info, "stock", "installed " + qs(f) + " into " + zoneDir);
    if (!ok) {
        QMessageBox::warning(this, "Install Edited Stock Map", qs(err));
        ctx_.setStatus("Install Edited Stock Map failed: " + qs(err));
        return false;
    }
    lastStockRebuildZone_ = zone;
    ctx_.setStatus(QString("Installed the edited %1 (%2 zone file(s)); File > Restore Stock Map In Game takes it out")
                       .arg(zone).arg(report.installed.size()));
    return true;
}

void MainWindow::restoreStockMap() {
    const QString zone = !stockZone_.isEmpty() ? stockZone_ : lastStockRebuildZone_;
    if (zone.isEmpty()) {
        QMessageBox::information(this, "Restore Stock Map", "Open the stock map whose edit you want to take out (File > Open IW8 Stock Map).");
        return;
    }
    const QString backup = stockBackupDir(zone);
    std::vector<std::string> installed = installedStockZones(backup.toStdString());
    for (const std::string& f : installedStockPatches(backup.toStdString())) installed.push_back(f);
    if (installed.empty()) {
        QMessageBox::information(this, "Restore Stock Map", "No edit of " + zone + " is installed by this editor.");
        return;
    }
    const QString zoneDir = QFileInfo(toolPath("replay")).absolutePath() + "/zone";
    QStringList names;
    for (const std::string& f : installed) names << qs(f);
    if (QMessageBox::question(this, "Restore Stock Map",
                              QString("Take the edit of %1 out of the game?\\n\\n%2\\n\\nFrom: %3\\n\\nThe stock files that were moved "
                                      "away at install are put back. Close the game before you continue.")
                                  .arg(zone, names.join("\\n"), QDir::toNativeSeparators(zoneDir)),
                              QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes)
        return;
    StockInstallReport report;
    std::string err;
    const bool ok = restoreStockZones(backup.toStdString(), zoneDir.toStdString(), stockKeepDir().toStdString(), report, &err);
    for (const std::string& f : report.removed) EditorLog::instance().add(LogLevel::Info, "stock", "removed " + qs(f) + " from " + zoneDir);
    for (const std::string& f : report.restored) EditorLog::instance().add(LogLevel::Info, "stock", "put back the stock " + qs(f));
    if (!ok) {
        QMessageBox::warning(this, "Restore Stock Map", qs(err));
        return;
    }
    ctx_.setStatus(QString("Restored the stock %1 (%2 file(s) removed, %3 stock file(s) put back)")
                       .arg(zone).arg(report.removed.size()).arg(report.restored.size()));
}

''')
s = read(E)
if '#include <QFileDialog>' not in s:
    i = s.index('#include <Q')
    s = s[:i] + '#include <QFileDialog>\n' + s[i:]
    write(E, s)

# --- tests ----------------------------------------------------------------------------------------------------------
T = 'tests/stock_patch_json_tests.cpp'
s = read(T)
s = s.rstrip() + '''

// Edits installed as whole zones (research 116): the edits file names the kept stock zone per zone file, and asks for
// no patch files.
TEST(stock_zone_edit_stock_zones_json) {
    const std::vector<StockPatchOverride> zones = {{"mp_a.ff", "E:\\\\game\\\\iw8r_stock\\\\mp_a.ff"}};
    CHECK(stockEditJson("e.ents", {}, {}, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, &zones, false) ==
          "{\\"entities\\":\\"e.ents\\",\\"stock_zones\\":{\\"mp_a.ff\\":\\"E:\\\\\\\\game\\\\\\\\iw8r_stock\\\\\\\\mp_a.ff\\"},\\"patch\\":false}");
    const std::vector<StockPatchOverride> none;
    CHECK(stockEditJson("", {}, {}, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, &none, false) == "{\\"patch\\":false}");
    CHECK(stockEditJson("", {}, {}, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, &none) == "{}");
}
'''
write(T, s)

T2 = 'tests/stock_install_tests.cpp'
s = read(T2)
s = s.rstrip() + '''

namespace {
// A zone header as the converter writes it for an unsigned zone (0x88 bytes), then some payload.
std::string unsignedZone(const std::string& payload, uint32_t xfile = 0xFF7, const char* magic = "IWffc100") {
    std::string h(0x88, '\\0');
    std::memcpy(h.data(), magic, 8);
    const uint32_t version = 11, resident = uint32_t(payload.size()), decodedLow = 0x1234;
    const uint64_t decoded = 0x1234;
    std::memcpy(h.data() + 8, &version, 4);
    std::memcpy(h.data() + 12, &xfile, 4);
    std::memcpy(h.data() + 20, &resident, 4);
    std::memcpy(h.data() + 28, &decodedLow, 4);
    std::memcpy(h.data() + 32, &decoded, 8);
    return h + payload;
}
}  // namespace

// Research 116: what the owner's client takes as an unsigned zone (its DB_ResetFileReader).
TEST(stock_install_unsigned_zone_header) {
    std::string why;
    CHECK(unsignedReplayZoneHeader(unsignedZone("x"), &why));
    CHECK(!unsignedReplayZoneHeader(unsignedZone("x", 0xFF7, "IWffa100"), &why) && why.find("signed") != std::string::npos);
    CHECK(!unsignedReplayZoneHeader(unsignedZone("x", 0xFF5), &why) && why.find("zone version") != std::string::npos);
    CHECK(!unsignedReplayZoneHeader("IWffc100", &why) && why.find("shorter") != std::string::npos);
    std::string h = unsignedZone("x");
    h[16] = 1;  // the compression flag of a signed stock zone
    CHECK(!unsignedReplayZoneHeader(h, &why));
    h = unsignedZone("x");
    h[18] = 3;  // a streamed zone kind
    CHECK(!unsignedReplayZoneHeader(h, &why));
    h = unsignedZone("x");
    h[28] = 0x35;  // the two decoded sizes differ
    CHECK(!unsignedReplayZoneHeader(h, &why) && why.find("decoded sizes") != std::string::npos);
    const fs::path root = fs::path(iw8r::test::tmpDir()) / "stock_install_header";
    std::error_code ec;
    fs::remove_all(root, ec);
    put(root / "a.ff", unsignedZone("payload"));
    put(root / "short.ff", "IWffc100");
    CHECK(unsignedReplayZone((root / "a.ff").string(), &why));
    CHECK(!unsignedReplayZone((root / "short.ff").string(), &why));
    CHECK(!unsignedReplayZone((root / "missing.ff").string(), &why));
    fs::remove_all(root, ec);
}

// Research 116: an edited stock map installs as whole unsigned zones; the stock zone and its stock patch are moved to
// a keep folder and moved back on restore. Only temporary folders are used here.
TEST(stock_install_zones_and_restore) {
    const fs::path root = fs::path(iw8r::test::tmpDir()) / "stock_install_zones";
    std::error_code ec;
    fs::remove_all(root, ec);
    const fs::path out = root / "work" / "out", zone = root / "game" / "zone", keep = root / "game" / "iw8r_stock";
    const fs::path backups = root / "stock_backup", backup = backups / "mp_a";
    const std::string stockA = unsignedZone("stock a", 0xFF7, "IWffa100"), stockT = unsignedZone("stock techsets", 0xFF7, "IWffa100");
    put(zone / "mp_a.ff", stockA);
    put(zone / "mp_a.fp", "stock patch of a");  // the game ships a patch for this zone
    put(zone / "techsets_mp_a.ff", stockT);
    put(zone / "mp_b.ff", "another map");
    const std::string editA = unsignedZone("edited a"), editT = unsignedZone("edited techsets");
    put(out / "mp_a.ff", editA);
    put(out / "techsets_mp_a.ff", editT);
    put(out / "mp_a.edit-report.json", "{}");
    put(out / "patch" / "mp_a.fp", "not installed any more");

    CHECK((stockZoneFiles(out.string()) == std::vector<std::string>{"mp_a.ff", "techsets_mp_a.ff"}));
    CHECK(installedStockZones(backup.string()).empty());
    CHECK(installedStockZoneOverrides(backups.string(), keep.string()).empty());

    StockInstallReport r;
    std::string err;
    CHECK(installStockZones(out.string(), zone.string(), keep.string(), backup.string(), r, &err));
    CHECK((r.installed == std::vector<std::string>{"mp_a.ff", "techsets_mp_a.ff"}));
    CHECK((r.backedUp == std::vector<std::string>{"mp_a.ff", "mp_a.fp", "techsets_mp_a.ff"}));
    CHECK(get(zone / "mp_a.ff") == editA && get(zone / "techsets_mp_a.ff") == editT);
    CHECK(!fs::exists(zone / "mp_a.fp"));  // the stock patch would stop the load of the edited zone
    CHECK(get(keep / "mp_a.ff") == stockA && get(keep / "mp_a.fp") == "stock patch of a" && get(keep / "techsets_mp_a.ff") == stockT);
    CHECK(get(zone / "mp_b.ff") == "another map");
    CHECK((installedStockZones(backup.string()) == std::vector<std::string>{"mp_a.ff", "techsets_mp_a.ff"}));
    CHECK(installedStockPatches(backup.string()).empty());
    const std::vector<StockPatchOverride> o = installedStockZoneOverrides(backups.string(), keep.string());
    CHECK(o.size() == 2);
    if (o.size() == 2) CHECK(o[0].name == "mp_a.ff" && fs::path(o[0].stockFile) == keep / "mp_a.ff" && o[1].name == "techsets_mp_a.ff");

    // A newer edit replaces ours; the kept stock files are not touched.
    const std::string editA2 = unsignedZone("edited a, second time");
    put(out / "mp_a.ff", editA2);
    StockInstallReport r2;
    CHECK(installStockZones(out.string(), zone.string(), keep.string(), backup.string(), r2, &err));
    CHECK(r2.backedUp.empty() && r2.installed.size() == 2);
    CHECK(get(zone / "mp_a.ff") == editA2 && get(keep / "mp_a.ff") == stockA);

    // Restore: our zones go, the stock zone and its patch come back, nothing stays marked or kept.
    StockInstallReport r3;
    CHECK(restoreStockZones(backup.string(), zone.string(), keep.string(), r3, &err));
    CHECK((r3.removed == std::vector<std::string>{"mp_a.ff", "techsets_mp_a.ff"}));
    CHECK((r3.restored == std::vector<std::string>{"mp_a.ff", "mp_a.fp", "techsets_mp_a.ff"}));
    CHECK(get(zone / "mp_a.ff") == stockA && get(zone / "mp_a.fp") == "stock patch of a" && get(zone / "techsets_mp_a.ff") == stockT);
    CHECK(!fs::exists(keep / "mp_a.ff") && !fs::exists(keep / "mp_a.fp") && installedStockZones(backup.string()).empty());
    StockInstallReport r4;
    CHECK(restoreStockZones(backup.string(), zone.string(), keep.string(), r4, &err) && r4.removed.empty() && r4.restored.empty());

    // Refusals, each before anything is changed: a signed or foreign file, a zone the game does not have, no files.
    put(out / "mp_a.ff", unsignedZone("signed", 0xFF7, "IWffa100"));
    StockInstallReport bad;
    CHECK(!installStockZones(out.string(), zone.string(), keep.string(), backup.string(), bad, &err) && err.find("signed") != std::string::npos);
    CHECK(bad.installed.empty() && bad.backedUp.empty() && get(zone / "techsets_mp_a.ff") == stockT && !fs::exists(keep / "techsets_mp_a.ff"));
    put(out / "mp_a.ff", editA);
    put(out / "mp_new.ff", unsignedZone("a zone the game does not have"));
    CHECK(!installStockZones(out.string(), zone.string(), keep.string(), backup.string(), bad, &err) && err.find("mp_new.ff") != std::string::npos);
    CHECK(get(zone / "mp_a.ff") == stockA);
    fs::remove(out / "mp_new.ff", ec);
    CHECK(!installStockZones((root / "empty").string(), zone.string(), keep.string(), backup.string(), bad, &err) && err.find("no .ff") != std::string::npos);

    // A patch of the old method is taken out by the new install, and its stock patch is kept with the stock zone.
    put(root / "old" / "patch" / "mp_a.fp", "our old patch");
    StockInstallReport oldR;
    CHECK(installStockPatches((root / "old" / "patch").string(), zone.string(), backup.string(), oldR, &err));
    CHECK(get(zone / "mp_a.fp") == "our old patch" && (installedStockPatches(backup.string()) == std::vector<std::string>{"mp_a.fp"}));
    StockInstallReport r5;
    CHECK(installStockZones(out.string(), zone.string(), keep.string(), backup.string(), r5, &err));
    CHECK(installedStockPatches(backup.string()).empty() && !fs::exists(zone / "mp_a.fp"));
    CHECK(get(keep / "mp_a.fp") == "stock patch of a" && get(zone / "mp_a.ff") == editA);

    // The kept stock zone went missing: restore changes nothing for it and says so.
    fs::rename(keep / "mp_a.ff", root / "lost.ff", ec);
    StockInstallReport r6;
    CHECK(!restoreStockZones(backup.string(), zone.string(), keep.string(), r6, &err) && err.find("mp_a.ff") != std::string::npos);
    CHECK(get(zone / "mp_a.ff") == editA);
    CHECK(installedStockZoneOverrides(backups.string(), keep.string()).size() == 1);  // only the zone whose stock file is there
    fs::rename(root / "lost.ff", keep / "mp_a.ff", ec);
    StockInstallReport r7;
    CHECK(restoreStockZones(backup.string(), zone.string(), keep.string(), r7, &err));
    CHECK(get(zone / "mp_a.ff") == stockA && get(zone / "mp_a.fp") == "stock patch of a" && get(zone / "techsets_mp_a.ff") == stockT);
    fs::remove_all(root, ec);
}
'''
if '#include <cstring>' not in s:
    s = s.replace('#include <filesystem>', '#include <cstdint>\n#include <cstring>\n#include <filesystem>', 1)
write(T2, s)
print('ok')
