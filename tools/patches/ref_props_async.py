import io
import re

# Stock map props: each model's GLB (LOD0 and further LODs) and detail file are read and converted into the camera's
# meshes on worker threads (startRefPropPrep) instead of inside rebuildReferencePropsUnguarded on the GUI thread (the
# stall watch: 10.7 s, 9.9 s and 4.6 s freezes while Hideout opened, readGlb 63%). A rebuild pass that finds a cached
# model not converted yet queues it and changes nothing on screen; when the workers are done the rebuild runs again and
# takes the prepared meshes. Owner 2026-11-15: "the viewport ... never frozen, and other workloads on others".

ROOT = 'E:/IW8-Radiant/'


def read(p):
    return io.open(ROOT + p, encoding='utf-8', newline='').read()


def write(p, s):
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s)


def sub_in(text, a, b, count=1):
    assert text.count(a) == count, (text.count(a), a[:100])
    return text.replace(a, b)


# ------------------------------------------------------------------ ModelCache: file-only readers, public and static
P, PH = 'src/qtapp/model_cache.cpp', 'src/qtapp/model_cache.h'
s = read(P)
nl = '\r\n' if '\r\n' in s else '\n'
if 'ModelCache::readDetail(' not in s:
    s = s.replace('\r\n', '\n')
    s = sub_in(s, '''std::optional<XModelInfo> ModelCache::loadDetail(const QString& name) const {
    QFile f(detailPath(name));''', '''std::optional<XModelInfo> ModelCache::loadDetail(const QString& name) const { return readDetail(detailPath(name)); }

std::optional<XModelInfo> ModelCache::readDetail(const QString& path) {
    QFile f(path);''')
    s = sub_in(s, '''std::optional<ImportedModel> ModelCache::loadGlb(const QString& path, const QStringList& materials, const QString& name,
                                                 QString* err) const {''', '''std::optional<ImportedModel> ModelCache::loadGlb(const QString& path, const QStringList& materials, const QString& name,
                                                 QString* err) {''')
    write(P, s.replace('\n', nl))
h = read(PH)
nl = '\r\n' if '\r\n' in h else '\n'
if 'readDetail(' not in h:
    h = h.replace('\r\n', '\n')
    h = sub_in(h, '''    std::optional<ImportedModel> loadGlb(const QString& path, const QStringList& materials, const QString& name,
                                         QString* err) const;
''', '')
    h = sub_in(h, '    std::optional<XModelInfo> loadDetail(const QString& name) const;\n',
               '''    std::optional<XModelInfo> loadDetail(const QString& name) const;
    // File-only readers (no cache state: safe on worker threads, MainWindow::startRefPropPrep). loadGlb reads a cached
    // GLB and names its meshes after `materials` (one per surface); readDetail reads a detail file (detailPath).
    static std::optional<ImportedModel> loadGlb(const QString& path, const QStringList& materials, const QString& name,
                                                QString* err);
    static std::optional<XModelInfo> readDetail(const QString& path);
''')
    write(PH, h.replace('\n', nl))

# ------------------------------------------------------------------ MainWindow header
MH = 'src/qtapp/main_window.h'
h = read(MH)
nl = '\r\n' if '\r\n' in h else '\n'
if 'RefPropJob' not in h:
    h = h.replace('\r\n', '\n')
    h = sub_in(h, '''    std::map<std::string, std::shared_ptr<const RefPropMesh>> refPropBuilt_;
''', '''    std::map<std::string, std::shared_ptr<const RefPropMesh>> refPropBuilt_;
    // Props read and converted on worker threads (startRefPropPrep): a rebuild pass that finds a cached model not
    // converted yet queues it and leaves the screen as it is; the pass after the workers takes refPropPrepared_.
    struct RefPropJob {
        std::string model;
        QString name, glb, detail;      // the GLB and detail files (resolved on the GUI thread from the cache index)
        QStringList materials;          // LOD0's, one per surface
        std::vector<QString> lodGlbs;   // LOD 1, 2, ...: the files, which may not exist
        bool lods = true;               // propLodsEnabled_ and not IW8R_NO_PROP_LODS
    };
    struct RefPropPrepared {
        std::shared_ptr<const RefPropMesh> mesh;  // null: unreadable (drawn as its box)
        std::vector<int> missingLods;             // LOD levels not cached (asked for when the mesh is used)
        bool overBudget = false;                  // not converted: the triangle budget was used up (a box)
    };
    static RefPropPrepared buildRefPropMesh(const RefPropJob& job);  // worker thread: files only
    void startRefPropPrep(std::vector<RefPropJob> jobs);
    std::map<std::string, RefPropPrepared> refPropPrepared_;
    std::set<std::string> refPropPrepQueued_;  // models the running workers have
    bool refPropPrepBusy_ = false;
    // Distinct models' LOD0 triangles drawn as meshes (the rest as boxes); see rebuildReferencePropsUnguarded.
    static constexpr size_t kPropTriangleBudget = 4'000'000;
''')
    write(MH, h.replace('\n', nl))

# ------------------------------------------------------------------ MainWindow: the rebuild
M = 'src/qtapp/main_window.cpp'
s = read(M)
nl = '\r\n' if '\r\n' in s else '\n'
if 'MainWindow::buildRefPropMesh(' not in s:
    s = s.replace('\r\n', '\n')
    start_mark = '            } else if (auto im = modelCacheStock_->loadModel(qs(in.model))) {\n'
    end_mark = '            if (built && built->tris > 0 && meshTris + built->tris <= kPropTriangleBudget) {\n'
    i = s.index(start_mark)
    j = s.index(end_mark, i)
    old = s[i:j]
    b0 = old.index('                auto fresh = std::make_shared<RefPropMesh>();\n')
    b1 = old.index('                fresh->hasBox = !fresh->tex.empty();\n') + len('                fresh->hasBox = !fresh->tex.empty();\n')
    body = old[b0:b1]
    # Dedent from the lambda (16) to a function body (4).
    body = '\n'.join(l[12:] if l.startswith(' ' * 12) else l for l in body.split('\n'))
    body = sub_in(body, '''    if (const auto detail = modelCacheStock_->loadDetail(qs(in.model)); detail && !detail->lods.empty())''',
                  '''    if (const auto& detail = modelDetail; detail && !detail->lods.empty())''')
    body = sub_in(body, '''    const auto modelDetail = modelCacheStock_->loadDetail(qs(in.model));\n''', '')
    body = sub_in(body, '''    if (const auto detail = modelCacheStock_->loadDetail(qs(in.model)); detail && detail->lods.size() > 1 && !fresh->tex.empty()) {''',
                  '''    if (const auto& detail = modelDetail; detail && detail->lods.size() > 1 && !fresh->tex.empty()) {''')
    body = sub_in(body, '''std::getenv("IW8R_NO_PROP_LODS") != nullptr || !propLodsEnabled_''', '''!job.lods''')
    body = sub_in(body, '''            if (!modelCacheStock_->hasLod(qs(in.model), int(li))) {
                if (!modelCacheStock_->lodFailed(qs(in.model), int(li))) wantLods_.append({qs(in.model), int(li)});
                continue;
            }
            auto lm = modelCacheStock_->loadModelLod(qs(in.model), int(li));''', '''            if (li > job.lodGlbs.size() || !QFileInfo::exists(job.lodGlbs[li - 1])) {
                out.missingLods.push_back(int(li));  // asked for when the mesh is used (unless the cache gave up on it)
                continue;
            }
            auto lm = loadLod(int(li));''')
    assert 'modelCacheStock_' not in body and 'in.model' not in body and 'wantLods_' not in body, body
    func = '''// Worker thread (startRefPropPrep): one stock model's meshes from its cached files: LOD0 (textured parts, and the
// untextured ones shaded by facing), the cached further LODs with the game's distances, the box. Files only.
MainWindow::RefPropPrepared MainWindow::buildRefPropMesh(const RefPropJob& job) {
    RefPropPrepared out;
    const auto im = ModelCache::loadGlb(job.glb, job.materials, job.name, nullptr);
    if (!im) return out;
    const std::optional<XModelInfo> modelDetail = ModelCache::readDetail(job.detail);
    const Vec3 light = normalize(Vec3{0.35, 0.55, 0.75});
    // A further LOD: its own material list from the detail file, else LOD0's when the surface counts match.
    const auto loadLod = [&](int lod) -> std::optional<ImportedModel> {
        QStringList mats;
        if (modelDetail && lod < int(modelDetail->lods.size()))
            for (const std::string& m : modelDetail->lods[size_t(lod)].materials) mats << QString::fromStdString(m);
        auto m = ModelCache::loadGlb(job.lodGlbs[size_t(lod - 1)], mats, job.name, nullptr);
        if (m && mats.isEmpty() && job.materials.size() == qsizetype(m->meshes.size()))
            for (size_t i = 0; i < m->meshes.size(); ++i) m->meshes[i].material = job.materials[qsizetype(i)].toStdString();
        return m;
    };
''' + body + '''    out.mesh = fresh;
    return out;
}

void MainWindow::startRefPropPrep(std::vector<RefPropJob> jobs) {
    refPropPrepBusy_ = true;
    struct Shared {
        std::vector<RefPropJob> jobs;
        std::atomic<size_t> next{0}, tris{0};
        std::atomic<int> running{0};
        std::mutex m;
        std::vector<std::pair<std::string, RefPropPrepared>> out;
    };
    auto sh = std::make_shared<Shared>();
    sh->jobs = std::move(jobs);
    sh->tris = refPropBuiltTris_;  // the kept meshes count toward the budget
    const unsigned hw = std::max(3u, std::thread::hardware_concurrency());
    const int n = int(std::min<size_t>(sh->jobs.size(), std::clamp(hw - 2, 1u, 8u)));
    sh->running = n;
    QPointer<MainWindow> self(this);
    for (int w = 0; w < n; ++w)
        std::thread(guardedWorker(__FILE__, __LINE__, [sh, self] {
            for (size_t k = sh->next++; k < sh->jobs.size(); k = sh->next++) {
                RefPropPrepared r;
                if (sh->tris.load() > kPropTriangleBudget) {
                    r.overBudget = true;  // drawn as a box anyway: not read
                } else {
                    try {
                        r = buildRefPropMesh(sh->jobs[k]);
                    } catch (const std::bad_alloc&) {
                        r = RefPropPrepared{};  // no memory for this one: its box
                    }
                    if (r.mesh) sh->tris += r.mesh->tris;
                }
                std::lock_guard lock(sh->m);
                sh->out.emplace_back(sh->jobs[k].model, std::move(r));
            }
            if (--sh->running == 0)
                QMetaObject::invokeMethod(qApp, [sh, self] {
                    if (!self) return;
                    for (auto& [model, r] : sh->out) self->refPropPrepared_[model] = std::move(r);
                    self->refPropPrepBusy_ = false;
                    self->refPropPrepQueued_.clear();
                    self->rebuildReferenceProps();  // takes the prepared meshes (and queues any model cached since)
                }, Qt::QueuedConnection);
        })).detach();
}

'''
    new_branch = '''            } else if (const auto prep = refPropPrepared_.find(in.model); prep != refPropPrepared_.end()) {
                // Read and converted on a worker thread (startRefPropPrep).
                if (prep->second.overBudget) {
                    refPropTrisKnown_[in.model] = kPropTriangleBudget + 1;  // the budget was used up: a box from now on
                } else if (prep->second.mesh) {
                    ++loadedModels;
                    // Cached before the GLB carried the self visibility (research 105 section 4): fetched again in the
                    // background; the model is drawn from its old GLB meanwhile.
                    static const bool noSelfVis = std::getenv("IW8R_NO_SELFVIS") != nullptr;
                    if (const auto e = modelCacheStock_->lookup(qs(in.model));
                        !noSelfVis && e && (e->selfVis < 0 || (e->lmapUv < 0 && (in.modelFlags & 2u)))) {
                        if (selfVisRefresh_.isEmpty())
                            QTimer::singleShot(0, this, [this] {
                                modelCacheStock_->refreshGeometry(selfVisRefresh_);
                                selfVisRefresh_.clear();
                            });
                        selfVisRefresh_ << qs(in.model);
                    }
                    // Plan 1.13: further LODs not cached yet are asked for (unless the cache gave up on them).
                    for (const int li : prep->second.missingLods)
                        if (!modelCacheStock_->lodFailed(qs(in.model), li)) wantLods_.append({qs(in.model), li});
                    built = prep->second.mesh;
                    refPropTrisKnown_[in.model] = built->tris;
                    if (refPropBuiltTris_ + built->tris <= kPropTriangleBudget) {  // kept copies stay within the draw budget
                        refPropBuiltTris_ += built->tris;
                        refPropBuilt_[in.model] = built;
                    }
                }
            } else if (const auto e = modelCacheStock_->lookup(qs(in.model)); e && !e->failed) {
                // Not converted yet: read and converted on worker threads; this pass leaves the screen as it is.
                propsPending = true;
                if (!refPropPrepBusy_ && refPropPrepQueued_.insert(in.model).second) {
                    RefPropJob job;
                    job.model = in.model;
                    job.name = qs(in.model);
                    job.glb = e->glbPath;
                    job.materials = e->materials;
                    job.detail = modelCacheStock_->detailPath(job.name);
                    for (int li = 1; li <= 8; ++li) job.lodGlbs.push_back(modelCacheStock_->lodGlbPath(job.name, li));
                    job.lods = std::getenv("IW8R_NO_PROP_LODS") == nullptr && propLodsEnabled_;
                    prepJobs.push_back(std::move(job));
                }
                return true;
            }
'''
    s = s[:i] + new_branch + s[j:]
    # The rebuild's own budget constant is the class one now.
    s = sub_in(s, '    constexpr size_t kPropTriangleBudget = 4\'000\'000;\n    const Vec3 light = normalize(Vec3{0.35, 0.55, 0.75});\n',
               '    std::vector<RefPropJob> prepJobs;  // models to read and convert on worker threads (startRefPropPrep)\n'
               '    bool propsPending = false;           // a model of this pass is not converted yet\n')
    # After the placement loop: a pass with models pending changes nothing on screen.
    s = sub_in(s, '''    // Plan 1.13: the props' further LODs not cached yet, in batches (lodReady rebuilds the props).
    if (!wantLods_.isEmpty() && modelCacheStock_) modelCacheStock_->requestLods(wantLods_);''', '''    if (propsPending) {
        // Models still read and converted on worker threads: the props on screen stay as they are; the rebuild runs
        // again when the workers are done (startRefPropPrep).
        if (!prepJobs.empty()) startRefPropPrep(std::move(prepJobs));
        return;
    }
    refPropPrepared_.clear();  // taken: kept in refPropBuilt_ within its budget, the others given back
    // Plan 1.13: the props' further LODs not cached yet, in batches (lodReady rebuilds the props).
    if (!wantLods_.isEmpty() && modelCacheStock_) modelCacheStock_->requestLods(wantLods_);''')
    # The worker functions go before the rebuild.
    s = sub_in(s, 'void MainWindow::rebuildReferencePropsUnguarded() {\n', func + 'void MainWindow::rebuildReferencePropsUnguarded() {\n')
    # Out of memory: the prepared meshes go too.
    s = sub_in(s, '''    } catch (const std::bad_alloc&) {
        refPropBuilt_.clear();
        refPropBuiltTris_ = 0;''', '''    } catch (const std::bad_alloc&) {
        refPropBuilt_.clear();
        refPropPrepared_.clear();
        refPropBuiltTris_ = 0;''')
    # Busy while props are being prepared.
    s = sub_in(s, '!refPropRebuildQueued_ && !refPropRelightQueued_ &&\n           stockLoadsInFlight_ == 0;',
               '!refPropRebuildQueued_ && !refPropRelightQueued_ &&\n           stockLoadsInFlight_ == 0 && !refPropPrepBusy_;')
    write(M, s.replace('\n', nl))
print('ok')
