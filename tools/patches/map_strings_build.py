import io

# ROADMAP 4.27: a map's own text. "<map name>.str" beside the map (core map_strings) is written into the extraction
# as localize/strings.tsv; the converter registers each entry in eng_<map> (tools/patches/zt_map_strings.py).

ROOT = 'E:/IW8-Radiant/'


def read(p):
    return io.open(ROOT + p, encoding='utf-8', newline='').read()


def write(p, s):
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s)


def patch(p, a, b, n=1):
    s = read(p)
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    write(p, s.replace(a, b))


def include_after(p, anchor, line):
    s = read(p)
    nl = '\r\n' if '\r\n' in s else '\n'
    if line in s:
        return
    a = anchor + nl
    assert s.count(a) == 1, (p, anchor)
    write(p, s.replace(a, a + line + nl, 1))


H = 'src/core/compile_iw8.h'
patch(H, '''    std::string hudImageDir;''', '''    std::string hudImageDir;
    // The map's own text (4.27, core map_strings.h): the file "<map name>.str". Its entries become localized strings
    // "<MAP ID>/<REFERENCE>" (extracted/localize/strings.tsv, which the converter registers in eng_<map>). Empty or
    // not a file: none.
    std::string stringsFile;''')
patch(H, '''    std::vector<std::string> hudImages;  // the hud_* images written (4.27), sorted''',
      '''    std::vector<std::string> hudImages;  // the hud_* images written (4.27), sorted
    std::vector<std::string> mapStrings; // the localized string keys written (4.27), sorted''')
C = 'src/core/compile_iw8.cpp'
patch(C, '''    for (const auto& [name, text] : physicPresets) customFiles.push_back({root / "extracted" / "physic" / name, text});''',
      '''    // 4.27: the map's own text for its script.
    if (!opt.stringsFile.empty()) {
        std::string strText;
        if (readFile(opt.stringsFile, strText)) {
            std::vector<std::string> strWarnings;
            const std::vector<MapString> strings = parseMapStrings(strText, id, &strWarnings);
            for (const std::string& w : strWarnings) rep.warnings.push_back(w);
            if (!strings.empty()) {
                customFiles.push_back({root / "extracted" / "localize" / "strings.tsv", mapStringsTsv(strings)});
                for (const MapString& s : strings) rep.mapStrings.push_back(s.key);
                rep.warnings.push_back(std::to_string(strings.size()) + " text entr" + (strings.size() == 1 ? "y" : "ies") +
                                       " for the map script (&\\"" + strings.front().key + "\\"" + (strings.size() > 1 ? ", ..." : "") + ")");
            }
        }
    }
    for (const auto& [name, text] : physicPresets) customFiles.push_back({root / "extracted" / "physic" / name, text});''')
patch(C, '''                if (!rep.hudImages.empty())
                    for (const std::string& n : hudNamesInScript(extras.userScript))''', '''                // The script's &"<MAP ID>/..." names against the map's text file: one that is not there shows as its
                // own name in the game.
                {
                    std::string pre = id + "/";
                    for (char& c : pre) c = char(std::toupper(static_cast<unsigned char>(c)));
                    for (const std::string& n : stringRefsInScript(extras.userScript))
                        if (n.rfind(pre, 0) == 0 && !std::binary_search(rep.mapStrings.begin(), rep.mapStrings.end(), n))
                            warn.push_back("the map script uses &\\"" + n + "\\", which is not in the map's text file (" +
                                           (opt.stringsFile.empty() ? std::string("<map name>.str") : fs::path(opt.stringsFile).filename().string()) + ")");
                }
                if (!rep.hudImages.empty())
                    for (const std::string& n : hudNamesInScript(extras.userScript))''')
include_after(C, '#include "hud_images.h"', '#include "map_strings.h"')
P = 'src/qtapp/compile_panel.cpp'
patch(P, '''    opt.hudImageDir = hudImageFolderFor(ctx_->doc.path());''', '''    opt.hudImageDir = hudImageFolderFor(ctx_->doc.path());
    // 4.27: "<map name>.str" beside the map holds the text for the map script.
    if (const std::string sf = mapStringsFileFor(ctx_->doc.path()); !sf.empty() && QFileInfo::exists(qs(sf))) opt.stringsFile = sf;''')
include_after(P, '#include "hud_images.h"', '#include "map_strings.h"')
M = 'src/qtapp/main_window.cpp'
patch(M, '''    addCommand(compile, "SetLoadscreen", "Set Loadscreen From Camera", QKeySequence(), [this] {''',
      '''    addCommand(compile, "MapTextFile", "Map Text File (hints and labels for the map script)...", QKeySequence(), [this] {
        // 4.27: "<map name>.str" beside the map; a new one starts with an example. Opened in the system's editor.
        const QString file = qs(mapStringsFileFor(ctx_.doc.path()));
        if (file.isEmpty()) return ctx_.setStatus("Save the map first: its text file lives beside it");
        const QString prefix = compile_ ? compile_->mapId() : QString("mp_map");
        if (!QFileInfo::exists(file)) {
            QFile f(file);
            if (f.open(QIODevice::WriteOnly)) f.write(QByteArray::fromStdString(mapStringsTemplate(prefix.toStdString())));
        }
        QDesktopServices::openUrl(QUrl::fromLocalFile(file));
        QFile f(file);
        const size_t have = f.open(QIODevice::ReadOnly) ? parseMapStrings(f.readAll().toStdString(), prefix.toStdString()).size() : 0;
        ctx_.setStatus(QString("Map text: %1 entr%2 in %3. In the map script an entry NAME is &\\"%4/NAME\\"; the next build carries them.")
                           .arg(have).arg(have == 1 ? "y" : "ies").arg(QDir::toNativeSeparators(file)).arg(prefix.toUpper()));
    });
    addCommand(compile, "SetLoadscreen", "Set Loadscreen From Camera", QKeySequence(), [this] {''')
include_after(M, '#include "hud_images.h"', '#include "map_strings.h"')
patch('src/qtapp/qt_gui_test.cpp', '''"CompareWithBackup", "HudImagesFolder"})''', '''"CompareWithBackup", "HudImagesFolder", "MapTextFile"})''')
print('ok')
