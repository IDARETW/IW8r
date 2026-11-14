import io

# The first-person look: menu entries (File > First-Person Look), declarations, and the command line
# (--viewmodel-look <shader file> --out <folder>).

ROOT = 'E:/IW8-Radiant/'


def patch(p, a, b, count=1):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == count, (p, s.count(a), a[:70])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


patch('src/qtapp/main_window.h', '''    bool installStockZonesQuiet(const QString& outDir, QStringList& report);
''', '''    bool installStockZonesQuiet(const QString& outDir, QStringList& report);
    // The first-person look (research 128; main_window_look.cpp): the map shader's filter on the weapon and hands a
    // player holds, as an edit of the shared zone kViewmodelLookZone that every map loads. Build compiles the filter of
    // the open map's shader (or asks for a shader file), runs the converter and the ACTS loader test in the background
    // and leaves the zone in viewmodelLookDir()/out. Install and Restore work as for a stock map, after the owner
    // confirms that the look is then on every map.
    void buildViewmodelLook();
    void installViewmodelLook();
    void restoreViewmodelLook();
    QString viewmodelLookDir() const;
    // The same build without a dialog (--viewmodel-look <shader file> --out <folder>): waits for the converter and
    // for ACTS; one line per step in `report`; the zone is left in <outDir>/out. Nothing is installed.
    bool buildViewmodelLookQuiet(const QString& shaderFile, const QString& outDir, QStringList& report);
    bool prepareViewmodelLook(const CustomShader& shader, const QString& work, QString* program, QStringList* args, QString* err);
''')

patch('src/qtapp/main_window.cpp', '''    addCommand(file, "RestoreStockMap", "Restore Stock Map In Game...", QKeySequence(), [this] { restoreStockMap(); });
''', '''    addCommand(file, "RestoreStockMap", "Restore Stock Map In Game...", QKeySequence(), [this] { restoreStockMap(); });
    {
        // The map shader's filter on the weapon and hands a player holds (research 128): a shared zone, so the look is
        // on every map while it is installed.
        QMenu* look = file->addMenu("First-Person Look (weapon and hands)");
        addCommand(look, "BuildViewmodelLook", "Build From Shader...", QKeySequence(), [this] { buildViewmodelLook(); });
        addCommand(look, "InstallViewmodelLook", "Install Into Game...", QKeySequence(), [this] { installViewmodelLook(); });
        addCommand(look, "RestoreViewmodelLook", "Restore In Game...", QKeySequence(), [this] { restoreViewmodelLook(); });
    }
''')

M = 'src/qtapp/main.cpp'
patch(M, '''    QString installStockDir, restoreStockZone;
''', '''    QString installStockDir, restoreStockZone;
    // --viewmodel-look <shader file> --out <folder>: File > First-Person Look > Build From Shader without a dialog; the
    // zone goes to <folder>/out, the report to <folder>/viewmodel-look.txt. Nothing is installed.
    QString viewmodelLookShader;
''')
patch(M, '''        else if (a == "--install-stock" && i + 1 < argc) installStockDir = QString::fromLocal8Bit(argv[++i]);
''', '''        else if (a == "--install-stock" && i + 1 < argc) installStockDir = QString::fromLocal8Bit(argv[++i]);
        else if (a == "--viewmodel-look" && i + 1 < argc) viewmodelLookShader = QString::fromLocal8Bit(argv[++i]);
''')
patch(M, '''    const bool stockInstall = !installStockDir.isEmpty() || !restoreStockZone.isEmpty();
    const bool smoke = !smokeFile.isEmpty() || !compileFile.isEmpty() || !propsZone.isEmpty() || featureShots || stockInstall;
    if (stockInstall && !outDir.isEmpty()) QDir().mkpath(outDir);
''', '''    const bool stockInstall = !installStockDir.isEmpty() || !restoreStockZone.isEmpty();
    const bool lookBuild = !viewmodelLookShader.isEmpty();
    const bool smoke = !smokeFile.isEmpty() || !compileFile.isEmpty() || !propsZone.isEmpty() || featureShots || stockInstall || lookBuild;
    if ((stockInstall || lookBuild) && !outDir.isEmpty()) QDir().mkpath(outDir);
''')
patch(M, '''        if (stockInstall) {
            QTimer::singleShot(300, &w, [&] {''', '''        if (lookBuild) {
            QTimer::singleShot(300, &w, [&] {
                QStringList report;
                const QString dir = outDir.isEmpty() ? QDir::tempPath() + "/iw8r_look" : outDir;
                QDir().mkpath(dir);
                const bool ok = w.buildViewmodelLookQuiet(QDir::fromNativeSeparators(viewmodelLookShader), QDir::fromNativeSeparators(dir), report);
                QFile rf(QDir(dir).filePath("viewmodel-look.txt"));
                if (rf.open(QIODevice::WriteOnly | QIODevice::Text)) rf.write((report.join("\\n") + "\\n").toUtf8());
                app.exit(ok ? 0 : 5);
            });
        }
        if (stockInstall) {
            QTimer::singleShot(300, &w, [&] {''')
print('ok')
