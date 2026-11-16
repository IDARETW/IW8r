import io

# The Replay reader (iw8r-acts.exe) and the zone writer (iw8r-zt.exe) come with the editor: the setup wizard and
# Tools > Tool Paths name them so, and a path equal to the bundled default is saved empty (the default), so a moved or
# reinstalled editor keeps finding its own copies.


def rw(p, pairs):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    s = s.replace('\r\n', '\n')
    for a, b in pairs:
        if b in s:
            continue
        assert s.count(a) == 1, (p, a)
        s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s.replace('\n', nl))


R = 'E:/IW8-Radiant/src/qtapp/'
rw(R + 'setup_wizard.cpp', [
    ('''    connect(addRow("ACTS (acts.exe)", acts_, "Browse...", actsStatus_), &QPushButton::clicked, this, [this] {
        browseFile(acts_, "ACTS (acts.exe)", "acts.exe (acts.exe);;Executables (*.exe);;All files (*)");''',
     '''    connect(addRow("Replay reader (iw8r-acts.exe)", acts_, "Browse...", actsStatus_), &QPushButton::clicked, this, [this] {
        browseFile(acts_, "Replay reader (iw8r-acts.exe)", "Executables (*.exe);;All files (*)");'''),
    ('''    connect(addRow("x64-zt (zonetool.exe)", zonetool_, "Browse...", zonetoolStatus_), &QPushButton::clicked, this, [this] {
        browseFile(zonetool_, "x64-zt (zonetool.exe)", "zonetool.exe (zonetool.exe);;Executables (*.exe);;All files (*)");''',
     '''    connect(addRow("Zone writer (iw8r-zt.exe)", zonetool_, "Browse...", zonetoolStatus_), &QPushButton::clicked, this, [this] {
        browseFile(zonetool_, "Zone writer (iw8r-zt.exe)", "Executables (*.exe);;All files (*)");'''),
    ('''    if (!isExistingFile(actsPath().toStdString())) notes << "ACTS (acts.exe) is not set or missing: Browse to it.";
    if (!isExistingFile(zonetoolPath().toStdString())) notes << "x64-zt zonetool.exe is not set: Browse to it (optional for the map editor).";''',
     '''    if (!isExistingFile(actsPath().toStdString()))
        notes << "The Replay reader (iw8r-acts.exe, installed with the editor) was not found: reinstall or Browse to it.";
    if (!isExistingFile(zonetoolPath().toStdString()))
        notes << "The zone writer (iw8r-zt.exe, installed with the editor) was not found: reinstall or Browse to it.";'''),
    ('''                              "ACTS (acts.exe) is not set or is missing: stock maps, stock assets and build validation will not work "
                              "until it is (Tools > Tool Paths). Save anyway?") != QMessageBox::Yes)''',
     '''                              "The Replay reader (iw8r-acts.exe) was not found: stock maps, stock assets and build validation will "
                              "not work until it is (Tools > Tool Paths). Save anyway?") != QMessageBox::Yes)'''),
    ('''    s.setValue("tools/acts", actsPath());''',
     '''    // The tools that come with the editor are saved empty when they are its own copies (empty = the default).
    const auto bundled = [](const QString& path, const char* exe) {
        const QString own = QDir::cleanPath(QCoreApplication::applicationDirPath() + "/" + exe);
        return QDir::cleanPath(path).compare(own, Qt::CaseInsensitive) == 0 ? QString() : path;
    };
    s.setValue("tools/acts", bundled(actsPath(), "iw8r-acts.exe"));'''),
    ('''    s.setValue("tools/zonetool", zonetoolPath());''', '''    s.setValue("tools/zonetool", bundled(zonetoolPath(), "iw8r-zt.exe"));'''),
])
rw(R + 'main_window.cpp', [
    ('''{"acts", "ACTS (acts.exe)"}''', '''{"acts", "Replay reader (empty = iw8r-acts.exe beside the editor)"}'''),
    ('''    for (auto it = edits.begin(); it != edits.end(); ++it) Settings::instance().setValue("tools/" + it.key(), it.value()->text());''',
     '''    // A path equal to the default (the tools that come with the editor) is saved empty, so it follows the install.
    for (auto it = edits.begin(); it != edits.end(); ++it) {
        Settings::instance().setValue("tools/" + it.key(), QString());
        const QString def = toolPath(it.key());
        const QString text = it.value()->text().trimmed();
        Settings::instance().setValue("tools/" + it.key(),
                                      QDir::cleanPath(text).compare(QDir::cleanPath(def), Qt::CaseInsensitive) == 0 ? QString() : text);
    }'''),
    ('''            ctx_.setStatus("ACTS (acts.exe) is not set: stock maps, stock assets and build validation need it (Tools > Tool Paths)");''',
     '''            ctx_.setStatus("The Replay reader (iw8r-acts.exe) was not found: stock maps, stock assets and build validation need it "
                           "(Tools > Tool Paths)");'''),
])
rw(R + 'qt_gui_test.cpp', [
    ('''"ACTS and x64-zt stay as they were (not on the fake folder)"''',
     '''"the Replay reader and zone writer stay as they were (not on the fake folder)"'''),
])
print('ok')
