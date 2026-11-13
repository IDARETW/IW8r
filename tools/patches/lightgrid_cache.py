import io


def patch(p, a, b, n=1):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    if b and s.count(b) >= 1:
        return
    assert s.count(a) == n, (p, s.count(a), a[:80])
    s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


H = 'src/qtapp/world_cache.h'
C = 'src/qtapp/world_cache.cpp'

# ---------------- header
patch(H, '''// .dec size free and never less than 3 GB. lightmapReady(zone) when a zone's queued tile jobs are all done.
#pragma once''', '''// .dec size free and never less than 3 GB. lightmapReady(zone) when a zone's queued tile jobs are all done.
//
// Static-model light grid (core stock_lightgrid.h, research 105): the same main-zone job also makes
//   <zone>/lightgrid.iw8rlg  the LGV block (atlas root, the two probe textures, per-volume packing and transforms),
//                            cut from the .ff.dec right after the box table of the cached gfx_map_trzone export;
//                            lightGridFile(zone). Read it with iw8r::parseStockLgvBlock.
// Missing, with no recorded error, it alone re-runs the job for a zone cached before it existed. A verdict on the data
// (no volumes, no atlas found) is kept as "lightgrid_error". lightGridReady(zone) when the job ends either way.
// Warzone tiles hold their own grids (not extracted yet: their props fall back to the probes).
#pragma once''')
patch(H, '''    QString probesErrorOf(const QString& zone) const;  // why the probe extraction failed ("" when made, pending or never run)
''', '''    QString probesErrorOf(const QString& zone) const;  // why the probe extraction failed ("" when made, pending or never run)
    // The zone's static-model light grid file (<zone cache folder>/lightgrid.iw8rlg), or nullopt when the zone is not
    // cached or it is not made (yet). Does not start a job (requestLightmap does).
    std::optional<QString> lightGridFile(const QString& zone) const;
    QString lightGridErrorOf(const QString& zone) const;  // why it was not made ("" when made, pending or never run)
    // requestLightmap has something to make for this cached zone (a lightmap, probes or the light grid).
    bool needsLightmapJob(const QString& zone) const;
''')
patch(H, '''    void probesReady(const QString& zone);    // probesFile() or probesErrorOf() now answers for this zone
''', '''    void probesReady(const QString& zone);    // probesFile() or probesErrorOf() now answers for this zone
    void lightGridReady(const QString& zone);  // lightGridFile() or lightGridErrorOf() now answers for this zone
''')
patch(H, '''        QString probesError;    // the probe extraction rejected this zone (not tried again)
    };''', '''        QString probesError;    // the probe extraction rejected this zone (not tried again)
        QString lightGridError; // the light grid extraction rejected this zone (not tried again)
    };''')
patch(H, '''    bool needsProbes(const QString& zone) const;
''', '''    bool needsProbes(const QString& zone) const;
    bool needsLightGrid(const QString& zone) const;
''')
patch(H, '''    // One output's scan result (the probes when `probes`, else the lightmap): progress line, and a failure is kept
    // in index.json (persistError) or in lmSkip_. Emits no ready signal.
    void noteExtraction(const QString& zone, bool probes, bool ok, const QString& message, bool persistError);''',
      '''    // One output's scan result (kind 0 the lightmap, 1 the probes, 2 the light grid): progress line, and a failure is
    // kept in index.json (persistError) or in lmSkip_. Emits no ready signal.
    void noteExtraction(const QString& zone, int kind, bool ok, const QString& message, bool persistError);''')

# ---------------- source
patch(C, '''#include "stock_lightmap.h"
#include "stock_probes.h"''', '''#include "stock_lightgrid.h"
#include "stock_lightmap.h"
#include "stock_probes.h"''')
patch(C, '''constexpr const char* kProbesFile = "probes.iw8rprb";
''', '''constexpr const char* kProbesFile = "probes.iw8rprb";
constexpr const char* kLightGridFile = "lightgrid.iw8rlg";
''')
patch(C, '''        e.probesError = o.value("probes_error").toString();
''', '''        e.probesError = o.value("probes_error").toString();
        e.lightGridError = o.value("lightgrid_error").toString();
''')
patch(C, '''        if (!e.probesError.isEmpty()) o.insert("probes_error", e.probesError);
''', '''        if (!e.probesError.isEmpty()) o.insert("probes_error", e.probesError);
        if (!e.lightGridError.isEmpty()) o.insert("lightgrid_error", e.lightGridError);
''')
patch(C, '''bool WorldCache::needsProbes(const QString& zone) const {
    return lookup(zone) && !probesFile(zone) && probesErrorOf(zone).isEmpty();
}
''', '''bool WorldCache::needsProbes(const QString& zone) const {
    return lookup(zone) && !probesFile(zone) && probesErrorOf(zone).isEmpty();
}

std::optional<QString> WorldCache::lightGridFile(const QString& zone) const {
    const auto dir = lookup(zone);
    if (!dir) return std::nullopt;
    const QString f = *dir + "/" + kLightGridFile;
    if (!QFileInfo::exists(f)) return std::nullopt;
    return f;
}

QString WorldCache::lightGridErrorOf(const QString& zone) const {
    const auto it = index_.find(zone);
    return it == index_.end() ? QString() : it->second.lightGridError;
}

bool WorldCache::needsLightGrid(const QString& zone) const {
    return lookup(zone) && !lightGridFile(zone) && lightGridErrorOf(zone).isEmpty();
}

bool WorldCache::needsLightmapJob(const QString& zone) const {
    return needsLightmap(zone) || needsProbes(zone) || needsLightGrid(zone);
}
''')
patch(C, '''    if (z.isEmpty() || (!needsLightmap(z) && !needsProbes(z)) || lmSkip_.contains(z) || lmQueue_.contains(z) || z == lmZone_)''',
      '''    if (z.isEmpty() || !needsLightmapJob(z) || lmSkip_.contains(z) || lmQueue_.contains(z) || z == lmZone_)''')
patch(C, '''    if (!needsLightmap(zone) && !needsProbes(zone)) return startNextLightmap();''',
      '''    if (!needsLightmapJob(zone)) return startNextLightmap();''')
patch(C, '''            emit lightmapReady(zone);
            emit probesReady(zone);
            return;''', '''            emit lightmapReady(zone);
            emit probesReady(zone);
            emit lightGridReady(zone);
            return;''')
# the main-zone scan: the light grid too
patch(C, '''    const bool wantLm = needsLightmap(zone), wantPr = needsProbes(zone);
    const QString gfxJson = QString::fromStdString(iw8r::findStockGfxMapJson(dir->toStdString()));
    QPointer<WorldCache> self(this);
    emit progress("Reading the " + zone + (wantLm && wantPr ? " lightmap and reflection probes..." : wantLm ? " lightmap..." : " reflection probes..."));
    std::thread([self, zone, scratch, dec, zoneDir, prTarget, wantLm, wantPr, gfxJson] {''',
      '''    const bool wantLm = needsLightmap(zone), wantPr = needsProbes(zone), wantLg = needsLightGrid(zone);
    const QString gfxJson = QString::fromStdString(iw8r::findStockGfxMapJson(dir->toStdString()));
    const QString lgTarget = *dir + "/" + kLightGridFile;
    const QString trzoneJson = *dir + "/assets/gfx_map_trzone/" + zone + ".0.asset.json";
    QPointer<WorldCache> self(this);
    QStringList what;
    if (wantLm) what << "lightmap";
    if (wantPr) what << "reflection probes";
    if (wantLg) what << "prop light grid";
    emit progress("Reading the " + zone + " " + what.join(", ") + "...");
    std::thread([self, zone, scratch, dec, zoneDir, prTarget, wantLm, wantPr, wantLg, gfxJson, lgTarget, trzoneJson] {''')
patch(C, '''        QDir(scratch).removeRecursively();  // every .dec, before the cache files are written
        const bool lmPersist = wantLm && !lmOk && verdict(lmErr);''', '''        // The static-model light grid (research 105): the box table from the trzone export, the atlas from the .dec.
        std::string lgBlock, lgErr;
        bool lgOk = false, lgPersist = false;
        size_t lgVolumes = 0;
        if (wantLg) {
            QFile tf(trzoneJson);
            if (!tf.open(QIODevice::ReadOnly)) {
                lgErr = "no gfx_map_trzone export at " + trzoneJson.toStdString();
                lgPersist = true;  // the export is what it is until the zone is exported again
            } else {
                const QByteArray text = tf.readAll();
                tf.close();
                std::vector<iw8r::StockLgvBox> boxes;
                std::vector<uint8_t> raw;
                if (!iw8r::parseStockLgvBoxes(text.toStdString(), boxes, &raw, &lgErr)) {
                    lgPersist = true;
                } else if (boxes.empty()) {
                    lgErr = "no static model light grid volumes";
                    lgPersist = true;
                } else {
                    lgVolumes = boxes.size();
                    lgOk = iw8r::extractStockLgvBlockFromFile(dec.toStdString(), raw, boxes.size(), lgBlock, &lgErr);
                    lgPersist = !lgOk && lgErr.find("open") == std::string::npos;  // not found: a verdict; unreadable: not
                }
            }
        }
        QDir(scratch).removeRecursively();  // every .dec, before the cache files are written
        QString lgSummary;
        if (lgOk) {
            lgSummary = QString("%1 volumes, %2 KB").arg(lgVolumes).arg(qint64(lgBlock.size()) / 1024);
            QSaveFile out(lgTarget);
            if (!out.open(QIODevice::WriteOnly) || out.write(lgBlock.data(), qint64(lgBlock.size())) != qint64(lgBlock.size()) ||
                !out.commit()) {
                lgOk = false;  // disk full or similar: not a verdict
                lgErr = "could not write " + lgTarget.toStdString();
            }
        }
        const bool lmPersist = wantLm && !lmOk && verdict(lmErr);''')
patch(C, '''        QMetaObject::invokeMethod(qApp, [self, zone, wantLm, wantPr, lmOk, prOk, lmPersist, prPersist, lmSummary, prSummary,
                                         lmErr, prErr] {
            if (!self) return;
            if (wantLm) self->noteExtraction(zone, false, lmOk, lmOk ? lmSummary : QString::fromStdString(lmErr), lmPersist);
            if (wantPr) self->noteExtraction(zone, true, prOk, prOk ? prSummary : QString::fromStdString(prErr), prPersist);
            self->lmZone_.clear();
            self->lmScratch_.clear();
            emit self->lightmapReady(zone);
            emit self->probesReady(zone);''', '''        QMetaObject::invokeMethod(qApp, [self, zone, wantLm, wantPr, wantLg, lmOk, prOk, lgOk, lmPersist, prPersist, lgPersist,
                                         lmSummary, prSummary, lgSummary, lmErr, prErr, lgErr] {
            if (!self) return;
            if (wantLm) self->noteExtraction(zone, 0, lmOk, lmOk ? lmSummary : QString::fromStdString(lmErr), lmPersist);
            if (wantPr) self->noteExtraction(zone, 1, prOk, prOk ? prSummary : QString::fromStdString(prErr), prPersist);
            if (wantLg) self->noteExtraction(zone, 2, lgOk, lgOk ? lgSummary : QString::fromStdString(lgErr), lgPersist);
            self->lmZone_.clear();
            self->lmScratch_.clear();
            emit self->lightmapReady(zone);
            emit self->probesReady(zone);
            emit self->lightGridReady(zone);''')
patch(C, '''void WorldCache::noteExtraction(const QString& zone, bool probes, bool ok, const QString& message, bool persistError) {
    const qint64 ms = lmTimer_.isValid() ? lmTimer_.elapsed() : 0;
    const QString what = probes ? "reflection probes" : "lightmap";''', '''void WorldCache::noteExtraction(const QString& zone, int kind, bool ok, const QString& message, bool persistError) {
    const qint64 ms = lmTimer_.isValid() ? lmTimer_.elapsed() : 0;
    const QString what = kind == 2 ? "prop light grid" : kind == 1 ? "reflection probes" : "lightmap";''')
patch(C, '''            (probes ? it->second.probesError : it->second.lightmapError) = message;''',
      '''            (kind == 2 ? it->second.lightGridError : kind == 1 ? it->second.probesError : it->second.lightmapError) = message;''')
patch(C, '''                if (needsProbes(zone)) it->second.probesError = error;
''', '''                if (needsProbes(zone)) it->second.probesError = error;
                if (needsLightGrid(zone)) it->second.lightGridError = error;
''')
patch(C, '''    emit lightmapReady(zone);
    emit probesReady(zone);
    startNextLightmap();''', '''    emit lightmapReady(zone);
    emit probesReady(zone);
    emit lightGridReady(zone);
    startNextLightmap();''')
print('ok')
