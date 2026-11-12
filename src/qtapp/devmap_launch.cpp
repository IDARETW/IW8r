#include "devmap_launch.h"
#include "game_modes.h"

#include "compile_panel.h"
#include "devmap_cover.h"
#include "settings.h"

#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QProcess>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>

#include "setup_detect.h"

namespace iw8r::qt {

namespace {

// The memory reads are a few ReadProcessMemory calls, so the state is polled fast; the process list and the client
// log are read less often.
constexpr int kTickMs = 100;
constexpr qint64 kPidScanEveryMs = 500;
constexpr qint64 kLaunchLogReadEveryMs = 250;
constexpr qint64 kLaunchLogMaxBytes = 2 * 1024 * 1024;
constexpr qint64 kHostProbeEveryMs = 500;
constexpr qint64 kHostStartTimeoutMs = 60000;
constexpr qint64 kDwLogReadEveryMs = 5000;
constexpr qint64 kDwLogTailBytes = 4 * 1024 * 1024;

qint64 nowMs() { return QDateTime::currentMSecsSinceEpoch(); }

QString dwLogPath(const QString& gameRoot) { return QDir(gameRoot).filePath("replay-dw-client.log"); }

// Reads the last `maxBytes` of a file that another process appends to (shared read).
std::string readTail(const QString& path, qint64 from, qint64 maxBytes) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    const qint64 size = f.size();
    qint64 start = from >= 0 ? from : size - maxBytes;
    if (start < 0) start = 0;
    if (start > size) start = size;  // the log was truncated or rotated
    if (size - start > maxBytes) start = size - maxBytes;
    f.seek(start);
    const QByteArray b = f.read(size - start);
    return std::string(b.constData(), size_t(b.size()));
}

}  // namespace

DevmapRunner::DevmapRunner(QObject* parent) : QObject(parent) {
    setObjectName("devmapRunner");
    timer_ = new QTimer(this);
    timer_->setInterval(kTickMs);
    connect(timer_, &QTimer::timeout, this, &DevmapRunner::tick);
}

DevmapRunner::~DevmapRunner() { dropCover(); }

bool DevmapRunner::coverSetting() { return Settings::instance().value("devmap/cover", true).toBool(); }

void DevmapRunner::note(const QString& text, bool error) {
    const QString line = stamp() + text;
    lastProgressMs_ = nowMs();
    coverLines_ << line.mid(3);  // without the "F5 " prefix
    while (coverLines_.size() > DevmapCover::kMaxLines) coverLines_.removeFirst();
    if (cover_) cover_->addLine(line.mid(3));
    emit status(line, error);
}

void DevmapRunner::dropCover() {
    if (!cover_) return;
    cover_->hide();
    cover_->deleteLater();
    cover_ = nullptr;
}

void DevmapRunner::updateCover(const devmap::GameSnapshot& snap) {
    if (!cfg_.cover || !launched_ || coverDone_) return;
    const qint64 now = nowMs();
    if (!devmap::coverWanted(true, startRequestMs_ < 0 ? -1 : now - startRequestMs_, snap)) {
        coverDone_ = true;  // the map is loading: the card never comes back in this run
        return dropCover();
    }
    if (now - lastProgressMs_ > devmap::kCoverStallMs) {
        coverDone_ = true;
        dropCover();
        return note("nothing new for 150 s: the cover card is removed so you can see the game");
    }
    // Over the game window; before the game has one, where its window was the last time.
    QRect rect = pids_.size() == 1 ? DevmapCover::gameWindowRect(pids_.front()) : QRect();
    if (!rect.isNull()) {
        if (rect != Settings::instance().value("devmap/lastGameRect").toRect()) Settings::instance().setValue("devmap/lastGameRect", rect);
    } else {
        rect = Settings::instance().value("devmap/lastGameRect").toRect();
    }
    if (rect.isNull()) return;
    if (!cover_) {
        cover_ = new DevmapCover;
        cover_->setMap(cfg_.mapId, cfg_.gametype);
        for (const QString& l : coverLines_) cover_->addLine(l);
    }
    cover_->setElapsed(double(now - runStartMs_) / 1000.0);
    cover_->coverRect(rect);
}

QString DevmapRunner::launchArgsSetting() {
    return Settings::instance().value("devmap/launchArgs", QString::fromLatin1(devmap::kDefaultLaunchArgs)).toString();
}
QString DevmapRunner::gametypeSetting() {
    const QString g = Settings::instance().value("devmap/gametype", "war").toString().trimmed();
    return g.isEmpty() ? QStringLiteral("war") : g;
}
std::vector<std::pair<std::string, std::string>> DevmapRunner::modeDvarsSetting() {
    return parseModeDvars(Settings::instance().value("devmap/modeDvars").toString().toStdString());
}
QString DevmapRunner::modePresetSetting() { return Settings::instance().value("devmap/modePreset").toString(); }
void DevmapRunner::setModePreset(const QString& id, const QString& gametype, const QString& dvarsText) {
    Settings& s = Settings::instance();
    s.setValue("devmap/modePreset", id);
    s.setValue("devmap/modeDvars", QString::fromStdString(modeDvarsText(parseModeDvars(dvarsText.toStdString()))));
    PlayOptions o = playOptions();
    o.gametype = gametype;
    setPlayOptions(o);
}
bool DevmapRunner::reloadSetting() { return Settings::instance().value("devmap/reload", true).toBool(); }
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

DevmapRunner::PlayOptions DevmapRunner::playOptions() {
    PlayOptions o;
    o.gametype = gametypeSetting();
    o.bots = botsSetting();
    o.cover = coverSetting();
    o.hostCheck = hostCheckSetting();
    o.hostCommand = Settings::instance().value("devmap/hostCommand").toString().trimmed();
    o.startRequest = startRequestSetting();
    return o;
}

void DevmapRunner::setPlayOptions(const PlayOptions& o) {
    // A game type is a plain lower-case name (ui_gametype); anything else falls back to Team Deathmatch.
    QString g = o.gametype.trimmed().toLower();
    bool plain = !g.isEmpty() && g.size() <= 11;
    for (const QChar c : g) plain = plain && ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_');
    Settings& s = Settings::instance();
    s.setValue("devmap/gametype", plain ? g : QStringLiteral("war"));
    s.setValue("devmap/bots", qBound(0, o.bots, 199));
    s.setValue("devmap/cover", o.cover);
    s.setValue("devmap/hostCheck", o.hostCheck);
    s.setValue("devmap/hostCommand", o.hostCommand.trimmed());
    s.setValue("devmap/startRequest", o.startRequest);
}

void DevmapRunner::applySettings(Config& cfg) {
    cfg.launchArgs = launchArgsSetting();
    cfg.gametype = gametypeSetting();
    cfg.reload = reloadSetting();
    cfg.hostCheck = hostCheckSetting();
    cfg.hostCommand = hostCommandSetting();
    cfg.hostHealthUrl = hostHealthSetting();
    // A mode with dvars is started by the editor (it sends them as console lines); the client's own start request
    // names only the map and the game type.
    cfg.startRequest = startRequestSetting() && modeDvarsSetting().empty();
    cfg.bots = botsSetting();
    cfg.cover = coverSetting();
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
    // HTTP/1.1 with a Host header: the real host answers an HTTP/1.0 request with "400 invalid_request" (owner
    // report 2026-11-11: F5 never started the game, because the host that was running was never recognised).
    sock.write("GET " + path + " HTTP/1.1\r\nHost: " + url.host().toLatin1() + ":" + QByteArray::number(url.port(80)) +
               "\r\nUser-Agent: IW8Radiant\r\nAccept: */*\r\nConnection: close\r\n\r\n");
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
        note("the local online-services host answers (" + cfg_.hostHealthUrl + ")");
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
        note("the local online-services host does not answer: starting it (" + cfg_.hostCommand + ")");
        return 0;
    }
    if (now - hostStartedMs_ > kHostStartTimeoutMs) {
        if (err) *err = "the local online-services host did not answer at " + cfg_.hostHealthUrl + " within 60 s of starting it (" + cfg_.hostCommand + ")";
        return -1;
    }
    return 0;
}

devmap::GameSnapshot DevmapRunner::snapshot(const live::DirectLink& link) {
    devmap::GameSnapshot s;
    if (!link.attached()) return s;
    const auto mode = link.readU8(devmap::kRvaActiveGameMode);
    const auto fe = link.readI32(devmap::kRvaFrontEndState);
    if (!mode || !fe) return s;
    s.valid = true;
    s.gameMode = *mode;
    s.frontEndState = *fe;
    s.inParty = link.readI32(devmap::kRvaPartyInParty).value_or(0) != 0;
    s.partyHost = link.readI32(devmap::kRvaPartyAreWeHost).value_or(0) != 0;
    s.serverRunning = link.readDvarBool(devmap::kRvaDvarServerRunning).value_or(false);
    s.inMatch = link.inMatch();
    s.systemlink = link.readDvarBool(devmap::kRvaDvarSystemlink).value_or(false);
    s.systemlinkHost = link.readDvarBool(devmap::kRvaDvarSystemlinkHost).value_or(false);
    s.privateMatch = link.readDvarBool(devmap::kRvaDvarPrivateMatch).value_or(false);
    s.uiMapname = link.readDvarString(devmap::kRvaDvarUiMapname).value_or(std::string());
    s.uiGametype = link.readDvarString(devmap::kRvaDvarUiGametype).value_or(std::string());
    s.commandBufferIdle = link.commandBufferIdle();
    return s;
}

void DevmapRunner::start(const Config& cfg, DeployFn deploy) {
    if (busy()) cancel("replaced by a new F5");
    cfg_ = cfg;
    deploy_ = std::move(deploy);
    deployed_ = false;
    dwBuild_ = false;
    dwInstalled_.clear();
    reloadLogOffset_ = -1;
    selectMpSent_ = hostLobbySent_ = 0;
    lastDwLogReadMs_ = -1;
    lastStatus_.clear();
    lastClientLine_.clear();
    runStartMs_ = nowMs();
    lastPidScanMs_ = lastLaunchLogMs_ = launchLogOffset_ = -1;
    hostStartedMs_ = lastHostProbeMs_ = -1;
    hostOk_ = launched_ = saidSignedIn_ = sentRequest_ = false;
    pids_.clear();
    launchLog_ = {};
    dropCover();
    coverLines_.clear();
    startRequestMs_ = -1;
    lastProgressMs_ = nowMs();
    coverDone_ = false;
    link_.detach();
    lastState_ = devmap::State::Unknown;
    stateSinceMs_ = nowMs();
    enter(devmap::Phase::Start);
    if (cfg_.mapId.isEmpty() || cfg_.gameRoot.isEmpty() || !QFileInfo(cfg_.gameRoot).isDir())
        return finish(false, "F5: no map id or no game folder (Tools > Tool Paths: Replay exe)");
    timer_->start();
    tick();
}

void DevmapRunner::cancel(const QString& why) {
    if (!busy()) return;
    timer_->stop();
    phase_ = devmap::Phase::Failed;
    link_.detach();
    dropCover();
    emit finished(false, "F5 stopped" + (why.isEmpty() ? QString() : ": " + why));
}

void DevmapRunner::enter(devmap::Phase p) {
    phase_ = p;
    phaseStartMs_ = nowMs();
}

void DevmapRunner::finish(bool ok, const QString& text) {
    timer_->stop();
    phase_ = ok ? devmap::Phase::Done : devmap::Phase::Failed;
    link_.detach();
    dropCover();
    emit status(text, !ok);
    emit finished(ok, text);
}

bool DevmapRunner::ensureDeployed(QString* err) {
    if (deployed_) return true;
    if (!deploy_) {
        if (err) *err = "no deploy step";
        return false;
    }
    deployed_ = deploy_(err);
    return deployed_;
}

bool DevmapRunner::send(const std::vector<std::string>& commands, QString* err) {
    std::string text, why;
    if (!devmap::joinCommands(commands, text, &why)) {
        if (err) *err = QString::fromStdString(why);
        return false;
    }
    if (!link_.sendCommand(text, &why)) {
        if (err) *err = QString::fromStdString(why);
        return false;
    }
    return true;
}

bool DevmapRunner::launch(QString* err) {
    if (cfg_.launchExe.isEmpty() || !QFileInfo::exists(cfg_.launchExe)) {
        if (err) *err = "no game exe at " + cfg_.launchExe + " (Tools > Tool Paths: launch / Replay exe)";
        return false;
    }
    // Absolute: the process starts in the exe's folder, where a relative path would no longer resolve.
    const QString exe = QFileInfo(cfg_.launchExe).absoluteFilePath();
    const QString dir = QFileInfo(exe).absolutePath();
    const QString suffix = QFileInfo(exe).suffix().toLower();
    QString program = exe;
    QStringList args;
    if (suffix == "cmd" || suffix == "bat") {
        // Scripts get no game arguments: a launcher script may pass them to a tool that refuses unknown parameters.
        program = "cmd.exe";
        args << "/c" << QDir::toNativeSeparators(exe);
    } else if (suffix == "ps1") {
        program = "powershell.exe";
        args << "-NoProfile" << "-ExecutionPolicy" << "Bypass" << "-File" << QDir::toNativeSeparators(exe);
    } else {
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
    note(QString("started %1%2; %3")
             .arg(QFileInfo(exe).fileName(), args.isEmpty() || program != exe ? QString() : " " + args.join(' '),
                  sentRequest_ ? "the client was asked to boot straight into the map" : "waiting for the Multiplayer menu"));
    return true;
}

void DevmapRunner::readDwLog(bool force) {
    const qint64 now = nowMs();
    if (!force && lastDwLogReadMs_ >= 0 && now - lastDwLogReadMs_ < kDwLogReadEveryMs) return;
    lastDwLogReadMs_ = now;
    bool found = false;
    const auto ids = devmap::parseDwInstalledMaps(readTail(dwLogPath(cfg_.gameRoot), -1, kDwLogTailBytes), &found);
    if (found) {
        dwBuild_ = true;
        dwInstalled_ = ids;
    }
}

void DevmapRunner::readLaunchLog() {
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
        note("the client signed in to the local host");
    }
    const QString line = QString::fromStdString(launchLog_.clientLine);
    if (!line.isEmpty() && line != lastClientLine_) {
        lastClientLine_ = line;
        note("client: " + line, launchLog_.client < 0);
        if (launchLog_.client < 0) note("the client's own start did not work; the editor does the menu steps instead");
        if (launchLog_.client > 0 && startRequestMs_ < 0 && devmap::clientLineIsStart(launchLog_.clientLine)) startRequestMs_ = now;
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
        if (launched_ && st != devmap::State::Unknown) note(QString("game state: ") + devmap::stateName(st));
    }
    readDwLog(phase_ == devmap::Phase::Start);
    readLaunchLog();
    updateCover(snap);

    devmap::Context c;
    c.mapId = cfg_.mapId.toStdString();
    c.gametype = cfg_.gametype.toStdString();
    c.processCount = int(pids.size());
    c.dwBuild = dwBuild_;
    c.dwInstalled = dwInstalled_;
    c.reloadAllowed = cfg_.reload;
    c.elapsedMs = now - phaseStartMs_;
    c.stableMs = now - stateSinceMs_;
    c.selectMpSent = selectMpSent_;
    c.hostLobbySent = hostLobbySent_;
    // A game the editor did not start has been up for a while: its sign-in is not waited for.
    c.signedIn = launched_ ? launchLog_.signedIn : true;
    c.clientRequest = launchLog_.client;
    c.clientMessage = launchLog_.clientLine;
    if (phase_ == devmap::Phase::WaitReload && reloadLogOffset_ >= 0)
        c.dwReload = devmap::parseDwReloadResult(readTail(dwLogPath(cfg_.gameRoot), reloadLogOffset_, kDwLogTailBytes), c.mapId);

    const devmap::Decision d = devmap::decide(phase_, snap, c);
    const QString msg = QString::fromStdString(d.message);
    QString err;
    auto say = [&](const QString& text) {
        if (text.isEmpty() || text == lastStatus_) return;
        lastStatus_ = text;
        note(text);
    };
    switch (d.action) {
        case devmap::Action::None:
            return;
        case devmap::Action::Launch:
            say(msg);
            if (!ensureDeployed(&err)) return finish(false, "F5: deploy failed: " + err);
            switch (hostReady(&err)) {
                case 0: return;  // the host is starting: this phase is asked again on the next tick
                case -1: return finish(false, "F5: " + err);
                default: break;
            }
            if (!launch(&err)) return finish(false, "F5: " + err);
            return enter(d.next);
        case devmap::Action::Deploy:
            say(msg);
            if (!ensureDeployed(&err)) return finish(false, "F5: deploy failed: " + err);
            if (d.next == devmap::Phase::Failed) return finish(false, "F5: " + msg);
            return enter(d.next);
        case devmap::Action::DeployThenReload:
            say(msg);
            if (!ensureDeployed(&err)) {
                // The running map holds its zones: end the match so they unload, then deploy from the lobby.
                emit status("F5: the running map's files are in use (" + err + "); ending the match first", false);
                if (!send({devmap::backToLobbyCommand()}, &err)) return;  // command buffer busy: retry next tick
                return enter(devmap::Phase::WaitFrontEnd);
            }
            if (!send({devmap::reloadCommand()}, &err)) return;  // busy: retry (already deployed)
            {
                QFileInfo log(dwLogPath(cfg_.gameRoot));
                reloadLogOffset_ = log.exists() ? log.size() : 0;
            }
            return enter(d.next);
        case devmap::Action::DeployThenStart:
            say(msg);
            if (!ensureDeployed(&err))
                return finish(false, "F5: deploy failed: " + err + ". Leave the match in the game, then press F5 again.");
            [[fallthrough]];
        case devmap::Action::StartMatch:
            say(msg);
            if (!snap.commandBufferIdle) return;
            if (!send(devmap::startMatchCommands(c.mapId, c.gametype, snap, modeDvarsSetting()), &err)) return;
            if (startRequestMs_ < 0) startRequestMs_ = now;
            return enter(d.next);
        case devmap::Action::SelectMultiplayer:
            say(msg);
            if (!send({devmap::selectMultiplayerCommand()}, &err)) return;
            ++selectMpSent_;
            return enter(d.next);
        case devmap::Action::HostLobby:
            say(msg);
            if (!send(devmap::hostLobbyCommands(), &err)) return;
            ++hostLobbySent_;
            return enter(d.next);
        case devmap::Action::BackToLobby:
            say(msg);
            if (!send({devmap::backToLobbyCommand()}, &err)) return;
            return enter(d.next);
        case devmap::Action::Succeed:
            return finish(true, stamp() + msg);
        case devmap::Action::Fail:
            return finish(false, "F5: " + msg);
    }
}

}  // namespace iw8r::qt
