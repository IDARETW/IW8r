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


H = 'src/qtapp/model_cache.h'
C = 'src/qtapp/model_cache.cpp'

patch(H, '''    void requestLod(const QString& name, int lod);''', '''    void requestLod(const QString& name, int lod);
    // Many LODs at once (stock prop LODs, plan 1.13): grouped by zone, up to 48 surfaces per ACTS run. Each answered
    // pair gets lodReady, as requestLod. Pairs already cached, pending or failed are skipped.
    void requestLods(const QList<QPair<QString, int>>& want);
    // Fetches still running or queued by requestLod / requestLods.
    int lodsPending() const { return int(lodPending_.size()); }''')
patch(C, '''std::optional<ImportedModel> ModelCache::loadModelLod(const QString& name, int lod, QString* err) const {''', '''void ModelCache::requestLods(const QList<QPair<QString, int>>& want) {
    // zone -> (surfs name -> (model, lod)), only what can be fetched
    std::map<QString, std::vector<std::tuple<QString, QString, int>>> byZone;
    for (const auto& [name, lod] : want) {
        const QString key = name + "#" + QString::number(lod);
        if (lod <= 0 || hasLod(name, lod) || lodPending_.count(key) || lodFailed_.count(key)) continue;
        const auto e = lookup(name);
        const auto d = loadDetail(name);
        if (!e || e->failed || e->zone.isEmpty() || !d || lod >= int(d->lods.size()) || d->lods[size_t(lod)].surfs.empty()) {
            lodFailed_.insert(key);
            continue;
        }
        lodPending_.insert(key);
        byZone[e->zone].push_back({QString::fromStdString(d->lods[size_t(lod)].surfs), name, lod});
    }
    constexpr size_t kPerRun = 48;
    for (auto& [zone, items] : byZone)
        for (size_t at = 0; at < items.size(); at += kPerRun) {
            const std::vector<std::tuple<QString, QString, int>> part(items.begin() + ptrdiff_t(at),
                                                                      items.begin() + ptrdiff_t(std::min(items.size(), at + kPerRun)));
            const QString dir = QDir::toNativeSeparators(root_ + "/w/lods" + QString::number(++runSeq_));
            QDir().mkpath(dir);
            ActsRequest request;
            request.zone = zone;
            request.types = "xmodelsurfs";
            for (const auto& [surfs, name, lod] : part) request.names << surfs;
            request.outDir = dir;
            request.geometry = true;
            request.xpaks = true;
            emit progress(QString("ACTS xmodelsurfs %1: %2 prop LODs").arg(zone).arg(part.size()));
            ActsRunner::shared()->submit(tools_, request, this, [this, part, dir, zone = zone](const ActsResult& result) {
                std::map<QString, QString> files;  // surfs -> exported GLB
                QFile journal(dir + "/mw19replay/" + zone + "/assets.jsonl");
                if (journal.open(QIODevice::ReadOnly)) {
                    while (!journal.atEnd()) {
                        const QJsonObject o = QJsonDocument::fromJson(journal.readLine()).object();
                        if (o.value("type").toString() != "xmodelsurfs" || o.value("status").toString() != "ok") continue;
                        const QString geo = o.value("geometry_file").toString();
                        const QString file = dir + "/mw19replay/" + zone + "/" + geo;
                        if (!geo.isEmpty() && QFileInfo::exists(file)) files[o.value("name").toString()] = file;
                    }
                    journal.close();
                }
                int ok = 0;
                for (const auto& [surfs, name, lod] : part) {
                    const QString key = name + "#" + QString::number(lod);
                    lodPending_.erase(key);
                    bool got = false;
                    if (const auto f = files.find(surfs); f != files.end()) {
                        QFile::remove(lodGlbPath(name, lod));
                        got = QFile::copy(f->second, lodGlbPath(name, lod));
                    }
                    if (!got) lodFailed_.insert(key);
                    ok += got ? 1 : 0;
                }
                QDir(dir).removeRecursively();
                emit progress(QString("Prop LODs from %1: %2 of %3 (exit %4)").arg(zone).arg(ok).arg(part.size()).arg(result.exitCode));
                for (const auto& [surfs, name, lod] : part) emit lodReady(name, lod);
            });
        }
}

std::optional<ImportedModel> ModelCache::loadModelLod(const QString& name, int lod, QString* err) const {''')
print('ok')
