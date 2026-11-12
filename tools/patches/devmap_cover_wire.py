import io
def edit(p, pairs):
    s = io.open(p, encoding='utf-8', newline='').read()
    crlf = '\r\n' in s
    s = s.replace('\r\n', '\n')
    for a, b in pairs:
        assert s.count(a) == 1, (p, s.count(a), a[:70])
        s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s.replace('\n', '\r\n') if crlf else s)

edit('src/core/devmap_plan.h', [
(r'''Decision decide(Phase phase, const GameSnapshot& s, const Context& c);''',
 r'''Decision decide(Phase phase, const GameSnapshot& s, const Context& c);

// The cover card of a cold start (src/qtapp/devmap_cover.*): shown over the game window from the launch until the map
// begins to load. `sinceStartRequestMs` is the time since the start request went out (the editor's start commands, or
// the client's own "DW_DEVMAP start ..." line), -1 before it. After the request the card stays only while the front
// end is still on screen, and never longer than kCoverMaxAfterStartMs.
inline constexpr int64_t kCoverMaxAfterStartMs = 4000;
bool coverWanted(bool launchedByEditor, int64_t sinceStartRequestMs, const GameSnapshot& s);
// True for a client line that says the map start went out ("start requested ...", "loading ...").
bool clientLineIsStart(const std::string& clientLine);'''),
])
edit('src/core/devmap_plan.cpp', [
(r'''Decision decide(Phase phase, const GameSnapshot& s, const Context& c) {
    const State st = classify(s);''',
 r'''bool coverWanted(bool launchedByEditor, int64_t sinceStartRequestMs, const GameSnapshot& s) {
    if (!launchedByEditor) return false;  // a game that was already on screen is not hidden
    if (s.valid && s.inMatch) return false;
    if (sinceStartRequestMs < 0) return true;
    return s.valid && s.frontEnd() && sinceStartRequestMs < kCoverMaxAfterStartMs;
}

bool clientLineIsStart(const std::string& line) { return line.compare(0, 5, "start") == 0 || line.compare(0, 7, "loading") == 0; }

Decision decide(Phase phase, const GameSnapshot& s, const Context& c) {
    const State st = classify(s);'''),
])

edit('src/qtapp/devmap_launch.h', [
(r'''        int bots = 0;                // "--bots <n>" of the start request
    };''',
 r'''        int bots = 0;                // "--bots <n>" of the start request
        bool cover = false;          // a launch by the editor: the cover card hides the game until the map loads
    };'''),
(r'''    static int botsSetting();              // devmap/bots (0)''',
 r'''    static int botsSetting();              // devmap/bots (0)
    static bool coverSetting();            // devmap/cover (true)'''),
(r'''    int hostReady(QString* err);''',
 r'''    int hostReady(QString* err);
    void updateCover(const devmap::GameSnapshot& snap);
    void dropCover();
    void note(const QString& text, bool error = false);  // a stamped status line, also shown on the cover card'''),
(r'''    QList<quint32> pids_;
    devmap::LaunchLog launchLog_;''',
 r'''    QList<quint32> pids_;
    devmap::LaunchLog launchLog_;
    QPointer<DevmapCover> cover_;
    QStringList coverLines_;
    qint64 startRequestMs_ = -1;
    bool coverDone_ = false;'''),
(r'''#include <QList>
#include <QObject>''', r'''#include <QList>
#include <QObject>
#include <QPointer>
#include <QStringList>'''),
(r'''class QTimer;

namespace iw8r::qt {
''', r'''class QTimer;

namespace iw8r::qt {

class DevmapCover;
'''),
])

edit('src/qtapp/devmap_launch.cpp', [
(r'''#include "compile_panel.h"
#include "settings.h"''', r'''#include "compile_panel.h"
#include "devmap_cover.h"
#include "settings.h"'''),
(r'''DevmapRunner::~DevmapRunner() = default;''', r'''DevmapRunner::~DevmapRunner() { dropCover(); }

bool DevmapRunner::coverSetting() { return Settings::instance().value("devmap/cover", true).toBool(); }

void DevmapRunner::note(const QString& text, bool error) {
    const QString line = stamp() + text;
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
}'''),
(r'''    cfg.bots = botsSetting();
}''', r'''    cfg.bots = botsSetting();
    cfg.cover = coverSetting();
}'''),
(r'''    pids_.clear();
    launchLog_ = {};''', r'''    pids_.clear();
    launchLog_ = {};
    dropCover();
    coverLines_.clear();
    startRequestMs_ = -1;
    coverDone_ = false;'''),
(r'''    timer_->stop();
    phase_ = devmap::Phase::Failed;
    link_.detach();
    emit finished(false, "F5 stopped" + (why.isEmpty() ? QString() : ": " + why));''',
 r'''    timer_->stop();
    phase_ = devmap::Phase::Failed;
    link_.detach();
    dropCover();
    emit finished(false, "F5 stopped" + (why.isEmpty() ? QString() : ": " + why));'''),
(r'''    timer_->stop();
    phase_ = ok ? devmap::Phase::Done : devmap::Phase::Failed;
    link_.detach();
    emit status(text, !ok);''',
 r'''    timer_->stop();
    phase_ = ok ? devmap::Phase::Done : devmap::Phase::Failed;
    link_.detach();
    dropCover();
    emit status(text, !ok);'''),
(r'''        emit status(stamp() + "the local online-services host answers (" + cfg_.hostHealthUrl + ")", false);''',
 r'''        note("the local online-services host answers (" + cfg_.hostHealthUrl + ")");'''),
(r'''        emit status(stamp() + "the local online-services host does not answer: starting it (" + cfg_.hostCommand + ")", false);''',
 r'''        note("the local online-services host does not answer: starting it (" + cfg_.hostCommand + ")");'''),
(r'''    emit status(stamp() + QString("started %1%2; %3")
                              .arg(QFileInfo(exe).fileName(), args.isEmpty() || program != exe ? QString() : " " + args.join(' '),
                                   sentRequest_ ? "the client was asked to boot straight into the map" : "waiting for the Multiplayer menu"),
                false);''',
 r'''    note(QString("started %1%2; %3")
             .arg(QFileInfo(exe).fileName(), args.isEmpty() || program != exe ? QString() : " " + args.join(' '),
                  sentRequest_ ? "the client was asked to boot straight into the map" : "waiting for the Multiplayer menu"));'''),
(r'''        emit status(stamp() + "the client signed in to the local host", false);''',
 r'''        note("the client signed in to the local host");'''),
(r'''        emit status(stamp() + "client: " + line, launchLog_.client < 0);
        if (launchLog_.client < 0)
            emit status(stamp() + "the client's own start did not work; the editor does the menu steps instead", false);''',
 r'''        note("client: " + line, launchLog_.client < 0);
        if (launchLog_.client < 0) note("the client's own start did not work; the editor does the menu steps instead");
        if (launchLog_.client > 0 && startRequestMs_ < 0 && devmap::clientLineIsStart(launchLog_.clientLine)) startRequestMs_ = now;'''),
(r'''        if (launched_ && st != devmap::State::Unknown) emit status(stamp() + "game state: " + devmap::stateName(st), false);
    }
    readDwLog(phase_ == devmap::Phase::Start);
    readLaunchLog();''',
 r'''        if (launched_ && st != devmap::State::Unknown) note(QString("game state: ") + devmap::stateName(st));
    }
    readDwLog(phase_ == devmap::Phase::Start);
    readLaunchLog();
    updateCover(snap);'''),
(r'''        lastStatus_ = text;
        emit status(stamp() + text, false);
    };''', r'''        lastStatus_ = text;
        note(text);
    };'''),
(r'''            if (!send(devmap::startMatchCommands(c.mapId, c.gametype, snap), &err)) return;
            return enter(d.next);''',
 r'''            if (!send(devmap::startMatchCommands(c.mapId, c.gametype, snap), &err)) return;
            if (startRequestMs_ < 0) startRequestMs_ = now;
            return enter(d.next);'''),
])
print('ok')
