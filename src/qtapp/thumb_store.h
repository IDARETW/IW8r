// IW8 Radiant Suite - persistent thumbnail store for the asset browsers.
//
// One append-only pack file per browser kind (<AppLocalData>/thumbs/<kind>.pack): each record is the asset name, a
// signature of the source it was made from (size + modification time of the cached DDS / GLB) and a small JPEG.
// Opening reads only the record headers into an index; a lookup reads one JPEG. A source that changed (new signature)
// gets a new record and the old one becomes garbage; the pack is rewritten without garbage when it reaches a quarter of
// the file. A torn last record (crash while appending) is ignored and truncated on the next write.
// Reads may come from worker threads (ThumbDecoder) while the GUI thread appends: the index and the file are guarded.
//
// ThumbDecoder: the browsers' pictures read from the packs and fitted to the tile size on worker threads, so a panel
// shows its stored thumbnails as soon as it is opened or scrolled, without loading them on the GUI thread.
#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include <QFile>
#include <QHash>
#include <QImage>
#include <QString>

namespace iw8r::qt {

// The tiles of a browser on screen now: how many, how many show their real picture, how many still wait for it (a
// placeholder on screen). The rest have no picture at all (nothing exported, a failed export). Bench and tests.
struct VisibleThumbs {
    int visible = 0, real = 0, waiting = 0;
};

class ThumbStore {
public:
    explicit ThumbStore(const QString& packPath);

    // Signature of a source file (0 when it does not exist).
    static quint64 signatureOf(const QString& file);

    // The stored thumbnail when its signature matches, else a null image.
    QImage get(const QString& name, quint64 signature);
    // The stored thumbnail whatever its signature (4.13 full packs: the source file may be gone), else a null image.
    QImage getAny(const QString& name);
    bool contains(const QString& name) const;
    // Stores a thumbnail (encoded as JPEG); replaces an older record of the same name.
    void put(const QString& name, quint64 signature, const QImage& image);

    int count() const;
    qint64 fileBytes() const;
    qint64 garbageBytes() const;

private:
    struct Rec {
        quint64 signature = 0;
        qint64 offset = 0;  // of the JPEG bytes
        quint32 length = 0;
        qint64 recordBytes = 0;
    };
    struct Scan {
        QHash<QString, Rec> index;
        qint64 fileBytes = 0, garbage = 0;
    };
    static Scan scan(const QString& path);  // worker thread
    void ready() const;                     // waits for the scan once and takes its result
    void compact();
    bool appendRecord(QFile& f, const QString& name, quint64 signature, const QByteArray& jpeg, Rec* rec);

    QString path_;
    mutable std::future<Scan> pending_;
    mutable std::once_flag once_;
    mutable QHash<QString, Rec> index_;
    mutable qint64 fileBytes_ = 0;  // valid bytes (a torn tail is excluded)
    mutable qint64 garbage_ = 0;
    mutable std::mutex mx_;  // index_, fileBytes_, garbage_ and the file (worker reads, GUI-thread appends)
};

// Background decoding of stored thumbnails. `peek` must be thread-safe and only read (packs, files): it returns the
// stored picture of a name or a null image. want() replaces the names not started yet with a new list (the rows on
// screen first, then the ones around them), so a fast scroll never decodes pages it has left; results come back on the
// GUI thread in small batches through `done`, fitted into side x side (aspect kept).
class ThumbDecoder {
public:
    using Peek = std::function<QImage(const QString& name)>;
    struct Result {
        QString name;
        QImage image;  // null: nothing stored (the caller makes or requests it)
        int side = 0;
    };
    using Done = std::function<void(std::vector<Result>)>;
    ThumbDecoder(Peek peek, Done done, QObject* context, int threads = 2);
    ~ThumbDecoder();
    void want(const QStringList& names, int side);
    bool busy() const;  // names queued or being decoded
    static QImage fit(const QImage& img, int side);  // the tile picture (any thread)

private:
    struct Shared;
    std::shared_ptr<Shared> s_;
    std::vector<std::thread> threads_;
};

}  // namespace iw8r::qt
