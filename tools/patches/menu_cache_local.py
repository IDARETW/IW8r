import io

# Owner 2026-11-16: "don't add them straight to the source, have a user starting tip to run the full cache". The menu
# packs live in the user's cache (<AppLocalData>/menu_packs), built on their machine: a tip at start-up (and Tools >
# Build Full Menu Cache) runs the builder in a separate process; the panels look the packs up on each use, so a
# finished build shows without a restart.

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
rw(S + 'menu_pack.h', [
('''// One read-only file per panel, data/menu_packs/<kind>.iw8mp (kind: materials, models, images, effects, sounds,''',
 '''// One read-only file per panel, <AppLocalData>/menu_packs/<kind>.iw8mp, built on the user's machine (owner: "don't
// add them straight to the source, have a user starting tip to run the full cache": a tip at start-up and Tools >
// Build Full Menu Cache run `IW8Radiant.exe --menu-pack all --fill` in a separate process) (kind: materials, models,
// images, effects, sounds,'''),
('''    // The shipped pack of a kind (data/menu_packs/<kind>.iw8mp beside the editor; IW8R_MENU_PACKS = another folder),
    // opened once for the process; null when there is none. IW8R_NO_MENU_PACKS=1 turns them off (tests of the caches).
    static std::shared_ptr<const MenuPack> shared(const QString& kind);
    static QString folder();''', '''    // The pack of a kind (<AppLocalData>/menu_packs/<kind>.iw8mp; IW8R_MENU_PACKS = another folder), opened once for
    // the process; null when there is none, while a build writes them (suspend) and with IW8R_NO_MENU_PACKS=1.
    static std::shared_ptr<const MenuPack> shared(const QString& kind);
    static QString folder();
    // Closes every pack (a build replaces the files) and, while suspended, opens none; reopened on the next use.
    static void suspend(bool on);
    // The full build has run (the builder writes <folder>/complete.txt after `--menu-pack all --fill`).
    static bool complete();'''),
])
rw(S + 'menu_pack.cpp', [
('''QString MenuPack::folder() {
    if (const QString env = qEnvironmentVariable("IW8R_MENU_PACKS"); !env.isEmpty()) return env;
    QDir d(QCoreApplication::applicationDirPath());
    for (int up = 0; up < 4; ++up, d.cdUp())
        if (QFileInfo::exists(d.filePath("data/menu_packs"))) return d.filePath("data/menu_packs");
    return QDir(QCoreApplication::applicationDirPath()).filePath("data/menu_packs");
}

std::shared_ptr<const MenuPack> MenuPack::shared(const QString& kind) {
    static QMutex mx;
    static std::map<QString, std::shared_ptr<const MenuPack>> open;
    if (qEnvironmentVariable("IW8R_NO_MENU_PACKS") == "1") return nullptr;
    QMutexLocker lock(&mx);
    if (const auto it = open.find(kind); it != open.end()) return it->second;''', '''QString MenuPack::folder() {
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
    if (const auto it = open.find(kind); it != open.end()) return it->second;'''),
('''#include <QSaveFile>''', '''#include <QSaveFile>
#include <QStandardPaths>'''),
])
rw(S + 'main_window_menu_pack.cpp', [
('''    const QString outDir = outDirArg.isEmpty() ? dataDir().filePath("menu_packs") : outDirArg;''',
 '''    const QString outDir = outDirArg.isEmpty() ? MenuPack::folder() : outDirArg;'''),
('''        std::printf("menu pack: unknown kind %s (sounds, effects, images, materials, models, prefabs or all)\\n", qPrintable(kind));
        ++failures;
    }
    return failures ? 1 : 0;''', '''        std::printf("menu pack: unknown kind %s (sounds, effects, images, materials, models, prefabs or all)\\n", qPrintable(kind));
        ++failures;
    }
    // The full build is done: the start-up tip stops asking.
    if (!failures && fill && kindsArg == "all")
        if (QFile done(QDir(outDir).filePath("complete.txt")); done.open(QIODevice::WriteOnly | QIODevice::Truncate))
            done.write(QDateTime::currentDateTime().toString(Qt::ISODate).toUtf8() + "\\n");
    return failures ? 1 : 0;'''),
('''#include <QCoreApplication>''', '''#include <QCoreApplication>
#include <QDateTime>'''),
])
# The builder process never maps the packs it writes.
rw(S + 'main.cpp', [
('''        else if (a == "--menu-pack" && i + 1 < argc) menuPackKinds = QString::fromLocal8Bit(argv[++i]);''',
 '''        else if (a == "--menu-pack" && i + 1 < argc) menuPackKinds = QString::fromLocal8Bit(argv[++i]), qputenv("IW8R_NO_MENU_PACKS", "1");'''),
])
# The asset browser asks for its pack on each use.
rw(S + 'asset_browser.h', [
('''    std::shared_ptr<const class MenuPack> pack_;  // the shipped menu pack of this panel (menu_pack.h), or null
    QHash<QString, QString> packFacts_;            // its facts, for the details strip''', '''    QString packKind_;                             // its menu pack (menu_pack.h): materials, models, ...
    std::shared_ptr<const class MenuPack> pack() const;  // the pack now (null: not built, or being built)
    QHash<QString, QString> packFacts_;            // its facts, for the details strip'''),
('''    void reloadCatalog() { load(); }  // after an availability audit (4.12)''', '''    void reloadCatalog() { load(); }  // after an availability audit (4.12)
    // The menu cache was (re)built: its pills, facts and pictures are read again.
    void reloadPack();'''),
])
rw(S + 'asset_browser.cpp', [
('''    pack_ = MenuPack::shared(kind == Kind::Materials ? "materials" : kind == Kind::Models ? "models" : kind == Kind::Effects ? "effects"
                             : kind == Kind::Sounds ? "sounds" : "images");
    if (pack_) packFacts_ = pack_->facts();''', '''    packKind_ = kind == Kind::Materials ? "materials" : kind == Kind::Models ? "models" : kind == Kind::Effects ? "effects"
                : kind == Kind::Sounds ? "sounds" : "images";
    if (const auto pk = pack()) packFacts_ = pk->facts();'''),
('''    if (pack_)
        for (auto [n, pill] : pack_->pills().asKeyValueRange()) facts_.insert(n, pill);''', '''    if (const auto pk = pack())
        for (auto [n, pill] : pk->pills().asKeyValueRange()) facts_.insert(n, pill);'''),
('''    if (pack_ && !imageFiles_.contains(name))
        if (const QImage img = pack_->thumb(name); !img.isNull()) {''', '''    if (const auto pk = pack(); pk && !imageFiles_.contains(name))
        if (const QImage img = pk->thumb(name); !img.isNull()) {'''),
('''        [peek = std::move(fn), pack = pack_](const QString& key) -> QImage {''', '''        [peek = std::move(fn), kind = packKind_](const QString& key) -> QImage {'''),
('''            if (pack)
                if (QImage img = pack->thumb(key); !img.isNull()) return img;
            return peek ? peek(key) : QImage();''', '''            if (const auto pack = MenuPack::shared(kind))
                if (QImage img = pack->thumb(key); !img.isNull()) return img;
            return peek ? peek(key) : QImage();'''),
('''            if (pack_)
                if (const QImage pv = pack_->preview(name); !pv.isNull()) pm = QPixmap::fromImage(pv.scaled(112, 112, Qt::KeepAspectRatio, Qt::SmoothTransformation));''',
 '''            if (const auto pk = pack())
                if (const QImage pv = pk->preview(name); !pv.isNull()) pm = QPixmap::fromImage(pv.scaled(112, 112, Qt::KeepAspectRatio, Qt::SmoothTransformation));'''),
('''void AssetBrowser::selectRow(int row) { view_->setCurrentIndex(proxy_->index(row, 0)); }''', '''void AssetBrowser::selectRow(int row) { view_->setCurrentIndex(proxy_->index(row, 0)); }

std::shared_ptr<const MenuPack> AssetBrowser::pack() const { return MenuPack::shared(packKind_); }

void AssetBrowser::reloadPack() {
    const auto pk = pack();
    packFacts_ = pk ? pk->facts() : QHash<QString, QString>();
    if (pk)
        for (auto [n, pill] : pk->pills().asKeyValueRange()) facts_.insert(n, pill);
    forgetThumbnails();  // the pictures are read again (from the new pack first)
    view_->viewport()->update();
    updateStrip();
}'''),
])
rw(S + 'prefab_browser.cpp', [
('''    g_picturePeek = [fn = std::move(fn), pack = MenuPack::shared("prefabs")](const QString& mapFile) -> QImage {
        if (pack)''', '''    g_picturePeek = [fn = std::move(fn)](const QString& mapFile) -> QImage {
        if (const auto pack = MenuPack::shared("prefabs"))'''),
])
print('ok')
