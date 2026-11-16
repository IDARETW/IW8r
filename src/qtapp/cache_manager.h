// IW8 Radiant - Tools > Cache Manager (roadmap 4.13): size of every cache under the app data folder, purge,
// a size cap for the caches that refetch on demand, and moving the caches to another drive.
//
// Caches whose index is loaded while the editor runs (material, model, world, thumbnails, image browser) are purged and
// trimmed at the next start, before any index is read: runCacheStartupTasks() in main.cpp. The indexes drop entries
// whose files are gone and fetch them again when needed. A move copies each cache folder to the chosen folder and
// leaves a directory junction at the old path, so every path the editor and the converter use keeps working.
#pragma once

#include <QDialog>
#include <QList>
#include <QString>

class QTableWidget;
class QLabel;
class QDoubleSpinBox;

namespace iw8r::qt {

struct CacheFolder {
    QString key;           // folder name under the app data folder
    QString label;         // shown in the dialog
    QString path;          // absolute
    bool trimmable = false;  // the size cap may delete its oldest files
    bool live = false;       // its index is loaded while the editor runs: purge at the next start
};

QList<CacheFolder> cacheFolders();
qint64 folderBytes(const QString& path, int* files = nullptr);

// Pending purges (setting cache/purge), a pending move (cache/moveTo) and the size cap (cache/capGB, applied to the
// trimmable caches together, oldest files first). Call before MainWindow exists. Returns what it did (empty: nothing).
QString runCacheStartupTasks();

class CacheManagerDialog : public QDialog {
    Q_OBJECT
public:
    explicit CacheManagerDialog(QWidget* parent = nullptr);
    void refresh();
    int rowCount() const;
    QString rowKey(int row) const;
    qint64 rowBytes(int row) const;
    // Purge (immediately when the cache is not live, else at the next start). Returns the status text.
    QString purge(const QString& key, bool confirm = true);
    QStringList pendingPurges() const;

private:
    QTableWidget* table_ = nullptr;
    QLabel* total_ = nullptr;
    QDoubleSpinBox* cap_ = nullptr;
    QList<CacheFolder> folders_;
    QList<qint64> bytes_;
};

}  // namespace iw8r::qt
