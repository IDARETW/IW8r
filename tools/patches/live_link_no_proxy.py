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
(r'''    QTimer* followTimer_ = nullptr;
    // Quick match''',
 r'''    // The four sections that talk to the old proxy DLL through <game>\.proxy\control.ini. They are shown, and the
    // 2 s status request is written, only when that DLL is installed in the game folder (proxyInstalledIn).
    QList<QWidget*> proxySections_;
    QGroupBox* directBox_ = nullptr;
    QTimer* followTimer_ = nullptr;
    // Quick match'''),
(r'''    static QStringList quickMatchCommands(''',
 r'''    // True when the old proxy DLL (mw120rproxy, loaded as XInput9_1_0.dll) is in the game folder. The recompiled
    // clients do not use it (research 76). Preferences live/proxyControls = true forces the sections on.
    static bool proxyInstalledIn(const QString& gameDir);
    bool proxySectionsVisible() const;
    static QStringList quickMatchCommands('''),
])
s = io.open('src/qtapp/live_link_panel.h', encoding='utf-8', newline='').read()
if 'class QGroupBox;' not in s:
    a = 'class QTimer;'
    assert s.count(a) == 1
    nl = '\r\n' if '\r\n' in s else '\n'
    s = s.replace(a, 'class QGroupBox;' + nl + a)
    io.open('src/qtapp/live_link_panel.h', 'w', encoding='utf-8', newline='').write(s)

edit('src/qtapp/live_link_panel.cpp', [
(r'''    lay->addWidget(camera);

    // Direct link:''', r'''    lay->addWidget(camera);
    proxySections_ = {status, select, console, camera};

    // Direct link:'''),
(r'''    auto* directBox = new QGroupBox("Direct (no proxy)", this);''',
 r'''    auto* directBox = directBox_ = new QGroupBox("Direct (no proxy)", this);'''),
(r'''void LiveLinkPanel::showEvent(QShowEvent* e) {
    QWidget::showEvent(e);
    const QString id = mapId_ ? mapId_().trimmed() : QString();
    deployedLabel_->setText(id.isEmpty() ? QStringLiteral("Deployed map: none") : "Deployed map: " + id);
    statusTimer_->start();
    onStatusTick();
}''',
 r'''bool LiveLinkPanel::proxyInstalledIn(const QString& gameDir) {
    if (Settings::instance().value("live/proxyControls", false).toBool()) return true;
    return !gameDir.trimmed().isEmpty() && QFileInfo::exists(QDir(gameDir.trimmed()).filePath("XInput9_1_0.dll"));
}

bool LiveLinkPanel::proxySectionsVisible() const { return !proxySections_.isEmpty() && !proxySections_.front()->isHidden(); }

void LiveLinkPanel::showEvent(QShowEvent* e) {
    QWidget::showEvent(e);
    const QString id = mapId_ ? mapId_().trimmed() : QString();
    deployedLabel_->setText(id.isEmpty() ? QStringLiteral("Deployed map: none") : "Deployed map: " + id);
    // Without the proxy DLL nothing reads control.ini: its sections are hidden and no request is written.
    const bool proxy = proxyInstalledIn(gameDir_ ? gameDir_() : QString());
    for (QWidget* section : proxySections_) section->setVisible(proxy);
    directBox_->setTitle(proxy ? "Direct (no proxy)" : "Game link");
    if (!proxy) return statusTimer_->stop();
    statusTimer_->start();
    onStatusTick();
}'''),
(r'''    auto* dNote = new QLabel("Works with any exe running from the game folder, with or without the proxy. "
                             "Commands are appended to the game's command buffer while it is idle.", directBox);''',
 r'''    auto* dNote = new QLabel("Works with any exe running from the game folder, stock or recompiled. "
                             "Commands are appended to the game's command buffer while it is idle.", directBox);'''),
])
s = io.open('src/qtapp/live_link_panel.cpp', encoding='utf-8', newline='').read()
for inc in ('#include <QDir>', '#include <QFileInfo>', '#include "settings.h"'):
    assert inc in s, inc

edit('src/qtapp/qt_gui_test.cpp', [
(r'''        if (LiveLinkPanel* ll = w.liveLinkPanel()) {''',
 r'''        {
            // The proxy sections of the Live Link dock exist only for a game folder that has the proxy DLL.
            const QString noProxy = outDir + "/live_no_proxy", withProxy = outDir + "/live_with_proxy";
            QDir().mkpath(noProxy);
            QDir().mkpath(withProxy);
            QFile dll(withProxy + "/XInput9_1_0.dll");
            if (dll.open(QIODevice::WriteOnly | QIODevice::Truncate)) dll.write("MZ");
            dll.close();
            r.check(!LiveLinkPanel::proxyInstalledIn(noProxy) && LiveLinkPanel::proxyInstalledIn(withProxy) && !LiveLinkPanel::proxyInstalledIn(QString()),
                    "the proxy is taken as installed only when its DLL is in the game folder");
            QDir().mkpath(noProxy + "/.proxy");
            LiveLinkPanel plain([&] { return noProxy; }, [] { return QString("mp_iw8r_test"); });
            plain.show();
            QApplication::processEvents();
            r.check(!plain.proxySectionsVisible() && plain.findChild<QGroupBox*>() != nullptr, "without the proxy DLL the Live Link dock shows only the game link section");
            QTest::qWait(150);
            r.check(!QFileInfo::exists(noProxy + "/.proxy/control.ini") && !QFileInfo::exists(noProxy + "/.proxy/control.ini.tmp"),
                    "without the proxy DLL no control request is written into the game folder");
            plain.hide();
            LiveLinkPanel old([&] { return withProxy; }, [] { return QString("mp_iw8r_test"); });
            old.show();
            QApplication::processEvents();
            r.check(old.proxySectionsVisible(), "with the proxy DLL the status, select, console and camera sections are shown");
            old.hide();
            w.activateWindow();
            QApplication::setActiveWindow(&w);
            QApplication::processEvents();
        }
        if (LiveLinkPanel* ll = w.liveLinkPanel()) {'''),
])
print('ok')
