import io

# Owner request 2026-11-14, part 2: the Models tab (list thumbnails and the side preview) and the Prefabs tab
# (thumbnails) show the textured pictures of TexturedThumbs (pre-generated, cached in thumbs/*.textured.pack), never
# the grey QPainter drawings.

ROOT = 'E:/IW8-Radiant/'


def patch(p, a, b, count=1):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == count, (p, s.count(a), a[:70])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


patch('src/qtapp/asset_preview_window.h', '''#include <QImage>''', '''#include <QImage>
#include <QStringList>''')

# --- Prefabs tab: a picture source before the drawing.
H = 'src/qtapp/prefab_browser.h'
patch(H, '''    static bool lastThumbnailIncomplete();  // the last thumbnail() call fell back to a box for at least one model''',
      '''    static bool lastThumbnailIncomplete();  // the last thumbnail() call fell back to a box for at least one model
    // Textured pictures (owner request 2026-11-14; TexturedThumbs): the stored picture of a prefab .map file, or a null
    // image with *pending set while it is being made (the row shows a placeholder and is asked again by
    // refreshIncomplete()). When set, the grey drawing of thumbnail() is not used for the list.
    using PictureFn = std::function<QImage(const QString& mapFile, bool* pending)>;
    static void setPictureSource(PictureFn fn);''')
C = 'src/qtapp/prefab_browser.cpp'
patch(C, '''bool g_thumbIncomplete = false;''', '''bool g_thumbIncomplete = false;
PrefabBrowser::PictureFn g_pictureSource;''')
patch(C, '''bool PrefabBrowser::lastThumbnailIncomplete() { return g_thumbIncomplete; }''',
      '''bool PrefabBrowser::lastThumbnailIncomplete() { return g_thumbIncomplete; }
void PrefabBrowser::setPictureSource(PictureFn fn) { g_pictureSource = std::move(fn); }''')
patch(C, '''void PrefabBrowser::loadThumbnail(QListWidgetItem* it) {
    it->setData(kDrawn, true);
    if (!it->data(kIsBundle).toBool()) {
        const QImage thumb = thumbnail(it->data(kPath).toString(), kThumb);
        it->setData(kIncomplete, lastThumbnailIncomplete());
        if (!thumb.isNull()) it->setIcon(QIcon(QPixmap::fromImage(thumb)));
        return;
    }''', '''void PrefabBrowser::loadThumbnail(QListWidgetItem* it) {
    it->setData(kDrawn, true);
    // The textured picture of a .map file (fitted into the tile), or none yet (pending: a placeholder until it is made).
    const auto textured = [](const QString& mapFile, bool* pending) -> QImage {
        if (!g_pictureSource) return {};
        const QImage pic = g_pictureSource(mapFile, pending);
        if (pic.isNull()) return {};
        QImage base(kThumb, kThumb, QImage::Format_ARGB32_Premultiplied);
        base.fill(QColor(32, 34, 38));
        const QImage fit = pic.scaled(kThumb, kThumb, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        QPainter p(&base);
        p.drawImage((kThumb - fit.width()) / 2, (kThumb - fit.height()) / 2, fit);
        return base;
    };
    if (!it->data(kIsBundle).toBool()) {
        if (g_pictureSource) {
            bool pending = false;
            const QImage pic = textured(it->data(kPath).toString(), &pending);
            it->setData(kIncomplete, pending);
            it->setIcon(pic.isNull() ? placeholderIcon() : QIcon(QPixmap::fromImage(pic)));
            return;
        }
        const QImage thumb = thumbnail(it->data(kPath).toString(), kThumb);
        it->setData(kIncomplete, lastThumbnailIncomplete());
        if (!thumb.isNull()) it->setIcon(QIcon(QPixmap::fromImage(thumb)));
        return;
    }''')
patch(C, '''    if (base.isNull())
        if (const QString contents = it->data(kContents).toString(); !contents.isEmpty()) {
            base = thumbnail(contents, kThumb, it->data(kModelRoots).toStringList());
            incomplete = lastThumbnailIncomplete();
        }''', '''    if (base.isNull())
        if (const QString contents = it->data(kContents).toString(); !contents.isEmpty()) {
            if (g_pictureSource) {
                base = textured(contents, &incomplete);
            } else {
                base = thumbnail(contents, kThumb, it->data(kModelRoots).toStringList());
                incomplete = lastThumbnailIncomplete();
            }
        }''')

# --- Main window.
MH = 'src/qtapp/main_window.h'
patch(MH, '''    class AssetPreviewWindow* assetPreview_ = nullptr;''', '''    class AssetPreviewWindow* assetPreview_ = nullptr;
    // Textured pictures of models and prefabs for the browsers (textured_thumbs.h), made once and kept in packs.
    class TexturedThumbs* thumbs3d_ = nullptr;
    class TexturedThumbs* texturedThumbs();
    // The hooks of an asset preview window (the window and the picture maker share them).
    class AssetPreviewWindow* makeAssetPreviewWindow();
public:
    // --thumb-pack models|prefabs: every catalogued stock model (or every prefab of the search folders) gets its
    // textured picture in the pack ahead of time. Returns the process exit code.
    int runTexturedThumbPack(const QString& kind, int limit);
    class TexturedThumbs* thumbs3dForTests() { return texturedThumbs(); }
private:''')

M = 'src/qtapp/main_window.cpp'
# The Models list thumbnails: the textured pack.
patch(M, '''            if (e->failed) return {};
            return cached(*modelThumbs, name, e->glbPath, [&] {
                const auto m = modelCacheStock_->loadModel(name);
                return m ? renderModelPreview(*m, 96) : QImage();
            });
        });''', '''            if (e->failed) return {};
            // The textured picture (textured_thumbs.h): made once, then read from its pack.
            if (TexturedThumbs* t = texturedThumbs()) {
                if (QImage pic = t->picture(TexturedThumbs::Model, name); !pic.isNull()) return pic;
                t->request(TexturedThumbs::Model, name);
                *pending = true;
                return {};
            }
            return cached(*modelThumbs, name, e->glbPath, [&] {
                const auto m = modelCacheStock_->loadModel(name);
                return m ? renderModelPreview(*m, 96) : QImage();
            });
        });''')
patch(M, '''            if (!e) {
                if (QImage full = modelFull->getAny(name); !full.isNull()) return full;
                modelCacheStock_->request({name});
                *pending = true;
                return {};
            }''', '''            if (!e) {
                if (QImage pic = texturedThumbs() ? texturedThumbs()->picture(TexturedThumbs::Model, name) : QImage(); !pic.isNull())
                    return pic;
                modelCacheStock_->request({name});
                *pending = true;
                return {};
            }''')
# The side preview.
patch(M, '''    size_t tris = 0;
    for (const auto& mesh : m->meshes) tris += mesh.idx.size() / 3;
    const auto& b = e->bounds;
    models_->setPreview(renderModelPreview(*m, 256),''', '''    size_t tris = 0;
    for (const auto& mesh : m->meshes) tris += mesh.idx.size() / 3;
    const auto& b = e->bounds;
    // The textured picture (textured_thumbs.h); made first in the queue when it is not stored yet.
    QImage pic;
    if (TexturedThumbs* t = texturedThumbs()) {
        pic = t->picture(TexturedThumbs::Model, name);
        if (pic.isNull()) {
            t->request(TexturedThumbs::Model, name, true);
            models_->setPreview({}, name + "\\nmaking the textured preview...");
            return;
        }
    } else {
        pic = renderModelPreview(*m, 256);
    }
    models_->setPreview(pic,''')
# The web thumbnails of models.
patch(M, '''                if (auto e = modelCacheStock_->lookup(name); e && !e->failed) {
                    if (const auto m = modelCacheStock_->loadModel(name)) img = renderModelPreview(*m, 96);
                } else if (!e) {''', '''                if (auto e = modelCacheStock_->lookup(name); e && !e->failed) {
                    if (TexturedThumbs* t = texturedThumbs()) {
                        img = t->picture(TexturedThumbs::Model, name).scaled(96, 96, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                        if (img.isNull()) t->request(TexturedThumbs::Model, name);
                    } else if (const auto m = modelCacheStock_->loadModel(name)) {
                        img = renderModelPreview(*m, 96);
                    }
                } else if (!e) {''')
# The Prefabs tab's pictures.
patch(M, '''        dock_->addDockWidgetTabToArea(makeDock("Prefabs", prefabs_, true), bottomArea);''', '''        // Textured pictures (textured_thumbs.h) instead of the grey drawings.
        PrefabBrowser::setPictureSource([this](const QString& mapFile, bool* pending) -> QImage {
            TexturedThumbs* t = texturedThumbs();
            if (!t) return {};
            if (QImage pic = t->picture(TexturedThumbs::Prefab, mapFile); !pic.isNull()) return pic;
            t->request(TexturedThumbs::Prefab, mapFile);
            *pending = true;
            return {};
        });
        dock_->addDockWidgetTabToArea(makeDock("Prefabs", prefabs_, true), bottomArea);''')
# Materials arriving: the preview window and the picture being made ask for their images again.
patch(M, '''    connect(matCache_, &MaterialCache::materialReady, this, [this](const QString&) {
        camera_->materialImagesChanged();''', '''    connect(matCache_, &MaterialCache::materialReady, this, [this](const QString&) {
        camera_->materialImagesChanged();
        if (assetPreview_) assetPreview_->materialsChanged();
        if (thumbs3d_) thumbs3d_->materialsChanged();''')
# The window factory, shared by openAssetPreview and the picture maker.
patch(M, '''AssetPreviewWindow* MainWindow::openAssetPreview(const QString& asset, bool prefab) {
    if (!assetPreview_) {
        AssetPreviewWindow::Sources src;''', '''AssetPreviewWindow* MainWindow::openAssetPreview(const QString& asset, bool prefab) {
    if (!assetPreview_) assetPreview_ = makeAssetPreviewWindow();
    if (prefab) {
        QString err;
        if (!assetPreview_->showPrefab(asset, &err)) ctx_.setStatus("Preview: " + err);
    } else {
        assetPreview_->showModel(asset);
    }
    assetPreview_->show();
    assetPreview_->raise();
    assetPreview_->activateWindow();
    return assetPreview_;
}

TexturedThumbs* MainWindow::texturedThumbs() {
    if (thumbs3d_ || !dev_ || !camera_) return thumbs3d_;
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/thumbs";
    thumbs3d_ = new TexturedThumbs(
        dir, [this] { return makeAssetPreviewWindow(); },
        [this](const QString& model) {
            if (!modelCacheStock_) return false;
            if (modelCacheStock_->lookup(model)) return false;  // cached (or failed: it draws as a marker)
            modelCacheStock_->request({model});
            return true;
        },
        this);
    // A picture is ready: the browsers read it (debounced, a burst of pictures refreshes once).
    auto* refresh = new QTimer(thumbs3d_);
    refresh->setSingleShot(true);
    refresh->setInterval(250);
    connect(refresh, &QTimer::timeout, this, [this] {
        if (models_) models_->refreshThumbnails();
        if (prefabs_) prefabs_->refreshIncomplete();
    });
    connect(thumbs3d_, &TexturedThumbs::pictureReady, this, [this, refresh](int kind, const QString& name) {
        refresh->start();
        if (kind == TexturedThumbs::Model && name == previewedModel_) previewModel(name);
    });
    return thumbs3d_;
}

AssetPreviewWindow* MainWindow::makeAssetPreviewWindow() {
    {
        AssetPreviewWindow::Sources src;''')
patch(M, '''        assetPreview_ = new AssetPreviewWindow(dev_, dc_, camera_, ctx_.scene, std::move(src), this);
    }
    if (prefab) {
        QString err;
        if (!assetPreview_->showPrefab(asset, &err)) ctx_.setStatus("Preview: " + err);
    } else {
        assetPreview_->showModel(asset);
    }
    assetPreview_->show();
    assetPreview_->raise();
    assetPreview_->activateWindow();
    return assetPreview_;
}''', '''        return new AssetPreviewWindow(dev_, dc_, camera_, ctx_.scene, std::move(src), this);
    }
}

int MainWindow::runTexturedThumbPack(const QString& kind, int limit) {
    TexturedThumbs* t = texturedThumbs();
    if (!t || (kind != "models" && kind != "prefabs")) {
        std::fprintf(stderr, "thumb pack: kind must be models or prefabs (and the 3D view must be available)\\n");
        return 2;
    }
    QStringList names;
    if (kind == "models") {
        for (const QString& n : auditCatalogNames("models"))
            if (t->picture(TexturedThumbs::Model, n).isNull()) names << n;
    } else {
        for (const QString& root : PrefabBrowser::searchRoots())
            for (QDirIterator it(root, {"*.map"}, QDir::Files, QDirIterator::Subdirectories); it.hasNext();) {
                const QString f = it.next();
                if (t->picture(TexturedThumbs::Prefab, f).isNull()) names << f;
            }
    }
    if (limit > 0 && names.size() > limit) names = names.mid(0, limit);
    std::printf("thumb pack %s (textured): %lld to make\\n", qPrintable(kind), qlonglong(names.size()));
    std::fflush(stdout);
    QElapsedTimer clock;
    clock.start();
    const int before = t->made();
    for (const QString& n : names) {
        if (kind == "models" && modelCacheStock_ && !modelCacheStock_->lookup(n)) modelCacheStock_->request({n});
        t->request(kind == "models" ? TexturedThumbs::Model : TexturedThumbs::Prefab, n);
    }
    // Models are exported first (the model cache), then pictured one at a time.
    qint64 lastReport = 0;
    while (t->busy() || (modelCacheStock_ && modelCacheStock_->busy())) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        if (kind == "models" && modelCacheStock_)  // a model whose export finished is pictured now
            for (const QString& n : names)
                if (const auto e = modelCacheStock_->lookup(n); e && !e->failed && t->picture(TexturedThumbs::Model, n).isNull())
                    t->request(TexturedThumbs::Model, n);
        if (clock.elapsed() - lastReport > 10000) {
            lastReport = clock.elapsed();
            std::printf("  made %d, %d queued (%.0f s)\\n", t->made() - before, t->queued(), clock.elapsed() / 1000.0);
            std::fflush(stdout);
        }
    }
    std::printf("thumb pack %s: made %d pictures in %.0f s; pack %d\\n", qPrintable(kind), t->made() - before, clock.elapsed() / 1000.0,
                t->store(kind == "models" ? TexturedThumbs::Model : TexturedThumbs::Prefab).count());
    return 0;
}''')
print('ok')
