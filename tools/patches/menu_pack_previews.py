import io

# Owner 2026-11-16: "compress previews too". A menu pack entry also has a larger preview picture (the details strip)
# and a data block (zlib 9: an effect's definition, a sprite's pixels), so previews play without an export.

def rw(p, pairs):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    s = s.replace('\r\n', '\n')
    for a, b in pairs:
        if b in s and a not in s:
            continue
        assert s.count(a) == 1, (p, s.count(a), a[:80])
        s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s.replace('\n', nl))


H = 'E:/IW8-Radiant/src/qtapp/menu_pack.h'
C = 'E:/IW8-Radiant/src/qtapp/menu_pack.cpp'
rw(H, [
('''// prefabs). Every entry has a name, a thumbnail (or none) and two short texts: the pill a row shows ("loop", "1.5 s",
// "0.4-1.2 s · ×3") and the facts the details strip shows ("1024 x 1024 · BC7 sRGB · 11 mips").''', '''// prefabs, vfx_sprites). Every entry has a name, a thumbnail (or none), a larger preview picture for the details strip
// (or none; owner: "compress previews too"), a data block (or none: an effect's definition, so its preview plays
// without an export; a sprite's pixels) and two short texts: the pill a row shows ("loop", "1.5 s", "0.4-1.2 s · x3")
// and the facts the details strip shows ("1024 x 1024 · BC7 sRGB · 11 mips").'''),
('''//   - the texts are one zlib (level 9) block, unpacked once when the pack opens (a few MB at most).''', '''//   - the texts are one zlib (level 9) block, unpacked once when the pack opens (a few MB at most); each data block is
//     zlib (level 9) by itself, unpacked when it is read.'''),
('''// names (UTF-8, packed); index (entries x {u32 name offset, u16 name length, u16 0, i32 blob or -1}); blob table
// (blobs x {u64 offset, u32 length, u16 width, u16 height, u8 header or 255 = a whole JPEG, 7 x 0}); jpeg headers''', '''// names (UTF-8, packed); index (entries x {u32 name offset, u16 name length, u16 0, i32 thumb, i32 preview, i32 data:
// blob or -1}); blob table (blobs x {u64 offset, u32 length, u16 width, u16 height, u8 header, 255 = a whole JPEG,
// 254 = zlib data; 7 x 0}); jpeg headers'''),
('''    QImage thumb(const QString& name) const;''', '''    QImage thumb(const QString& name) const;
    QImage preview(const QString& name) const;   // the details strip's larger picture (null: none)
    QByteArray data(const QString& name) const;  // the entry's data block, unpacked (empty: none)
    bool hasData(const QString& name) const;'''),
('''    struct Entry {
        int blob = -1;
    };''', '''    struct Entry {
        int blob = -1, preview = -1, data = -1;
    };
    QImage picture(int blob) const;'''),
('''    void add(const QString& name, const QImage& thumb, const QString& pill, const QString& facts);''', '''    void add(const QString& name, const QImage& thumb, const QString& pill, const QString& facts, const QImage& preview = QImage(),
             const QByteArray& data = QByteArray());'''),
('''        int entries = 0, thumbs = 0, uniqueThumbs = 0, headers = 0, wholeJpegs = 0;
        qint64 bytes = 0, thumbBytes = 0, textBytes = 0, savedHeaderBytes = 0;''', '''        int entries = 0, thumbs = 0, previews = 0, datas = 0, uniqueThumbs = 0, headers = 0, wholeJpegs = 0;
        qint64 bytes = 0, thumbBytes = 0, textBytes = 0, dataBytes = 0, dataPlainBytes = 0, savedHeaderBytes = 0;'''),
('''    struct Item {
        QString name, pill, facts;
        int blob = -1;
    };''', '''    struct Item {
        QString name, pill, facts;
        int blob = -1, preview = -1, data = -1;
    };
    int picture(const QImage& img);  // a picture's blob (one copy of identical ones)
    qint64 dataPlain_ = 0;'''),
])
rw(C, [
('''    if (!inside(kHeaderBytes, kindLen) || !inside(indexAt, quint64(entries) * 12) || !inside(blobAt, quint64(blobs) * 24) || !inside(textsAt, 8))''',
 '''    if (!inside(kHeaderBytes, kindLen) || !inside(indexAt, quint64(entries) * 20) || !inside(blobAt, quint64(blobs) * 24) || !inside(textsAt, 8))'''),
('''        if (!inside(x.offset, x.length) || (x.header != 255 && x.header >= headers_.size())) return fail("a damaged menu pack (blobs)");''',
 '''        if (!inside(x.offset, x.length) || (x.header < 254 && x.header >= headers_.size())) return fail("a damaged menu pack (blobs)");'''),
('''        const uchar* p = map_ + indexAt + quint64(e) * 12;
        const quint32 off = rd<quint32>(p);
        const quint16 len = rd<quint16>(p + 4);
        const qint32 blob = rd<qint32>(p + 8);
        if (!inside(namesAt + off, len) || blob >= qint32(blobs)) return fail("a damaged menu pack (index)");
        const QString name = QString::fromUtf8(reinterpret_cast<const char*>(map_ + namesAt + off), len);
        entries_[e].blob = blob;''', '''        const uchar* p = map_ + indexAt + quint64(e) * 20;
        const quint32 off = rd<quint32>(p);
        const quint16 len = rd<quint16>(p + 4);
        const qint32 blob = rd<qint32>(p + 8), pv = rd<qint32>(p + 12), dt = rd<qint32>(p + 16);
        if (!inside(namesAt + off, len) || blob >= qint32(blobs) || pv >= qint32(blobs) || dt >= qint32(blobs))
            return fail("a damaged menu pack (index)");
        const QString name = QString::fromUtf8(reinterpret_cast<const char*>(map_ + namesAt + off), len);
        entries_[e].blob = blob, entries_[e].preview = pv, entries_[e].data = dt;'''),
('''QImage MenuPack::thumb(const QString& name) const {
    const auto it = index_.constFind(name);
    if (!valid_ || it == index_.constEnd()) return {};
    const int b = entries_[size_t(*it)].blob;
    if (b < 0) return {};
    const Blob& x = blobs_[size_t(b)];''', '''QImage MenuPack::thumb(const QString& name) const {
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
    const Blob& x = blobs_[size_t(b)];'''),
('''void MenuPackWriter::add(const QString& name, const QImage& thumb, const QString& pill, const QString& facts) {
    int blob = -1;
    if (!thumb.isNull()) {''', '''void MenuPackWriter::add(const QString& name, const QImage& thumb, const QString& pill, const QString& facts, const QImage& preview,
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
    Item item{name, QString(pill).replace('\\t', ' ').replace('\\n', ' '), QString(facts).replace('\\t', ' ').replace('\\n', ' '), blob, pv, dt};
    if (const auto it = byName_.constFind(name); it != byName_.constEnd()) {
        items_[size_t(*it)] = item;
        return;
    }
    byName_.insert(name, int(items_.size()));
    items_.push_back(std::move(item));
}

int MenuPackWriter::picture(const QImage& thumb) {
    int blob = -1;
    if (!thumb.isNull()) {'''),
('''            blobByHash_.insert(key, blob);
        }
    }
    Item item{name, QString(pill).replace('\\t', ' ').replace('\\n', ' '), QString(facts).replace('\\t', ' ').replace('\\n', ' '), blob};
    if (const auto it = byName_.constFind(name); it != byName_.constEnd()) {
        items_[size_t(*it)] = item;
        return;
    }
    byName_.insert(name, int(items_.size()));
    items_.push_back(std::move(item));
}''', '''            blobByHash_.insert(key, blob);
        }
    }
    return blob;
}'''),
('''        wr<qint32>(index, it.blob);''', '''        wr<qint32>(index, it.blob);
        wr<qint32>(index, it.preview);
        wr<qint32>(index, it.data);'''),
('''    qint64 thumbBytes = 0;
    for (const Blob& b : blobs_) {
        f.write(b.data);
        thumbBytes += b.data.size();
    }''', '''    qint64 thumbBytes = 0, dataBytes = 0;
    for (const Blob& b : blobs_) {
        f.write(b.data);
        (b.header == 254 ? dataBytes : thumbBytes) += b.data.size();
    }'''),
('''        for (const Item& it : items_) stats->thumbs += it.blob >= 0;''', '''        for (const Item& it : items_) stats->thumbs += it.blob >= 0, stats->previews += it.preview >= 0, stats->datas += it.data >= 0;
        stats->dataBytes = dataBytes;
        stats->dataPlainBytes = dataPlain_;'''),
])
print('ok')
