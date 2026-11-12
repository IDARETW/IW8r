#include "techset_export.h"

#include <QByteArray>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

#include "acts_runner.h"

namespace iw8r::qt {
namespace {

constexpr const char* kTechsetZone = "techsets_common_mp";

// Stock technique sets are named with a leading comma when a material refers to them. The asset name has none.
QString bareTechsetName(QString name) {
    if (name.startsWith(',')) name.remove(0, 1);
    return name;
}

// <AppLocalData>/techset_cache, or empty when the application data folder is unknown.
QString cacheRoot() {
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    return base.isEmpty() ? QString() : base + "/techset_cache";
}

// Scratch folders are numbered; only the GUI thread asks for them.
int nextScratchNumber() {
    static int seq = 0;
    return ++seq;
}

// The first exported *.asset.json under `dir`, in any subfolder (the asset name may contain '/').
QString firstAssetJson(const QString& dir) {
    QDirIterator it(dir, {"*.asset.json"}, QDir::Files, QDirIterator::Subdirectories);
    return it.hasNext() ? it.next() : QString();
}

// The last non-empty line of the ACTS output, shortened (ACTS says "0 tested" when the name is not in the zone).
QString lastLogLine(const QByteArray& log) {
    const QStringList lines = QString::fromUtf8(log).split('\n', Qt::SkipEmptyParts);
    for (auto it = lines.rbegin(); it != lines.rend(); ++it) {
        const QString line = it->trimmed();
        if (!line.isEmpty()) return line.left(160);
    }
    return {};
}

}  // namespace

QString techsetCachePath(const QString& techsetName) {
    const QString name = bareTechsetName(techsetName);
    const QString root = cacheRoot();
    if (name.isEmpty() || root.isEmpty()) return {};
    // Every character outside [A-Za-z0-9_.-] becomes '_' (so '/' in a name cannot leave the folder).
    QString safe;
    safe.reserve(name.size());
    for (const QChar c : name) {
        const bool keep = (c.unicode() < 128 && c.isLetterOrNumber()) || c == '_' || c == '-' || c == '.';
        safe += keep ? c : QChar('_');
    }
    return root + '/' + safe + ".asset.json";
}

void requestTechsetExport(const std::function<QString(const QString&)>& tools, const QString& techsetName, QObject* context,
                          std::function<void(const QString& path, const QString& error)> done) {
    const QString name = bareTechsetName(techsetName);
    if (name.isEmpty()) {
        done({}, "no technique set name");
        return;
    }
    const QString cached = techsetCachePath(name);
    if (cached.isEmpty()) {
        done({}, "no application data folder");
        return;
    }
    if (QFileInfo::exists(cached)) {
        done(cached, {});
        return;
    }

    const QString scratch = cacheRoot() + "/w/" + QString::number(nextScratchNumber());
    QDir().mkpath(scratch);

    ActsRequest request;
    request.zone = kTechsetZone;
    request.types = "techset";
    request.names = {name};
    request.outDir = QDir::toNativeSeparators(scratch);
    request.xpaks = false;  // a technique set body is resident in the fastfile; no image payloads

    ActsRunner::shared()->submit(tools, request, context,
                                 [scratch, cached, done = std::move(done)](const ActsResult& result) {
        const QString assetDir = scratch + "/mw19replay/" + kTechsetZone + "/assets/techset";
        const QString found = firstAssetJson(assetDir);
        QString path, error;
        if (!found.isEmpty()) {
            QDir().mkpath(QFileInfo(cached).absolutePath());
            QFile::remove(cached);
            if (QFile::copy(found, cached))
                path = cached;
            else
                error = "could not write the cache file";
        } else if (!result.started) {
            error = "acts.exe could not be started";
        } else if (!result.error.isEmpty()) {
            error = result.error.section('\n', 0, 0).left(200);
        } else {
            error = QString("ACTS wrote no technique set (exit %1)").arg(result.exitCode);
            const QString tail = lastLogLine(result.log);
            if (!tail.isEmpty()) error += ": " + tail;
        }
        QDir(scratch).removeRecursively();
        done(path, error);
    });
}

}  // namespace iw8r::qt
