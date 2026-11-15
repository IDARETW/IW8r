#include "cache_manager.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QStandardPaths>
#include <QTableWidget>
#include <QUrl>
#include <QVBoxLayout>

#include <vector>

#include "cache_trim.h"
#include "settings.h"

namespace iw8r::qt {
namespace {

QString appData() { return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation); }

QString fmt(qint64 b) { return QString::fromStdString(formatCacheBytes(uint64_t(std::max<qint64>(0, b)))); }

bool copyTree(const QString& from, const QString& to) {
    QDir().mkpath(to);
    QDirIterator it(from, QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString f = it.next();
        const QString rel = QDir(from).relativeFilePath(f);
        const QString dest = QDir(to).filePath(rel);
        QDir().mkpath(QFileInfo(dest).absolutePath());
        QFile::remove(dest);
        if (!QFile::copy(f, dest)) return false;
    }
    return true;
}

// Removes the folder's contents (the folder itself stays; a junction is left alone and its target emptied).
void emptyFolder(const QString& path) {
    QDir d(path);
    for (const QFileInfo& fi : d.entryInfoList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot)) {
        if (fi.isDir() && !fi.isSymLink() && !fi.isJunction()) QDir(fi.absoluteFilePath()).removeRecursively();
        else QFile::remove(fi.absoluteFilePath());
    }
}

}  // namespace

QList<CacheFolder> cacheFolders() {
    const QString root = appData();
    const auto f = [&](const char* key, const char* label, bool trim, bool live) {
        return CacheFolder{key, label, QDir(root).filePath(key), trim, live};
    };
    return {
        f("material_cache", "Stock material textures (ACTS exports)", true, true),
        f("model_cache", "Stock model meshes and LODs", true, true),
        f("world_cache", "Stock map geometry (references)", false, true),
        f("x64zt_output", "Built maps and the converter's stock cache", false, false),
        f("thumbs", "Browser thumbnail packs", false, true),
        f("image_browser", "Images browser previews", true, true),
        f("vfx_cache", "Effect exports (Effect Info, previews)", true, false),
        f("stock_cache", "Stock asset scratch", true, false),
        f("shader_cache", "Compiled viewport shaders", false, true),
        f("crashes", "Crash reports", false, false),
    };
}

qint64 folderBytes(const QString& path, int* files) {
    qint64 total = 0;
    int n = 0;
    QDirIterator it(path, QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        total += it.fileInfo().size();
        ++n;
    }
    if (files) *files = n;
    return total;
}

QString runCacheStartupTasks() {
    Settings& s = Settings::instance();
    QStringList done;
    const QList<CacheFolder> folders = cacheFolders();
    // 1. Purges asked for in the last session.
    const QStringList purge = s.value("cache/purge").toStringList();
    for (const CacheFolder& c : folders)
        if (purge.contains(c.key) && QFileInfo::exists(c.path)) {
            emptyFolder(c.path);
            done << "purged " + c.key;
        }
    if (!purge.isEmpty()) s.setValue("cache/purge", QStringList());
    // 2. A move to another folder: copy, remove, junction at the old path.
    const QString moveTo = s.value("cache/moveTo").toString();
    if (!moveTo.isEmpty()) {
        s.setValue("cache/moveTo", QString());
        for (const CacheFolder& c : folders) {
            const QFileInfo fi(c.path);
            if (fi.isJunction() || fi.isSymLink() || c.key == "crashes") continue;
            const QString dest = QDir(moveTo).filePath(c.key);
            if (fi.exists()) {
                if (!copyTree(c.path, dest)) {
                    done << "move of " + c.key + " failed (copy); left in place";
                    continue;
                }
                QDir(c.path).removeRecursively();
            } else {
                QDir().mkpath(dest);
            }
            const int rc = QProcess::execute("cmd", {"/c", "mklink", "/J", QDir::toNativeSeparators(c.path), QDir::toNativeSeparators(dest)});
            if (rc != 0) {
                // No junction: put the files back so nothing is lost.
                copyTree(dest, c.path);
                done << "junction for " + c.key + " failed; kept in the app data folder";
            } else {
                done << "moved " + c.key + " to " + dest;
            }
        }
    }
    // 3. The size cap on the trimmable caches together.
    const double capGB = s.value("cache/capGB", 0.0).toDouble();
    if (capGB > 0) {
        std::vector<CacheFile> files;
        QStringList paths;
        for (const CacheFolder& c : folders) {
            if (!c.trimmable) continue;
            QDirIterator it(c.path, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
            while (it.hasNext()) {
                const QString p = it.next();
                const QFileInfo& fi = it.fileInfo();
                files.push_back({p.toStdString(), uint64_t(fi.size()), fi.lastModified().toSecsSinceEpoch()});
                paths << p;
            }
        }
        const auto trim = cacheFilesToTrim(files, uint64_t(capGB * 1e9));
        uint64_t freed = 0;
        for (size_t i : trim)
            if (QFile::remove(paths[qsizetype(i)])) freed += files[i].bytes;
        if (!trim.empty()) done << QString("cache cap %1 GB: removed %2 old file(s), %3").arg(capGB).arg(trim.size()).arg(fmt(qint64(freed)));
    }
    return done.join("; ");
}

CacheManagerDialog::CacheManagerDialog(QWidget* parent) : QDialog(parent) {
    setObjectName("cacheManagerDialog");
    setWindowTitle("Cache Manager");
    auto* lay = new QVBoxLayout(this);
    auto* intro = new QLabel("Everything here is fetched or rebuilt again when needed. Caches the editor has open are purged "
                             "at the next start.", this);
    intro->setWordWrap(true);
    lay->addWidget(intro);
    table_ = new QTableWidget(0, 4, this);
    table_->setObjectName("cacheTable");
    table_->setHorizontalHeaderLabels({"Cache", "Size", "Files", "Folder"});
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->verticalHeader()->hide();
    table_->setMinimumSize(760, 300);
    lay->addWidget(table_);
    total_ = new QLabel(this);
    lay->addWidget(total_);
    auto* row = new QHBoxLayout;
    auto* refreshB = new QPushButton("Refresh", this);
    auto* open = new QPushButton("Open Folder", this);
    auto* purgeB = new QPushButton("Purge...", this);
    purgeB->setObjectName("cachePurge");
    auto* move = new QPushButton("Move Caches...", this);
    row->addWidget(refreshB);
    row->addWidget(open);
    row->addWidget(purgeB);
    row->addWidget(move);
    row->addStretch();
    row->addWidget(new QLabel("Size cap (textures, meshes, previews):", this));
    cap_ = new QDoubleSpinBox(this);
    cap_->setObjectName("cacheCap");
    cap_->setRange(0, 4000);
    cap_->setDecimals(1);
    cap_->setSuffix(" GB");
    cap_->setSpecialValueText("none");
    cap_->setValue(Settings::instance().value("cache/capGB", 0.0).toDouble());
    cap_->setToolTip("Applied at the next start: the oldest files of the trimmable caches are removed until they fit");
    row->addWidget(cap_);
    lay->addLayout(row);
    connect(cap_, &QDoubleSpinBox::valueChanged, this, [](double v) { Settings::instance().setValue("cache/capGB", v); });
    connect(refreshB, &QPushButton::clicked, this, &CacheManagerDialog::refresh);
    connect(open, &QPushButton::clicked, this, [this] {
        const int r = table_->currentRow();
        const QString p = r >= 0 && r < folders_.size() ? folders_[r].path : appData();
        QDir().mkpath(p);
        QDesktopServices::openUrl(QUrl::fromLocalFile(p));
    });
    connect(purgeB, &QPushButton::clicked, this, [this] {
        const int r = table_->currentRow();
        if (r < 0 || r >= folders_.size()) return;
        total_->setText(purge(folders_[r].key, true));
    });
    connect(move, &QPushButton::clicked, this, [this] {
        const QString dir = QFileDialog::getExistingDirectory(this, "Move the caches to (another drive)");
        if (dir.isEmpty()) return;
        if (QMessageBox::question(this, "Move Caches",
                                  "At the next start the caches are copied to\n" + dir +
                                      "\nand removed from the app data folder; a directory junction keeps the old paths "
                                      "working. Continue?") != QMessageBox::Yes)
            return;
        Settings::instance().setValue("cache/moveTo", dir);
        total_->setText("The caches move to " + dir + " at the next start.");
    });
    refresh();
}

void CacheManagerDialog::refresh() {
    folders_ = cacheFolders();
    bytes_.clear();
    table_->setRowCount(int(folders_.size()));
    const QStringList pending = pendingPurges();
    qint64 total = 0;
    for (int r = 0; r < folders_.size(); ++r) {
        const CacheFolder& c = folders_[r];
        int files = 0;
        const qint64 b = folderBytes(c.path, &files);
        bytes_ << b;
        total += b;
        const QFileInfo fi(c.path);
        QString label = c.label + (pending.contains(c.key) ? "  (purged at the next start)" : QString());
        table_->setItem(r, 0, new QTableWidgetItem(label));
        table_->setItem(r, 1, new QTableWidgetItem(fmt(b)));
        table_->setItem(r, 2, new QTableWidgetItem(QString::number(files)));
        table_->setItem(r, 3, new QTableWidgetItem(QDir::toNativeSeparators(c.path) + (fi.isJunction() ? "  (junction)" : QString())));
        table_->item(r, 0)->setData(Qt::UserRole, c.key);
    }
    table_->resizeColumnsToContents();
    total_->setText("Total: " + fmt(total) + " in " + QDir::toNativeSeparators(appData()));
}

int CacheManagerDialog::rowCount() const { return int(folders_.size()); }
QString CacheManagerDialog::rowKey(int row) const { return row >= 0 && row < folders_.size() ? folders_[row].key : QString(); }
qint64 CacheManagerDialog::rowBytes(int row) const { return row >= 0 && row < bytes_.size() ? bytes_[row] : 0; }

QStringList CacheManagerDialog::pendingPurges() const { return Settings::instance().value("cache/purge").toStringList(); }

QString CacheManagerDialog::purge(const QString& key, bool confirm) {
    int idx = -1;
    for (int i = 0; i < folders_.size(); ++i)
        if (folders_[i].key == key) idx = i;
    if (idx < 0) return "No cache " + key;
    const CacheFolder c = folders_[idx];
    if (confirm &&
        QMessageBox::question(this, "Purge Cache",
                              "Delete everything in\n" + QDir::toNativeSeparators(c.path) + "\n(" + fmt(rowBytes(idx)) +
                                  ")? It is fetched or rebuilt again when needed; this cannot be undone." +
                                  (c.live ? "\n\nThe editor has this cache open: it is emptied at the next start." : QString())) !=
            QMessageBox::Yes)
        return "Purge cancelled";
    QString msg;
    if (c.live) {
        QStringList p = pendingPurges();
        if (!p.contains(key)) p << key;
        Settings::instance().setValue("cache/purge", p);
        msg = c.label + ": purged at the next start";
    } else {
        emptyFolder(c.path);
        msg = c.label + ": purged";
    }
    refresh();
    return msg;
}

}  // namespace iw8r::qt
