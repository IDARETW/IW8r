#include "thumb_pack.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QImage>
#include <QStandardPaths>

#include <cstdio>
#include <memory>

#include "asset_audit.h"
#include "material_cache.h"
#include "model_cache.h"
#include "model_preview.h"
#include "texture_decode.h"
#include "thumb_store.h"

namespace iw8r::qt {

int runThumbPack(const QString& kind, int limit, int batch) {
    if (kind != "materials" && kind != "models") {
        std::fprintf(stderr, "thumb pack: kind must be materials or models\n");
        return 2;
    }
    batch = std::max(10, batch);
    const QString thumbDir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/thumbs";
    QDir().mkpath(thumbDir);
    ThumbStore pack(thumbDir + "/" + kind + ".full.pack");
    const QHash<QString, QString> status = auditStatus(kind);
    QStringList todo;
    int skipped = 0;
    for (const QString& n : auditCatalogNames(kind)) {
        if (pack.contains(n)) continue;
        const QString st = status.value(n);
        if (st == "missing" || st == "grey") {
            ++skipped;
            continue;
        }
        todo << n;
    }
    if (limit > 0 && todo.size() > limit) todo = todo.mid(0, limit);
    std::printf("thumb pack %s: %d in the pack, %d not buildable skipped, %lld to make (batch %d)\n", qPrintable(kind), pack.count(),
                skipped, qlonglong(todo.size()), batch);
    const QString scratch = QDir::tempPath() + "/iw8r_thumb_pack_" + kind;
    QElapsedTimer total;
    total.start();
    int made = 0, failed = 0;
    for (int at = 0; at < todo.size(); at += batch) {
        const QStringList names = todo.mid(at, batch);
        QDir(scratch).removeRecursively();
        QDir().mkpath(scratch);
        if (kind == "materials") {
            qputenv("IW8R_MATERIAL_CACHE_ROOT", scratch.toUtf8());
            auto cache = std::make_unique<MaterialCache>(auditToolPath);
            cache->request(names);
            auditWaitIdle([&] { return cache->busy(); }, 60 * 60 * 1000);
            for (const QString& n : names) {
                const auto e = cache->lookup(n);
                app::Rgba8Image small;
                if (e && !e->failed && !e->colorDds.isEmpty() && app::loadDdsPreview(e->colorDds.toStdString(), 96, small)) {
                    // Signature 1: a full-pack record (no source file); the browser reads it with getAny. Same look
                    // as the browser: colour only (stock alpha is often specular).
                    const QImage q = QImage(small.pixels.data(), small.width, small.height, small.width * 4, QImage::Format_RGBA8888)
                                         .convertToFormat(QImage::Format_RGB32);
                    pack.put(n, 1, q);
                    ++made;
                } else {
                    ++failed;
                }
            }
        } else {
            qputenv("IW8R_MODEL_CACHE_ROOT", scratch.toUtf8());
            auto cache = std::make_unique<ModelCache>(auditToolPath);
            cache->request(names);
            auditWaitIdle([&] { return cache->busy(); }, 60 * 60 * 1000);
            for (const QString& n : names) {
                const auto m = cache->loadModel(n);
                const QImage img = m ? renderModelPreview(*m, 96) : QImage();
                if (!img.isNull()) {
                    pack.put(n, 1, img);
                    ++made;
                } else {
                    ++failed;
                }
            }
        }
        QDir(scratch).removeRecursively();
        std::printf("  %lld/%lld  made %d  failed %d  pack %d (%.1f MB)  (%.0f s)\n", qlonglong(std::min<qsizetype>(at + batch, todo.size())),
                    qlonglong(todo.size()), made, failed, pack.count(), pack.fileBytes() / 1e6, total.elapsed() / 1000.0);
        std::fflush(stdout);
    }
    return 0;
}

}  // namespace iw8r::qt
