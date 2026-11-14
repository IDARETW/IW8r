import io

# A development switch for browser checks: IW8R_WEB_PORT=<port> starts the web server of this editor instance on
# 127.0.0.1 at that port, whatever the saved settings say, and saves nothing (no LAN, no UPnP, no TLS).

p = 'E:/IW8-Radiant/src/qtapp/main_window.cpp'
s = io.open(p, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'


def rep(a, b):
    global s
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == 1, a[:70]
    s = s.replace(a, b)


rep('''    const bool on = s.value("web/enabled", false).toBool();
    if (QAction* a = action("WebServer")) a->setChecked(on);
    upnp_->remove();
    web_->stop();
''', '''    // IW8R_WEB_PORT=<port>: this instance serves on this machine only at that port (browser checks during development);
    // the saved settings are not read for it and not changed.
    const int devPort = qEnvironmentVariableIntValue("IW8R_WEB_PORT");
    const bool on = devPort > 0 || s.value("web/enabled", false).toBool();
    if (QAction* a = action("WebServer")) a->setChecked(on);
    upnp_->remove();
    web_->stop();
''')
rep('''    const bool lan = s.value("web/lan", false).toBool();
    if (lan && !WebServer::hasCredentials()) {''', '''    const bool lan = devPort <= 0 && s.value("web/lan", false).toBool();
    if (lan && !WebServer::hasCredentials()) {''')
rep('''    const quint16 port = quint16(s.value("web/port", 8780).toInt());
    QString err;''', '''    const quint16 port = quint16(devPort > 0 ? devPort : s.value("web/port", 8780).toInt());
    QString err;''')
rep('''    const bool tls = s.value("web/tls", false).toBool();''', '''    const bool tls = devPort <= 0 && s.value("web/tls", false).toBool();''')
io.open(p, 'w', encoding='utf-8', newline='').write(s)
print('ok')
