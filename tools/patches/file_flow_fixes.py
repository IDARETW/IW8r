import io
import os

# File and build-flow quirks from the 2026-11-15 review:
#  - opening a map that no longer exists says so before asking to save (and drops it from Recent);
#  - Save on a map opened from the autosave folder asks where to save (it wrote over the autosave copy);
#  - Save As adds .map when no extension is typed; a failed save explains read-only / in-use files and offers Save As;
#  - closing while a build runs asks first; Restart in Safe Mode / Reset All Settings ask once and start the new
#    editor only after this window has closed;
#  - a failed deploy is a message box, not a status line;
#  - a tool path left empty uses the default; the gsc-tool default comes from the Downloads folder, not a fixed path;
#  - a second F7 / Shift+F7 while a build runs says so; the build log says when unsaved edits are built; every tool the
#    queued stages need is checked before the first one starts.

ROOT = 'E:/IW8-Radiant/'


def patch(p, a, b, count=1):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    if b in s:  # applied already (b can contain a: never apply twice)
        return
    assert s.count(a) == count, (p, s.count(a), a[:80])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


M = 'src/qtapp/main_window.cpp'
patch(M, '''void MainWindow::openPath(const QString& path) {
    if (!confirmDiscard()) return;
    loadDocument(path);
}''', '''void MainWindow::openPath(const QString& path) {
    // A map that is gone (moved, deleted, a Recent entry from another machine): say so before asking to save, and drop
    // it from Recent.
    if (!path.isEmpty() && !path.startsWith("stock:") && !QFileInfo::exists(path)) {
        QStringList recent = Settings::instance().recentFiles();
        if (recent.removeAll(path) > 0) {
            Settings::instance().setValue("recentFiles", recent);
            rebuildRecentMenu();
        }
        QMessageBox::warning(this, "Open", path + "\\n\\nThis file no longer exists (moved or deleted). It was removed from File > Recent.");
        return;
    }
    if (!confirmDiscard()) return;
    loadDocument(path);
}''')
patch(M, '''bool MainWindow::save() {
    if (ctx_.doc.path().empty()) return saveAsDialog(false);
    std::string err;
    if (!ctx_.doc.save(&err)) {
        QMessageBox::critical(this, "Save", qs(err));
        return false;
    }''', '''bool MainWindow::save() {
    if (ctx_.doc.path().empty()) return saveAsDialog(false);
    // A map recovered from the autosave history: Save asks where it goes (writing it back over the autosave copy would
    // leave the original map without the recovered work).
    if (QFileInfo(qs(ctx_.doc.path())).absolutePath().startsWith(QFileInfo(autosaveDir()).absoluteFilePath(), Qt::CaseInsensitive)) {
        ctx_.setStatus("A recovered autosave: choose where to save the map");
        return saveAsDialog(false);
    }
    std::string err;
    if (!ctx_.doc.save(&err)) {
        // A read-only file or one open in another program comes back as error 5 / 32: say which, and offer Save As.
        QString msg = qs(err);
        if (msg.contains("(error 5)") || msg.contains("(error 32)"))
            msg += "\\n\\nThe file is read-only or open in another program. Save the map somewhere else with Save As, or close "
                   "the other program and save again.";
        if (QMessageBox::critical(this, "Save", msg, QMessageBox::Save | QMessageBox::Cancel, QMessageBox::Cancel) == QMessageBox::Save)
            return saveAsDialog(false);
        return false;
    }''')
patch(M, '''    const QString path = QFileDialog::getSaveFileName(this, copyOnly ? "Save a Copy" : "Save As", start,
                                                      "Map source (*.map);;IW8 entity string (*.ents);;All files (*)");
    if (path.isEmpty()) return false;''', '''    QString filter;
    QString path = QFileDialog::getSaveFileName(this, copyOnly ? "Save a Copy" : "Save As", start,
                                                "Map source (*.map);;IW8 entity string (*.ents);;All files (*)", &filter);
    if (path.isEmpty()) return false;
    // A name typed without an extension: the chosen type's (.map unless .ents was picked); Open and the build rely on it.
    if (QFileInfo(path).suffix().isEmpty()) path += filter.contains("*.ents") ? ".ents" : ".map";''')
patch(M, '''void MainWindow::closeEvent(QCloseEvent* e) {
    if (!confirmDiscard()) {
        e->ignore();
        return;
    }''', '''void MainWindow::closeEvent(QCloseEvent* e) {
    // A running build would be stopped with the window: ask first.
    if (compile_ && compile_->busy() &&
        QMessageBox::question(this, "IW8 Radiant Suite", "A build is running. Stop it and close the editor?",
                              QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes) {
        e->ignore();
        return;
    }
    if (!discardConfirmed_ && !confirmDiscard()) {
        e->ignore();
        return;
    }
    discardConfirmed_ = false;''')
patch(M, '''        if (!confirmDiscard()) return;
        QStringList args{"--safe-mode"};
        if (!ctx_.doc.path().empty()) args << qs(ctx_.doc.path());
        if (QProcess::startDetached(QCoreApplication::applicationFilePath(), args)) close();''', '''        if (!confirmDiscard()) return;
        QStringList args{"--safe-mode"};
        if (!ctx_.doc.path().empty()) args << qs(ctx_.doc.path());
        // Asked once; the new editor starts only when this one has really closed (a running build can still stop it).
        discardConfirmed_ = true;
        if (close()) QProcess::startDetached(QCoreApplication::applicationFilePath(), args);
        else discardConfirmed_ = false;''')
patch(M, '''        if (!confirmDiscard()) return;
        QFile::remove(ini + ".bak");
        Settings::instance().setVolatile(true);  // nothing more is written on the way out
        QFile::rename(ini, ini + ".bak");
        QStringList args;
        if (!ctx_.doc.path().empty()) args << qs(ctx_.doc.path());
        QProcess::startDetached(QCoreApplication::applicationFilePath(), args);
        close();''', '''        if (!confirmDiscard()) return;
        QStringList args;
        if (!ctx_.doc.path().empty()) args << qs(ctx_.doc.path());
        Settings::instance().setVolatile(true);  // nothing more is written on the way out
        discardConfirmed_ = true;
        if (!close()) {  // a running build kept the window: nothing is reset
            discardConfirmed_ = false;
            Settings::instance().setVolatile(false);
            return;
        }
        QFile::remove(ini + ".bak");
        QFile::rename(ini, ini + ".bak");
        QProcess::startDetached(QCoreApplication::applicationFilePath(), args);''')
patch(M, '''        if (!compile_->deploy(gameRoot, &err)) return ctx_.setStatus("Deploy failed: " + err);''',
      '''        if (!compile_->deploy(gameRoot, &err)) {
            // The build itself succeeded (its toast said so): a failed copy into the game must not pass unseen.
            QMessageBox::warning(this, "Deploy", "The map was built, but it could not be copied into the game:\\n\\n" + err);
            return ctx_.setStatus("Deploy failed: " + err);
        }''')
patch(M, '''        {"gsc_tool", "C:/Users/''' + os.environ.get('USERNAME', '') + '''/Downloads/iw8 level maker/gsc-tool.exe"},
    };''','''        {"gsc_tool", QStandardPaths::writableLocation(QStandardPaths::DownloadLocation) + "/iw8 level maker/gsc-tool.exe"},
    };''')
patch(M, '''    return Settings::instance().value("tools/" + key, defaults.value(key)).toString();
}''', '''    // A path left empty in Tools > Tool Paths means the default, not "no tool".
    const QString set = Settings::instance().value("tools/" + key, defaults.value(key)).toString();
    return set.trimmed().isEmpty() ? defaults.value(key) : set;
}''')

H = 'src/qtapp/main_window.h'
patch(H, '''    void showDock(const QString& name);''', '''    void showDock(const QString& name);
    bool discardConfirmed_ = false;  // the close in progress has already asked about unsaved changes''')

C = 'src/qtapp/compile_panel.cpp'
patch(C, '''void CompilePanel::run(const std::vector<Stage>& stages) {
    if (running_) return;''', '''void CompilePanel::run(const std::vector<Stage>& stages) {
    if (running_) {
        // F7 again during a build: say so instead of doing nothing.
        ctx_->setStatus("A build is already running (Compile panel: Cancel stops it)");
        return;
    }
    // Every tool the queued stages need, before the first (a missing validator used to show up after minutes of zones).
    {
        QStringList need;
        const auto has = [&](Stage s) { return std::find(stages.begin(), stages.end(), s) != stages.end(); };
        if (has(Stage::Zones)) need << "zonetool" << "replay";
        if (has(Stage::Validate)) need << "acts" << "replay" << "oodle";
        need.removeDuplicates();
        QStringList missing;
        for (const QString& k : need)
            if (!QFileInfo::exists(toolPath_(k))) missing << QString("%1 (%2)").arg(k, toolPath_(k).isEmpty() ? QString("not set") : toolPath_(k));
        if (!missing.isEmpty()) {
            log_->clear();
            append("error: the build needs these tools, not found: " + missing.join(", ") + ". Set them in Tools > Tool Paths.", true);
            status_->setText("<span style='color:#e66'>Not started:</span> missing tools (Tools &gt; Tool Paths)");
            ctx_->setStatus("Build not started: " + QString::number(missing.size()) + " tool(s) missing (Tools > Tool Paths)");
            emit finished(false);
            return;
        }
    }''')
patch(C, '''    append(QString("== %1  map %2  ->  %3").arg(QDateTime::currentDateTime().toString(Qt::ISODate), mapId(), buildRoot()), true);''',
      '''    append(QString("== %1  map %2  ->  %3%4")
               .arg(QDateTime::currentDateTime().toString(Qt::ISODate), mapId(), buildRoot(),
                    ctx_->doc.modified() ? QStringLiteral("  (unsaved edits included: the build uses the map as it is in the editor)") : QString()),
           true);''')
print('ok')
