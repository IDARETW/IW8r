import io

# Tools > Shader Editor (plan M6): map shaders and the editor's templates (stock lighting, toon).


def patch(p, a, b, n=1):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


C = 'E:/IW8-Radiant/src/qtapp/main_window.cpp'
patch(C, '''#include "toon_outline.h"
''', '''#include "toon_outline.h"
#include "shader_editor.h"
''')
patch(C, '''    addCommand(tools, "ToonInkOutlines",''', '''    addCommand(tools, "ShaderEditor", "Shader Editor (map shaders, stock lighting)...", QKeySequence(), [this] {
        // M6: the map's surface shaders: new ones from the editor's templates (stock = the game's own lighting),
        // edited, checked against the viewport's shader, saved, and used for the map (worldspawn iw8r_surface_shader).
        QDir d(QCoreApplication::applicationDirPath());
        for (int up = 0; up < 3 && !QFileInfo::exists(d.filePath("data/shaders")); ++up) d.cdUp();
        const QString editorDir = d.filePath("data/shaders");
        const QString mapDir = ctx_.doc.path().empty() ? QString() : QFileInfo(qs(ctx_.doc.path())).absoluteDir().filePath("shaders");
        QString current;
        if (const Entity* ws = ctx_.doc.worldspawn())
            if (const std::string* v = ws->value("iw8r_surface_shader")) current = qs(*v);
        ShaderEditorDialog dlg(mapDir, editorDir, current, this);
        const auto refresh = [this] {
            reloadCustomAssets();
            camera_->materialImagesChanged();
            ctx_.scene.invalidate();
        };
        dlg.onSaved = refresh;
        dlg.onUse = [this, refresh](const QString& name) {
            Entity* ws = ctx_.doc.worldspawn();
            if (!ws) return;
            if (name.isEmpty()) ctx_.doc.removeKey(ws->id, "iw8r_surface_shader");
            else ctx_.doc.setKeyValue(ws->id, "iw8r_surface_shader", name.toStdString());
            ctx_.documentEdited(name.isEmpty() ? QString("Map surface shader removed: the game's own lighting")
                                               : "Map surface shader: " + name + " (Game Lighting and the build)");
            refresh();
        };
        dlg.exec();
    });
    addCommand(tools, "ToonInkOutlines",''')
print('ok')
