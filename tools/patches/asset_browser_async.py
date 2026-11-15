import io
import re

# The asset browsers read their catalogues (iw8_<kind>.tsv, the availability status files, the kit list, the stock
# image index) on a worker thread (owner 2026-11-15: the window "never frozen", other workloads on other threads). The
# stall watch measured about 3.4 s of the start-up freeze in AssetBrowser::load. The rows are made on the GUI thread
# in one batch insert. catalogLoaded() / catalogReady() say when the list is filled; MainWindow::waitForCatalogs()
# holds automated runs (GUI test, feature shots, bench, smoke) until then.

ROOT = 'E:/IW8-Radiant/'


def read(p):
    return io.open(ROOT + p, encoding='utf-8', newline='').read()


def write(p, s):
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s)


def patch(p, a, b, count=1):
    s = read(p)
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    if b in s and a not in s:
        return
    assert s.count(a) == count, (p, s.count(a), a[:90])
    write(p, s.replace(a, b))


def replace_between(p, start, end, body):
    s = read(p)
    nl = '\r\n' if '\r\n' in s else '\n'
    i = s.index(start)
    j = s.index(end, i)
    write(p, s[:i] + body.replace('\n', nl) + s[j:])


A = 'src/qtapp/asset_browser.cpp'
H = 'src/qtapp/asset_browser.h'

if 'struct AssetBrowser::Catalog' not in read(A):
    # Constructor: the kit box is made empty and hidden; applyCatalog fills it.
    patch(A, '''    if (kind == Kind::Materials || kind == Kind::Models) {
        // Kits: stock families from tools/build_asset_kits.py (excluded_* kits are left out of the list).
        QFile kf(repoData().filePath("asset_catalog/iw8_kits.tsv"));
        const QString want = kind == Kind::Models ? "model" : "material";
        if (kf.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream in(&kf);
            while (!in.atEnd()) {
                const QStringList c = in.readLine().split('\\t');
                if (c.size() < 3 || c[0].startsWith('#') || c[0] == "kit" || c[1] != want || c[0].startsWith("excluded")) continue;
                kitNames_[c[0]] << c[2];
            }
        }
        if (!kitNames_.isEmpty()) {
            kit_ = new QComboBox(this);
            kit_->setObjectName("assetKit");
            QStringList names = kitNames_.keys();
            names.sort();
            kit_->addItem("All kits");
            for (const QString& k : names) kit_->addItem(QString(k).replace('_', ' '), k);
            kit_->setToolTip("Stock families (containers, foliage, furniture, ...) from data/asset_catalog/iw8_kits.tsv");
            kit_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
            kit_->setMinimumContentsLength(12);  // long kit names are elided, not widening the panel
            options->addWidget(kit_);
        }
    }''', '''    if (kind == Kind::Materials || kind == Kind::Models) {
        // Kits: stock families from tools/build_asset_kits.py (excluded_* kits are left out of the list), read with the
        // catalogue on the worker thread; the box is shown once applyCatalog() has filled it.
        kit_ = new QComboBox(this);
        kit_->setObjectName("assetKit");
        kit_->addItem("All kits");
        kit_->setToolTip("Stock families (containers, foliage, furniture, ...) from data/asset_catalog/iw8_kits.tsv");
        kit_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        kit_->setMinimumContentsLength(12);  // long kit names are elided, not widening the panel
        kit_->setVisible(false);
        options->addWidget(kit_);
    }''')

    replace_between(A, 'void AssetBrowser::load() {', 'QIcon AssetBrowser::thumbnail(', '''// The catalogue as the worker thread read it: plain values, made into list rows on the GUI thread (applyCatalog).
struct AssetBrowser::Catalog {
    struct Row {
        QString name, tip;
        int tone = 0;  // 0 normal, 1 builds grey (no colour image), 2 not available in a build, 3 a looping sound
        bool loop = false;
    };
    std::vector<Row> rows;
    int hiddenStreamOnly = 0, hiddenMissing = 0;
    QString fileName;
    QHash<QString, QStringList> kits;
    QHash<QString, QString> imageFiles;
};

// Worker thread: reads files only (no widget, no QCoreApplication call: `dataDir` comes from the GUI thread).
std::shared_ptr<AssetBrowser::Catalog> AssetBrowser::readCatalog(Kind kind, bool buildableOnly, const QString& dataDir, bool wantKits) {
    auto c = std::make_shared<Catalog>();
    const QDir data(dataDir);
    // Availability catalog: name -> (status, reason).
    QHash<QString, QPair<QString, QString>> status;
    if (kind == Kind::Materials || kind == Kind::Models || kind == Kind::Effects || kind == Kind::Images) {
        // Every status file of the kind (an audit can run as two halves: iw8_<kind>_status.tsv and ..._status_b.tsv).
        const QDir cat(data.filePath("asset_catalog"));
        const QString pattern = kind == Kind::Materials ? "iw8_materials_status*.tsv"
                                : kind == Kind::Effects ? "iw8_effects_status*.tsv"
                                : kind == Kind::Images  ? "iw8_images_status*.tsv"
                                                        : "iw8_models_status*.tsv";
        for (const QString& fn : cat.entryList({pattern}, QDir::Files)) {
            QFile sf(cat.filePath(fn));
            if (!sf.open(QIODevice::ReadOnly | QIODevice::Text)) continue;
            QTextStream in(&sf);
            while (!in.atEnd()) {
                const QString l = in.readLine();
                if (l.startsWith('#')) continue;
                const QStringList f = l.split('\\t');
                if (f.size() >= 2 && f[1] != "ok") status.insert(f[0], {f[1], f.value(2)});
            }
        }
    }
    const QString file = data.filePath(kind == Kind::Materials ? "asset_catalog/iw8_materials.tsv"
                                       : kind == Kind::Effects ? "asset_catalog/iw8_common_vfx.tsv"
                                       : kind == Kind::Images  ? "asset_catalog/iw8_images.tsv"
                                       : kind == Kind::Sounds  ? "asset_catalog/iw8_common_sounds.tsv"
                                                               : "asset_catalog/iw8_models.tsv");
    c->fileName = QFileInfo(file).fileName();
    QFile f(file);
    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&f);
        in.readLine();  // header (names themselves may start with '#')
        while (!in.atEnd()) {
            const QString line = in.readLine();
            const int tab = line.indexOf('\\t');
            if (tab <= 0) continue;
            Catalog::Row row;
            row.name = line.left(tab);
            if (kind == Kind::Sounds) {
                // alias<TAB>loop<TAB>distMax<TAB>zones
                const QStringList s = line.split('\\t');
                if (line.startsWith('#') || s.size() < 4) continue;
                row.loop = s[1] == "1";
                row.tip = QString("%1\\n%2, audible to %3 units\\nzones: %4").arg(s[0], row.loop ? "loop" : "one-shot", s[2], s[3]);
                row.tone = row.loop ? 3 : 0;
                c->rows.push_back(std::move(row));
                continue;
            }
            if (kind == Kind::Models) {
                // Weapons, attachments and cosmetics that only the streaming zone lists (8.9k of 15.6k) are not map
                // props and ACTS cannot export them from there: left out so every listed model can be placed and built.
                bool streamOnly = true;
                for (const QString& z : line.mid(tab + 1).split(',', Qt::SkipEmptyParts))
                    streamOnly &= z.trimmed() == "global_stream_mp" || z.trimmed() == "techsets_global_stream_mp";
                if (streamOnly) {
                    ++c->hiddenStreamOnly;
                    continue;
                }
            }
            if (const auto st = status.constFind(row.name); st != status.constEnd()) {
                if (st->first == "missing" && buildableOnly) {
                    ++c->hiddenMissing;
                    continue;
                }
                row.tone = st->first == "grey" ? 1 : 2;
                row.tip = row.name + "\\nzones: " + line.mid(tab + 1).left(300) +
                          (st->first == "grey" ? "\\nBUILDS GREY: no colour image in Replay (" : "\\nNOT AVAILABLE in a build (") +
                          st->second.left(200) + ")";
                c->rows.push_back(std::move(row));
                continue;
            }
            row.tip = row.name + "\\nzones: " + line.mid(tab + 1).left(300);
            c->rows.push_back(std::move(row));
        }
    }
    // Colour images from ACTS exports (data/stock_assets/<zone>/index.json).
    if (kind == Kind::Materials) {
        const QDir assets(data.filePath("stock_assets"));
        for (const QString& zone : assets.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            QFile idx(assets.filePath(zone + "/index.json"));
            if (!idx.open(QIODevice::ReadOnly)) continue;
            const QJsonArray mats = QJsonDocument::fromJson(idx.readAll()).object().value("materials").toArray();
            for (const auto& v : mats) {
                const QJsonObject m = v.toObject();
                const QString img = m.value("color_image_file").toString();
                if (!img.isEmpty()) c->imageFiles.insert(m.value("name").toString(), assets.filePath(zone + "/" + img));
            }
        }
    }
    if (wantKits) {
        QFile kf(data.filePath("asset_catalog/iw8_kits.tsv"));
        const QString want = kind == Kind::Models ? "model" : "material";
        if (kf.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream in(&kf);
            while (!in.atEnd()) {
                const QStringList k = in.readLine().split('\\t');
                if (k.size() < 3 || k[0].startsWith('#') || k[0] == "kit" || k[1] != want || k[0].startsWith("excluded")) continue;
                c->kits[k[0]] << k[2];
            }
        }
    }
    return c;
}

void AssetBrowser::load() {
    const int gen = ++loadGen_;
    const bool buildable = buildableOnly_ && buildableOnly_->isChecked();
    const QString dataDir = repoData().absolutePath();
    const bool wantKits = kit_ && kitNames_.isEmpty();
    const Kind kind = kind_;
    if (model_->rowCount() == 0) info_->setText("Reading the catalogue...");
    QPointer<AssetBrowser> self(this);
    std::thread(guardedWorker(__FILE__, __LINE__, [self, gen, kind, buildable, dataDir, wantKits] {
        std::shared_ptr<const Catalog> cat = readCatalog(kind, buildable, dataDir, wantKits);
        QMetaObject::invokeMethod(qApp, [self, gen, cat] {
            if (self && gen == self->loadGen_) self->applyCatalog(*cat);  // a newer load() replaces this one
        }, Qt::QueuedConnection);
    })).detach();
}

QStandardItem* AssetBrowser::customItem(const QString& name, const QString& image) const {
    auto* item = new QStandardItem(name);
    item->setEditable(false);
    item->setData(true, Qt::UserRole + 1);
    item->setToolTip(name + "\\ncustom material: " + image);
    return item;
}

void AssetBrowser::applyCatalog(const Catalog& c) {
    loadedGen_ = loadGen_;
    hiddenStreamOnly_ = c.hiddenStreamOnly;
    hiddenMissing_ = c.hiddenMissing;
    QList<QStandardItem*> items;
    items.reserve(qsizetype(customMaterials_.size()) + qsizetype(c.rows.size()));
    for (const auto& [name, image] : customMaterials_) items.append(customItem(name, image));  // the custom ones first
    for (const Catalog::Row& r : c.rows) {
        auto* item = new QStandardItem(r.name);
        item->setEditable(false);
        item->setToolTip(r.tip);
        if (kind_ == Kind::Sounds) item->setData(r.loop, Qt::UserRole + 2);
        if (r.tone == 1) item->setForeground(QColor(200, 160, 90));
        else if (r.tone == 2) item->setForeground(QColor(130, 130, 130));
        else if (r.tone == 3) item->setForeground(QColor(120, 200, 255));
        items.append(item);
    }
    model_->clear();
    model_->invisibleRootItem()->appendRows(items);  // one insert (a row at a time made the proxy filter each)
    for (auto it = c.imageFiles.constBegin(); it != c.imageFiles.constEnd(); ++it) imageFiles_.insert(it.key(), it.value());
    if (kit_ && kitNames_.isEmpty() && !c.kits.isEmpty()) {
        kitNames_ = c.kits;
        QStringList names = kitNames_.keys();
        names.sort();
        const QSignalBlocker block(kit_);
        for (const QString& k : names) kit_->addItem(QString(k).replace('_', ' '), k);
        kit_->setVisible(true);
    }
    if (kind_ == Kind::Effects) {
        // Narrow column: the summary drops the source file name (the search tooltip says what the list is), so it stays on one line.
        catalogInfo_ = QString("%1 stock effects (Replay 1.20)").arg(c.rows.size())
                       + (hiddenMissing_ ? QString("; %1 not buildable hidden").arg(hiddenMissing_) : QString());
    } else {
        catalogInfo_ = (QString("%1 stock %2 (Replay 1.20, %3)%4")
                           .arg(c.rows.size())
                           .arg(kind_ == Kind::Materials ? "materials" : kind_ == Kind::Images ? "images"
                                : kind_ == Kind::Sounds ? "sound aliases (blue = loop)" : "models")
                           .arg(c.fileName)
                           .arg(kind_ == Kind::Materials ? QString(", %1 with thumbnails").arg(imageFiles_.size())
                                : kind_ == Kind::Models && hiddenStreamOnly_
                                    ? QString("; %1 weapon/attachment models of the streaming zone not listed").arg(hiddenStreamOnly_)
                                    : QString())
                           + (hiddenMissing_ ? QString("; %1 not buildable hidden").arg(hiddenMissing_) : QString()));
    }
    updateInfo();
    emit catalogReady();
}

void AssetBrowser::setCustomMaterials(const QList<QPair<QString, QString>>& materials) {
    customMaterials_ = materials;  // kept for a catalogue (re)load that lands later
    // Drop the previous custom rows (flagged with UserRole + 1), then insert the new ones at the top.
    for (int r = model_->rowCount() - 1; r >= 0; --r)
        if (model_->item(r)->data(Qt::UserRole + 1).toBool()) model_->removeRow(r);
    for (int i = materials.size() - 1; i >= 0; --i) {
        model_->insertRow(0, customItem(materials[i].first, materials[i].second));
        imageFiles_.insert(materials[i].first, materials[i].second);
        icons_.remove(materials[i].first);
    }
    updateInfo();
}

''')
    s = read(A)
    nl = '\r\n' if '\r\n' in s else '\n'
    s = s.replace('#include "brush_tools.h"' + nl, '#include "brush_tools.h"' + nl + '#include "worker_guard.h"' + nl, 1)
    s = s.replace('#include <set>' + nl, '#include <memory>' + nl + '#include <set>' + nl + '#include <thread>' + nl, 1)
    write(A, s)

# Header.
patch(H, '''    void previewRequested(const QString& name);  // Models: open the 3D preview window
''', '''    void previewRequested(const QString& name);  // Models: open the 3D preview window
    void catalogReady();  // the catalogue read on the worker thread is in the list
''')
patch(H, '''    void reloadCatalog() { load(); }  // after an availability audit (4.12)
''', '''    void reloadCatalog() { load(); }  // after an availability audit (4.12)
    // The catalogue is read on a worker thread (seconds of files at start-up): true once the list holds the latest read.
    bool catalogLoaded() const { return loadedGen_ == loadGen_; }
''')
patch(H, '''private:
    void load();
''', '''private:
    void load();  // starts reading the catalogue on a worker thread; applyCatalog() fills the list
    struct Catalog;
    static std::shared_ptr<Catalog> readCatalog(Kind kind, bool buildableOnly, const QString& dataDir, bool wantKits);
    void applyCatalog(const Catalog& c);
    QStandardItem* customItem(const QString& name, const QString& image) const;
    int loadGen_ = 0, loadedGen_ = -1;  // the latest load() started and the one in the list
    QList<QPair<QString, QString>> customMaterials_;  // the last setCustomMaterials list
''')
s = read(H)
if '#include <memory>' not in s:
    nl = '\r\n' if '\r\n' in s else '\n'
    s = s.replace('#pragma once' + nl, '#pragma once' + nl + nl + '#include <memory>' + nl, 1)
    write(H, s)
if 'class QStandardItem;' not in read(H):
    patch(H, 'class QStandardItemModel;\n', 'class QStandardItem;\nclass QStandardItemModel;\n')

# MainWindow::waitForCatalogs.
M = 'src/qtapp/main_window.cpp'
MH = 'src/qtapp/main_window.h'
patch(MH, '''    AssetBrowser* materialsBrowser() const { return materials_; }  // tests
''', '''    AssetBrowser* materialsBrowser() const { return materials_; }  // tests
    // Automated runs (GUI test, feature shots, bench, smoke): waits, running the event loop, until every asset browser
    // has its catalogue (read on worker threads). False on the time-out.
    bool waitForCatalogs(int timeoutMs = 120000);
''')
if 'bool MainWindow::waitForCatalogs' not in read(M):
    s = read(M)
    nl = '\r\n' if '\r\n' in s else '\n'
    anchor = 'MainWindow::MainWindow(ID3D11Device* dev, ID3D11DeviceContext* dc, QWidget* parent)'
    i = s.index(anchor)
    add = '''bool MainWindow::waitForCatalogs(int timeoutMs) {
    const auto ready = [this] {
        for (AssetBrowser* b : {materials_, models_, effects_, images_, sounds_})
            if (b && !b->catalogLoaded()) return false;
        return true;
    };
    QElapsedTimer t;
    t.start();
    while (!ready() && t.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(5);
    }
    return ready();
}

'''.replace('\n', nl)
    s = s[:i] + add + s[i:]
    write(M, s)
print('ok')
