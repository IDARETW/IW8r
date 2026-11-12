#include "asset_audit.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QSet>
#include <QSettings>
#include <QTextStream>
#include <QTimer>
#include <algorithm>
#include <cstdio>
#include <memory>

#include "acts_runner.h"
#include "material_cache.h"
#include "model_cache.h"

namespace iw8r::qt {

namespace {

QString repoData() {
    QDir d(QCoreApplication::applicationDirPath());
    for (int up = 0; up < 4; ++up, d.cdUp())
        if (QFileInfo::exists(d.filePath("data/asset_catalog"))) return d.filePath("data");
    return {};
}

// Catalog names; models drop the streaming-zone-only weapons/attachments the browser leaves out too. zoneOf (when
// given) gets each name's first listed zone.
QStringList catalogNames(const QString& kind, QHash<QString, QStringList>* zoneOf = nullptr) {
    const QString file = kind == "materials" ? "asset_catalog/iw8_materials.tsv"
                         : kind == "effects" ? "asset_catalog/iw8_common_vfx.tsv"
                         : kind == "images"  ? "asset_catalog/iw8_images.tsv"
                                             : "asset_catalog/iw8_models.tsv";
    QFile f(QDir(repoData()).filePath(file));
    QStringList out;
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return out;
    QTextStream in(&f);
    in.readLine();
    while (!in.atEnd()) {
        const QString line = in.readLine();
        const int tab = line.indexOf('\t');
        if (tab <= 0 || line.startsWith("# ")) continue;
        if (zoneOf) {
            QStringList zones;
            for (const QString& z : line.mid(tab + 1).split(',', Qt::SkipEmptyParts)) zones << z.trimmed();
            zoneOf->insert(line.left(tab), zones);
        }
        if (kind == "models") {
            bool streamOnly = true;
            for (const QString& z : line.mid(tab + 1).split(',', Qt::SkipEmptyParts))
                streamOnly &= z.trimmed() == "global_stream_mp" || z.trimmed() == "techsets_global_stream_mp";
            if (streamOnly) continue;
        }
        out << line.left(tab);
    }
    return out;
}

QString toolPath(const QString& key) {
    QSettings ini(QDir(QCoreApplication::applicationDirPath()).filePath("IW8Radiant.ini"), QSettings::IniFormat);
    static const QMap<QString, QString> defaults = {
        {"acts", "E:/mw124/atian-cod-tools/build/bin/Release/acts.exe"},
        {"replay", "E:/IW8/Builds/1.20-replay/game_dx12_ship_replay.exe"},
        {"oodle", "E:/IW8/Builds/1.20-replay/oo2core_7_win64.dll"},
    };
    return ini.value("tools/" + key, defaults.value(key)).toString();
}

// Runs the event loop until `busy` turns false (or the timeout).
template <class Busy>
void waitIdle(Busy busy, int timeoutMs) {
    QEventLoop loop;
    QTimer poll;
    poll.setInterval(100);
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
        if (!busy()) loop.quit();
    });
    QTimer::singleShot(timeoutMs, &loop, &QEventLoop::quit);
    poll.start();
    QTimer::singleShot(200, &loop, [&] {
        if (!busy()) loop.quit();
    });
    loop.exec();
}

QString clean(QString s) { return s.replace('\t', ' ').replace('\n', ' ').replace('\r', ' ').left(300); }

}  // namespace

// 4.12: effects / images: the batch's names grouped by zone, one ACTS export each (scratch output, wiped after);
// ok = the journal says ok and the file is there, else missing with the journal's reason. A name "not in zone" is
// tried in its next listed zone (up to four).
static QStringList auditActsBatch(const QString& type, const QStringList& names, const QHash<QString, QStringList>& zoneOf,
                           const QString& scratch, int counts[3]) {
    QHash<QString, QPair<QString, QString>> result;  // name -> (status, reason)
    QHash<QString, int> attempt;
    QStringList pending = names;
    int outstanding = 0, seq = 0;
    QObject ctx;
    for (int round = 0; round < 4 && !pending.isEmpty(); ++round) {
    QMap<QString, QStringList> byZone;
    for (const QString& n : pending) byZone[zoneOf.value(n).value(attempt.value(n))] << n;
    for (auto it = byZone.begin(); it != byZone.end(); ++it) {
        if (it.key().isEmpty()) {
            for (const QString& n : it.value()) result.insert(n, {"missing", "no zone in the catalog"});
            continue;
        }
        const QString dir = scratch + "/" + QString::number(++seq);
        QDir().mkpath(dir);
        ActsRequest req;
        req.zone = it.key();
        req.types = type;
        req.names = it.value();
        req.outDir = QDir::toNativeSeparators(dir);
        req.xpaks = true;
        ++outstanding;
        const QStringList zoneNames = it.value();
        const QString zone = it.key();
        ActsRunner::shared()->submit(toolPath, req, &ctx, [&, dir, zone, zoneNames](const ActsResult& r) {
            QHash<QString, QJsonObject> recs;
            QFile journal(dir + "/mw19replay/" + zone + "/assets.jsonl");
            const bool hasJournal = journal.open(QIODevice::ReadOnly);
            if (hasJournal) {
                while (!journal.atEnd()) {
                    const QJsonObject o = QJsonDocument::fromJson(journal.readLine()).object();
                    if (o.value("type").toString() == type) recs.insert(o.value("name").toString(), o);
                }
                journal.close();
            }
            for (const QString& n : zoneNames) {
                const QJsonObject rec = recs.value(n);
                const QString file = rec.value("file").toString();
                if (rec.value("status").toString() == "ok" && !file.isEmpty() &&
                    QFileInfo::exists(dir + "/mw19replay/" + zone + "/" + file))
                    result.insert(n, {"ok", QString()});
                else if (!hasJournal)
                    result.insert(n, {"missing", QString("ACTS wrote no journal (exit %1) %2").arg(r.started ? r.exitCode : -1).arg(r.error)});
                else
                    result.insert(n, {"missing", rec.isEmpty() ? zone + ": not in zone"
                                                               : zone + ": " + QString("%1 %2 %3")
                                                                                   .arg(rec.value("status").toString(), rec.value("reason").toString(),
                                                                                        rec.value("error").toString())
                                                                                   .trimmed()});
            }
            --outstanding;
        });
    }
    waitIdle([&] { return outstanding > 0; }, 60 * 60 * 1000);
    QStringList next;
    for (const QString& n : pending) {
        const auto st = result.value(n);
        if (st.first == "missing" && st.second.endsWith("not in zone") && attempt.value(n) + 1 < zoneOf.value(n).size()) {
            attempt[n] = attempt.value(n) + 1;
            next << n;
        }
    }
    pending = next;
    }
    QStringList lines;
    for (const QString& n : names) {
        const auto st = result.value(n, {"missing", "no answer from ACTS"});
        ++counts[st.first == "ok" ? 0 : 2];
        lines << n + "\t" + st.first + "\t" + clean(st.second);
    }
    return lines;
}

int runAssetAudit(const QString& kind, const QString& outTsv, int limit, int batch, bool reverse) {
    if (kind != "materials" && kind != "models" && kind != "effects" && kind != "images") {
        std::fprintf(stderr, "asset audit: kind must be materials, models, effects or images\n");
        return 2;
    }
    batch = std::max(10, batch);
    QSet<QString> done;
    {
        QFile f(outTsv);
        if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream in(&f);
            while (!in.atEnd()) {
                const QString l = in.readLine();
                if (!l.startsWith('#')) done.insert(l.section('\t', 0, 0));
            }
        }
    }
    QStringList todo;
    QHash<QString, QStringList> zoneOf;
    for (const QString& n : catalogNames(kind, &zoneOf))
        if (!done.contains(n)) todo << n;
    if (reverse) std::reverse(todo.begin(), todo.end());
    if (limit > 0 && todo.size() > limit) todo = todo.mid(0, limit);
    QDir().mkpath(QFileInfo(outTsv).absolutePath());
    QFile out(outTsv);
    if (!out.open(QIODevice::Append | QIODevice::Text)) {
        std::fprintf(stderr, "asset audit: cannot write %s\n", qPrintable(outTsv));
        return 2;
    }
    if (out.size() == 0)
        out.write(QString("# IW8 Radiant stock %1 availability (Replay 1.20): name<TAB>ok|grey|missing<TAB>reason\n").arg(kind).toUtf8());
    const QString scratch = QDir::tempPath() + "/iw8r_asset_audit_" + kind + "_" + QFileInfo(outTsv).completeBaseName();
    std::printf("asset audit %s: %lld already recorded, %lld to check (batch %d)\n", qPrintable(kind), qlonglong(done.size()),
                qlonglong(todo.size()), batch);
    QElapsedTimer total;
    total.start();
    int counts[3] = {0, 0, 0};
    for (int at = 0; at < todo.size(); at += batch) {
        const QStringList names = todo.mid(at, batch);
        QDir(scratch).removeRecursively();
        QDir().mkpath(scratch);
        QStringList lines;
        if (kind == "effects" || kind == "images") {
            lines = auditActsBatch(kind == "effects" ? "vfx" : "image", names, zoneOf, scratch, counts);
        } else if (kind == "materials") {
            qputenv("IW8R_MATERIAL_CACHE_ROOT", scratch.toUtf8());
            auto cache = std::make_unique<MaterialCache>(toolPath);
            cache->request(names);
            waitIdle([&] { return cache->busy(); }, 60 * 60 * 1000);
            for (const QString& n : names) {
                const auto e = cache->lookup(n);
                QString status = "missing", why = "no answer from the cache";
                if (e && !e->failed && !e->colorDds.isEmpty()) status = "ok", why.clear();
                else if (e && !e->failed) status = "grey", why = e->error;
                else if (e) why = e->error;
                ++counts[status == "ok" ? 0 : status == "grey" ? 1 : 2];
                lines << n + "\t" + status + "\t" + clean(why);
            }
        } else {
            qputenv("IW8R_MODEL_CACHE_ROOT", scratch.toUtf8());
            auto cache = std::make_unique<ModelCache>(toolPath);
            cache->request(names);
            waitIdle([&] { return cache->busy(); }, 60 * 60 * 1000);
            for (const QString& n : names) {
                const auto e = cache->lookup(n);
                QString status = "missing", why = "no answer from the cache";
                if (e && !e->failed && !e->glbPath.isEmpty()) status = "ok", why.clear();
                else if (e) why = e->error;
                ++counts[status == "ok" ? 0 : 2];
                lines << n + "\t" + status + "\t" + clean(why);
            }
        }
        out.write((lines.join('\n') + '\n').toUtf8());
        out.flush();
        QDir(scratch).removeRecursively();
        std::printf("  %lld/%lld  ok %d  grey %d  missing %d  (%.0f s)\n", qlonglong(std::min<qsizetype>(at + batch, todo.size())),
                    qlonglong(todo.size()), counts[0], counts[1], counts[2], total.elapsed() / 1000.0);
        std::fflush(stdout);
    }
    return 0;
}

QStringList auditCatalogNames(const QString& kind) { return catalogNames(kind); }
QString auditToolPath(const QString& key) { return toolPath(key); }
void auditWaitIdle(const std::function<bool()>& busy, int timeoutMs) { waitIdle(busy, timeoutMs); }

QHash<QString, QString> auditStatus(const QString& kind) {
    QHash<QString, QString> out;
    const QDir cat(QDir(repoData()).filePath("asset_catalog"));
    for (const QString& fn : cat.entryList({"iw8_" + kind + "_status*.tsv"}, QDir::Files)) {
        QFile f(cat.filePath(fn));
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) continue;
        QTextStream in(&f);
        while (!in.atEnd()) {
            const QString l = in.readLine();
            if (l.startsWith('#')) continue;
            const QStringList c = l.split('	');
            if (c.size() >= 2) out.insert(c[0], c[1]);
        }
    }
    return out;
}

}  // namespace iw8r::qt
