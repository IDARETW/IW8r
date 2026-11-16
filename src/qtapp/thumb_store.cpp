#include "thumb_store.h"

#include <QBuffer>
#include <QDataStream>
#include <QDir>
#include <QFileInfo>
#include <QPointer>
#include <QSaveFile>

#include <cstring>

namespace iw8r::qt {

namespace {
constexpr char kMagic[8] = {'I', 'W', '8', 'R', 'T', 'H', 'M', '1'};
constexpr quint32 kMaxName = 4096, kMaxJpeg = 1u << 20;

// Record: u16 name length, name (UTF-8), u64 signature, u32 JPEG length, JPEG bytes. Little endian.
qint64 headerBytes(int nameLen) { return 2 + nameLen + 8 + 4; }
}  // namespace

ThumbStore::ThumbStore(const QString& packPath) : path_(packPath) {
    QDir().mkpath(QFileInfo(packPath).absolutePath());
    // The record index is read on a worker thread (opening and mapping the 74 MB material pack held the start-up for
    // about half a second); the first use waits for it (ready()).
    pending_ = std::async(std::launch::async, [p = packPath] { return scan(p); });
}

void ThumbStore::ready() const {
    std::call_once(once_, [this] {
        Scan s = pending_.get();
        index_ = std::move(s.index);
        fileBytes_ = s.fileBytes;
        garbage_ = s.garbage;
    });
}

bool ThumbStore::contains(const QString& name) const {
    ready();
    std::lock_guard lock(mx_);
    return index_.contains(name);
}

int ThumbStore::count() const {
    ready();
    std::lock_guard lock(mx_);
    return int(index_.size());
}

qint64 ThumbStore::fileBytes() const {
    ready();
    std::lock_guard lock(mx_);
    return fileBytes_;
}

qint64 ThumbStore::garbageBytes() const {
    ready();
    std::lock_guard lock(mx_);
    return garbage_;
}

quint64 ThumbStore::signatureOf(const QString& file) {
    const QFileInfo fi(file);
    if (file.isEmpty() || !fi.isFile()) return 0;
    return (quint64(fi.size()) << 20) ^ quint64(fi.lastModified().toMSecsSinceEpoch());
}

ThumbStore::Scan ThumbStore::scan(const QString& path) {
    Scan out;
    QHash<QString, Rec>& index_ = out.index;
    qint64& garbage_ = out.garbage;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return out;
    const qint64 size = f.size();
    if (size < 8) return out;
    // The record headers are read from a mapped view of the file (a seek and three reads per record took about a
    // second on the GUI thread at start-up for the 74 MB material pack).
    const uchar* base = f.map(0, size);
    if (!base) return out;
    if (std::memcmp(base, kMagic, 8) != 0) {
        f.unmap(const_cast<uchar*>(base));
        return out;
    }
    const auto le = [&](qint64 at, int bytes) {
        quint64 v = 0;
        for (int i = bytes - 1; i >= 0; --i) v = (v << 8) | base[at + i];
        return v;
    };
    qint64 pos = 8;
    index_.reserve(int(size / 4096));
    while (pos < size) {
        if (size - pos < 2) break;
        const int nameLen = int(le(pos, 2));
        if (nameLen == 0 || nameLen > int(kMaxName) || size - pos < headerBytes(nameLen)) break;
        const quint64 sig = le(pos + 2 + nameLen, 8);
        const quint32 len = quint32(le(pos + 2 + nameLen + 8, 4));
        if (len > kMaxJpeg || size - pos - headerBytes(nameLen) < qint64(len)) break;
        Rec r;
        r.signature = sig;
        r.offset = pos + headerBytes(nameLen);
        r.length = len;
        r.recordBytes = headerBytes(nameLen) + len;
        const QString key = QString::fromUtf8(reinterpret_cast<const char*>(base + pos + 2), nameLen);
        if (auto it = index_.find(key); it != index_.end()) garbage_ += it->recordBytes;
        index_.insert(key, r);
        pos += r.recordBytes;
    }
    f.unmap(const_cast<uchar*>(base));
    out.fileBytes = pos;  // anything after is a torn record
    return out;
}

QImage ThumbStore::get(const QString& name, quint64 signature) {
    ready();
    QByteArray jpeg;
    {
        // The bytes are read under the lock (a compaction rewrites the file); the JPEG is decoded outside it.
        std::lock_guard lock(mx_);
        const auto it = index_.constFind(name);
        if (it == index_.constEnd() || it->signature != signature || signature == 0) return {};
        QFile f(path_);
        if (!f.open(QIODevice::ReadOnly) || !f.seek(it->offset)) return {};
        jpeg = f.read(it->length);
    }
    QImage img;
    img.loadFromData(jpeg, "JPG");
    return img;
}

QImage ThumbStore::getAny(const QString& name) {
    ready();
    QByteArray jpeg;
    {
        std::lock_guard lock(mx_);
        const auto it = index_.constFind(name);
        if (it == index_.constEnd()) return {};
        QFile f(path_);
        if (!f.open(QIODevice::ReadOnly) || !f.seek(it->offset)) return {};
        jpeg = f.read(it->length);
    }
    QImage img;
    img.loadFromData(jpeg, "JPG");
    return img;
}

bool ThumbStore::appendRecord(QFile& f, const QString& name, quint64 signature, const QByteArray& jpeg, Rec* rec) {
    const QByteArray n = name.toUtf8();
    if (n.isEmpty() || quint32(n.size()) > kMaxName || quint32(jpeg.size()) > kMaxJpeg) return false;
    QByteArray buf;
    {
        QDataStream out(&buf, QIODevice::WriteOnly);
        out.setByteOrder(QDataStream::LittleEndian);
        out << quint16(n.size());
        out.writeRawData(n.constData(), int(n.size()));
        out << signature << quint32(jpeg.size());
        out.writeRawData(jpeg.constData(), int(jpeg.size()));
    }
    const qint64 at = f.pos();
    if (f.write(buf) != buf.size()) return false;
    rec->signature = signature;
    rec->offset = at + headerBytes(int(n.size()));
    rec->length = quint32(jpeg.size());
    rec->recordBytes = buf.size();
    return true;
}

void ThumbStore::put(const QString& name, quint64 signature, const QImage& image) {
    if (image.isNull() || signature == 0) return;
    ready();
    QByteArray jpeg;
    {
        QBuffer b(&jpeg);
        b.open(QIODevice::WriteOnly);
        image.convertToFormat(QImage::Format_RGB32).save(&b, "JPG", 85);
    }
    std::lock_guard lock(mx_);
    QFile f(path_);
    if (!f.open(QIODevice::ReadWrite)) return;
    if (fileBytes_ < 8) {
        f.resize(0);
        f.write(kMagic, 8);
        fileBytes_ = 8;
    } else if (f.size() != fileBytes_) {
        f.resize(fileBytes_);  // drop a torn tail
    }
    f.seek(fileBytes_);
    Rec r;
    if (!appendRecord(f, name, signature, jpeg, &r)) return;
    if (auto it = index_.find(name); it != index_.end()) garbage_ += it->recordBytes;
    index_.insert(name, r);
    fileBytes_ += r.recordBytes;
    f.close();
    if (garbage_ > 32 * 1024 && garbage_ * 4 > fileBytes_) compact();
}

void ThumbStore::compact() {
    QFile src(path_);
    if (!src.open(QIODevice::ReadOnly)) return;
    QSaveFile dst(path_);
    if (!dst.open(QIODevice::WriteOnly)) return;
    dst.write(kMagic, 8);
    QHash<QString, Rec> fresh;
    qint64 bytes = 8;
    for (auto it = index_.constBegin(); it != index_.constEnd(); ++it) {
        if (!src.seek(it->offset)) continue;
        const QByteArray jpeg = src.read(it->length);
        if (jpeg.size() != qint64(it->length)) continue;
        // Same record layout as appendRecord, serialised through a buffer (QSaveFile is not a QFile).
        QByteArray buf;
        QBuffer b(&buf);
        b.open(QIODevice::WriteOnly);
        const QByteArray n = it.key().toUtf8();
        QDataStream out(&b);
        out.setByteOrder(QDataStream::LittleEndian);
        out << quint16(n.size());
        out.writeRawData(n.constData(), int(n.size()));
        out << it->signature << quint32(jpeg.size());
        out.writeRawData(jpeg.constData(), int(jpeg.size()));
        if (dst.write(buf) != buf.size()) {
            dst.cancelWriting();
            return;
        }
        Rec r = *it;
        r.offset = bytes + headerBytes(int(n.size()));
        r.recordBytes = buf.size();
        fresh.insert(it.key(), r);
        bytes += buf.size();
    }
    src.close();
    if (!dst.commit()) return;
    index_ = std::move(fresh);
    fileBytes_ = bytes;
    garbage_ = 0;
}

// ---------------------------------------------------------------------------------------------------------------
// ThumbDecoder

struct ThumbDecoder::Shared {
    std::mutex mx;
    std::condition_variable cv;
    std::deque<QString> queue;
    int side = 64;
    int active = 0;
    bool stop = false;
    Peek peek;
    Done done;
    QPointer<QObject> context;
};

ThumbDecoder::ThumbDecoder(Peek peek, Done done, QObject* context, int threads) : s_(std::make_shared<Shared>()) {
    s_->peek = std::move(peek);
    s_->done = std::move(done);
    s_->context = context;
    for (int i = 0; i < std::max(1, threads); ++i)
        threads_.emplace_back([s = s_] {
            for (;;) {
                std::vector<QString> batch;
                int side = 64;
                {
                    std::unique_lock lock(s->mx);
                    s->cv.wait(lock, [&] { return s->stop || !s->queue.empty(); });
                    if (s->stop) return;
                    // A few names per batch: the first tiles appear quickly, and a new want() is seen soon.
                    while (!s->queue.empty() && batch.size() < 6) {
                        batch.push_back(std::move(s->queue.front()));
                        s->queue.pop_front();
                    }
                    side = s->side;
                    ++s->active;
                }
                std::vector<Result> out;
                out.reserve(batch.size());
                for (const QString& n : batch) {
                    QImage img = s->peek ? s->peek(n) : QImage();
                    out.push_back({n, img.isNull() ? QImage() : fit(img, side), side});
                }
                QObject* ctx = nullptr;
                {
                    std::lock_guard lock(s->mx);
                    --s->active;
                    ctx = s->context.data();
                    if (s->stop) return;
                }
                // Back on the GUI thread; dropped when the context object is gone by then.
                if (ctx)
                    QMetaObject::invokeMethod(ctx, [s, out = std::move(out)]() mutable {
                        if (s->done) s->done(std::move(out));
                    }, Qt::QueuedConnection);
            }
        });
}

ThumbDecoder::~ThumbDecoder() {
    {
        std::lock_guard lock(s_->mx);
        s_->stop = true;
        s_->queue.clear();
    }
    s_->cv.notify_all();
    for (std::thread& t : threads_) t.join();
}

void ThumbDecoder::want(const QStringList& names, int side) {
    {
        std::lock_guard lock(s_->mx);
        s_->queue.assign(names.begin(), names.end());
        s_->side = side;
    }
    s_->cv.notify_all();
}

bool ThumbDecoder::busy() const {
    std::lock_guard lock(s_->mx);
    return !s_->queue.empty() || s_->active > 0;
}

QImage ThumbDecoder::fit(const QImage& img, int side) {
    if (img.isNull() || side <= 0) return img;
    QImage out = img.width() > side || img.height() > side ? img.scaled(side, side, Qt::KeepAspectRatio, Qt::SmoothTransformation) : img;
    return out.format() == QImage::Format_RGB32 || out.format() == QImage::Format_ARGB32_Premultiplied
               ? out
               : out.convertToFormat(QImage::Format_ARGB32_Premultiplied);
}

}  // namespace iw8r::qt
