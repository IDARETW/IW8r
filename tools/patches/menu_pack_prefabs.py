import io

# The Prefabs panel reads its shipped menu pack first (pictures by the path under data/prefabs, the strip's facts).

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


P = 'E:/IW8-Radiant/src/qtapp/prefab_browser.cpp'
rw(P, [
('''#include "prefab_browser.h"''', '''#include "prefab_browser.h"
#include "menu_pack.h"'''),
('''void PrefabBrowser::setPicturePeek(PicturePeek fn) {
    // IW8R_THUMB_DECODER=0: the pictures load on the GUI thread as before (thumb_bench.h compares the two).
    if (qEnvironmentVariable("IW8R_THUMB_DECODER") == "0") return;
    g_picturePeek = std::move(fn);
}''', '''namespace {
// The shipped menu pack's key of a prefab file: its path under the bundled library (data/prefabs), "" outside it.
QString packKey(const QString& mapFile) {
    static const QString root = QDir(MenuPack::folder() + "/../prefabs").absolutePath();
    const QString rel = QDir(root).relativeFilePath(mapFile);
    return rel.startsWith("..") || QDir::isAbsolutePath(rel) ? QString() : rel;
}
}  // namespace

void PrefabBrowser::setPicturePeek(PicturePeek fn) {
    // IW8R_THUMB_DECODER=0: the pictures load on the GUI thread as before (thumb_bench.h compares the two).
    if (qEnvironmentVariable("IW8R_THUMB_DECODER") == "0") return;
    // The shipped menu pack first (owner 2026-11-16): the library's pictures without drawing or rendering them.
    g_picturePeek = [fn = std::move(fn), pack = MenuPack::shared("prefabs")](const QString& mapFile) -> QImage {
        if (pack)
            if (const QString key = packKey(mapFile); !key.isEmpty())
                if (QImage img = pack->thumb(key); !img.isNull()) return img;
        return fn ? fn(mapFile) : QImage();
    };
}'''),
('''    strip_->setTitle(it->text(), facts.join(factSep()));''', '''    if (const auto pack = MenuPack::shared("prefabs"))
        if (const QString pf = pack->facts(packKey(fileAt(row))); !pf.isEmpty()) facts << pf;
    strip_->setTitle(it->text(), facts.join(factSep()));'''),
])
print('ok')
