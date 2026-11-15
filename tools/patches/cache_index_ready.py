import io

# indexReady() / indexLoaded() on the material and model caches: the asset browsers' thumbnail and status sources do
# not wait for the index read on the worker thread (cache_index_async.py); they show placeholders and refresh when it
# lands.

ROOT = 'E:/IW8-Radiant/'


def patch(p, a, b, count=1):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    if b in s and a not in s:
        return
    assert s.count(a) == count, (p, s.count(a), a[:90])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


for h, sig in (('src/qtapp/material_cache.h', '    void materialReady(const QString& material);  // lookup() now returns an entry (possibly failed)\n'),
               ('src/qtapp/model_cache.h', '    void modelReady(const QString& name);  // lookup() now returns an entry (possibly failed)\n')):
    patch(h, '    bool busy() const;\n', '''    bool busy() const;
    // The index file has been read (on the worker thread): lookup() answers at once. Before, lookup() waits for it.
    bool indexReady() const;
''')
    patch(h, sig, sig + '    void indexLoaded();  // the index read on the worker thread is ready (indexReady())\n')

patch('src/qtapp/material_cache.cpp', '''    indexLoad_ = std::async(std::launch::async, [root = root_, data = repoDataDir()] {
        IndexFiles f;
        f.index = readIndex(root);
        f.seeds = readSeeds(data, f.index);
        return f;
    });
}
''', '''    indexLoad_ = std::async(std::launch::async, [root = root_, data = repoDataDir(), self = QPointer<MaterialCache>(this)] {
        IndexFiles f;
        f.index = readIndex(root);
        f.seeds = readSeeds(data, f.index);
        QMetaObject::invokeMethod(qApp, [self] { if (self) emit self->indexLoaded(); }, Qt::QueuedConnection);
        return f;
    });
}

bool MaterialCache::indexReady() const {
    return !indexLoad_.valid() || indexLoad_.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
}
''')
patch('src/qtapp/model_cache.cpp', '''    indexLoad_ = std::async(std::launch::async, [root = root_] { return readIndex(root); });
}
''', '''    indexLoad_ = std::async(std::launch::async, [root = root_, self = QPointer<ModelCache>(this)] {
        auto index = readIndex(root);
        QMetaObject::invokeMethod(qApp, [self] { if (self) emit self->indexLoaded(); }, Qt::QueuedConnection);
        return index;
    });
}

bool ModelCache::indexReady() const {
    return !indexLoad_.valid() || indexLoad_.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
}
''')

M = 'src/qtapp/main_window.cpp'
patch(M, '''        materials_->setThumbnailSource([this, decodeFile, matThumbs, matFull, cached](const QString& m, bool* pending) -> QImage {
            if (auto e = matCache_->lookup(m)) {''', '''        materials_->setThumbnailSource([this, decodeFile, matThumbs, matFull, cached](const QString& m, bool* pending) -> QImage {
            if (!matCache_->indexReady()) {  // still read on its worker thread: a placeholder, asked again on indexLoaded
                *pending = true;
                return {};
            }
            if (auto e = matCache_->lookup(m)) {''')
patch(M, '''        connect(matCache_, &MaterialCache::materialReady, materials_, [this] { materials_->refreshThumbnails(); });
        // Availability marks: what the caches tried and could not resolve is greyed with the reason.
        materials_->setStatusSource([this](const QString& m) -> QString {
            const auto e = matCache_->lookup(m);''', '''        connect(matCache_, &MaterialCache::materialReady, materials_, [this] { materials_->refreshThumbnails(); });
        connect(matCache_, &MaterialCache::indexLoaded, materials_, [this] { materials_->refreshThumbnails(); });
        // Availability marks: what the caches tried and could not resolve is greyed with the reason.
        materials_->setStatusSource([this](const QString& m) -> QString {
            if (!matCache_->indexReady()) return {};  // not known yet
            const auto e = matCache_->lookup(m);''')
patch(M, '''        models_->setStatusSource([this](const QString& name) -> QString {
            const auto e = modelCacheStock_->lookup(name);''', '''        models_->setStatusSource([this](const QString& name) -> QString {
            if (!modelCacheStock_->indexReady()) return {};  // not known yet
            const auto e = modelCacheStock_->lookup(name);''')
patch(M, '''        models_->setThumbnailSource([this, modelThumbs, modelFull, cached](const QString& name, bool* pending) -> QImage {
            const auto e = modelCacheStock_->lookup(name);''', '''        connect(modelCacheStock_, &ModelCache::indexLoaded, models_, [this] { models_->refreshThumbnails(); });
        models_->setThumbnailSource([this, modelThumbs, modelFull, cached](const QString& name, bool* pending) -> QImage {
            if (!modelCacheStock_->indexReady()) {  // still read on its worker thread: a placeholder, asked again on indexLoaded
                *pending = true;
                return {};
            }
            const auto e = modelCacheStock_->lookup(name);''')

for p in ('src/qtapp/material_cache.cpp', 'src/qtapp/model_cache.cpp'):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    for inc in ('#include <QPointer>', '#include <QMetaObject>', '#include <chrono>'):
        if inc not in s:
            s = s.replace('#include <future>' + nl, '#include <future>' + nl + inc + nl, 1)
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s)
print('ok')
