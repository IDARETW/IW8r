import io

# GUI test of the menu packs (menu_pack.h).

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


rw('E:/IW8-Radiant/src/qtapp/qt_gui_test_sidebar.cpp', [
('''#include "map_check_panel.h"''', '''#include "map_check_panel.h"
#include "menu_pack.h"'''),
('''    section("side bar: stored thumbnails off the GUI thread");''', '''    section("side bar: menu packs");
    {
        // Owner 2026-11-16: thumbnails, previews and facts made once, compressed, shipped (menu_pack.h).
        const auto picture = [](int w, int h, QColor c) {
            QImage img(w, h, QImage::Format_RGB32);
            img.fill(c);
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; x += 7) img.setPixel(x, y, qRgb((x * 3) & 255, (y * 5) & 255, 90));
            return img;
        };
        const QString path = QDir(outDir).filePath("test.iw8mp");
        QFile::remove(path);
        MenuPackWriter w("test", 80);
        const QByteArray json = QByteArray("{\\"name\\":\\"vfx/test\\"}").repeated(40);
        w.add("one", picture(64, 48, Qt::red), "loop", "3 emitters", picture(160, 120, Qt::red), json);
        w.add("two", picture(64, 48, Qt::red), "", "the same picture as one");
        w.add("three", picture(40, 64, Qt::blue), "1.5 s", "");
        w.add("four", {}, "", "no picture");
        MenuPackWriter::Stats st;
        QString err;
        const bool saved = w.save(path, &err, &st);
        MenuPack pack(path);
        const QImage one = pack.thumb("one"), three = pack.thumb("three"), pv = pack.preview("one");
        check(saved && pack.valid() && pack.count() == 4 && pack.thumbCount() == 3 && st.uniqueThumbs == 3 /* two thumbs + one preview */ &&
                  st.headers >= 1 && st.wholeJpegs == 0 && st.savedHeaderBytes > 500 && one.size() == QSize(64, 48) && three.size() == QSize(40, 64) &&
                  pv.size() == QSize(160, 120) && pack.preview("two").isNull() && pack.thumb("four").isNull() && pack.pill("one") == "loop" &&
                  pack.facts("two") == "the same picture as one" && pack.data("one") == json && pack.data("two").isEmpty() &&
                  qRed(one.pixel(3, 3)) > 200 && qBlue(three.pixel(3, 3)) > 200 && st.dataBytes < json.size() / 4,
              QString("a menu pack keeps thumbnails, previews, data and texts, one copy of identical pictures, without their JPEG headers "
                      "(%1 entries, %2 pictures stored, %3 header(s), %4 header bytes saved, data %5 -> %6 bytes) %7")
                  .arg(st.entries).arg(st.uniqueThumbs).arg(st.headers).arg(st.savedHeaderBytes).arg(json.size()).arg(st.dataBytes).arg(err));
        // The shipped packs open, and their pictures read (when the build has them).
        for (const char* kind : {"materials", "models", "images", "effects", "sounds", "prefabs"}) {
            const auto shipped = MenuPack::shared(kind);
            if (!shipped) continue;
            QString first;
            for (const QString& n : shipped->names())
                if (shipped->hasThumb(n)) {
                    first = n;
                    break;
                }
            check(shipped->valid() && shipped->kind() == kind && (first.isEmpty() || !shipped->thumb(first).isNull()),
                  QString("the shipped %1 pack reads (%2 entries, %3 pictures, %4 MB)").arg(kind).arg(shipped->count()).arg(shipped->thumbCount())
                      .arg(shipped->fileBytes() / 1048576.0, 0, 'f', 1));
        }
    }

    section("side bar: stored thumbnails off the GUI thread");'''),
])
print('ok')
