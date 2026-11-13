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

patch(H, '''// Warzone tiles hold their own grids (not extracted yet: their props fall back to the probes).''',
      '''// Warzone tiles hold their own grids: the tile job (below) also writes
//   <zone>/streamed/<tile>/lightgrid.iw8rlg  the tile's block (same format), for the props whose worldTransient is the
//                                           tile's transientZoneIndex
//   <zone>/streamed/<tile>/lightgrid.done    "ok <transientZoneIndex> <volumes>", or "error <why>" (a verdict, not
//                                           tried again); tileLightGrids(zone) lists the made ones.''')
patch(H, '''    // requestLightmap has something to make for this cached zone (a lightmap, probes or the light grid).
    bool needsLightmapJob(const QString& zone) const;''', '''    // requestLightmap has something to make for this cached zone (a lightmap, probes or the light grid).
    bool needsLightmapJob(const QString& zone) const;
    // The streamed tiles' light grid files of a zone: transientZoneIndex -> lightgrid.iw8rlg path. Starts no job.
    std::map<int, QString> tileLightGrids(const QString& zone) const;''')
patch(C, '''constexpr const char* kTileLightmapDone = "lightmap.done";''', '''constexpr const char* kTileLightmapDone = "lightmap.done";
constexpr const char* kTileLightGridDone = "lightgrid.done";''')
patch(C, '''bool WorldCache::needsLightmapJob(const QString& zone) const {''', '''std::map<int, QString> WorldCache::tileLightGrids(const QString& zone) const {
    std::map<int, QString> out;
    const auto dir = lookup(zone);
    if (!dir) return out;
    for (const QFileInfo& t : QDir(*dir + "/streamed").entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        QFile f(t.absoluteFilePath() + "/" + kTileLightGridDone);
        if (!f.open(QIODevice::ReadOnly)) continue;
        const QStringList w = QString::fromUtf8(f.readLine()).simplified().split(' ');
        bool ok = false;
        const int k = w.size() >= 2 && w[0] == "ok" ? w[1].toInt(&ok) : -1;
        const QString file = t.absoluteFilePath() + "/" + kLightGridFile;
        if (ok && k > 0 && QFileInfo::exists(file)) out[k] = file;
    }
    return out;
}

bool WorldCache::needsLightmapJob(const QString& zone) const {''')
patch(C, '''    if (!dir || QFileInfo::exists(*dir + "/streamed/" + tile + "/" + kTileLightmapDone) || lmSkip_.contains(zone + "/" + tile) ||
        (zone == lmZone_ && tile == lmTile_))
        return;''', '''    // Both the tile's lightmaps and its light grid have their verdict: nothing to do.
    if (!dir ||
        (QFileInfo::exists(*dir + "/streamed/" + tile + "/" + kTileLightmapDone) &&
         QFileInfo::exists(*dir + "/streamed/" + tile + "/" + kTileLightGridDone)) ||
        lmSkip_.contains(zone + "/" + tile) || (zone == lmZone_ && tile == lmTile_))
        return;''')
# the tile scan: lightmaps when missing, the light grid when missing
patch(C, '''        QPointer<WorldCache> self(this);
        emit progress("Reading the " + tile + " lightmap...");
        std::thread([self, zone, tile, scratch, dec, dir = *dir] {
            std::vector<iw8r::StockLightmap> lms;
            std::string e;
            bool ok = iw8r::extractStockLightmapsFromDecompressedZone(dec.toStdString(), lms, &e);
            QDir(scratch).removeRecursively();  // the .dec, before the cache files are written''', '''        QPointer<WorldCache> self(this);
        const QString tileDir = *dir + "/streamed/" + tile;
        const bool wantLm = !QFileInfo::exists(tileDir + "/" + kTileLightmapDone);
        const bool wantLg = !QFileInfo::exists(tileDir + "/" + kTileLightGridDone);
        emit progress("Reading the " + tile + (wantLm ? " lightmap..." : " prop light grid..."));
        std::thread([self, zone, tile, scratch, dec, dir = *dir, tileDir, wantLm, wantLg] {
            // The tile's static-model light grid (research 105 section 5.1): its own box table and atlas, for the props
            // whose worldTransient is this tile's transientZoneIndex. Its verdict goes to lightgrid.done.
            if (wantLg) {
                QString trzone;
                for (QDirIterator it(tileDir + "/assets/gfx_map_trzone", {"*.json"}, QDir::Files); it.hasNext();) {
                    trzone = it.next();
                    break;
                }
                std::string lgErr, block;
                int transient = -1;
                bool verdict = true;
                size_t volumes = 0;
                QFile tf(trzone);
                if (trzone.isEmpty() || !tf.open(QIODevice::ReadOnly)) {
                    lgErr = "no gfx_map_trzone export in the tile";
                } else {
                    const QByteArray text = tf.readAll();
                    tf.close();
                    static const QRegularExpression tzi("\\"transientZoneIndex\\"\\\\s*:\\\\s*(\\\\d+)");
                    const auto m = tzi.match(QString::fromLatin1(text.left(4096)));
                    if (m.hasMatch()) transient = m.captured(1).toInt();
                    std::vector<iw8r::StockLgvBox> boxes;
                    std::vector<uint8_t> raw;
                    if (transient <= 0) {
                        lgErr = "the tile export has no transientZoneIndex";
                    } else if (!iw8r::parseStockLgvBoxes(text.toStdString(), boxes, &raw, &lgErr)) {
                        // a verdict on the export
                    } else if (boxes.empty()) {
                        lgErr = "no static model light grid volumes";
                    } else if (!iw8r::extractStockLgvBlockFromFile(dec.toStdString(), raw, boxes.size(), block, &lgErr)) {
                        verdict = lgErr.find("not found") != std::string::npos || lgErr.find("runs past") != std::string::npos;
                    } else {
                        volumes = boxes.size();
                    }
                }
                bool written = false;
                if (volumes > 0) {
                    QSaveFile out(tileDir + "/" + kLightGridFile);
                    written = out.open(QIODevice::WriteOnly) && out.write(block.data(), qint64(block.size())) == qint64(block.size()) && out.commit();
                    if (!written) verdict = false;  // disk full or similar: tried again in the next session
                }
                if (written || verdict) {
                    QSaveFile f(tileDir + "/" + kTileLightGridDone);
                    if (f.open(QIODevice::WriteOnly)) {
                        f.write((written ? QString("ok %1 %2\\n").arg(transient).arg(volumes) : "error " + QString::fromStdString(lgErr) + "\\n").toUtf8());
                        f.commit();
                    }
                }
            }
            if (!wantLm) {
                QDir(scratch).removeRecursively();
                QMetaObject::invokeMethod(qApp, [self, zone, tile] {
                    if (self) self->endTileLightmap(zone, tile, true, "light grid only", false, true);
                });
                return;
            }
            std::vector<iw8r::StockLightmap> lms;
            std::string e;
            bool ok = iw8r::extractStockLightmapsFromDecompressedZone(dec.toStdString(), lms, &e);
            QDir(scratch).removeRecursively();  // the .dec, before the cache files are written''')
patch(C, '''void WorldCache::endTileLightmap(const QString& zone, const QString& tile, bool ok, const QString& message, bool persist) {
    const qint64 ms = lmTimer_.isValid() ? lmTimer_.elapsed() : 0;
    if (persist) {''', '''void WorldCache::endTileLightmap(const QString& zone, const QString& tile, bool ok, const QString& message, bool persist,
                                 bool lightGridOnly) {
    const qint64 ms = lmTimer_.isValid() ? lmTimer_.elapsed() : 0;
    if (lightGridOnly) {
        // A tile that already had its lightmaps: only its light grid was read (its lightgrid.done says how it went).
    } else if (persist) {''')
patch(C, '''    bool more = false;
    for (const auto& q : tileLmQueue_) more |= q.first == zone;
    if (!more) emit lightmapReady(zone);
    startNextLightmap();''', '''    bool more = false;
    for (const auto& q : tileLmQueue_) more |= q.first == zone;
    if (!more) emit lightmapReady(zone);
    emit lightGridReady(zone);  // the tile's light grid (tileLightGrids) may be new
    startNextLightmap();''')
patch(H, '''    void endTileLightmap(const QString& zone, const QString& tile, bool ok, const QString& message, bool persist);''',
      '''    void endTileLightmap(const QString& zone, const QString& tile, bool ok, const QString& message, bool persist,
                         bool lightGridOnly = false);''')
patch(C, '''    startDecompress(ff, QString("ACTS decompress %1 for its lightmap (%2 more)").arg(tile).arg(tileLmQueue_.size()));''',
      '''    startDecompress(ff, QString("ACTS decompress %1 for its lightmap and prop light grid (%2 more)").arg(tile).arg(tileLmQueue_.size()));''')
print('ok')
