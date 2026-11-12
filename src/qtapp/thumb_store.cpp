#include "thumb_store.h"

#include <QBuffer>
#include <QDataStream>
#include <QDir>
#include <QFileInfo>
#include <QSaveFile>

namespace iw8r::qt {

namespace {
constexpr char kMagic[8] = {'I', 'W', '8', 'R', 'T', 'H', 'M', '1'};
constexpr quint32 kMaxName = 4096, kMaxJpeg = 1u << 20;

// Record: u16 name length, name (UTF-8), u64 signature, u32 JPEG length, JPEG bytes. Little endian.
qint64 headerBytes(int nameLen) { return 2 + nameLen + 8 + 4; }
}  // namespace

ThumbStore::ThumbStore(const QString& packPath) : path_(packPath) {
    QDir().mkpath(QFileInfo(packPath).absolutePath());
    load();
}

quint64 ThumbStore::signatureOf(const QString& file) {
    const QFileInfo fi(file);
    if (file.isEmpty() || !fi.isFile()) return 0;
    return (quint64(fi.size()) << 20) ^ quint64(fi.lastModified().toMSecsSinceEpoch());
}

void ThumbStore::load() {
    index_.clear();
    fileBytes_ = garbage_ = 0;
    QFile f(path_);
    if (!f.open(QIODevice::ReadOnly)) return;
    if (f.read(8) != QByteArray(kMagic, 8)) return;
    qint64 pos = 8;
    const qint64 size = f.size();
    QDataStream in(&f);
    in.setByteOrder(QDataStream::LittleEndian);
    while (pos < size) {
        quint16 nameLen = 0;
        quint64 sig = 0;
        quint32 len = 0;
        if (size - pos < 2) break;
        in >> nameLen;
        if (nameLen == 0 || nameLen > kMaxName || size - pos < headerBytes(nameLen)) break;
        const QByteArray name = f.read(nameLen);
        in >> sig >> len;
        if (in.status() != QDataStream::Ok || len > kMaxJpeg || size - pos - headerBytes(nameLen) < qint64(len)) break;
        Rec r;
        r.signature = sig;
        r.offset = pos + headerBytes(nameLen);
        r.length = len;
        r.recordBytes = headerBytes(nameLen) + len;
        const QString key = QString::fromUtf8(name);
        if (auto it = index_.find(key); it != index_.end()) garbage_ += it->recordBytes;
        index_.insert(key, r);
        pos += r.recordBytes;
        if (!f.seek(pos)) break;
    }
    fileBytes_ = pos;  // anything after is a torn record
}

QImage ThumbStore::get(const QString& name, quint64 signature) {
    const auto it = index_.constFind(name);
    if (it == index_.constEnd() || it->signature != signature || signature == 0) return {};
    QFile f(path_);
    if (!f.open(QIODevice::ReadOnly) || !f.seek(it->offset)) return {};
    QImage img;
    img.loadFromData(f.read(it->length), "JPG");
    return img;
}

QImage ThumbStore::getAny(const QString& name) {
    const auto it = index_.constFind(name);
    if (it == index_.constEnd()) return {};
    QFile f(path_);
    if (!f.open(QIODevice::ReadOnly) || !f.seek(it->offset)) return {};
    QImage img;
    img.loadFromData(f.read(it->length), "JPG");
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
    QByteArray jpeg;
    {
        QBuffer b(&jpeg);
        b.open(QIODevice::WriteOnly);
        image.convertToFormat(QImage::Format_RGB32).save(&b, "JPG", 85);
    }
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

}  // namespace iw8r::qt
