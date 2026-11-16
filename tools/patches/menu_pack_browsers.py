import io

# The side panels read the shipped menu packs first (menu_pack.h): thumbnails (the decoder threads and the slow
# path), the rows' pills, the strips' facts and preview pictures; the model strip shows the pack's picture at once.

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


S = 'E:/IW8-Radiant/src/qtapp/'
rw(S + 'main_window.cpp', [
('''void MainWindow::previewModel(const QString& name) {
    previewedModel_ = name;
    if (!modelCacheStock_) return;
    const auto e = modelCacheStock_->lookup(name);
    if (!e) {''', '''void MainWindow::previewModel(const QString& name) {
    previewedModel_ = name;
    // The shipped menu pack's picture and facts, at once and without an export (owner 2026-11-16: "compress previews
    // too"); the 3D button still exports the model.
    if (const auto pack = MenuPack::shared("models"))
        if (const QImage pv = pack->preview(name); !pv.isNull()) {
            models_->setPreview(pv, name + "\\n" + pack->facts(name));
            return;
        }
    if (!modelCacheStock_) return;
    const auto e = modelCacheStock_->lookup(name);
    if (!e) {'''),
])

rw(S + 'asset_browser.h', [
('''    bool shownOnce_ = false;''', '''    bool shownOnce_ = false;
    std::shared_ptr<const class MenuPack> pack_;  // the shipped menu pack of this panel (menu_pack.h), or null
    QHash<QString, QString> packFacts_;            // its facts, for the details strip'''),
])

P = S + 'asset_browser.cpp'
rw(P, [
('''#include "panel_chrome.h"
#include "vfx_preview.h"''', '''#include "menu_pack.h"
#include "panel_chrome.h"
#include "vfx_preview.h"'''),
('''    model_ = new QStandardItemModel(this);
    auto* filter = new NameFilter(this);''', '''    // The shipped menu pack of this panel: thumbnails, pills, facts and previews made once (menu_pack.h).
    pack_ = MenuPack::shared(kind == Kind::Materials ? "materials" : kind == Kind::Models ? "models" : kind == Kind::Effects ? "effects"
                             : kind == Kind::Sounds ? "sounds" : "images");
    if (pack_) packFacts_ = pack_->facts();
    model_ = new QStandardItemModel(this);
    auto* filter = new NameFilter(this);'''),
# pills from the pack (an effect's length, a sound's length and variants) under what this session found out
('''    if (kind == Kind::Effects) {
        // What earlier previews found (loop / one-shot length), kept in the settings.''', '''    if (pack_)
        for (auto [n, pill] : pack_->pills().asKeyValueRange()) facts_.insert(n, pill);
    if (kind == Kind::Effects) {
        // What earlier previews found (loop / one-shot length), kept in the settings.'''),
# thumbnails: the pack before the user's caches
('''    decoder_ = std::make_unique<ThumbDecoder>(
        [peek = std::move(fn)](const QString& key) -> QImage {
            // "file:<path>": a row's own image file; else a stored picture by name. The decoder fits it to the tile.
            if (key.startsWith("file:")) return decodeImageFile(key.mid(5), 256);
            return peek ? peek(key) : QImage();
        },''', '''    decoder_ = std::make_unique<ThumbDecoder>(
        [peek = std::move(fn), pack = pack_](const QString& key) -> QImage {
            // "file:<path>": a row's own image file; else the shipped pack's picture, then a stored one by name. The
            // decoder fits it to the tile.
            if (key.startsWith("file:")) return decodeImageFile(key.mid(5), 256);
            if (pack)
                if (QImage img = pack->thumb(key); !img.isNull()) return img;
            return peek ? peek(key) : QImage();
        },'''),
('''QIcon AssetBrowser::thumbnail(const QString& name) {
    if (auto it = icons_.find(name); it != icons_.end()) return *it;
    QIcon icon;''', '''QIcon AssetBrowser::thumbnail(const QString& name) {
    if (auto it = icons_.find(name); it != icons_.end()) return *it;
    QIcon icon;
    if (pack_ && !imageFiles_.contains(name))
        if (const QImage img = pack_->thumb(name); !img.isNull()) {
            // The shipped pack: no cache, no export.
            icon = QIcon(QPixmap::fromImage(ThumbDecoder::fit(img, iconSide())));
            realThumbs_.insert(name);
            icons_.insert(name, icon);
            return icon;
        }'''),
# the strip: the pack's facts (images take theirs with the preview) and the material's larger picture
('''    } else if (kind_ == Kind::Materials || kind_ == Kind::Models) {
        if (!group.isEmpty()) facts << group;''', '''    } else if (kind_ == Kind::Materials || kind_ == Kind::Models) {
        if (!group.isEmpty()) facts << group;
        if (const QString pf = packFacts_.value(name); !pf.isEmpty() && kind_ == Kind::Materials) facts << pf;'''),
('''            QPixmap pm;
            if (const QString file = imageFiles_.value(name); !file.isEmpty())''', '''            QPixmap pm;
            if (pack_)
                if (const QImage pv = pack_->preview(name); !pv.isNull()) pm = QPixmap::fromImage(pv.scaled(112, 112, Qt::KeepAspectRatio, Qt::SmoothTransformation));
            if (const QString file = imageFiles_.value(name); pm.isNull() && !file.isEmpty())'''),
('''    if (kind_ == Kind::Effects) {
        if (!group.isEmpty()) facts << group;
        if (const QString f = facts_.value(name); !f.isEmpty()) facts << (f == "loop" ? QString("looping") : "one-shot " + f);''', '''    if (kind_ == Kind::Effects) {
        if (!group.isEmpty()) facts << group;
        if (const QString f = facts_.value(name); !f.isEmpty()) facts << (f == "loop" ? QString("looping") : "one-shot " + f);
        if (const QString pf = packFacts_.value(name); !pf.isEmpty()) facts << pf;'''),
])
print('ok')
