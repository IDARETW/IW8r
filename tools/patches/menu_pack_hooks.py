import io

# Hooks of the menu packs: --menu-pack on the command line, MainWindow::runMenuPack, the DDS facts shared with the
# builder, and the side panels reading the shipped packs first (thumbnails, pills, facts, previews, effect definitions).

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
rw(S + 'main.cpp', [
('''//   IW8Radiant.exe --thumb-pack materials|models [--limit N] [--batch N]   full browser thumbnail pack (thumb_pack.h)''',
 '''//   IW8Radiant.exe --thumb-pack materials|models [--limit N] [--batch N]   full browser thumbnail pack (thumb_pack.h)
//   IW8Radiant.exe --menu-pack <kinds|all> [--fill] [--limit N] [--out dir]   the shipped menu packs (menu_pack.h)'''),
('''    QString thumbPackKind;''', '''    QString thumbPackKind;
    QString menuPackKinds;  // --menu-pack
    bool menuPackFill = false;'''),
('''        else if (a == "--limit" && i + 1 < argc) auditLimit = std::atoi(argv[++i]);''', '''        else if (a == "--limit" && i + 1 < argc) auditLimit = std::atoi(argv[++i]);
        else if (a == "--menu-pack" && i + 1 < argc) menuPackKinds = QString::fromLocal8Bit(argv[++i]);
        else if (a == "--fill") menuPackFill = true;'''),
('''        if (guiTest || smoke || texturedPack || !thumbTest.isEmpty()) w.waitForCatalogs();
        if (texturedPack) return w.runTexturedThumbPack(thumbPackKind, auditLimit);  // the window is never shown''',
 '''        if (guiTest || smoke || texturedPack || !thumbTest.isEmpty() || !menuPackKinds.isEmpty()) w.waitForCatalogs();
        if (texturedPack) return w.runTexturedThumbPack(thumbPackKind, auditLimit);  // the window is never shown
        if (!menuPackKinds.isEmpty()) return w.runMenuPack(menuPackKinds, outDir, menuPackFill, auditLimit);'''),
])
rw(S + 'main_window.h', [
('''    int runTexturedThumbPack(const QString& kind, int limit);''', '''    int runTexturedThumbPack(const QString& kind, int limit);
    // --menu-pack: the side panels' shipped packs (menu_pack.h, main_window_menu_pack.cpp).
    int runMenuPack(const QString& kinds, const QString& outDir, bool fill, int limit);'''),
])
rw(S + 'main_window.cpp', [
('''void MainWindow::previewImage(const QString& name) {
    previewedImage_ = name;''', '''QStringList ddsFactsOf(const std::string& headerBytes) { return ddsFacts(headerBytes); }

void MainWindow::previewImage(const QString& name) {
    previewedImage_ = name;
    // The shipped menu pack first (owner 2026-11-16: "compress previews too"): no export, at once.
    if (const auto pack = MenuPack::shared("images"))
        if (const QImage pv = pack->preview(name); !pv.isNull()) {
            images_->setPreview(pv, name + "\\n" + pack->facts(name).replace(factSep(), "\\n"));
            return;
        }'''),
('''#include "numeric_drag.h"''', '''#include "numeric_drag.h"
#include "menu_pack.h"'''),
# effects: the shipped definition when there is no export
('''        const auto showFx = [this, previewed](const QString& name) {
            *previewed = name;
            const QString f = findVfxExport(name);
            std::string text;
            VfxSimDef def;
            if (!f.isEmpty() && readFile(f.toStdString(), text) && buildVfxSim(text, def) && !def.emitters.empty()) {''',
 '''        const auto showFx = [this, previewed](const QString& name) {
            *previewed = name;
            QString f = findVfxExport(name);
            std::string text;
            VfxSimDef def;
            // The shipped menu pack holds the definition of every effect it could export: the preview plays at once.
            if (f.isEmpty())
                if (const auto pack = MenuPack::shared("effects"); pack && pack->hasData(name)) {
                    text = pack->data(name).toStdString();
                    f = "pack";
                }
            if (!f.isEmpty() && (f == "pack" || readFile(f.toStdString(), text)) && buildVfxSim(text, def) && !def.emitters.empty()) {'''),
('''        if (const auto it = vfxPreviewDefs_.constFind(name); it != vfxPreviewDefs_.constEnd()) return *it;
        if (const QString f = findVfxExport(name); !f.isEmpty()) {''', '''        if (const auto it = vfxPreviewDefs_.constFind(name); it != vfxPreviewDefs_.constEnd()) return *it;
        if (const auto pack = MenuPack::shared("effects"); pack && pack->hasData(name) && findVfxExport(name).isEmpty()) {
            auto def = std::make_shared<VfxSimDef>();
            if (buildVfxSim(pack->data(name).toStdString(), *def)) {
                vfxPreviewDefs_.insert(name, def);
                return def;
            }
        }
        if (const QString f = findVfxExport(name); !f.isEmpty()) {'''),
])
print('ok')
