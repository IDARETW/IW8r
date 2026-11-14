// MainWindow: the first-person look (research 128). The weapon and hands a player holds keep their materials and
// technique sets in a zone every map loads (kViewmodelLookZone), so a map's own technique-set zone cannot give them the
// map shader's filter. File > First-Person Look builds that shared zone with the filter spliced into the sets of the
// weapon and arm materials only, every name kept; installing it puts the look on every map until it is restored.
// Only Install and Restore write to the game folder, and only after the owner confirms.
#include "main_window.h"
#include "custom_shader.h"
#include "editor_log.h"
#include "render.h"
#include "stock_install.h"
#include "stock_zone_edit.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QProcess>
#include <QStandardPaths>

namespace iw8r::qt {

namespace {
QString qs(const std::string& s) { return QString::fromStdString(s); }

bool writeBytes(const QString& path, const std::vector<uint8_t>& bytes) {
    QFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(reinterpret_cast<const char*>(bytes.data()), qint64(bytes.size())) == qint64(bytes.size());
}

QString lookZoneFile(const QString& work) { return work + "/out/techsets_" + QString(kViewmodelLookZone) + ".ff"; }

// What the converter's report says about the filter, as one line ("" when there is no report).
QString lookReportLine(const QString& work) {
    QFile f(work + "/out/" + QString(kViewmodelLookZone) + ".edit-report.json");
    if (!f.open(QIODevice::ReadOnly)) return {};
    const QJsonObject lit = QJsonDocument::fromJson(f.readAll()).object().value("lit_filter").toObject();
    if (lit.isEmpty()) return {};
    return QString("%1 weapon and arm material(s) matched; %2 technique set(s) filtered, %3 shader(s) (%4 with the screen), %5 refused")
        .arg(lit.value("materials_matched").toInt()).arg(lit.value("technique_sets").toInt()).arg(lit.value("shaders").toInt())
        .arg(lit.value("shaders_with_screen").toInt()).arg(lit.value("shaders_refused").toInt() + lit.value("screen_refused").toInt());
}

// How many technique sets the converter's report says were filtered (0: none, or no report).
int lookReportSets(const QString& work) {
    QFile f(work + "/out/" + QString(kViewmodelLookZone) + ".edit-report.json");
    if (!f.open(QIODevice::ReadOnly)) return 0;
    return QJsonDocument::fromJson(f.readAll()).object().value("lit_filter").toObject().value("technique_sets").toInt();
}

// The ACTS loader test of the built zone. The output folder is short on purpose: ACTS cannot write below a long path.
QString lookValidateDir() { return QDir::tempPath() + "/iw8r_fpl"; }
QStringList lookActsArgs(const QString& replay, const QString& oodle, const QString& zoneFile) {
    QStringList a{"--noUpdater", "fastfile", "-r", "mw19replay", "-g", replay};
    if (!oodle.isEmpty()) a << "--oodle" << oodle;
    a << "--test" << "-o" << lookValidateDir() << zoneFile;
    return a;
}
// The "Replay: N tested, M failed" line of ACTS ("" when it printed none); *ok when nothing failed.
QString lookActsResult(const QString& text, bool* ok) {
    QString last;
    *ok = false;
    for (const QString& line : text.split('\n', Qt::SkipEmptyParts))
        if (line.startsWith("Replay:")) {
            last = line.trimmed();
            *ok = line.contains(" 0 failed");
        }
    return last;
}
}  // namespace

QString MainWindow::viewmodelLookDir() const {
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/first_person_look";
}

bool MainWindow::prepareViewmodelLook(const CustomShader& shader, const QString& work, QString* program, QStringList* args, QString* err) {
    const QString zonetool = toolPath("zonetool"), replay = toolPath("replay");
    if (zonetool.isEmpty() || !QFileInfo::exists(zonetool) || replay.isEmpty() || !QFileInfo::exists(replay)) {
        if (err) *err = "The converter (zonetool) and the game exe must be set in Tools > Tool Paths.";
        return false;
    }
    if (!customShaderHasFilter(shader.code)) {
        if (err)
            *err = "The shader " + qs(shader.name) + " has no float3 iw8rFilter(float3 colour) function. The weapon and hands keep the "
                   "game's lighting, so only a filter can change their look.";
        return false;
    }
    // The folder is this feature's own: an older result goes first, so a failed build never leaves an old zone behind.
    QDir(work + "/out").removeRecursively();
    if (!QDir().mkpath(work)) {
        if (err) *err = "Cannot make " + work;
        return false;
    }
    std::vector<uint8_t> bytes;
    std::string cerr;
    const QString programPath = work + "/look_filter.cso";
    if (!iw8r::app::SceneRenderer::compileProgram(customShaderFilterProgramSource(shader), kIw8rFilterEntry, bytes, &cerr)) {
        if (err) *err = "The shader's filter does not compile: " + qs(cerr);
        return false;
    }
    if (!writeBytes(programPath, bytes)) {
        if (err) *err = "Cannot write " + programPath;
        return false;
    }
    QString screenPath;
    if (customShaderHasScreenFilter(shader.code)) {
        screenPath = work + "/look_filter_screen.cso";
        bytes.clear();
        if (!iw8r::app::SceneRenderer::compileProgram(customShaderScreenFilterProgramSource(shader), kIw8rFilterEntry, bytes, &cerr)) {
            if (err) *err = "The shader's screen filter (iw8rFilterScreen) does not compile: " + qs(cerr);
            return false;
        }
        if (!writeBytes(screenPath, bytes)) {
            if (err) *err = "Cannot write " + screenPath;
            return false;
        }
    }
    // An edit that is installed in the game took the place of a stock zone file: the converter reads the kept stock
    // file instead, so the look is always built from the stock game (also when an older look is installed).
    const std::string backupRoot = (QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/stock_backup").toStdString();
    const std::vector<StockPatchOverride> stockZones = installedStockZoneOverrides(backupRoot, stockKeepDir().toStdString());
    const std::string json = stockSharedLookEditJson(QDir::toNativeSeparators(programPath).toStdString(),
                                                     QDir::toNativeSeparators(screenPath).toStdString(), kViewmodelLookMaterials, &stockZones);
    const QString editsPath = work + "/edits.json";
    QFile jf(editsPath);
    if (!jf.open(QIODevice::WriteOnly) || jf.write(json.data(), qint64(json.size())) != qint64(json.size())) {
        if (err) *err = "Cannot write " + editsPath;
        return false;
    }
    jf.close();
    if (program) *program = zonetool;
    if (args)
        *args = {"-game", "iw8", "-path", QDir::toNativeSeparators(QFileInfo(replay).absolutePath()), "editzone", kViewmodelLookZone,
                 QDir::toNativeSeparators(editsPath), QDir::toNativeSeparators(work + "/out")};
    return true;
}

// File > First-Person Look > Build: the open map's shader, or a shader file. The converter and the ACTS loader test
// run in the background; nothing is installed.
void MainWindow::buildViewmodelLook() {
    std::shared_ptr<const CustomShader> shader = mapSurfaceShader();
    if (!shader || !customShaderHasFilter(shader->code)) {
        QDir d(QCoreApplication::applicationDirPath());
        for (int up = 0; up < 3 && !QFileInfo::exists(d.filePath("data/shaders")); ++up) d.cdUp();
        const QString f = QFileDialog::getOpenFileName(this, "Shader for the first-person look", d.filePath("data/shaders"),
                                                       "IW8 Radiant shader (*.iw8shader.hlsl)");
        if (f.isEmpty()) return;
        auto read = std::make_shared<CustomShader>();
        std::string serr;
        if (!readCustomShader(f.toStdString(), *read, &serr)) {
            QMessageBox::warning(this, "First-Person Look", qs(serr));
            return;
        }
        shader = read;
    }
    const QString work = viewmodelLookDir();
    QString program, err;
    QStringList args;
    if (!prepareViewmodelLook(*shader, work, &program, &args, &err)) {
        QMessageBox::warning(this, "First-Person Look", err);
        return;
    }
    const QString name = qs(shader->name);
    auto* proc = new QProcess(this);
    proc->setProgram(program);
    proc->setArguments(args);
    proc->setProcessChannelMode(QProcess::MergedChannels);
    connect(proc, &QProcess::readyRead, this, [proc] {
        for (const QString& line : QString::fromLocal8Bit(proc->readAll()).split('\n', Qt::SkipEmptyParts))
            EditorLog::instance().add(LogLevel::Info, "look", line.trimmed());
    });
    connect(proc, &QProcess::finished, this, [this, proc, work, name](int code) {
        proc->deleteLater();
        const QString zone = lookZoneFile(work);
        if (code != 0 || !QFileInfo::exists(zone)) {
            ctx_.setStatus(QString("First-person look: the converter failed (exit %1); see the Console. An old converter does not know "
                                   "\"keep_names\": update it in Tools > Tool Paths.").arg(code));
            return;
        }
        if (const QString line = lookReportLine(work); !line.isEmpty()) EditorLog::instance().add(LogLevel::Info, "look", line);
        if (lookReportSets(work) == 0) {
            QFile::remove(zone);
            ctx_.setStatus("First-person look: the converter filtered no technique set (see the Console); nothing to install");
            return;
        }
        const QString acts = toolPath("acts"), replay = toolPath("replay"), oodle = toolPath("oodle");
        if (acts.isEmpty() || !QFileInfo::exists(acts)) {
            ctx_.setStatus("First-person look built from " + name + " (not checked: ACTS is not set; not installed): " + QDir::toNativeSeparators(zone));
            return;
        }
        QDir(lookValidateDir()).removeRecursively();
        auto* check = new QProcess(this);
        check->setProgram(acts);
        check->setArguments(lookActsArgs(replay, oodle, zone));
        check->setProcessChannelMode(QProcess::MergedChannels);
        connect(check, &QProcess::finished, this, [this, check, zone, name](int) {
            bool ok = false;
            const QString line = lookActsResult(QString::fromLocal8Bit(check->readAll()), &ok);
            check->deleteLater();
            QDir(lookValidateDir()).removeRecursively();
            if (!line.isEmpty()) EditorLog::instance().add(LogLevel::Info, "look", QFileInfo(zone).fileName() + ": " + line);
            if (!ok) {
                // A zone the game's loaders refuse must not be installable.
                QFile::remove(zone);
                ctx_.setStatus("First-person look: the ACTS loader test reports failures (see the Console); the zone was deleted");
                return;
            }
            ctx_.setStatus("First-person look built from " + name + " and checked (ACTS, 0 failed); not installed: File > First-Person Look > "
                           "Install Into Game");
        });
        check->start();
        ctx_.setStatus("First-person look: built; the game's loaders are checking it (ACTS)");
    });
    proc->start();
    ctx_.setStatus("First-person look: building from the shader " + name + " (under a minute; the Console shows progress)");
}

bool MainWindow::buildViewmodelLookQuiet(const QString& shaderFile, const QString& outDir, QStringList& out) {
    CustomShader shader;
    std::string serr;
    if (!readCustomShader(shaderFile.toStdString(), shader, &serr)) {
        out << "FAIL " + qs(serr);
        return false;
    }
    const QString work = QDir(outDir).absolutePath();
    QString program, err;
    QStringList args;
    if (!prepareViewmodelLook(shader, work, &program, &args, &err)) {
        out << "FAIL " + err;
        return false;
    }
    out << "shader " + qs(shader.name) << "zone " + QString(kViewmodelLookZone) << "materials " + QString(kViewmodelLookMaterials);
    QProcess proc;
    proc.setProgram(program);
    proc.setArguments(args);
    proc.setProcessChannelMode(QProcess::MergedChannels);
    proc.start();
    const bool ran = proc.waitForStarted(30000) && proc.waitForFinished(-1);
    const QByteArray log = proc.readAll();
    if (QFile lf(work + "/converter-log.txt"); lf.open(QIODevice::WriteOnly)) lf.write(log);
    const QString zone = lookZoneFile(work);
    if (!ran || proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0 || !QFileInfo::exists(zone)) {
        out << QString("FAIL the converter failed (exit %1); see converter-log.txt").arg(ran ? proc.exitCode() : -1);
        return false;
    }
    if (const QString line = lookReportLine(work); !line.isEmpty()) out << line;
    if (lookReportSets(work) == 0) {
        QFile::remove(zone);
        out << "FAIL the converter filtered no technique set";
        return false;
    }
    out << QString("built %1 (%2 bytes)").arg(QDir::toNativeSeparators(zone)).arg(QFileInfo(zone).size());
    const QString acts = toolPath("acts"), replay = toolPath("replay"), oodle = toolPath("oodle");
    if (acts.isEmpty() || !QFileInfo::exists(acts)) {
        out << "FAIL ACTS is not set (Tools > Tool Paths): the zone is not checked by the game's loaders";
        return false;
    }
    QDir(lookValidateDir()).removeRecursively();
    QProcess check;
    check.setProgram(acts);
    check.setArguments(lookActsArgs(replay, oodle, zone));
    check.setProcessChannelMode(QProcess::MergedChannels);
    check.start();
    bool ok = false;
    const QString line = check.waitForStarted(30000) && check.waitForFinished(-1) ? lookActsResult(QString::fromLocal8Bit(check.readAll()), &ok) : QString();
    QDir(lookValidateDir()).removeRecursively();
    out << "ACTS " + (line.isEmpty() ? QString("printed no result") : line);
    if (!ok) {
        QFile::remove(zone);
        out << "FAIL the game's loaders refuse the zone; it was deleted";
        return false;
    }
    out << "ok the look is in " + QDir::toNativeSeparators(work + "/out") + " (not installed)";
    return true;
}

void MainWindow::installViewmodelLook() {
    const QString outDir = viewmodelLookDir() + "/out";
    if (!QFileInfo::exists(lookZoneFile(viewmodelLookDir()))) {
        QMessageBox::information(this, "First-Person Look", "No first-person look is built yet (File > First-Person Look > Build From Shader).");
        return;
    }
    if (QMessageBox::question(this, "First-Person Look",
                              "The weapon and hands a player holds use shaders of a zone that every map loads.\n\n"
                              "While this look is installed, weapons and hands have it on EVERY map (also the weapons other "
                              "players carry, and some character arms), not only on the map it was made for. File > First-Person Look "
                              "> Restore In Game takes it out again.\n\nGo on to the list of files?",
                              QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes)
        return;
    const QString mapZone = lastStockRebuildZone_;
    if (installEditedStockZones(outDir, kViewmodelLookZone, true))
        ctx_.setStatus("First-person look installed (on every map); File > First-Person Look > Restore In Game takes it out");
    lastStockRebuildZone_ = mapZone;  // the look is not the stock map File > Restore Stock Map In Game is about
}

void MainWindow::restoreViewmodelLook() {
    const QString zone = kViewmodelLookZone;
    const QString backup = stockBackupDir(zone);
    const std::vector<std::string> installed = installedStockZones(backup.toStdString());
    if (installed.empty()) {
        QMessageBox::information(this, "First-Person Look", "No first-person look is installed by this editor.");
        return;
    }
    const QString zoneDir = QFileInfo(toolPath("replay")).absolutePath() + "/zone";
    QStringList names;
    for (const std::string& f : installed) names << qs(f);
    if (QMessageBox::question(this, "First-Person Look",
                              QString("Take the first-person look out of the game?\n\n%1\n\nFrom: %2\n\nThe stock file that was moved "
                                      "away at install is put back. Close the game before you continue.")
                                  .arg(names.join("\n"), QDir::toNativeSeparators(zoneDir)),
                              QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes)
        return;
    StockInstallReport report;
    std::string err;
    const bool ok = restoreStockZones(backup.toStdString(), zoneDir.toStdString(), stockKeepDir().toStdString(), report, &err);
    for (const std::string& f : report.removed) EditorLog::instance().add(LogLevel::Info, "look", "removed " + qs(f) + " from " + zoneDir);
    for (const std::string& f : report.restored) EditorLog::instance().add(LogLevel::Info, "look", "put back the stock " + qs(f));
    if (!ok) {
        QMessageBox::warning(this, "First-Person Look", qs(err));
        return;
    }
    ctx_.setStatus(QString("First-person look taken out (%1 file(s) removed, %2 stock file(s) put back)").arg(report.removed.size()).arg(report.restored.size()));
}

}  // namespace iw8r::qt
