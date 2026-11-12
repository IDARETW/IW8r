import io
def edit(p, pairs):
    s = io.open(p, encoding='utf-8', newline='').read()
    crlf = '\r\n' in s
    s = s.replace('\r\n', '\n')
    for a, b in pairs:
        assert s.count(a) == 1, (p, s.count(a), a[:70])
        s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s.replace('\n', '\r\n') if crlf else s)

edit('src/qtapp/qt_gui_test.cpp', [
(r'''        runner.cancel("test");
        r.check(!runner.busy() && done && !doneOk, "cancel stops the runner and reports it");
        r.check(!CompilePanel::gameRunningFrom(fake), "no game runs from the scratch folder");
    }''',
 r'''        runner.cancel("test");
        r.check(!runner.busy() && done && !doneOk, "cancel stops the runner and reports it");
        r.check(!CompilePanel::gameRunningFrom(fake), "no game runs from the scratch folder");
        r.check(statuses.join("\n").contains(QRegularExpression("F5 \\+\\d+\\.\\d s: ")), "F5 status lines carry the time since F5");

        // Which exe F5 starts when Tool Paths 'launch' is empty (research 76: the stock exe cannot load custom maps).
        const QString pick = fake + "/pick";
        QDir().mkpath(pick);
        auto writeExe = [&](const QString& name, bool recompiled, const QByteArray& extra = QByteArray()) {
            QByteArray b(0x200, '\0');
            b[0] = 'M', b[1] = 'Z';
            if (recompiled) b.replace(0x40, 8, "RPLYCMP2");
            b += extra;
            QFile f(pick + "/" + name);
            if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) f.write(b);
            return pick + "/" + name;
        };
        const QString stockExe = writeExe("game_dx12_ship_replay.exe", false);
        QString note;
        QString picked = DevmapRunner::chooseLaunchExe(QString(), stockExe, &note);
        r.check(QFileInfo(picked).fileName() == "game_dx12_ship_replay.exe" && note.startsWith("warning"),
                "only a stock exe in the game folder: it is used, with a warning that it cannot load custom maps");
        const QString dwExe = writeExe("game_dx12_ship_replay_dw.exe", true);
        const QString localExe = writeExe("game_dx12_ship_replay_local_dw.exe", true, QByteArray(300, 'x') + "t DW_DEVMAP request %s");
        picked = DevmapRunner::chooseLaunchExe(QString(), stockExe, &note);
        r.check(QFileInfo(picked).fileName() == "game_dx12_ship_replay_local_dw.exe" && !note.startsWith("warning") && !note.isEmpty(),
                "the local DW client is preferred over the Tool Paths default (the stock exe)");
        r.check(DevmapRunner::chooseLaunchExe("C:/x/launch.cmd", stockExe, &note) == "C:/x/launch.cmd" && note.isEmpty(),
                "a Tool Paths 'launch' entry is always used as it is");
        r.check(DevmapRunner::exeHasClientRequest(localExe) && !DevmapRunner::exeHasClientRequest(dwExe) &&
                    !DevmapRunner::exeHasClientRequest(script.fileName()),
                "an exe gets the start request only when it contains the client's DW_DEVMAP marker");
        {
            QFile ini(pick + "/game_dx12_ship_replay_local_dw.ini");
            if (ini.open(QIODevice::WriteOnly | QIODevice::Truncate)) ini.write("host=127.0.0.1\r\nbypass_update_restart=1\r\n");
        }
        r.check(DevmapRunner::localHostOf(localExe) == "127.0.0.1" && DevmapRunner::localHostOf(dwExe).isEmpty(),
                "the ini beside the exe tells a local-host client from the others");

        // The host probe, against a one-shot fake host on a free loopback port.
        std::atomic<int> port{0};
        std::thread fakeHost([&port] {
            QTcpServer srv;
            if (!srv.listen(QHostAddress::LocalHost, 0)) return port.store(-1);
            port.store(srv.serverPort());
            if (!srv.waitForNewConnection(8000)) return;
            QTcpSocket* c = srv.nextPendingConnection();
            c->waitForReadyRead(2000);
            c->write("HTTP/1.0 200 OK\r\nContent-Type: application/json\r\n\r\n{\"server\":\"iw8-dw\",\"local_qos_enabled\":true}");
            c->waitForBytesWritten(2000);
            c->disconnectFromHost();
            if (c->state() != QAbstractSocket::UnconnectedState) c->waitForDisconnected(1000);
            delete c;
        });
        t.restart();
        while (port.load() == 0 && t.elapsed() < 5000) QThread::msleep(5);
        const bool probed = port.load() > 0 && DevmapRunner::probeHost(QString("http://127.0.0.1:%1/health").arg(port.load()));
        if (!probed && port.load() > 0) {  // let the fake host's accept return
            QTcpSocket poke;
            poke.connectToHost(QHostAddress::LocalHost, quint16(port.load()));
            poke.waitForConnected(500);
        }
        fakeHost.join();
        r.check(probed, "the host probe accepts a health answer whose server is iw8-dw");
        const QString deadUrl = QString("http://127.0.0.1:%1/health").arg(port.load());  // the fake host is gone
        r.check(!DevmapRunner::probeHost(deadUrl), "the host probe fails when nothing listens");
        r.check(!DevmapRunner::probeHost("http://example.com/health"), "the host probe only ever talks to this machine");

        // A local-host client with no host and no command to start one: F5 stops before it starts the game.
        DevmapRunner hostRunner;
        QStringList hostStatuses;
        bool hostDone = false, hostOk = true;
        QString hostEnd;
        QObject::connect(&hostRunner, &DevmapRunner::status, [&](const QString& s, bool) { hostStatuses << s; });
        QObject::connect(&hostRunner, &DevmapRunner::finished, [&](bool ok, const QString& s) { hostDone = true, hostOk = ok, hostEnd = s; });
        DevmapRunner::Config hc;
        hc.mapId = "mp_iw8r_test";
        hc.gameRoot = pick;
        hc.launchExe = localExe;
        hc.hostHealthUrl = deadUrl;
        hc.hostCommand.clear();
        int hostDeploys = 0;
        hostRunner.start(hc, [&](QString*) { return ++hostDeploys, true; });
        t.restart();
        while (!hostDone && t.elapsed() < 10000) QApplication::processEvents(QEventLoop::AllEvents, 50);
        r.check(hostDone && !hostOk && hostEnd.contains("online-services host") && hostEnd.contains("host=127.0.0.1"),
                "a local-host client whose host does not answer: F5 stops with the reason instead of starting the game");
        r.check(!CompilePanel::gameRunningFrom(pick), "the fake exe was not started");
    }'''),
])
s = io.open('src/qtapp/qt_gui_test.cpp', encoding='utf-8', newline='').read()
need = ['#include <QTcpServer>', '#include <QTcpSocket>', '#include <QThread>', '#include <QRegularExpression>', '#include <atomic>', '#include <thread>']
missing = [n for n in need if n not in s]
if missing:
    nl = '\r\n' if '\r\n' in s else '\n'
    anchor = '#include <QApplication>'
    assert s.count(anchor) == 1
    s = s.replace(anchor, anchor + nl + nl.join(missing))
    io.open('src/qtapp/qt_gui_test.cpp', 'w', encoding='utf-8', newline='').write(s)
print('ok', missing)
