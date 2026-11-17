#include "techset_export.h"
#include "acts_runner.h"
#include "techset_document.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QPointer>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <memory>

namespace iw8r::qt {
namespace {
QString bare(QString n) {
    if (n.startsWith(','))
        n.remove(0, 1);
    return n;
}
QString root() {
    const auto base = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    return base.isEmpty() ? QString() : base + "/techset_cache";
}
QString hash(const QByteArray &bytes) {
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}
bool save(const QString &file, const QByteArray &data) {
    QDir().mkpath(QFileInfo(file).absolutePath());
    QSaveFile f(file);
    return f.open(QIODevice::WriteOnly) && f.write(data) == data.size() && f.commit();
}
bool readSet(const QString &file, const QString &name, TechsetDocument *out = nullptr) {
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    TechsetDocument d;
    std::string error;
    if (!importTechsetDocument(f.readAll().toStdString(), d, error) ||
        bare(QString::fromStdString(d.name)) != bare(name))
        return false;
    if (out)
        *out = std::move(d);
    return true;
}
struct Export : std::enable_shared_from_this<Export> {
    std::function<QString(const QString &)> tools;
    std::function<void(const QString &, const QString &)> done;
    QPointer<QObject> owner;
    bool hadOwner = false;
    QString name, cached;
    QStringList zones, shaders;
    int next = 0;
    TechsetDocument document;
    TechsetJson programs = TechsetJson::object();
    std::unique_ptr<QTemporaryDir> scratch;
    bool alive() const { return !hadOwner || owner; }
    void finish(const QString &error = {}) {
        if (alive())
            done(error.isEmpty() ? cached : QString(), error);
    }
    void body() {
        if (!alive())
            return;
        if (next >= zones.size()) {
            finish("Technique set '" + name + "' was not found in the owning or shared zones.");
            return;
        }
        const QString zone = zones[next++];
        ActsRequest req;
        req.zone = zone;
        req.types = "techset";
        req.names = {name};
        req.outDir = scratch->path();
        req.xpaks = false;
        ActsRunner::shared()->submit(tools, req, owner, [self = shared_from_this(), zone](const ActsResult &result) {
            if (!self->alive())
                return;
            QDirIterator files(self->scratch->path() + "/mw19replay/" + zone + "/assets/techset", {"*.asset.json"},
                               QDir::Files, QDirIterator::Subdirectories);
            while (files.hasNext()) {
                const auto file = files.next();
                if (!readSet(file, self->name, &self->document))
                    continue;
                QFile f(file);
                if (!f.open(QIODevice::ReadOnly) || !save(self->cached, f.readAll())) {
                    self->finish("Cannot write the technique-set cache.");
                    return;
                }
                self->document.sourceZone = zone.toStdString();
                self->zones.removeAll(zone);
                self->zones.prepend(zone);
                self->startPrograms();
                return;
            }
            if (!result.started) {
                self->finish(result.error.isEmpty() ? "The zone reader could not start." : result.error);
                return;
            }
            self->body();
        });
    }
    void startPrograms() {
        for (const auto &t : document.techniques)
            for (const auto &s : t.stages)
                if (!s.reference.empty())
                    shaders << bare(QString::fromStdString(s.reference));
        shaders.removeDuplicates();
        next = 0;
        payloads();
    }
    void payloads() {
        if (!alive())
            return;
        if (shaders.isEmpty() || next >= zones.size()) {
            if (!save(cached + ".programs.json", QByteArray::fromStdString(programs.dump(2)))) {
                finish("Cannot save shader payload index.");
                return;
            }
            finish();
            return;
        }
        const auto zone = zones[next++];
        ActsRequest req;
        req.zone = zone;
        req.types = "vertexshader,pixelshader,hullshader,domainshader,computeshader,libshader";
        req.names = shaders;
        req.outDir = scratch->path();
        req.xpaks = false;
        ActsRunner::shared()->submit(tools, req, owner, [self = shared_from_this(), zone](const ActsResult &) {
            if (!self->alive())
                return;
            const auto base = self->scratch->path() + "/mw19replay/" + zone;
            QFile journal(base + "/assets.jsonl");
            if (journal.open(QIODevice::ReadOnly))
                while (!journal.atEnd()) {
                    try {
                        const auto row = TechsetJson::parse(journal.readLine().toStdString());
                        const QString asset = bare(QString::fromStdString(row.value("name", "")));
                        const QString file = QString::fromStdString(row.value("file", ""));
                        if (!self->shaders.contains(asset) || !file.endsWith(".cso") || row.value("status", "") != "ok")
                            continue;
                        const auto resolved = QDir::cleanPath(QDir(base).absoluteFilePath(file));
                        if (!resolved.startsWith(QDir(base).absolutePath() + "/"))
                            continue;
                        QFile payload(resolved);
                        if (!payload.open(QIODevice::ReadOnly))
                            continue;
                        const auto bytes = payload.readAll();
                        if (bytes.size() < 32 || !bytes.startsWith("DXBC"))
                            continue;
                        const auto destination = QFileInfo(self->cached).absolutePath() + "/programs/" +
                                                 hash(asset.toUtf8() + zone.toUtf8() + bytes) + ".cso";
                        if (!save(destination, bytes))
                            continue;
                        self->programs[asset.toStdString()] = destination.toStdString();
                        self->programs[("," + asset).toStdString()] = destination.toStdString();
                        self->shaders.removeAll(asset);
                    } catch (const std::exception &) {
                    }
                }
            self->payloads();
        });
    }
};
} // namespace
QString techsetCachePath(const QString &name) {
    return name.isEmpty() || root().isEmpty() ? QString() : root() + "/" + hash(bare(name).toUtf8()) + ".asset.json";
}
void requestTechsetExport(const std::function<QString(const QString &)> &tools, const QString &techsetName,
                          QObject *context, std::function<void(const QString &, const QString &)> done,
                          const QStringList &owningZones) {
    const auto name = bare(techsetName);
    if (name.isEmpty() || root().isEmpty()) {
        done({}, "No technique-set name or cache folder.");
        return;
    }
    QStringList zones;
    for (const auto &z : owningZones) {
        if (z.isEmpty())
            continue;
        zones << z;
        if (!z.startsWith("techsets_"))
            zones << "techsets_" + z;
    }
    QDir data(QCoreApplication::applicationDirPath());
    for (int i = 0; i < 3 && !QFileInfo::exists(data.filePath("data/asset_catalog")); ++i)
        data.cdUp();
    QFile catalog(data.filePath("data/asset_catalog/iw8_techsets.tsv"));
    if (catalog.open(QIODevice::ReadOnly))
        while (!catalog.atEnd()) {
            const auto line = QString::fromUtf8(catalog.readLine()).trimmed();
            if (bare(line.section('\t', 0, 0)) == name)
                zones << line.section('\t', 1).split(',', Qt::SkipEmptyParts);
        }
    zones << "techsets_common_mp" << "techsets_common_core_mp" << "techsets_common_base_mp" << "techsets_common"
          << "techsets_common_stream_mp" << "techsets_global_mp" << "techsets_global_core_mp" << "techsets_global"
          << "techsets_global_stream_mp" << "techsets_common_br_mp";
    zones.removeDuplicates();
    QByteArray fingerprint = "replay-1.20-v2\n" + name.toUtf8();
    const auto addFile = [&](const QString &file) {
        QFileInfo f(file);
        fingerprint += '\n' + f.absoluteFilePath().toUtf8() + ':' + QByteArray::number(f.size()) + ':' +
                       QByteArray::number(f.lastModified().toMSecsSinceEpoch());
    };
    addFile(tools("replay"));
    addFile(tools("acts"));
    for (auto it = zones.begin(); it != zones.end();) {
        const auto file = stockZoneFile(tools("replay"), *it);
        if (!QFileInfo::exists(file))
            it = zones.erase(it);
        else {
            addFile(file);
            ++it;
        }
    }
    if (zones.isEmpty()) {
        done({}, "No source zones are available in the configured game folder.");
        return;
    }
    const auto cached = root() + "/" + hash(fingerprint) + "/techset.asset.json";
    auto job = std::make_shared<Export>();
    job->tools = tools;
    job->done = std::move(done);
    job->owner = context;
    job->hadOwner = context != nullptr;
    job->name = name;
    job->cached = cached;
    job->zones = zones;
    QDir().mkpath(root() + "/w");
    job->scratch = std::make_unique<QTemporaryDir>(root() + "/w/export_XXXXXX");
    if (!job->scratch->isValid()) {
        job->finish("Cannot create an export scratch folder.");
        return;
    }
    if (readSet(cached, name, &job->document)) {
        if (QFileInfo::exists(cached + ".programs.json")) {
            job->finish();
            return;
        }
        job->startPrograms();
    } else
        job->body();
}
} // namespace iw8r::qt
