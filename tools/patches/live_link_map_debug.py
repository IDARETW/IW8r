import io
def edit(p, pairs):
    s = io.open(p, encoding='utf-8', newline='').read()
    crlf = '\r\n' in s
    s = s.replace('\r\n', '\n')
    for a, b in pairs:
        assert s.count(a) == 1, (p, s.count(a), a[:70])
        s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s.replace('\n', '\r\n') if crlf else s)

edit('src/qtapp/live_link_panel.h', [
(r'''    live::DirectLink& direct() { return direct_; }''',
 r'''    live::DirectLink& direct() { return direct_; }
    // In-game map debug overlay of the recompiled client (docs/research/78; core map_debug_dump): sets the dvar
    // iw8r_mapDebug to `mask` and the filter. False with a message when the game is not reachable or the filter has
    // characters the client would not take.
    bool setDebugOverlay(int mask, const QString& filter);
    // Sends iw8r_mapDebugDump and reads the answer from replay-dw-client.log (up to 5 s); the report compares the
    // game's entities with the map (setMapSource). The report text is also kept in lastDumpReport().
    void requestEntityDump();
    QString lastDumpReport() const { return dumpReport_; }
    // The report for a dump found in `logText` (the last block), against `map` (may be null: no comparison).
    static QString dumpReport(const QString& logText, const MapFile* map, MapDebugDump* parsed = nullptr);
    void setMapSource(std::function<const MapFile*()> fn) { mapSource_ = std::move(fn); }
    // The targetname of the selected entity, for the overlay filter's "Selected" button.
    void setSelectionNameSource(std::function<QString()> fn) { selectionName_ = std::move(fn); }'''),
(r'''    QList<QWidget*> proxySections_;''',
 r'''    QList<QWidget*> proxySections_;
    QComboBox* debugPreset_ = nullptr;
    QLineEdit* debugFilter_ = nullptr;
    QTimer* dumpTimer_ = nullptr;
    qint64 dumpLogOffset_ = 0, dumpSinceMs_ = 0;
    QString dumpReport_;
    std::function<const MapFile*()> mapSource_;
    std::function<QString()> selectionName_;
    void onDumpTick();
    void showDumpReport(const QString& report, const MapDebugDump& dump);'''),
])
s = io.open('src/qtapp/live_link_panel.h', encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
if '#include "map_debug_dump.h"' not in s:
    a = '#include "live_direct.h"'
    assert s.count(a) == 1
    s = s.replace(a, a + nl + '#include "map_debug_dump.h"' + nl + '#include "map_text.h"')
for cls in ('class QComboBox;', 'class QLineEdit;'):
    if cls not in s:
        a = 'class QTimer;'
        assert s.count(a) == 1
        s = s.replace(a, cls + nl + a)
io.open('src/qtapp/live_link_panel.h', 'w', encoding='utf-8', newline='').write(s)

edit('src/qtapp/live_link_panel.cpp', [
(r'''    directLay->addLayout(dRow1);
    directLay->addLayout(dRow2);
    directLay->addWidget(dNote);''',
 r'''    // Map debug overlay (research 78): the recompiled client draws the map's entities, triggers, the player's
    // position and what the crosshair is on. The editor sets its dvars and reads its entity dump.
    auto* dRow3 = new QHBoxLayout();
    dRow3->addWidget(new QLabel("Debug overlay", directBox));
    debugPreset_ = new QComboBox(directBox);
    debugPreset_->setObjectName("liveLinkDebugPreset");
    for (const MapDebugPreset& p : mapDebugPresets()) {
        debugPreset_->addItem(QString::fromLatin1(p.name), p.mask);
        debugPreset_->setItemData(debugPreset_->count() - 1, QString::fromLatin1(p.description), Qt::ToolTipRole);
    }
    debugPreset_->setToolTip("What the game draws over your map while you play it (dvar iw8r_mapDebug). Needs a client built with the map debug overlay; you must host the match.");
    dRow3->addWidget(debugPreset_);
    debugFilter_ = new QLineEdit(directBox);
    debugFilter_->setObjectName("liveLinkDebugFilter");
    debugFilter_->setPlaceholderText("filter: trigger door1 -spawn");
    debugFilter_->setClearButtonEnabled(true);
    debugFilter_->setToolTip("Only entities whose class, name, target or model contains one of these words are drawn; a word that starts with - hides matches");
    dRow3->addWidget(debugFilter_, 1);
    auto* debugSel = new QPushButton("Selected", directBox);
    debugSel->setObjectName("liveLinkDebugSelected");
    debugSel->setToolTip("Filter the overlay to the entity selected in the editor (by its targetname)");
    dRow3->addWidget(debugSel);
    auto* debugDump = new QPushButton("Compare with game", directBox);
    debugDump->setObjectName("liveLinkDebugDump");
    debugDump->setToolTip("Asks the game for its entity list (iw8r_mapDebugDump) and lists which named entities of the map are missing or at another place in the running match");
    dRow3->addWidget(debugDump);
    auto applyOverlay = [this] { setDebugOverlay(debugPreset_->currentData().toInt(), debugFilter_->text()); };
    connect(debugPreset_, &QComboBox::activated, this, applyOverlay);
    connect(debugFilter_, &QLineEdit::returnPressed, this, applyOverlay);
    connect(debugSel, &QPushButton::clicked, this, [this, applyOverlay] {
        const QString name = selectionName_ ? selectionName_().trimmed() : QString();
        if (name.isEmpty()) return showMessage("Debug overlay: select an entity that has a targetname first.", true);
        debugFilter_->setText(name);
        if (debugPreset_->currentData().toInt() == 0) debugPreset_->setCurrentIndex(2);
        applyOverlay();
    });
    connect(debugDump, &QPushButton::clicked, this, [this] { requestEntityDump(); });
    directLay->addLayout(dRow1);
    directLay->addLayout(dRow2);
    directLay->addLayout(dRow3);
    directLay->addWidget(dNote);'''),
(r'''QString LiveLinkPanel::captureGameScreenshot(const QString& folder) {''',
 r'''bool LiveLinkPanel::setDebugOverlay(int mask, const QString& filter) {
    bool ok = true;
    const std::string clean = cleanMapDebugFilter(filter.toStdString(), &ok);
    if (!ok) {
        showMessage("Debug overlay: the filter may hold letters, digits, _ and - with spaces between the words (at most 96 characters).", true);
        return false;
    }
    if (!direct_.attached() && !directAttach()) return false;
    std::string text, err;
    for (const std::string& c : mapDebugCommands(mask, clean)) text += (text.empty() ? "" : "\n") + c;
    if (!direct_.sendCommand(text, &err)) {
        showMessage("Debug overlay: " + QString::fromStdString(err), true);
        return false;
    }
    showMessage(mask == 0 ? QStringLiteral("Debug overlay off (iw8r_mapDebug 0).")
                          : QString("Debug overlay: iw8r_mapDebug %1%2. It draws in a match you host, with a client that has the overlay.")
                                .arg(mask)
                                .arg(clean.empty() ? QString() : ", filter '" + QString::fromStdString(clean) + "'"),
                false);
    return true;
}

QString LiveLinkPanel::dumpReport(const QString& logText, const MapFile* map, MapDebugDump* parsed) {
    const MapDebugDump dump = parseMapDebugDump(logText.toStdString());
    if (parsed) *parsed = dump;
    if (!dump.found) return {};
    QStringList lines;
    if (!dump.complete) lines << "The dump is not complete yet (no end line).";
    if (map) {
        for (const std::string& l : compareDumpWithMap(dump, *map).lines) lines << QString::fromStdString(l);
    } else {
        lines << QString("%1 entities in the game").arg(dump.entities.size());
    }
    return lines.join('\n');
}

void LiveLinkPanel::requestEntityDump() {
    if (!direct_.attached() && !directAttach()) return;
    const QString log = QDir(gameDir_ ? gameDir_() : QString()).filePath("replay-dw-client.log");
    dumpLogOffset_ = QFileInfo::exists(log) ? QFileInfo(log).size() : 0;
    std::string err;
    if (!direct_.sendCommand(kMapDebugDumpCommand, &err)) return showMessage("Compare with game: " + QString::fromStdString(err), true);
    if (!dumpTimer_) {
        dumpTimer_ = new QTimer(this);
        dumpTimer_->setInterval(200);
        connect(dumpTimer_, &QTimer::timeout, this, &LiveLinkPanel::onDumpTick);
    }
    dumpSinceMs_ = QDateTime::currentMSecsSinceEpoch();
    dumpTimer_->start();
    showMessage("Compare with game: asked the game for its entity list...", false);
}

void LiveLinkPanel::onDumpTick() {
    const QString path = QDir(gameDir_ ? gameDir_() : QString()).filePath("replay-dw-client.log");
    QFile f(path);
    QString text;
    if (f.open(QIODevice::ReadOnly)) {
        if (f.size() < dumpLogOffset_) dumpLogOffset_ = 0;  // the log was rotated
        f.seek(dumpLogOffset_);
        text = QString::fromUtf8(f.read(8 * 1024 * 1024));
    }
    MapDebugDump dump;
    const QString report = dumpReport(text, mapSource_ ? mapSource_() : nullptr, &dump);
    if (dump.found && dump.complete) {
        dumpTimer_->stop();
        return showDumpReport(report, dump);
    }
    if (QDateTime::currentMSecsSinceEpoch() - dumpSinceMs_ > 5000) {
        dumpTimer_->stop();
        showMessage(dump.found ? "Compare with game: the game started its entity list but did not finish it within 5 s."
                               : "Compare with game: no answer within 5 s. The client needs the map debug overlay (a build with DW_MapDebug.cpp), "
                                 "and you must be in a match you host.",
                    true);
    }
}

void LiveLinkPanel::showDumpReport(const QString& report, const MapDebugDump& dump) {
    dumpReport_ = report;
    showMessage("Compare with game: " + report.section('\n', 1, 1), false);
    if (QGuiApplication::platformName() == "offscreen") return;  // tests read lastDumpReport()
    auto* dlg = new QDialog(this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setWindowTitle("Game entities compared with the map");
    dlg->resize(820, 480);
    auto* lay = new QVBoxLayout(dlg);
    auto* view = new QPlainTextEdit(report, dlg);
    view->setReadOnly(true);
    view->setLineWrapMode(QPlainTextEdit::NoWrap);
    QFont mono("Consolas");
    mono.setStyleHint(QFont::Monospace);
    view->setFont(mono);
    lay->addWidget(view);
    auto* row = new QHBoxLayout();
    auto* go = new QPushButton("Move editor camera to the player", dlg);
    go->setEnabled(dump.hasView);
    row->addWidget(go);
    row->addStretch(1);
    auto* close = new QPushButton("Close", dlg);
    row->addWidget(close);
    lay->addLayout(row);
    const Vec3 at = dump.view;
    const double pitch = dump.pitch, yaw = dump.yaw;
    connect(go, &QPushButton::clicked, this, [this, at, pitch, yaw] { emit moveEditorCameraRequested(at.x, at.y, at.z, pitch, yaw, 0); });
    connect(close, &QPushButton::clicked, dlg, &QDialog::close);
    dlg->show();
}

QString LiveLinkPanel::captureGameScreenshot(const QString& folder) {'''),
])
s = io.open('src/qtapp/live_link_panel.cpp', encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
for inc in ('#include <QComboBox>', '#include <QDialog>', '#include <QGuiApplication>', '#include <QPlainTextEdit>', '#include <QFile>'):
    if inc not in s:
        a = '#include <QGroupBox>'
        assert s.count(a) == 1
        s = s.replace(a, inc + nl + a)
io.open('src/qtapp/live_link_panel.cpp', 'w', encoding='utf-8', newline='').write(s)

edit('src/qtapp/main_window.cpp', [
(r'''        live->setEditorCameraSource([this] { return ctx_.cam.pos; });''',
 r'''        live->setEditorCameraSource([this] { return ctx_.cam.pos; });
        live->setMapSource([this]() -> const MapFile* { return &ctx_.doc.map(); });
        live->setSelectionNameSource([this]() -> QString {
            for (const auto& sel : ctx_.doc.selection())
                if (const Entity* e = ctx_.doc.findEntity(sel.entity))
                    if (const std::string* tn = e->value("targetname"); tn && !tn->empty()) return QString::fromStdString(*tn);
            return {};
        });'''),
])

edit('src/qtapp/qt_gui_test.cpp', [
(r'''        if (LiveLinkPanel* ll = w.liveLinkPanel()) {''',
 r'''        if (LiveLinkPanel* ll = w.liveLinkPanel()) {
            // Map debug overlay controls (research 78): presets, filter, and the report made from the client's dump.
            auto* preset = ll->findChild<QComboBox*>("liveLinkDebugPreset");
            auto* filter = ll->findChild<QLineEdit*>("liveLinkDebugFilter");
            r.check(preset && filter && preset->count() == 5 && preset->itemData(2).toInt() == 7 && preset->itemData(4).toInt() == 63 &&
                        ll->findChild<QPushButton*>("liveLinkDebugSelected") && ll->findChild<QPushButton*>("liveLinkDebugDump"),
                    "the Live Link dock has the debug overlay presets, the filter and the Compare with game button");
            r.check(!ll->setDebugOverlay(7, "door1; quit") && ll->messageText().contains("filter"),
                    "a debug overlay filter that could carry a second command is refused before anything is sent");
            const QString log = "t IW8R_MAPDEBUG begin map=mp_x\nt IW8R_VIEW 1.00 2.00 3.00 4.00 5.00\nt IW8R_FEET 1.00 2.00 -57.00 0\n"
                                "t IW8R_ENT 40|script_model|script_model||gui_dump_a||||||10.00 20.00 30.00|0.00 0.00 0.00|0.00 0.00 0.00|8.00 8.00 8.00|1|0|-1\n"
                                "t IW8R_MAPDEBUG end entities=1\n";
            Id next = 900000;
            const ParseResult pm = parseMap("iwmap 4\n{\n\"classname\" \"worldspawn\"\n}\n{\n\"classname\" \"script_model\"\n\"targetname\" \"gui_dump_a\"\n\"origin\" \"10 20 30\"\n}\n"
                                            "{\n\"classname\" \"script_origin\"\n\"targetname\" \"gui_dump_b\"\n\"origin\" \"0 0 0\"\n}\n", next);
            MapDebugDump parsed;
            const QString report = pm.map ? LiveLinkPanel::dumpReport(log, &*pm.map, &parsed) : QString();
            r.check(parsed.complete && parsed.hasView && report.contains("2 named map entities looked for: 1 found in place, 0 at another place, 1 missing") &&
                        report.contains("MISSING  gui_dump_b (script_origin)"),
                    "the game's entity dump is compared with the map: found, moved and missing entities are listed");
            r.check(LiveLinkPanel::dumpReport("no dump here", nullptr).isEmpty(), "a log without a dump gives no report");
        }
        if (LiveLinkPanel* ll = w.liveLinkPanel()) {'''),
])
print('ok')
