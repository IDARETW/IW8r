import io

# The first-person look: Install and Restore write to the game folder, so a browser can never run them; a GUI check of
# the three commands and of the build's refusal of a shader without a filter.

ROOT = 'E:/IW8-Radiant/'


def patch(p, a, b, count=1):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == count, (p, s.count(a), a[:70])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


patch('src/qtapp/main_window.cpp',
      '''"InstallStockMap", "RestoreStockMap", "OpenInNewWindow", "RestartSafeMode"};''',
      '''"InstallStockMap", "RestoreStockMap", "OpenInNewWindow", "RestartSafeMode",
                                           // they write to the game folder
                                           "InstallViewmodelLook", "RestoreViewmodelLook"};''')

T = 'src/qtapp/qt_gui_test.cpp'
patch(T, '''            QAction* snap = w.action("ToggleGameLighting");
            const bool was = snap && snap->isChecked();''', '''            {
                // The first-person look (research 128): three commands under File; the two that write to the game folder
                // are for the editor only, and the build refuses a shader without a filter before any tool runs.
                QJsonObject lookBuild, lookInstall, lookRestore;
                for (const QJsonValue& v : list) {
                    const QJsonObject c = v.toObject();
                    if (c.value("id").toString() == "BuildViewmodelLook") lookBuild = c;
                    if (c.value("id").toString() == "InstallViewmodelLook") lookInstall = c;
                    if (c.value("id").toString() == "RestoreViewmodelLook") lookRestore = c;
                }
                r.check(lookBuild.value("menu").toString() == "File/First-Person Look (weapon and hands)" &&
                            lookInstall.value("blocked").toString() == "only at the editor itself" &&
                            lookRestore.value("blocked").toString() == "only at the editor itself" && !lookBuild.value("blocked").toString().isEmpty(),
                        "the first-person look has its menu, and a browser cannot install or restore it");
                QString why;
                r.check(!w.runWebCommand("InstallViewmodelLook", &why) && !why.isEmpty(), "a browser's install of the first-person look is refused: " + why);
                iw8r::CustomShader plain;
                plain.name = "plain";
                plain.code = "void iw8rSurface(inout IW8RSurface s) {}";
                QString program, lookErr;
                QStringList lookArgs;
                const QString lookWork = QDir(outDir).filePath("look_check");
                const bool prepared = w.prepareViewmodelLook(plain, lookWork, &program, &lookArgs, &lookErr);
                r.check(!prepared && !lookErr.isEmpty() && !QFileInfo::exists(lookWork + "/edits.json"),
                        "the first-person look is not built without tools or from a shader without a filter: " + lookErr.left(90));
                r.check(w.viewmodelLookDir().endsWith("/first_person_look"), "the first-person look has its own folder");
            }
            QAction* snap = w.action("ToggleGameLighting");
            const bool was = snap && snap->isChecked();''')
print('ok')
