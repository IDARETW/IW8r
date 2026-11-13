import io


def patch(p, a, b, n=1):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


H = 'src/qtapp/world_cache.h'
C = 'src/qtapp/world_cache.cpp'

patch(H, '''    // requestLightmap has something to make for this cached zone (a lightmap, probes or the light grid).
    bool needsLightmapJob(const QString& zone) const;''', '''    // requestLightmap has something to make for this cached zone (a lightmap, probes or the light grid).
    bool needsLightmapJob(const QString& zone) const;
    // Warzone reflection probes (research 108): the main job writes every record and the zone-0 images into
    // probes.iw8rprb (v2); the shared probes' images (streamed from the XPaks) come next through ACTS, batches of 450
    // names, merged into the same file (<zone>/probes_shared.done when finished, probesReady then). Each tile job cuts
    // the tile's own images into <zone>/streamed/<tile>/probes.iw8rprb (<tile>/probes.done: "ok N" / "none" / "error").
    void requestSharedProbes(const QString& zone);
    QStringList tileProbeFiles(const QString& zone) const;''')
patch(H, '''    QSet<QString> lmSkip_;     // ACTS or disk failures of this session (zone, or zone/tile): not tried again until the next start''',
      '''    QSet<QString> lmSkip_;     // ACTS or disk failures of this session (zone, or zone/tile): not tried again until the next start
    QSet<QString> sharedProbesAsked_;  // requestSharedProbes this session''')

patch(C, '''constexpr const char* kTileLightGridDone = "lightgrid.done";''', '''constexpr const char* kTileLightGridDone = "lightgrid.done";
constexpr const char* kTileProbesDone = "probes.done";
constexpr const char* kSharedProbesDone = "probes_shared.done";''')
# old probe verdicts ("does not match", "no array image") were the Warzone layout, decoded now: asked again once
patch(C, '''        e.probesError = o.value("probes_error").toString();''', '''        e.probesError = o.value("probes_error").toString();
        if (o.value("probeRules").toInt() < 2) e.probesError.clear();  // 2: the Warzone layout (research 108) is read''')
patch(C, '''        if (!e.probesError.isEmpty()) o.insert("probes_error", e.probesError);''', '''        if (!e.probesError.isEmpty()) o.insert("probes_error", e.probesError);
        o.insert("probeRules", 2);''')
patch(C, '''std::map<int, QString> WorldCache::tileLightGrids(const QString& zone) const {''', '''QStringList WorldCache::tileProbeFiles(const QString& zone) const {
    QStringList out;
    const auto dir = lookup(zone);
    if (!dir) return out;
    for (const QFileInfo& t : QDir(*dir + "/streamed").entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot))
        if (const QString f = t.absoluteFilePath() + "/" + kProbesFile; QFileInfo::exists(f)) out << f;
    return out;
}

void WorldCache::requestSharedProbes(const QString& zone) {
    const auto dir = lookup(zone);
    if (!dir || sharedProbesAsked_.contains(zone) || QFileInfo::exists(*dir + "/" + kSharedProbesDone)) return;
    const QString mainFile = *dir + "/" + kProbesFile;
    iw8r::StockProbeSet rec;
    if (!iw8r::readStockProbeCacheRecords(mainFile.toStdString(), rec) || !rec.streamed) return;
    sharedProbesAsked_.insert(zone);
    QStringList names;
    for (size_t i = 0; i < rec.probes.size(); ++i)
        if (iw8r::stockProbeIsShared(rec.probes[i].imageIndirection)) names << QString::fromStdString(iw8r::stockProbeSharedImageName(rec, int(i)));
    if (names.isEmpty()) return;
    // One ACTS run per 450 names (research 108 section 4.2: 460 names took 1.4 s), then one merge.
    const QString scratch = root_ + "/w/probes_" + zone;
    QDir(scratch).removeRecursively();
    QDir().mkpath(scratch);
    auto pending = std::make_shared<int>(0);
    QPointer<WorldCache> self(this);
    emit progress(QString("ACTS: %1 shared reflection probe images of %2").arg(names.size()).arg(zone));
    for (int at = 0, part = 0; at < names.size(); at += 450, ++part) {
        ActsRequest request;
        request.zone = zone;
        request.types = "image";
        request.names = names.mid(at, 450);
        request.outDir = QDir::toNativeSeparators(scratch + "/" + QString::number(part));
        request.xpaks = true;
        ++*pending;
        ActsRunner::shared()->submit(tools_, request, this, [self, zone, scratch, mainFile, pending, dir = *dir](const ActsResult&) {
            if (--*pending > 0 || !self) return;
            emit self->progress("Reading the shared reflection probe images of " + zone + "...");
            std::thread([self, zone, scratch, mainFile, dir] {
                iw8r::StockProbeSet set;
                std::string err;
                size_t got = 0;
                bool ok = iw8r::readStockProbeCache(mainFile.toStdString(), set, &err, false);
                if (ok) {
                    // The journal names each export's file ("*" names are encoded); the probe index ends the name.
                    for (QDirIterator it(scratch, {"assets.jsonl"}, QDir::Files, QDirIterator::Subdirectories); it.hasNext();) {
                        QFile j(it.next());
                        if (!j.open(QIODevice::ReadOnly)) continue;
                        const QString base = QFileInfo(j.fileName()).absolutePath();
                        while (!j.atEnd()) {
                            const QJsonObject o = QJsonDocument::fromJson(j.readLine()).object();
                            if (o.value("status").toString() != "ok") continue;
                            const QString name = o.value("name").toString();
                            bool num = false;
                            const int probe = name.section('_', -1).toInt(&num);
                            if (!num || probe < 0 || size_t(probe) >= set.probes.size()) continue;
                            std::vector<uint8_t> bc6h;
                            if (!iw8r::readStockProbeDds((base + "/" + o.value("file").toString()).toStdString(), bc6h)) continue;
                            if (set.size == 0) set.size = iw8r::kStockProbeImageSize, set.mipCount = iw8r::kStockProbeImageMips;
                            set.probes[size_t(probe)].bc6h = std::move(bc6h);
                            ++got;
                        }
                    }
                    ok = got > 0 && iw8r::writeStockProbeCache(mainFile.toStdString(), set, &err);
                }
                QDir(scratch).removeRecursively();
                if (ok) {
                    QSaveFile done(dir + "/" + kSharedProbesDone);
                    if (done.open(QIODevice::WriteOnly)) {
                        done.write(QString("ok %1\\n").arg(got).toUtf8());
                        done.commit();
                    }
                }
                QMetaObject::invokeMethod(qApp, [self, zone, ok, got, err] {
                    if (!self) return;
                    emit self->progress(ok ? QString("world cache: %1 shared reflection probe images of %2 cached").arg(got).arg(zone)
                                           : "world cache: shared reflection probes of " + zone + " not read: " + QString::fromStdString(err));
                    emit self->probesReady(zone);
                });
            }).detach();
        });
    }
}

std::map<int, QString> WorldCache::tileLightGrids(const QString& zone) const {''')
# the main job: the Warzone layout
patch(C, '''            } else if (!iw8r::readStockProbeRecords(gfxJson.toStdString(), probes, &prErr)) {
                prPersist = verdict(prErr);  // a map without probes is a verdict
            } else {
                prOk = iw8r::extractStockProbeImages(dec.toStdString(), probes, &prErr);
                prPersist = !prOk && verdict(prErr);
            }''', '''            } else if (!iw8r::readStockProbeRecords(gfxJson.toStdString(), probes, &prErr)) {
                prPersist = verdict(prErr);  // a map without probes is a verdict
            } else if (probes.streamed) {
                // Warzone (research 108): every record and the main zone's own images now; the shared images through ACTS
                // next (requestSharedProbes), each tile's with its tile job.
                iw8r::StockProbeTransientZone tz;
                iw8r::StockProbeImages imgs;
                if (!iw8r::readStockProbeTransientZone(trzoneJson.toStdString(), tz, &prErr)) {
                    prPersist = verdict(prErr);
                } else if (!iw8r::extractStockProbeZoneImages(dec.toStdString(), tz, probes, imgs, &prErr)) {
                    prPersist = verdict(prErr);
                } else {
                    prOk = iw8r::mergeStockProbeImages(probes, imgs, &prErr);
                    if (probes.size == 0) probes.size = iw8r::kStockProbeImageSize, probes.mipCount = iw8r::kStockProbeImageMips;
                }
            } else {
                prOk = iw8r::extractStockProbeImages(dec.toStdString(), probes, &prErr);
                prPersist = !prOk && verdict(prErr);
            }''')
patch(C, '''            prSummary = QString("%1 probes, %2 boxes, %3x%3 BC6H, %4 levels")
                            .arg(probes.probes.size())
                            .arg(probes.boxes.size())
                            .arg(probes.size)
                            .arg(probes.mipCount);''', '''            prSummary = QString("%1 probes, %2 boxes, %3x%3 BC6H, %4 levels")
                            .arg(probes.probes.size())
                            .arg(probes.boxes.size())
                            .arg(probes.size)
                            .arg(probes.mipCount);
            if (probes.streamed) prSummary += QString(" (Warzone: %1 shared images next, the rest with the tiles)").arg(probes.sharedCount);''')
patch(C, '''            if (wantPr) self->noteExtraction(zone, 1, prOk, prOk ? prSummary : QString::fromStdString(prErr), prPersist);''',
      '''            if (wantPr) self->noteExtraction(zone, 1, prOk, prOk ? prSummary : QString::fromStdString(prErr), prPersist);
            if (wantPr && prOk) self->requestSharedProbes(zone);  // Warzone only (it checks)''')
# the tile job: the tile's own images
patch(C, '''        const bool wantLg = !QFileInfo::exists(tileDir + "/" + kTileLightGridDone);
        emit progress("Reading the " + tile + (wantLm ? " lightmap..." : " prop light grid..."));
        std::thread([self, zone, tile, scratch, dec, dir = *dir, tileDir, wantLm, wantLg] {''', '''        const bool wantLg = !QFileInfo::exists(tileDir + "/" + kTileLightGridDone);
        // The tile's own reflection probe images, once the main file holds the records (research 108 section 4.2).
        const QString mainProbes = *dir + "/" + kProbesFile;
        const bool wantPr = !QFileInfo::exists(tileDir + "/" + kTileProbesDone) && QFileInfo::exists(mainProbes);
        emit progress("Reading the " + tile + (wantLm ? " lightmap..." : " prop light grid and probes..."));
        std::thread([self, zone, tile, scratch, dec, dir = *dir, tileDir, wantLm, wantLg, wantPr, mainProbes] {
            if (wantPr) {
                QString trzone;
                for (QDirIterator it(tileDir + "/assets/gfx_map_trzone", {"*.json"}, QDir::Files); it.hasNext();) {
                    trzone = it.next();
                    break;
                }
                iw8r::StockProbeTransientZone tz;
                iw8r::StockProbeSet rec;
                iw8r::StockProbeImages imgs;
                std::string prErr;
                QString verdictText;
                if (trzone.isEmpty() || !iw8r::readStockProbeTransientZone(trzone.toStdString(), tz, &prErr)) {
                    verdictText = "error " + QString::fromStdString(prErr.empty() ? std::string("no gfx_map_trzone export") : prErr);
                } else if (!tz.hasProbes || tz.exclusiveCount == 0) {
                    verdictText = "none";
                } else if (!iw8r::readStockProbeCacheRecords(mainProbes.toStdString(), rec, &prErr) || !rec.streamed) {
                    verdictText.clear();  // the records are not there yet: asked again later
                } else if (!iw8r::extractStockProbeZoneImages(dec.toStdString(), tz, rec, imgs, &prErr)) {
                    verdictText = "error " + QString::fromStdString(prErr);
                } else if (iw8r::writeStockProbeTileCache((tileDir + "/" + kProbesFile).toStdString(), imgs, &prErr)) {
                    verdictText = QString("ok %1").arg(imgs.probes.size());
                }
                if (!verdictText.isEmpty()) {
                    QSaveFile f(tileDir + "/" + kTileProbesDone);
                    if (f.open(QIODevice::WriteOnly)) {
                        f.write((verdictText + "\\n").toUtf8());
                        f.commit();
                    }
                }
            }''')
patch(C, '''    // Both the tile's lightmaps and its light grid have their verdict: nothing to do.
    if (!dir ||
        (QFileInfo::exists(*dir + "/streamed/" + tile + "/" + kTileLightmapDone) &&
         QFileInfo::exists(*dir + "/streamed/" + tile + "/" + kTileLightGridDone)) ||''', '''    // The tile's lightmaps, its light grid and (once the main file holds the records) its probe images have their
    // verdict: nothing to do.
    const bool probesDue = dir && QFileInfo::exists(*dir + "/" + kProbesFile) &&
                           !QFileInfo::exists(*dir + "/streamed/" + tile + "/" + kTileProbesDone);
    if (!dir ||
        (QFileInfo::exists(*dir + "/streamed/" + tile + "/" + kTileLightmapDone) &&
         QFileInfo::exists(*dir + "/streamed/" + tile + "/" + kTileLightGridDone) && !probesDue) ||''')
patch(C, '''    emit lightGridReady(zone);  // the tile's light grid (tileLightGrids) may be new
    startNextLightmap();''', '''    emit lightGridReady(zone);  // the tile's light grid (tileLightGrids) may be new
    emit probesReady(zone);     // and its probe images (tileProbeFiles)
    startNextLightmap();''')
print('ok')
