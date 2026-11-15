import io
import shutil

# The asset preview window: its files go into src/qtapp, the Models and Prefabs browsers get "Preview in 3D", and the
# main window opens one reused window with the main view's hooks.

ROOT = 'E:/IW8-Radiant/'


def patch(p, a, b, count=1):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == count, (p, s.count(a), a[:70])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


for f in ('asset_preview_window.h', 'asset_preview_window.cpp'):
    shutil.copyfile(ROOT + 'out/agent/pv/' + f, ROOT + 'src/qtapp/' + f)

# The swatch: a decoded image (material images are DDS, which QImage does not read).
P = 'src/qtapp/asset_preview_window.h'
patch(P, '''        // The colour image file of a material ("" = none or not cached yet).
        std::function<QString(const std::string& material)> materialImage;''', '''        // A material's colour image, decoded (null = none or not cached yet).
        std::function<QImage(const std::string& material)> materialImage;''')
patch(P, '''#include <QWidget>''', '''#include <QImage>
#include <QWidget>''')
P = 'src/qtapp/asset_preview_window.cpp'
patch(P, '''        const QString file = sources_.materialImage && !m.isEmpty() ? sources_.materialImage(m.toStdString()) : QString();
        const QImage img = file.isEmpty() ? QImage() : QImage(file);
        if (img.isNull()) swatch_->setText(file.isEmpty() ? "no image" : QFileInfo(file).fileName());
        else swatch_->setPixmap(QPixmap::fromImage(img.scaled(158, 158, Qt::KeepAspectRatio, Qt::SmoothTransformation)));''',
      '''        const QImage img = sources_.materialImage && !m.isEmpty() ? sources_.materialImage(m.toStdString()) : QImage();
        if (img.isNull()) swatch_->setText("no image (yet)");
        else swatch_->setPixmap(QPixmap::fromImage(img.scaled(158, 158, Qt::KeepAspectRatio, Qt::SmoothTransformation)));''')

# Models browser: "Preview in 3D" in the context menu and a button under the still preview.
A = 'src/qtapp/asset_browser.h'
patch(A, '''    void modelHighlighted(const QString& name);  // the current model row changed (preview)''',
      '''    void modelHighlighted(const QString& name);  // the current model row changed (preview)
    void previewRequested(const QString& name);  // Models: open the 3D preview window''')
A = 'src/qtapp/asset_browser.cpp'
patch(A, '''            if (kind_ == Kind::Materials || kind_ == Kind::Models || kind_ == Kind::Effects)
                menu.addAction(kind_ == Kind::Materials ? "Material Info..." : kind_ == Kind::Models ? "Model Info..." : "Effect Info...", this,
                               [this, name] { emit infoRequested(name); });''', '''            if (kind_ == Kind::Models) menu.addAction("Preview in 3D...", this, [this, name] { emit previewRequested(name); });
            if (kind_ == Kind::Materials || kind_ == Kind::Models || kind_ == Kind::Effects)
                menu.addAction(kind_ == Kind::Materials ? "Material Info..." : kind_ == Kind::Models ? "Model Info..." : "Effect Info...", this,
                               [this, name] { emit infoRequested(name); });''')
patch(A, '''        side->addWidget(preview_);
        side->addWidget(previewCaption_);''', '''        side->addWidget(preview_);
        side->addWidget(previewCaption_);
        if (kind == Kind::Models) {
            auto* open3d = new QPushButton("Preview in 3D...", this);
            open3d->setObjectName("modelPreview3d");
            open3d->setToolTip("A textured 3D view of the model: orbit, pan and zoom, levels of detail, materials, collision hull");
            connect(open3d, &QPushButton::clicked, this, [this] {
                const QModelIndex idx = view_->currentIndex();
                if (idx.isValid()) emit previewRequested(idx.data(Qt::DisplayRole).toString());
            });
            side->addWidget(open3d);
        }''')

# Prefabs browser: a context menu with Place and Preview in 3D.
B = 'src/qtapp/prefab_browser.h'
patch(B, '''    void refresh();                  // rescans the folders''', '''    void refresh();                  // rescans the folders
    // The prefab's map file for a preview (a bundle: its main contents; "" when there is none).
    QString previewFileAt(int row) const;''')
patch(B, '''protected:
    void showEvent(QShowEvent*) override;''', '''signals:
    void previewRequested(const QString& mapFile);  // context menu > Preview in 3D

protected:
    void showEvent(QShowEvent*) override;''')
B = 'src/qtapp/prefab_browser.cpp'
patch(B, '''    connect(list_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* it) {
        QString err;
        if (!stamp(list_->row(it), &err) && !err.isEmpty()) QMessageBox::warning(this, "Insert Prefab", err);
    });
}''', '''    connect(list_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* it) {
        QString err;
        if (!stamp(list_->row(it), &err) && !err.isEmpty()) QMessageBox::warning(this, "Insert Prefab", err);
    });
    list_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(list_, &QListWidget::customContextMenuRequested, this, [this](const QPoint& at) {
        QListWidgetItem* it = list_->itemAt(at);
        if (!it) return;
        const int row = list_->row(it);
        QMenu menu(this);
        menu.addAction("Place", this, [this, row] {
            QString err;
            if (!stamp(row, &err) && !err.isEmpty()) QMessageBox::warning(this, "Insert Prefab", err);
        });
        const QString file = previewFileAt(row);
        QAction* pv = menu.addAction("Preview in 3D...", this, [this, file] { emit previewRequested(file); });
        pv->setEnabled(!file.isEmpty());
        menu.exec(list_->viewport()->mapToGlobal(at));
    });
}

QString PrefabBrowser::previewFileAt(int row) const {
    const QString f = fileAt(row);
    if (f.isEmpty()) return {};
    const QFileInfo fi(f);
    if (!fi.isDir()) return f;
    // A bundle folder: its main contents (a .map named like the folder, else contents.map, else the first .map).
    const QDir d(f);
    for (const QString& name : {fi.fileName() + ".map", QString("contents.map"), QString("prefab.map")})
        if (d.exists(name)) return d.filePath(name);
    const QStringList maps = d.entryList({"*.map"}, QDir::Files, QDir::Name);
    return maps.isEmpty() ? QString() : d.filePath(maps.front());
}''')
s = io.open(ROOT + B, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
for inc in ('#include <QMenu>', '#include <QDir>', '#include <QFileInfo>'):
    if inc not in s:
        s = s.replace('#include "prefab_browser.h"', '#include "prefab_browser.h"' + nl + inc, 1)
io.open(ROOT + B, 'w', encoding='utf-8', newline='').write(s)

# Main window: one reused preview window.
M = 'src/qtapp/main_window.h'
patch(M, '''    class PrefabBrowser* prefabs_ = nullptr;''', '''    class PrefabBrowser* prefabs_ = nullptr;
    class AssetPreviewWindow* assetPreview_ = nullptr;
public:
    // Opens the asset preview window (a textured, navigable 3D view) on a model or a prefab .map file.
    class AssetPreviewWindow* openAssetPreview(const QString& modelOrPrefab, bool prefab);
private:''')
M = 'src/qtapp/main_window.cpp'
patch(M, '''    connect(models_, &AssetBrowser::modelHighlighted, this, [this](const QString& name) { previewModel(name); });''',
      '''    connect(models_, &AssetBrowser::modelHighlighted, this, [this](const QString& name) { previewModel(name); });
    connect(models_, &AssetBrowser::previewRequested, this, [this](const QString& name) { openAssetPreview(name, false); });''')
s = io.open(ROOT + M, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
anchor = 'int MainWindow::stockLodFor(const Entity& e, const std::string& model) {'
assert s.count(anchor) == 1
fn = '''AssetPreviewWindow* MainWindow::openAssetPreview(const QString& asset, bool prefab) {
    if (!assetPreview_) {
        AssetPreviewWindow::Sources src;
        src.model = [this](const std::string& m, int lod) -> std::shared_ptr<const ImportedModel> {
            const double s = ctx_.scene.modelScale > 0 ? 1.0 / ctx_.scene.modelScale : 1.0;
            const auto scaled = [&](ImportedModel im) {
                auto out = std::make_shared<ImportedModel>(std::move(im));
                for (auto& mesh : out->meshes)
                    for (auto& p : mesh.pos) p = p * s;
                return out;
            };
            if (lod > 0 && modelCacheStock_) {
                if (auto l = modelCacheStock_->loadModelLod(qs(m), lod)) return scaled(std::move(*l));
                modelCacheStock_->requestLod(qs(m), lod);
                return nullptr;
            }
            if (auto it = modelCache_.find(m); it != modelCache_.end() && it->second) return it->second;
            if (modelCacheStock_)
                if (auto ent = modelCacheStock_->lookup(qs(m)); ent && !ent->failed && !ent->glbPath.isEmpty())
                    if (auto loaded = modelCacheStock_->loadModel(qs(m))) return scaled(std::move(*loaded));
            return nullptr;
        };
        src.lodCount = [this](const std::string& m) -> int {
            if (!modelCacheStock_) return 1;
            const auto ent = modelCacheStock_->lookup(qs(m));
            if (!ent || ent->failed) return 1;
            const auto d = modelCacheStock_->loadDetail(qs(m));
            return d ? std::max<int>(1, int(d->lods.size())) : 1;
        };
        src.materialImage = [this](const std::string& m) -> QImage {
            const QString file = camera_->materialImagePath(m);
            app::Rgba8Image img;
            if (file.isEmpty()) return {};
            if (file.endsWith(".dds", Qt::CaseInsensitive)) {
                if (!app::loadDdsPreview(file.toStdString(), 256, img)) return {};
                return QImage(img.pixels.data(), img.width, img.height, img.width * 4, QImage::Format_RGBA8888).convertToFormat(QImage::Format_RGB32);
            }
            return QImage(file);
        };
        assetPreview_ = new AssetPreviewWindow(dev_, dc_, camera_, ctx_.scene, std::move(src), this);
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
}

'''.replace('\n', nl)
s = s.replace(anchor, fn + anchor)
s = s.replace('#include "vfx_info.h"', '#include "vfx_info.h"' + nl + '#include "asset_preview_window.h"', 1)
io.open(ROOT + M, 'w', encoding='utf-8', newline='').write(s)
print('ok')

patch(M, '''            [this](const QString& f, QString* err) { return insertPrefab(f, err); });''',
      '''            [this](const QString& f, QString* err) { return insertPrefab(f, err); });
        connect(prefabs_, &PrefabBrowser::previewRequested, this, [this](const QString& f) { openAssetPreview(f, true); });''')
print('prefab ok')
