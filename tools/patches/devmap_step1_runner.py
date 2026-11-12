import io
def edit(p, pairs):
    s = io.open(p, encoding='utf-8', newline='').read()
    crlf = '\r\n' in s
    s = s.replace('\r\n', '\n')
    for a, b in pairs:
        assert s.count(a) == 1, (p, s.count(a), a[:70])
        s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s.replace('\n', '\r\n') if crlf else s)

edit('src/qtapp/devmap_launch.h', [
(r'''//   game not running  -> deploy, start the configured exe with "+mpMode 1", wait for the MP menu, host a private
//                        match (xstartprivatematch), set ui_mapname / ui_gametype, xpartygo''',
 r'''//   game not running  -> deploy, make sure the local online-services host answers, start the exe (the local DW
//                        client when Tool Paths "launch" is empty), select Multiplayer once the client has signed in,
//                        host a private match (xstartprivatematch), set ui_mapname / ui_gametype, "xpartygo 1" (no
//                        countdown). A client with the start-request feature ("--devmap <id>", DW_DEVMAP log lines)
//                        does all of that itself and the editor only watches (docs/research/76, 77).'''),
(r'''        bool reload = true;   // use map_reload (DW) for a rebuild of the running map
    };''',
 r'''        bool reload = true;   // use map_reload (DW) for a rebuild of the running map
        bool hostCheck = true;       // an exe whose ini names a loopback host: the host must answer before the launch
        QString hostCommand;         // what starts the host when it does not answer (empty: stop with a message)
        QString hostHealthUrl;       // http://127.0.0.1:8085/health
        bool startRequest = true;    // pass "--devmap <id> --gametype <g>" to an exe that has the client feature
        int bots = 0;                // "--bots <n>" of the start request
    };'''),
(r'''    static bool reloadSetting();
''',
 r'''    static bool reloadSetting();
    static bool hostCheckSetting();        // devmap/hostCheck (true)
    static QString hostCommandSetting();   // devmap/hostCommand (Host-Local-Server.ps1 -NoLaunch when that file exists)
    static QString hostHealthSetting();    // devmap/hostHealth
    static bool startRequestSetting();     // devmap/startRequest (true)
    static int botsSetting();              // devmap/bots (0)
    // Fills the settings-backed fields of a Config (everything but mapId, gameRoot and launchExe).
    static void applySettings(Config& cfg);
    // The exe F5 and Launch Replay start. Tool Paths "launch" when set. Otherwise the best exe in the game folder
    // (devmap::pickLaunchExe: the local DW client first); `note` says what was picked, and warns for a stock exe.
    static QString chooseLaunchExe(const QString& launchSetting, const QString& replayExe, QString* note = nullptr);
    // True when the exe file contains the client start-request marker (devmap::kClientRequestMarker). Cached.
    static bool exeHasClientRequest(const QString& exe);
    // The loopback host named by "<exe stem>.ini" beside the exe ("host=127.0.0.1"), or empty.
    static QString localHostOf(const QString& exe);
    // One blocking GET of the health address (loopback, at most about a second). True when a DW host answered.
    static bool probeHost(const QString& healthUrl);
'''),
(r'''    void readDwLog(bool force);
    void enter(devmap::Phase p);''',
 r'''    void readDwLog(bool force);
    void readLaunchLog();
    void enter(devmap::Phase p);
    // 1 ready (or not needed), 0 still waiting for the host, -1 stop with *err.
    int hostReady(QString* err);
    QString stamp() const;  // "F5 +12.3 s: " (time since F5; the measuring tool for the timeline)'''),
(r'''    int selectMpSent_ = 0;
    QString lastStatus_;''',
 r'''    int selectMpSent_ = 0, hostLobbySent_ = 0;
    QString lastStatus_, lastClientLine_;
    qint64 runStartMs_ = 0, lastPidScanMs_ = -1, launchLogOffset_ = -1, lastLaunchLogMs_ = -1;
    qint64 hostStartedMs_ = -1, lastHostProbeMs_ = -1;
    bool hostOk_ = false, launched_ = false, saidSignedIn_ = false, sentRequest_ = false;
    QList<quint32> pids_;
    devmap::LaunchLog launchLog_;'''),
(r'''#include <QObject>
#include <QString>''', r'''#include <QList>
#include <QObject>
#include <QString>'''),
])

edit('src/qtapp/devmap_launch.cpp', [
(r'''#include <QProcess>
#include <QTimer>''', r'''#include <QHash>
#include <QProcess>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>

#include "setup_detect.h"'''),
(r'''constexpr int kTickMs = 500;
constexpr qint64 kDwLogReadEveryMs = 5000;''',
 r'''// The memory reads are a few ReadProcessMemory calls, so the state is polled fast; the process list and the client
// log are read less often.
constexpr int kTickMs = 100;
constexpr qint64 kPidScanEveryMs = 500;
constexpr qint64 kLaunchLogReadEveryMs = 250;
constexpr qint64 kLaunchLogMaxBytes = 2 * 1024 * 1024;
constexpr qint64 kHostProbeEveryMs = 500;
constexpr qint64 kHostStartTimeoutMs = 60000;
constexpr qint64 kDwLogReadEveryMs = 5000;'''),
(r'''bool DevmapRunner::reloadSetting() { return Settings::instance().value("devmap/reload", true).toBool(); }
''',
 r'''bool DevmapRunner::reloadSetting() { return Settings::instance().value("devmap/reload", true).toBool(); }
bool DevmapRunner::hostCheckSetting() { return Settings::instance().value("devmap/hostCheck", true).toBool(); }
QString DevmapRunner::hostCommandSetting() {
    const QString set = Settings::instance().value("devmap/hostCommand").toString().trimmed();
    if (!set.isEmpty()) return set == "-" ? QString() : set;
    // The owner's host script, when it is where the DW project keeps it. -NoLaunch: the host only, no game.
    const QString script = QStringLiteral("E:/DW/IW8/Host-Local-Server.ps1");
    if (!QFileInfo::exists(script)) return {};
    return "powershell -NoProfile -ExecutionPolicy Bypass -File \"" + QDir::toNativeSeparators(script) + "\" -NoLaunch";
}
QString DevmapRunner::hostHealthSetting() {
    const QString u = Settings::instance().value("devmap/hostHealth").toString().trimmed();
    return u.isEmpty() ? QStringLiteral("http://127.0.0.1:8085/health") : u;
}
bool DevmapRunner::startRequestSetting() { return Settings::instance().value("devmap/startRequest", true).toBool(); }
int DevmapRunner::botsSetting() { return qBound(0, Settings::instance().value("devmap/bots", 0).toInt(), 199); }

void DevmapRunner::applySettings(Config& cfg) {
    cfg.launchArgs = launchArgsSetting();
    cfg.gametype = gametypeSetting();
    cfg.reload = reloadSetting();
    cfg.hostCheck = hostCheckSetting();
    cfg.hostCommand = hostCommandSetting();
    cfg.hostHealthUrl = hostHealthSetting();
    cfg.startRequest = startRequestSetting();
    cfg.bots = botsSetting();
}

QString DevmapRunner::chooseLaunchExe(const QString& launchSetting, const QString& replayExe, QString* note) {
    if (note) note->clear();
    if (!launchSetting.trimmed().isEmpty()) return launchSetting;
    const QString dir = QFileInfo(replayExe).absolutePath();
    std::vector<devmap::ExeCandidate> list;
    for (const std::string& exe : gameExesIn(dir.toStdString()))
        list.push_back({exe, classifyExe(exe) == ExeKind::Recompiled});
    const int at = devmap::pickLaunchExe(list);
    if (at < 0) return replayExe;
    const QString picked = QString::fromStdString(list[size_t(at)].path);
    if (note) {
        if (list[size_t(at)].recompiled)
            *note = "starting " + QFileInfo(picked).fileName() + " (Tool Paths 'launch' is empty; a recompiled client was found)";
        else
            *note = "warning: " + QFileInfo(picked).fileName() + " is a stock exe and no recompiled client was found in " +
                    QDir::toNativeSeparators(dir) + "; a stock exe cannot load custom maps";
    }
    return picked;
}

bool DevmapRunner::exeHasClientRequest(const QString& exe) {
    static QHash<QString, bool> cache;
    const QFileInfo fi(exe);
    if (!fi.isFile() || fi.suffix().toLower() != "exe") return false;
    const QString key = fi.absoluteFilePath() + "|" + QString::number(fi.size()) + "|" + QString::number(fi.lastModified().toMSecsSinceEpoch());
    const auto it = cache.constFind(key);
    if (it != cache.constEnd()) return *it;
    bool found = false;
    QFile f(fi.absoluteFilePath());
    if (f.open(QIODevice::ReadOnly)) {
        const QByteArray marker(devmap::kClientRequestMarker);
        QByteArray carry;
        while (!found && !f.atEnd()) {
            const QByteArray block = carry + f.read(8 * 1024 * 1024);
            found = block.contains(marker);
            carry = block.right(marker.size() - 1);
        }
    }
    cache.insert(key, found);
    return found;
}

QString DevmapRunner::localHostOf(const QString& exe) {
    const QFileInfo fi(exe);
    QFile ini(fi.absolutePath() + "/" + fi.completeBaseName() + ".ini");
    if (!ini.open(QIODevice::ReadOnly)) return {};
    const std::string host = devmap::iniValue(ini.read(64 * 1024).toStdString(), "host");
    return devmap::isLoopbackHost(host) ? QString::fromStdString(host) : QString();
}

bool DevmapRunner::probeHost(const QString& healthUrl) {
    const QUrl url(healthUrl);
    if (!url.isValid() || url.scheme() != "http" || !devmap::isLoopbackHost(url.host().toStdString())) return false;
    QTcpSocket sock;
    sock.connectToHost(url.host(), quint16(url.port(80)));
    if (!sock.waitForConnected(300)) return false;
    const QByteArray path = url.path().isEmpty() ? QByteArray("/") : url.path(QUrl::FullyEncoded).toLatin1();
    sock.write("GET " + path + " HTTP/1.0\r\nHost: " + url.host().toLatin1() + "\r\nConnection: close\r\n\r\n");
    QByteArray answer;
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < 1000 && answer.size() < 64 * 1024) {
        if (!sock.waitForReadyRead(200) && sock.state() != QAbstractSocket::ConnectedState) break;
        answer += sock.readAll();
        if (devmap::healthIsDwHost(answer.toStdString())) return true;
    }
    answer += sock.readAll();
    return devmap::healthIsDwHost(answer.toStdString());
}

QString DevmapRunner::stamp() const {
    return QString("F5 +%1 s: ").arg(double(nowMs() - runStartMs_) / 1000.0, 0, 'f', 1);
}

int DevmapRunner::hostReady(QString* err) {
    if (hostOk_ || !cfg_.hostCheck) return 1;
    const QString host = localHostOf(cfg_.launchExe);
    if (host.isEmpty()) return hostOk_ = true, 1;  // not a local-host client (or a launcher script that starts its own host)
    const qint64 now = nowMs();
    if (lastHostProbeMs_ >= 0 && now - lastHostProbeMs_ < kHostProbeEveryMs) return 0;
    lastHostProbeMs_ = now;
    if (probeHost(cfg_.hostHealthUrl)) {
        emit status(stamp() + "the local online-services host answers (" + cfg_.hostHealthUrl + ")", false);
        return hostOk_ = true, 1;
    }
    if (hostStartedMs_ < 0) {
        const std::vector<std::string> parts = devmap::splitLaunchArgs(cfg_.hostCommand.toStdString());
        if (parts.empty()) {
            if (err)
                *err = "the local online-services host does not answer at " + cfg_.hostHealthUrl + ", and " + QFileInfo(cfg_.launchExe).fileName() +
                       " needs it (its ini says host=" + host + "). Start it (Host-Local-Server.ps1 -NoLaunch), or set devmap/hostCommand in IW8Radiant.ini";
            return -1;
        }
        QStringList args;
        for (size_t i = 1; i < parts.size(); ++i) args << QString::fromStdString(parts[i]);
        if (!QProcess::startDetached(QString::fromStdString(parts[0]), args)) {
            if (err) *err = "could not start the local online-services host: " + cfg_.hostCommand;
            return -1;
        }
        hostStartedMs_ = now;
        emit status(stamp() + "the local online-services host does not answer: starting it (" + cfg_.hostCommand + ")", false);
        return 0;
    }
    if (now - hostStartedMs_ > kHostStartTimeoutMs) {
        if (err) *err = "the local online-services host did not answer at " + cfg_.hostHealthUrl + " within 60 s of starting it (" + cfg_.hostCommand + ")";
        return -1;
    }
    return 0;
}
'''),
(r'''    reloadLogOffset_ = -1;
    selectMpSent_ = 0;
    lastDwLogReadMs_ = -1;
    lastStatus_.clear();''',
 r'''    reloadLogOffset_ = -1;
    selectMpSent_ = hostLobbySent_ = 0;
    lastDwLogReadMs_ = -1;
    lastStatus_.clear();
    lastClientLine_.clear();
    runStartMs_ = nowMs();
    lastPidScanMs_ = lastLaunchLogMs_ = launchLogOffset_ = -1;
    hostStartedMs_ = lastHostProbeMs_ = -1;
    hostOk_ = launched_ = saidSignedIn_ = sentRequest_ = false;
    pids_.clear();
    launchLog_ = {};'''),
(r'''    } else {
        for (const std::string& a : devmap::splitLaunchArgs(cfg_.launchArgs.toStdString())) args << QString::fromStdString(a);
    }
    if (!QProcess::startDetached(program, args, dir)) {
        if (err) *err = "could not start " + exe;
        return false;
    }
    emit status(QString("F5: started %1%2; waiting for the Multiplayer menu")
                    .arg(QFileInfo(exe).fileName(), args.isEmpty() || program != exe ? QString() : " " + args.join(' ')),
                false);
    return true;''',
 r'''    } else {
        for (const std::string& a : devmap::splitLaunchArgs(cfg_.launchArgs.toStdString())) args << QString::fromStdString(a);
        // The start request, for a client that reads it (the engine itself drops the text).
        if (cfg_.startRequest && exeHasClientRequest(exe)) {
            const auto req = devmap::startRequestArgs(cfg_.mapId.toStdString(), cfg_.gametype.toStdString(), cfg_.bots);
            for (const std::string& a : req) args << QString::fromStdString(a);
            sentRequest_ = !req.empty();
        }
    }
    {
        // Lines the client appends from here on belong to this launch.
        const QFileInfo log(dwLogPath(cfg_.gameRoot));
        launchLogOffset_ = log.exists() ? log.size() : 0;
    }
    if (!QProcess::startDetached(program, args, dir)) {
        if (err) *err = "could not start " + exe;
        return false;
    }
    launched_ = true;
    emit status(stamp() + QString("started %1%2; %3")
                              .arg(QFileInfo(exe).fileName(), args.isEmpty() || program != exe ? QString() : " " + args.join(' '),
                                   sentRequest_ ? "the client was asked to boot straight into the map" : "waiting for the Multiplayer menu"),
                false);
    return true;'''),
(r'''void DevmapRunner::tick() {
    if (!busy()) return timer_->stop();
    const qint64 now = nowMs();
    const QList<quint32> pids = CompilePanel::gamePidsFrom(cfg_.gameRoot);
    if (pids.size() == 1 && (!link_.attached() || link_.pid() != pids.front())) {
        std::string err;
        if (!link_.attach(pids.front(), &err)) link_.detach();  // early boot: modules not listed yet; try again
    } else if (pids.size() != 1) {
        link_.detach();
    }
    const devmap::GameSnapshot snap = snapshot(link_);
    const devmap::State st = devmap::classify(snap);
    if (st != lastState_) {
        lastState_ = st;
        stateSinceMs_ = now;
    }
    readDwLog(phase_ == devmap::Phase::Start);
''',
 r'''void DevmapRunner::readLaunchLog() {
    if (!launched_ || launchLogOffset_ < 0) return;
    const qint64 now = nowMs();
    if (lastLaunchLogMs_ >= 0 && now - lastLaunchLogMs_ < kLaunchLogReadEveryMs) return;
    lastLaunchLogMs_ = now;
    const QString path = dwLogPath(cfg_.gameRoot);
    const QFileInfo log(path);
    if (log.exists() && log.size() < launchLogOffset_) launchLogOffset_ = 0;  // the log was truncated or rotated
    launchLog_ = devmap::parseDwLaunchLog(readTail(path, launchLogOffset_, kLaunchLogMaxBytes));
    if (launchLog_.signedIn && !saidSignedIn_) {
        saidSignedIn_ = true;
        emit status(stamp() + "the client signed in to the local host", false);
    }
    const QString line = QString::fromStdString(launchLog_.clientLine);
    if (!line.isEmpty() && line != lastClientLine_) {
        lastClientLine_ = line;
        emit status(stamp() + "client: " + line, launchLog_.client < 0);
        if (launchLog_.client < 0)
            emit status(stamp() + "the client's own start did not work; the editor does the menu steps instead", false);
    }
}

void DevmapRunner::tick() {
    if (!busy()) return timer_->stop();
    const qint64 now = nowMs();
    const bool scan = lastPidScanMs_ < 0 || now - lastPidScanMs_ >= kPidScanEveryMs;
    if (scan) {
        lastPidScanMs_ = now;
        pids_ = CompilePanel::gamePidsFrom(cfg_.gameRoot);
    }
    const QList<quint32>& pids = pids_;
    if (pids.size() == 1 && (!link_.attached() || link_.pid() != pids.front())) {
        std::string err;
        if (scan && !link_.attach(pids.front(), &err)) link_.detach();  // early boot: modules not listed yet; try again
    } else if (pids.size() != 1) {
        link_.detach();
    }
    const devmap::GameSnapshot snap = snapshot(link_);
    const devmap::State st = devmap::classify(snap);
    if (st != lastState_) {
        lastState_ = st;
        stateSinceMs_ = now;
        // The timeline of a launch: every change of the game's state with its time.
        if (launched_ && st != devmap::State::Unknown) emit status(stamp() + "game state: " + devmap::stateName(st), false);
    }
    readDwLog(phase_ == devmap::Phase::Start);
    readLaunchLog();
'''),
(r'''    c.selectMpSent = selectMpSent_;
''',
 r'''    c.selectMpSent = selectMpSent_;
    c.hostLobbySent = hostLobbySent_;
    // A game the editor did not start has been up for a while: its sign-in is not waited for.
    c.signedIn = launched_ ? launchLog_.signedIn : true;
    c.clientRequest = launchLog_.client;
    c.clientMessage = launchLog_.clientLine;
'''),
(r'''        emit status("F5: " + text, false);
    };''', r'''        emit status(stamp() + text, false);
    };'''),
(r'''            if (!ensureDeployed(&err)) return finish(false, "F5: deploy failed: " + err);
            if (!launch(&err)) return finish(false, "F5: " + err);
            return enter(d.next);''',
 r'''            if (!ensureDeployed(&err)) return finish(false, "F5: deploy failed: " + err);
            switch (hostReady(&err)) {
                case 0: return;  // the host is starting: this phase is asked again on the next tick
                case -1: return finish(false, "F5: " + err);
                default: break;
            }
            if (!launch(&err)) return finish(false, "F5: " + err);
            return enter(d.next);'''),
(r'''            if (!send(devmap::hostLobbyCommands(), &err)) return;
            return enter(d.next);''', r'''            if (!send(devmap::hostLobbyCommands(), &err)) return;
            ++hostLobbySent_;
            return enter(d.next);'''),
(r'''        case devmap::Action::Succeed:
            return finish(true, "F5: " + msg);''', r'''        case devmap::Action::Succeed:
            return finish(true, stamp() + msg);'''),
])

edit('src/qtapp/main_window.cpp', [
(r'''            cfg.launchExe = toolPath("launch").isEmpty() ? toolPath("replay") : toolPath("launch");
            cfg.launchArgs = DevmapRunner::launchArgsSetting();
            cfg.gametype = DevmapRunner::gametypeSetting();
            cfg.reload = DevmapRunner::reloadSetting();
            runner->start(cfg,''',
 r'''            QString exeNote;
            cfg.launchExe = DevmapRunner::chooseLaunchExe(toolPath("launch"), toolPath("replay"), &exeNote);
            DevmapRunner::applySettings(cfg);
            if (!exeNote.isEmpty() && !CompilePanel::gameRunningFrom(gameRoot)) compile_->appendLog("F5: " + exeNote, exeNote.startsWith("warning"));
            runner->start(cfg,'''),
(r'''        const QString exe = toolPath("launch").isEmpty() ? toolPath("replay") : toolPath("launch");
        const QString dir = QFileInfo(exe).absolutePath();
        if (!QFileInfo::exists(exe)) return ctx_.setStatus("No game exe at " + exe + " (Tools > Tool Paths)");''',
 r'''        const QString exe = DevmapRunner::chooseLaunchExe(toolPath("launch"), toolPath("replay"));
        const QString dir = QFileInfo(exe).absolutePath();
        if (!QFileInfo::exists(exe)) return ctx_.setStatus("No game exe at " + exe + " (Tools > Tool Paths)");
        if (DevmapRunner::hostCheckSetting() && !DevmapRunner::localHostOf(exe).isEmpty() &&
            !DevmapRunner::probeHost(DevmapRunner::hostHealthSetting()))
            return ctx_.setStatus("The local online-services host does not answer (" + DevmapRunner::hostHealthSetting() + "); " +
                                  QFileInfo(exe).fileName() + " needs it. Start Host-Local-Server.ps1 -NoLaunch, or press F5 (it starts the host)");'''),
])
print('ok')
