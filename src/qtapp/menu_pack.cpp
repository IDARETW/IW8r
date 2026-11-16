#include "menu_pack.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QImageWriter>
#include <QMutex>
#include <QSaveFile>
#include <QStandardPaths>

#include <algorithm>
#include <cstring>
#include <map>

namespace iw8r::qt {

namespace {

constexpr char kMagic[8] = {'I', 'W', '8', 'R', 'M', 'P', '0', '1'};
constexpr int kHeaderBytes = 72;  // magic, 4 counts, 5 offsets, 2 text sizes

template <class T>
T rd(const uchar* p) {
    T v;
    std::memcpy(&v, p, sizeof v);
    return v;
}
template <class T>
void wr(QByteArray& b, T v) {
    b.append(reinterpret_cast<const char*>(&v), sizeof v);
}
template <class T>
void put(QByteArray& b, qsizetype at, T v) {
    std::memcpy(b.data() + at, &v, sizeof v);
}

// The frame header's size fields (SOF0 / SOF1: segment start + 5 .. + 8, big endian).
bool frameSizeAt(const QByteArray& j, qsizetype* at) {
    qsizetype i = 2;
    while (i + 4 <= j.size()) {
        if (uchar(j[i]) != 0xFF) return false;
        const uchar m = uchar(j[i + 1]);
        const int len = (uchar(j[i + 2]) << 8) | uchar(j[i + 3]);
        if (m == 0xC0 || m == 0xC1) {
            *at = i + 5;
            return i + 9 <= j.size();
        }
        if (m == 0xDA) return false;
        i += 2 + len;
    }
    return false;
}

}  // namespace

bool splitJpeg(const QByteArray& j, QByteArray* header, QByteArray* scan, int* width, int* height) {
    if (j.size() < 4 || uchar(j[0]) != 0xFF || uchar(j[1]) != 0xD8) return false;
    qsizetype sof = -1;
    if (!frameSizeAt(j, &sof)) return false;
    // The start-of-scan segment ends the header.
    qsizetype i = 2;
    while (i + 4 <= j.size()) {
        if (uchar(j[i]) != 0xFF) return false;
        const uchar m = uchar(j[i + 1]);
        const int len = (uchar(j[i + 2]) << 8) | uchar(j[i + 3]);
        if (m == 0xDA) {
            const qsizetype end = i + 2 + len;
            if (end > j.size()) return false;
            *height = (uchar(j[sof]) << 8) | uchar(j[sof + 1]);
            *width = (uchar(j[sof + 2]) << 8) | uchar(j[sof + 3]);
            *header = j.left(end);
            for (int k = 0; k < 4; ++k) (*header)[sof + k] = 0;
            *scan = j.mid(end);
            return true;
        }
        i += 2 + len;
    }
    return false;
}

QByteArray joinJpeg(const QByteArray& header, const QByteArray& scan, int width, int height) {
    QByteArray j = header + scan;
    qsizetype sof = -1;
    if (!frameSizeAt(j, &sof)) return {};
    j[sof] = char(height >> 8), j[sof + 1] = char(height & 0xFF), j[sof + 2] = char(width >> 8), j[sof + 3] = char(width & 0xFF);
    return j;
}

// ------------------------------------------------------------------------------------------------- MenuPack

MenuPack::MenuPack(const QString& path) : file_(path) {
    const auto fail = [this](const QString& why) {
        error_ = why;
        valid_ = false;
    };
    if (!file_.open(QIODevice::ReadOnly)) { fail("cannot open " + path); return; }
    size_ = file_.size();
    map_ = size_ >= kHeaderBytes ? file_.map(0, size_) : nullptr;
    if (!map_) { fail("cannot map " + path); return; }
    if (std::memcmp(map_, kMagic, 8) != 0) { fail("not a menu pack"); return; }
    const quint32 entries = rd<quint32>(map_ + 8), headers = rd<quint32>(map_ + 12), blobs = rd<quint32>(map_ + 16), kindLen = rd<quint32>(map_ + 20);
    const quint64 namesAt = rd<quint64>(map_ + 24), indexAt = rd<quint64>(map_ + 32), blobAt = rd<quint64>(map_ + 40), headersAt = rd<quint64>(map_ + 48),
                  textsAt = rd<quint64>(map_ + 56);
    const auto inside = [this](quint64 at, quint64 n) { return at <= quint64(size_) && n <= quint64(size_) - at; };
    if (!inside(kHeaderBytes, kindLen) || !inside(indexAt, quint64(entries) * 20) || !inside(blobAt, quint64(blobs) * 24) || !inside(textsAt, 8))
        { fail("a damaged menu pack"); return; }
    kind_ = QString::fromUtf8(reinterpret_cast<const char*>(map_ + kHeaderBytes), int(kindLen));
    // JPEG headers.
    quint64 at = headersAt;
    for (quint32 h = 0; h < headers; ++h) {
        if (!inside(at, 4)) { fail("a damaged menu pack (headers)"); return; }
        const quint32 n = rd<quint32>(map_ + at);
        if (!inside(at + 4, n)) { fail("a damaged menu pack (headers)"); return; }
        headers_.emplace_back(reinterpret_cast<const char*>(map_ + at + 4), int(n));
        at += 4 + n;
    }
    blobs_.resize(blobs);
    for (quint32 b = 0; b < blobs; ++b) {
        const uchar* p = map_ + blobAt + quint64(b) * 24;
        Blob& x = blobs_[b];
        x.offset = rd<quint64>(p), x.length = rd<quint32>(p + 8), x.width = rd<quint16>(p + 12), x.height = rd<quint16>(p + 14), x.header = p[16];
        if (!inside(x.offset, x.length) || (x.header < 254 && x.header >= headers_.size())) { fail("a damaged menu pack (blobs)"); return; }
    }
    entries_.resize(entries);
    names_.reserve(int(entries));
    index_.reserve(int(entries));
    for (quint32 e = 0; e < entries; ++e) {
        const uchar* p = map_ + indexAt + quint64(e) * 20;
        const quint32 off = rd<quint32>(p);
        const quint16 len = rd<quint16>(p + 4);
        const qint32 blob = rd<qint32>(p + 8), pv = rd<qint32>(p + 12), dt = rd<qint32>(p + 16);
        if (!inside(namesAt + off, len) || blob >= qint32(blobs) || pv >= qint32(blobs) || dt >= qint32(blobs))
            { fail("a damaged menu pack (index)"); return; }
        const QString name = QString::fromUtf8(reinterpret_cast<const char*>(map_ + namesAt + off), len);
        entries_[e].blob = blob, entries_[e].preview = pv, entries_[e].data = dt;
        names_ << name;
        index_.insert(name, int(e));
    }
    // The texts: one zlib block.
    const quint32 packed = rd<quint32>(map_ + kHeaderBytes - 8), plain = rd<quint32>(map_ + kHeaderBytes - 4);
    (void)plain;
    if (!inside(textsAt, packed)) { fail("a damaged menu pack (texts)"); return; }
    const QByteArray text = qUncompress(map_ + textsAt, qsizetype(packed));
    const QList<QByteArray> lines = text.split('\n');
    pills_.reserve(int(entries));
    facts_.reserve(int(entries));
    for (quint32 e = 0; e < entries; ++e) {
        const QByteArray l = e < quint32(lines.size()) ? lines[int(e)] : QByteArray();
        const qsizetype tab = l.indexOf('\t');
        pills_ << QString::fromUtf8(tab < 0 ? l : l.left(tab));
        facts_ << (tab < 0 ? QString() : QString::fromUtf8(l.mid(tab + 1)));
    }
    valid_ = true;
}

MenuPack::~MenuPack() {
    if (map_) file_.unmap(const_cast<uchar*>(map_));
}

int MenuPack::thumbCount() const {
    int n = 0;
    for (const Entry& e : entries_) n += e.blob >= 0;
    return n;
}

bool MenuPack::hasThumb(const QString& name) const {
    const auto it = index_.constFind(name);
    return it != index_.constEnd() && entries_[size_t(*it)].blob >= 0;
}

QImage MenuPack::thumb(const QString& name) const {
    const auto it = index_.constFind(name);
    return !valid_ || it == index_.constEnd() ? QImage() : picture(entries_[size_t(*it)].blob);
}

QImage MenuPack::preview(const QString& name) const {
    const auto it = index_.constFind(name);
    return !valid_ || it == index_.constEnd() ? QImage() : picture(entries_[size_t(*it)].preview);
}

bool MenuPack::hasData(const QString& name) const {
    const auto it = index_.constFind(name);
    return it != index_.constEnd() && entries_[size_t(*it)].data >= 0;
}

QByteArray MenuPack::data(const QString& name) const {
    const auto it = index_.constFind(name);
    if (!valid_ || it == index_.constEnd() || entries_[size_t(*it)].data < 0) return {};
    const Blob& x = blobs_[size_t(entries_[size_t(*it)].data)];
    return qUncompress(map_ + x.offset, qsizetype(x.length));
}

QImage MenuPack::picture(int b) const {
    if (b < 0 || size_t(b) >= blobs_.size() || blobs_[size_t(b)].header == 254) return {};
    const Blob& x = blobs_[size_t(b)];
    const QByteArray scan = QByteArray::fromRawData(reinterpret_cast<const char*>(map_ + x.offset), qsizetype(x.length));
    const QByteArray jpeg = x.header == 255 ? QByteArray(scan) : joinJpeg(headers_[x.header], scan, x.width, x.height);
    QImage img;
    img.loadFromData(jpeg, "JPG");
    return img;
}

QString MenuPack::pill(const QString& name) const {
    const auto it = index_.constFind(name);
    return it == index_.constEnd() ? QString() : pills_[*it];
}

QString MenuPack::facts(const QString& name) const {
    const auto it = index_.constFind(name);
    return it == index_.constEnd() ? QString() : facts_[*it];
}

QHash<QString, QString> MenuPack::pills() const {
    QHash<QString, QString> out;
    for (int i = 0; i < names_.size(); ++i)
        if (!pills_[i].isEmpty()) out.insert(names_[i], pills_[i]);
    return out;
}

QHash<QString, QString> MenuPack::facts() const {
    QHash<QString, QString> out;
    for (int i = 0; i < names_.size(); ++i)
        if (!facts_[i].isEmpty()) out.insert(names_[i], facts_[i]);
    return out;
}

QString MenuPack::folder() {
    if (const QString env = qEnvironmentVariable("IW8R_MENU_PACKS"); !env.isEmpty()) return env;
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/menu_packs";
}

namespace {
QMutex& packMutex() {
    static QMutex mx;
    return mx;
}
std::map<QString, std::shared_ptr<const MenuPack>>& openPacks() {
    static std::map<QString, std::shared_ptr<const MenuPack>> open;
    return open;
}
bool& suspended() {
    static bool on = false;
    return on;
}
}  // namespace

void MenuPack::suspend(bool on) {
    QMutexLocker lock(&packMutex());
    suspended() = on;
    openPacks().clear();  // a pack is unmapped when its last reader lets go
}

bool MenuPack::complete() { return QFileInfo::exists(QDir(folder()).filePath("complete.txt")); }

std::shared_ptr<const MenuPack> MenuPack::shared(const QString& kind) {
    if (qEnvironmentVariable("IW8R_NO_MENU_PACKS") == "1") return nullptr;
    QMutexLocker lock(&packMutex());
    if (suspended()) return nullptr;
    auto& open = openPacks();
    if (const auto it = open.find(kind); it != open.end()) return it->second;
    const QString path = QDir(folder()).filePath(kind + ".iw8mp");
    std::shared_ptr<const MenuPack> pack;
    if (QFileInfo::exists(path)) {
        auto p = std::make_shared<MenuPack>(path);
        if (p->valid()) pack = p;
    }
    open[kind] = pack;
    return pack;
}

// ------------------------------------------------------------------------------------------------- MenuPackWriter

MenuPackWriter::MenuPackWriter(const QString& kind, int jpegQuality) : kind_(kind), quality_(std::clamp(jpegQuality, 30, 100)) {}

void MenuPackWriter::add(const QString& name, const QImage& thumb, const QString& pill, const QString& facts, const QImage& preview,
                         const QByteArray& data) {
    const int blob = picture(thumb), pv = picture(preview);
    int dt = -1;
    if (!data.isEmpty()) {
        Blob x;
        x.data = qCompress(data, 9);
        x.header = 254;
        dataPlain_ += data.size();
        dt = int(blobs_.size());
        blobs_.push_back(std::move(x));
    }
    Item item{name, QString(pill).replace('\t', ' ').replace('\n', ' '), QString(facts).replace('\t', ' ').replace('\n', ' '), blob, pv, dt};
    if (const auto it = byName_.constFind(name); it != byName_.constEnd()) {
        items_[size_t(*it)] = item;
        return;
    }
    byName_.insert(name, int(items_.size()));
    items_.push_back(std::move(item));
}

int MenuPackWriter::picture(const QImage& thumb) {
    int blob = -1;
    if (!thumb.isNull()) {
        QByteArray jpeg;
        {
            QBuffer b(&jpeg);
            b.open(QIODevice::WriteOnly);
            QImageWriter w(&b, "JPG");
            w.setQuality(quality_);
            w.setOptimizedWrite(false);    // the standard Huffman tables: one header for the whole pack
            w.setProgressiveScanWrite(false);
            w.write(thumb.convertToFormat(QImage::Format_RGB32));
        }
        QByteArray header, scan;
        int w = 0, h = 0;
        Blob x;
        if (splitJpeg(jpeg, &header, &scan, &w, &h)) {
            auto it = headerIndex_.find(header);
            if (it == headerIndex_.end() && headers_.size() < 255) {
                it = headerIndex_.insert(header, int(headers_.size()));
                headers_.push_back(header);
            }
            if (it != headerIndex_.end()) {
                x.data = scan, x.w = w, x.h = h, x.header = *it;
                savedHeaderBytes_ += header.size();
            } else {
                x.data = jpeg;
            }
        } else {
            x.data = jpeg;
        }
        if (x.header == 255) ++wholeJpegs_;
        // One copy of identical pictures.
        QByteArray key = QCryptographicHash::hash(x.data, QCryptographicHash::Sha1);
        key.append(char(x.header)).append(char(x.w & 0xFF)).append(char(x.w >> 8)).append(char(x.h & 0xFF)).append(char(x.h >> 8));
        if (const auto f = blobByHash_.constFind(key); f != blobByHash_.constEnd()) {
            blob = *f;
            savedHeaderBytes_ -= x.header != 255 ? headers_[size_t(x.header)].size() : 0;
        } else {
            blob = int(blobs_.size());
            blobs_.push_back(std::move(x));
            blobByHash_.insert(key, blob);
        }
    }
    return blob;
}

bool MenuPackWriter::save(const QString& path, QString* err, Stats* stats) const {
    // Names sorted (a stable file for the same input).
    std::vector<int> order(items_.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = int(i);
    std::sort(order.begin(), order.end(), [this](int a, int b) { return items_[size_t(a)].name < items_[size_t(b)].name; });
    QByteArray names, index, table, hdrs, texts, data;
    for (const int i : order) {
        const Item& it = items_[size_t(i)];
        const QByteArray n = it.name.toUtf8();
        wr<quint32>(index, quint32(names.size()));
        wr<quint16>(index, quint16(std::min<qsizetype>(n.size(), 65535)));
        wr<quint16>(index, 0);
        wr<qint32>(index, it.blob);
        wr<qint32>(index, it.preview);
        wr<qint32>(index, it.data);
        names.append(n.left(65535));
        texts.append(it.pill.toUtf8()).append('\t').append(it.facts.toUtf8()).append('\n');
    }
    for (const QByteArray& h : headers_) {
        wr<quint32>(hdrs, quint32(h.size()));
        hdrs.append(h);
    }
    const QByteArray packedTexts = qCompress(texts, 9);
    const quint32 kindLen = quint32(kind_.toUtf8().size());
    const quint64 namesAt = kHeaderBytes + kindLen, indexAt = namesAt + quint64(names.size()), blobAt = indexAt + quint64(index.size()),
                  headersAt = blobAt + quint64(blobs_.size()) * 24, textsAt = headersAt + quint64(hdrs.size()),
                  dataAt = textsAt + quint64(packedTexts.size());
    quint64 off = dataAt;
    for (const Blob& b : blobs_) {
        wr<quint64>(table, off);
        wr<quint32>(table, quint32(b.data.size()));
        wr<quint16>(table, quint16(b.w));
        wr<quint16>(table, quint16(b.h));
        table.append(char(b.header));
        table.append(7, '\0');
        off += quint64(b.data.size());
    }
    QByteArray head;
    head.append(kMagic, 8);
    wr<quint32>(head, quint32(items_.size()));
    wr<quint32>(head, quint32(headers_.size()));
    wr<quint32>(head, quint32(blobs_.size()));
    wr<quint32>(head, kindLen);
    wr<quint64>(head, namesAt);
    wr<quint64>(head, indexAt);
    wr<quint64>(head, blobAt);
    wr<quint64>(head, headersAt);
    wr<quint64>(head, textsAt);
    head.resize(kHeaderBytes - 8, '\0');
    wr<quint32>(head, quint32(packedTexts.size()));
    wr<quint32>(head, quint32(texts.size()));
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        if (err) *err = "cannot write " + path;
        return false;
    }
    f.write(head);
    f.write(kind_.toUtf8());
    f.write(names);
    f.write(index);
    f.write(table);
    f.write(hdrs);
    f.write(packedTexts);
    qint64 thumbBytes = 0, dataBytes = 0;
    for (const Blob& b : blobs_) {
        f.write(b.data);
        (b.header == 254 ? dataBytes : thumbBytes) += b.data.size();
    }
    if (!f.commit()) {
        if (err) *err = "cannot write " + path;
        return false;
    }
    if (stats) {
        stats->entries = int(items_.size());
        stats->thumbs = 0;
        for (const Item& it : items_) stats->thumbs += it.blob >= 0, stats->previews += it.preview >= 0, stats->datas += it.data >= 0;
        stats->dataBytes = dataBytes;
        stats->dataPlainBytes = dataPlain_;
        stats->uniqueThumbs = int(std::count_if(blobs_.begin(), blobs_.end(), [](const Blob& b) { return b.header != 254; }));
        stats->headers = int(headers_.size());
        stats->wholeJpegs = wholeJpegs_;
        stats->bytes = QFileInfo(path).size();
        stats->thumbBytes = thumbBytes;
        stats->textBytes = packedTexts.size();
        stats->savedHeaderBytes = savedHeaderBytes_;
    }
    return true;
}

}  // namespace iw8r::qt
