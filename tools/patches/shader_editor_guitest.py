import io

# GUI test section "shader editor" (plan M6).


def patch(p, a, b, n=1):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    s = s.replace(a, b, 1)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


T = 'E:/IW8-Radiant/src/qtapp/qt_gui_test.cpp'
s = io.open(T, encoding='utf-8').read()
if '#include "shader_editor.h"' not in s:
    i = s.index('#include "')
    s2 = s[:i] + '#include "shader_editor.h"\n' + s[i:]
    io.open(T, 'w', encoding='utf-8', newline='').write(s2)
patch(T, '''    r.section("sun from sky");
    if (const iw8r::Entity* ws = ctx.doc.worldspawn()) {''', '''    r.section("shader editor");
    {
        // The editor's templates compile into the viewport's Game Lighting shader; a broken one reports its own line.
        QDir d(QCoreApplication::applicationDirPath());
        for (int up = 0; up < 3 && !QFileInfo::exists(d.filePath("data/shaders")); ++up) d.cdUp();
        const QString editorDir = d.filePath("data/shaders");
        for (const char* t : {"stock", "toon"}) {
            QFile f(editorDir + "/" + t + ".iw8shader.hlsl");
            const bool read = f.open(QIODevice::ReadOnly);
            const QString err = read ? iw8r::qt::ShaderEditorDialog::checkCode(t, QString::fromUtf8(f.readAll())) : "missing";
            r.check(read && err.isEmpty(), QString("template %1 compiles into the viewport shader %2").arg(t, err));
        }
        const QString bad = iw8r::qt::ShaderEditorDialog::checkCode(
            "broken", "float3 iw8rSurface(IW8RSurface s)\\n{\\n    return s.albedo * undefinedThing;\\n}\\n");
        r.check(bad.contains("broken.iw8shader.hlsl(3"), "a broken shader reports its own file and line: " + bad.left(120));
        // A map shader edited and saved in the dialog; Use for This Map sets the worldspawn key (one undo step).
        QTemporaryDir tmp;
        const QString mapDir = tmp.path() + "/shaders";
        QDir().mkpath(mapDir);
        QFile::copy(editorDir + "/stock.iw8shader.hlsl", mapDir + "/my_look.iw8shader.hlsl");
        iw8r::qt::ShaderEditorDialog dlg(mapDir, editorDir, QString());
        int saved = 0;
        QString used = "-";
        dlg.onSaved = [&] { ++saved; };
        dlg.onUse = [&](const QString& n) { used = n; };
        r.check(dlg.selectShader("my_look") && dlg.currentCode().contains("iw8rSurface"), "the map shader is listed and loaded");
        dlg.setCode(dlg.currentCode().replace("return s.albedo * light;", "return s.albedo * light * 1.5;"));
        QString err;
        r.check(dlg.saveCurrent(&err) && saved == 1, "saving a map shader writes it and refreshes the viewport " + err);
        QFile back(mapDir + "/my_look.iw8shader.hlsl");
        r.check(back.open(QIODevice::ReadOnly) && QString::fromUtf8(back.readAll()).contains("* 1.5"), "the saved file holds the edit");
        r.check(dlg.selectShader("stock") && !dlg.saveCurrent(&err), "an editor template is never overwritten");
    }

    r.section("sun from sky");
    if (const iw8r::Entity* ws = ctx.doc.worldspawn()) {''', n=2)
print('ok')
