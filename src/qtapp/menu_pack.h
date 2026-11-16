// IW8 Radiant (Qt shell) - menu packs: the side panels' thumbnails and facts, made once and shipped with the
// project (owner 2026-11-16: "build a one-time full metadata and thumbnails for everything we can in the menus,
// compress it as best as we can while allowing for fast decompression to read data and have smooth, fast-loading
// menus, and include it as part of the project").
//
// One read-only file per panel, <AppLocalData>/menu_packs/<kind>.iw8mp, built on the user's machine (owner: "don't
// add them straight to the source, have a user starting tip to run the full cache": a tip at start-up and Tools >
// Build Full Menu Cache run `IW8Radiant.exe --menu-pack all --fill` in a separate process) (kind: materials, models,
// images, effects, sounds,
// prefabs, vfx_sprites). Every entry has a name, a thumbnail (or none), a larger preview picture for the details strip
// (or none; owner: "compress previews too"), a data block (or none: an effect's definition, so its preview plays
// without an export; a sprite's pixels) and two short texts: the pill a row shows ("loop", "1.5 s", "0.4-1.2 s · x3")
// and the facts the details strip shows ("1024 x 1024 · BC7 sRGB · 11 mips").
//
// Small and fast to read:
//   - thumbnails are baseline JPEGs with the standard Huffman tables, stored without their headers: every JPEG of a
//     pack made at one quality has the same header but for its width and height in the frame header, so the header
//     is stored once (its size fields zeroed) and put back in front of the scan data when a picture is read (a third
//     of a 64 px picture's bytes were header). Identical pictures are stored once (many materials share an image).
//   - the texts are one zlib (level 9) block, unpacked once when the pack opens (a few MB at most); each data block is
//     zlib (level 9) by itself, unpacked when it is read.
//   - the file is memory-mapped; reads are thread-safe (the browsers' decoder threads read pictures directly).
// Layout (little endian): 72-byte header {magic "IW8RMP01", u32 entries, u32 headers, u32 blobs, u32 kind length,
// u64 names, u64 index, u64 blob table, u64 jpeg headers, u64 texts, u32 texts packed size, u32 texts size}; the kind;
// names (UTF-8, packed); index (entries x {u32 name offset, u16 name length, u16 0, i32 thumb, i32 preview, i32 data:
// blob or -1}); blob table (blobs x {u64 offset, u32 length, u16 width, u16 height, u8 header, 255 = a whole JPEG,
// 254 = zlib data; 7 x 0}); jpeg headers
// (u32 length + bytes each); the texts ("pill\tfacts\n" per entry in index order, qCompress); the blobs.
#pragma once

#include <QByteArray>
#include <QFile>
#include <QHash>
#include <QImage>
#include <QString>
#include <QStringList>

#include <memory>
#include <vector>

namespace iw8r::qt {

class MenuPack {
public:
    explicit MenuPack(const QString& path);
    ~MenuPack();
    bool valid() const { return valid_; }
    QString kind() const { return kind_; }
    QString error() const { return error_; }
    int count() const { return int(names_.size()); }
    int thumbCount() const;
    bool contains(const QString& name) const { return index_.contains(name); }
    bool hasThumb(const QString& name) const;
    // The thumbnail (null when the entry has none or the name is not in the pack). Thread-safe.
    QImage thumb(const QString& name) const;
    QImage preview(const QString& name) const;   // the details strip's larger picture (null: none)
    QByteArray data(const QString& name) const;  // the entry's data block, unpacked (empty: none)
    bool hasData(const QString& name) const;
    QString pill(const QString& name) const;
    QString facts(const QString& name) const;
    QHash<QString, QString> pills() const;   // every entry with a pill
    QHash<QString, QString> facts() const;   // every entry with facts
    QStringList names() const { return names_; }
    qint64 fileBytes() const { return size_; }

    // The pack of a kind (<AppLocalData>/menu_packs/<kind>.iw8mp; IW8R_MENU_PACKS = another folder), opened once for
    // the process; null when there is none, while a build writes them (suspend) and with IW8R_NO_MENU_PACKS=1.
    static std::shared_ptr<const MenuPack> shared(const QString& kind);
    static QString folder();
    // Closes every pack (a build replaces the files) and, while suspended, opens none; reopened on the next use.
    static void suspend(bool on);
    // The full build has run (the builder writes <folder>/complete.txt after `--menu-pack all --fill`).
    static bool complete();

private:
    struct Entry {
        int blob = -1, preview = -1, data = -1;
    };
    QImage picture(int blob) const;
    struct Blob {
        quint64 offset = 0;
        quint32 length = 0;
        quint16 width = 0, height = 0;
        quint8 header = 255;
    };
    QFile file_;
    const uchar* map_ = nullptr;
    qint64 size_ = 0;
    bool valid_ = false;
    QString kind_, error_;
    QStringList names_;
    QHash<QString, int> index_;
    std::vector<Entry> entries_;
    std::vector<Blob> blobs_;
    std::vector<QByteArray> headers_;
    QStringList pills_, facts_;
};

// Writes a pack (the builder: MainWindow::runMenuPack).
class MenuPackWriter {
public:
    MenuPackWriter(const QString& kind, int jpegQuality);
    // One entry; a null thumb = none. A name added twice keeps the last.
    void add(const QString& name, const QImage& thumb, const QString& pill, const QString& facts, const QImage& preview = QImage(),
             const QByteArray& data = QByteArray());
    struct Stats {
        int entries = 0, thumbs = 0, previews = 0, datas = 0, uniqueThumbs = 0, headers = 0, wholeJpegs = 0;
        qint64 bytes = 0, thumbBytes = 0, textBytes = 0, dataBytes = 0, dataPlainBytes = 0, savedHeaderBytes = 0;
    };
    bool save(const QString& path, QString* err = nullptr, Stats* stats = nullptr) const;

private:
    QString kind_;
    int quality_;
    struct Item {
        QString name, pill, facts;
        int blob = -1, preview = -1, data = -1;
    };
    int picture(const QImage& img);  // a picture's blob (one copy of identical ones)
    qint64 dataPlain_ = 0;
    std::vector<Item> items_;
    QHash<QString, int> byName_;
    struct Blob {
        QByteArray data;
        int w = 0, h = 0, header = 255;
    };
    std::vector<Blob> blobs_;
    QHash<QByteArray, int> blobByHash_;
    std::vector<QByteArray> headers_;
    QHash<QByteArray, int> headerIndex_;
    qint64 savedHeaderBytes_ = 0;
    int wholeJpegs_ = 0;
};

// A baseline JPEG split into its header (everything up to and with the start-of-scan segment, the frame size zeroed)
// and its scan data (to the end); false when it is not one (progressive, no frame header). Exposed for tests.
bool splitJpeg(const QByteArray& jpeg, QByteArray* header, QByteArray* scan, int* width, int* height);
// The JPEG again from a split one.
QByteArray joinJpeg(const QByteArray& header, const QByteArray& scan, int width, int height);

}  // namespace iw8r::qt
