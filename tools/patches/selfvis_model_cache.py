import io

# Model cache, SELFVIS (research 133 section 7): an entry records whether its GLB carries _SELFVIS (index key
# "selfvis": 1 has, 0 checked without; absent = cached before the ACTS fork 3667413c). refreshGeometry(names) runs the
# geometry step again for such entries (the xmodel tables are known), keeping the old entry until the new GLB is in
# and when the export fails; the model's LOD GLBs are removed after a refresh so they are fetched again with it.

ROOT = 'E:/IW8-Radiant/'


def patch(p, a, b, count=1):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == count, (p, s.count(a), a[:70])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


H = 'src/qtapp/model_cache.h'
patch(H, '''        bool failed = false;
        QString error;
    };''', '''        bool failed = false;
        QString error;
        // The GLB's per-vertex self visibility (_SELFVIS, ACTS fork 3667413c; research 105 section 4): 1 present,
        // 0 checked and absent, -1 not known (cached before it was recorded: refreshGeometry fetches it again).
        int selfVis = -1;
    };''')
patch(H, '''    void request(const QStringList& names);                  // async; de-duplicated; batched per zone''',
      '''    void request(const QStringList& names);                  // async; de-duplicated; batched per zone
    // Runs the geometry export again for cached models whose GLB predates the self visibility (Entry::selfVis -1),
    // once per model and session, after any requested models. The old GLB stays in use until the new one is in, and
    // stays when the export fails. modelReady / batchEnded follow as for request().
    void refreshGeometry(const QStringList& names);
    int refreshPending() const { return int(refreshPending_.size()); }''')
patch(H, '''    void startBatch();
    void buildTableRound();''', '''    void startBatch();
    void startRefreshBatch();
    void buildTableRound();''')
patch(H, '''    std::set<QString> refetched_;  // refetch() ran for these (never twice: a model whose detail cannot be read)''',
      '''    std::set<QString> refetched_;  // refetch() ran for these (never twice: a model whose detail cannot be read)
    QStringList refreshPending_;       // refreshGeometry, not yet in a batch
    std::set<QString> refreshed_;      // refreshGeometry took these (once per session)
    bool refreshBatch_ = false;        // the running batch refreshes geometry (a failure keeps the old entry)''')

C = 'src/qtapp/model_cache.cpp'
# index read / write
patch(C, '''        e.failed = o.value("failed").toBool();
        e.error = o.value("error").toString();
        if (!e.failed) {''', '''        e.failed = o.value("failed").toBool();
        e.error = o.value("error").toString();
        e.selfVis = o.contains("selfvis") ? o.value("selfvis").toInt() : -1;
        if (!e.failed) {''')
patch(C, '''        o.insert("failed", e.failed);
        if (!e.error.isEmpty()) o.insert("error", e.error);''', '''        o.insert("failed", e.failed);
        if (!e.error.isEmpty()) o.insert("error", e.error);
        if (e.selfVis >= 0) o.insert("selfvis", e.selfVis);''')
# detection helper + refresh entry points, before request()
patch(C, '''void ModelCache::request(const QStringList& names) {''', '''namespace {
// Whether a cached GLB's JSON chunk names the _SELFVIS attribute (the ACTS fork's self visibility).
int glbSelfVis(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return 0;
    const QByteArray head = f.read(20);
    if (head.size() < 20 || !head.startsWith("glTF")) return 0;
    uint32_t len = 0;
    std::memcpy(&len, head.constData() + 12, 4);
    if (len == 0 || len > (64u << 20)) return 0;
    return f.read(qint64(len)).contains("\\"_SELFVIS\\"") ? 1 : 0;
}
}  // namespace

void ModelCache::refreshGeometry(const QStringList& names) {
    bool added = false;
    for (const QString& n : names) {
        const auto it = index_.find(n);
        if (it == index_.end() || it->second.failed || it->second.selfVis >= 0 || queued_.count(n) || !refreshed_.insert(n).second) continue;
        refreshPending_ << n;
        added = true;
    }
    if (added && !running_) startRefreshBatch();
}

void ModelCache::startRefreshBatch() {
    if (refreshPending_.isEmpty()) return;
    running_ = true;
    refreshBatch_ = true;
    works_.clear();
    queue_.clear();
    phase_ = Phase::Geometry;  // the tables are known: only the surface buffers again
    for (const QString& f : {tools_("acts"), tools_("replay"), tools_("oodle")})
        if (!QFileInfo::exists(f)) {
            problem_ = "Missing tool, model cache idle: " + f;
            emit progress(problem_);
            refreshPending_.clear();
            running_ = refreshBatch_ = false;
            emit batchEnded(0);
            return;
        }
    QStringList names;
    while (!refreshPending_.isEmpty() && names.size() < kBatchModels) names << refreshPending_.takeFirst();
    for (const QString& n : names) {
        const auto it = index_.find(n);
        if (it == index_.end() || it->second.failed) continue;
        Work w;
        w.name = n;
        w.found = true;
        w.zone = it->second.zone;
        w.lod0Surfs = it->second.lod0Surfs;
        w.bounds = it->second.bounds;
        w.materials = it->second.materials;
        queued_.insert(n);
        works_.push_back(w);
    }
    emit progress(QString("model cache: %1 model(s) fetched again for their self visibility").arg(works_.size()));
    pump();
}

void ModelCache::request(const QStringList& names) {''')
# finishBatch: refresh failures keep the old entry; record selfVis; drop the old LODs of refreshed models
patch(C, '''void ModelCache::finishBatch() {
    QStringList done;
    for (const Work& w : works_) {
        Entry e;
        e.name = w.name;
        e.zone = w.zone;
        e.lod0Surfs = w.lod0Surfs;
        if (w.found && !w.glb.isEmpty() && w.error.isEmpty()) {
            e.glbPath = w.glb;
            e.bounds = w.bounds;
            e.materials = w.materials;
        } else {''', '''void ModelCache::finishBatch() {
    QStringList done;
    const bool refresh = refreshBatch_;
    refreshBatch_ = false;
    for (const Work& w : works_) {
        Entry e;
        e.name = w.name;
        e.zone = w.zone;
        e.lod0Surfs = w.lod0Surfs;
        const bool ok = w.found && !w.glb.isEmpty() && w.error.isEmpty();
        if (refresh && !ok) {
            // A refresh that could not be done: the model keeps the GLB it had, now checked (not asked again).
            if (const auto it = index_.find(w.name); it != index_.end()) it->second.selfVis = glbSelfVis(it->second.glbPath);
            done << w.name;
            continue;
        }
        if (ok) {
            e.glbPath = w.glb;
            e.bounds = w.bounds;
            e.materials = w.materials;
            e.selfVis = glbSelfVis(w.glb);
            if (refresh)  // its LODs were exported before too: fetched again when next wanted
                for (int lod = 1; lod <= 8; ++lod) {
                    QFile::remove(lodGlbPath(w.name, lod));
                    lodFailed_.erase(w.name + "#" + QString::number(lod));
                }
        } else {''')
patch(C, '''    if (running_) return;  // a handler asked for more and that batch has started
    if (!pending_.isEmpty()) startBatch();
    else if (!running_) QDir(root_ + "/w").removeRecursively();''', '''    if (running_) return;  // a handler asked for more and that batch has started
    if (!pending_.isEmpty()) startBatch();
    else if (!refreshPending_.isEmpty()) startRefreshBatch();
    else if (!running_) QDir(root_ + "/w").removeRecursively();''')
patch(C, '''        if (n.isEmpty() || lookup(n) || queued_.count(n)) continue;
        queued_.insert(n);
        pending_.append(n);
        added = true;
    }
    if (added && !running_) startBatch();''', '''        if (n.isEmpty() || lookup(n) || queued_.count(n)) continue;
        queued_.insert(n);
        pending_.append(n);
        added = true;
    }
    if (added && !running_) startBatch();  // (a refresh batch running: finishBatch starts these first)''')
print('ok')
