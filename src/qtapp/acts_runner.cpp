#include "acts_runner.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTimer>

#include <algorithm>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#endif

#include "settings.h"

namespace iw8r::qt {
namespace {

// Zone XPaks with a payload are far larger than the 32 KiB headers that most zones carry (docs/research/20).
constexpr qint64 kXpakMinBytes = 64 * 1024;
// Same budget as the x64-zt stock export (replay_stock.cpp RunExport).
constexpr int kRunTimeoutMs = 15 * 60 * 1000;
constexpr int kGreetingTimeoutMs = 30 * 1000;
constexpr int kLogCap = 64 * 1024;
// An idle server gives its zones back after two minutes (the owner may be in game by then) and ends after ten.
constexpr int kTrimAfterMs = 2 * 60 * 1000;
constexpr int kQuitAfterMs = 10 * 60 * 1000;

QString zoneFileOf(const QString& replay, const QString& zone) {
    return QFileInfo(replay).absolutePath() + "/zone/" + zone + ".ff";
}

QStringList populatedXpaks(const QString& replay) {
    QStringList out;
    const QDir dir(QFileInfo(replay).absolutePath() + "/zone");
    for (const QFileInfo& f : dir.entryInfoList({"*.xpak"}, QDir::Files, QDir::Name))
        if (f.size() >= kXpakMinBytes) out << f.absoluteFilePath();
    return out;
}

// Megabytes of physical memory that are free right now (0 when unknown).
qint64 freeMemoryMb() {
#ifdef _WIN32
    MEMORYSTATUSEX status{};
    status.dwLength = sizeof(status);
    if (GlobalMemoryStatusEx(&status)) return qint64(status.ullAvailPhys / (1024 * 1024));
#endif
    return 0;
}

}  // namespace

ActsRunner* ActsRunner::shared() {
    static QPointer<ActsRunner> runner;
    if (!runner) runner = new ActsRunner(QCoreApplication::instance());
    return runner;
}

ActsRunner::ActsRunner(QObject* parent) : QObject(parent) {
    tracePath_ = qEnvironmentVariable("IW8R_ACTS_TRACE");
    idle_ = new QTimer(this);
    idle_->setInterval(15 * 1000);
    connect(idle_, &QTimer::timeout, this, &ActsRunner::idleTick);
    lastActivity_.start();
    if (QCoreApplication::instance())
        connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this, &ActsRunner::shutdown);
}

ActsRunner::~ActsRunner() { shutdown(); }

void ActsRunner::trace(const QString& line) const {
    if (tracePath_.isEmpty()) return;
    QFile f(tracePath_);
    if (f.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
        f.write((QDateTime::currentDateTime().toString("HH:mm:ss.zzz") + ' ' + line + '\n').toUtf8());
}

QString ActsRunner::describe() const {
    QStringList q, ws;
    for (const Job& job : queue_)
        q << QString("%1:%2%3").arg(job.id).arg(job.request.zone, job.oneShot ? "(one-shot)" : "");
    for (const auto& w : workers_)
        ws << QString("w%1[%2%3%4%5 job=%6 zones=%7]")
                  .arg(w->serial)
                  .arg(w->server ? "server" : "one-shot", w->ready ? " ready" : "", w->busy ? " busy" : " idle",
                       w->stale ? " stale" : "")
                  .arg(w->busy ? QString("%1:%2").arg(w->job.id).arg(w->job.request.zone) : QString("-"))
                  .arg(w->zones.join(','));
    return QString("queue(%1)=[%2] workers=%3 serve=%4")
        .arg(queue_.size())
        .arg(q.join(' '), ws.join(' '))
        .arg(serve_ == Serve::Yes ? "yes" : serve_ == Serve::No ? "no" : "unknown");
}

int ActsRunner::maxServers() const {
    return std::clamp(Settings::instance().value("acts/servers", 2).toInt(), 1, 4);
}

// Memory budget of one server for the zones it keeps (--serve-cache-mb). Automatic: the servers together may hold
// about half of what is free when a server starts, between 384 MiB and 2 GiB each. The zone in use is always kept.
int ActsRunner::cacheMb() const {
    const int fixed = Settings::instance().value("acts/cacheMb", 0).toInt();
    if (fixed > 0) return std::clamp(fixed, 64, 16384);
    const qint64 free = freeMemoryMb();
    if (free <= 0) return 1024;
    return int(std::clamp<qint64>(free / (2 * maxServers()), 384, 2048));
}

bool ActsRunner::busy() const {
    if (!queue_.empty()) return true;
    for (const auto& w : workers_)
        if (w->busy) return true;
    return false;
}

void ActsRunner::submit(const ToolPathFn& tools, const ActsRequest& request, QObject* owner, Done done) {
    Job job;
    job.id = nextId_++;
    job.request = request;
    job.acts = tools("acts");
    job.replay = tools("replay");
    job.oodle = tools("oodle");
    job.owner = owner;
    job.hadOwner = owner != nullptr;
    job.done = std::move(done);
    trace(QString("submit %1 zone=%2 types=%3 names=%4 owner=%5")
              .arg(job.id)
              .arg(job.request.zone, job.request.types)
              .arg(job.request.names.size())
              .arg(owner ? owner->metaObject()->className() : "-"));
    queue_.push_back(std::move(job));
    lastActivity_.restart();
    trimmed_ = false;
    if (!idle_->isActive()) idle_->start();
    // Dispatch from the event loop: a cache submits one request per zone in a row, and the whole set is then
    // matched to the servers that already hold those zones.
    QTimer::singleShot(0, this, &ActsRunner::pump);
}

void ActsRunner::cancel(QObject* owner) {
    if (!owner) return;
    const size_t before = queue_.size();
    std::erase_if(queue_, [owner](const Job& job) { return job.hadOwner && job.owner.data() == owner; });
    if (queue_.size() != before) trace(QString("cancel %1 queued request(s)").arg(before - queue_.size()));
}

QStringList ActsRunner::baseArgs(const Job& job, bool withXpaks) const {
    QStringList args{"--noUpdater", "fastfile", "-r", "mw19replay", "-g", job.replay, "--oodle", job.oodle};
    if (withXpaks)
        for (const QString& x : populatedXpaks(job.replay)) args << "--xpak" << x;
    return args;
}

void ActsRunner::finish(Job& job, const ActsResult& result) {
    if (result.served) {
        ++served_;
        if (result.cached) ++cached_;
    }
    lastActivity_.restart();
    trace(QString("finish %1 zone=%2 exit=%3 served=%4 cached=%5 ms=%6%7")
              .arg(job.id)
              .arg(job.request.zone)
              .arg(result.exitCode)
              .arg(result.served)
              .arg(result.cached)
              .arg(result.ms)
              .arg(result.error.isEmpty() ? QString() : " error=" + result.error.left(200)));
    if (!job.done) return;
    if (job.hadOwner && !job.owner) return;  // the cache that asked is gone
    const Done done = std::move(job.done);
    done(result);
}

// ---------------------------------------------------------------------------------------------------------------
// Dispatch

void ActsRunner::pump() {
    if (shuttingDown_ || queue_.empty()) return;

    // The verdict on --serve belongs to one acts.exe: a replaced file is asked again.
    {
        const QFileInfo exe(queue_.front().acts);
        const QString key = exe.absoluteFilePath() + '|' + QString::number(exe.size()) + '|' +
                            QString::number(exe.lastModified().toMSecsSinceEpoch());
        if (key != serveKey_) {
            // Servers of the file that was there before finish what they are doing and end: the next request
            // starts the new build (an ACTS update takes effect without restarting the editor).
            if (!serveKey_.isEmpty())
                for (const auto& w : workers_)
                    if (w->server) w->stale = true;
            serveKey_ = key;
            serve_ = Serve::Unknown;
        }
    }
    const bool wantServe = serve_ != Serve::No && Settings::instance().value("acts/serve", true).toBool();
    const QString key = keyOf(queue_.front());

    // Servers started with other tool paths (the owner changed them) end as soon as they are idle.
    for (size_t i = workers_.size(); i-- > 0;) {
        Worker* w = workers_[i].get();
        if (w->server && !w->busy && (w->key != key || !wantServe || w->stale)) retire(w, false);
    }

    const auto oneShotRunning = [this] {
        return std::any_of(workers_.begin(), workers_.end(), [](const auto& w) { return !w->server; });
    };
    const auto take = [this](size_t index) {
        Job job = std::move(queue_[index]);
        queue_.erase(queue_.begin() + qsizetype(index));
        return job;
    };

    if (!wantServe) {
        // One process per request, one at a time, as the caches ran ACTS before.
        if (!oneShotRunning()) startOneShot(take(0));
        return;
    }

    // Retries of requests whose server died run alone.
    for (size_t i = 0; i < queue_.size(); ++i) {
        if (!queue_[i].oneShot) continue;
        if (!oneShotRunning()) startOneShot(take(i));
        break;
    }

    const auto heldElsewhere = [this](const QString& zone, const Worker* except) {
        for (const auto& w : workers_) {
            if (w.get() == except || !w->server) continue;
            if (w->zones.contains(zone) || (w->busy && w->job.request.zone == zone)) return true;
        }
        return false;
    };

    // Idle servers: first a request whose zone the server holds, else the oldest one whose zone no server holds.
    for (const auto& wp : workers_) {
        Worker* w = wp.get();
        if (!w->server || !w->ready || w->busy || w->key != key) continue;
        int pick = -1;
        for (size_t i = 0; i < queue_.size() && pick < 0; ++i)
            if (!queue_[i].oneShot && keyOf(queue_[i]) == w->key && w->zones.contains(queue_[i].request.zone)) pick = int(i);
        for (size_t i = 0; i < queue_.size() && pick < 0; ++i)
            if (!queue_[i].oneShot && keyOf(queue_[i]) == w->key && !heldElsewhere(queue_[i].request.zone, w)) pick = int(i);
        if (pick >= 0) send(w, take(size_t(pick)));
    }

    // Another server when requests wait for zones that nobody holds and no server is free for them. While the
    // first server has not greeted yet, nothing is known about --serve: no second one is started.
    int servers = 0, spare = 0;
    for (const auto& w : workers_) {
        if (!w->server) continue;
        ++servers;
        if (!w->busy) ++spare;  // idle, or still starting
    }
    QStringList unheld;
    for (const Job& job : queue_)
        if (!job.oneShot && keyOf(job) == key && !heldElsewhere(job.request.zone, nullptr) && !unheld.contains(job.request.zone))
            unheld << job.request.zone;
    const bool mayStart = servers == 0 || serve_ == Serve::Yes;
    if (mayStart && servers < maxServers() && unheld.size() > spare) {
        for (const Job& job : queue_)
            if (!job.oneShot && keyOf(job) == key) {
                startServer(job);
                break;
            }
    }
    if (!queue_.empty()) trace("pump: waiting " + describe());
}

ActsRunner::Worker* ActsRunner::startServer(const Job& like) {
    auto worker = std::make_unique<Worker>();
    Worker* w = worker.get();
    w->serial = nextSerial_++;
    w->server = true;
    w->key = keyOf(like);
    w->proc = new QProcess(this);
    w->proc->setProgram(like.acts);
    QStringList args = baseArgs(like, true);
    args << "--serve" << "--serve-cache-mb" << QString::number(cacheMb()) << "-l";
    w->proc->setArguments(args);
    w->proc->setProcessChannelMode(QProcess::MergedChannels);
    w->timeout = new QTimer(w->proc);
    w->timeout->setSingleShot(true);
    connect(w->timeout, &QTimer::timeout, w->proc, [w] {
        if (w->proc && w->proc->state() != QProcess::NotRunning) w->proc->kill();
    });
    connect(w->proc, &QProcess::readyReadStandardOutput, this, [this, w] { onOutput(w); });
    connect(w->proc, &QProcess::errorOccurred, this, [this, w](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) onFinished(w, -1, true);
    });
    connect(w->proc, &QProcess::finished, this, [this, w](int code, QProcess::ExitStatus) { onFinished(w, code, false); });
    workers_.push_back(std::move(worker));
    ++starts_;
    w->timer.start();
    w->timeout->start(kGreetingTimeoutMs);
    w->proc->start();
    trace(QString("start server w%1 cache_mb=%2").arg(w->serial).arg(cacheMb()));
    return w;
}

void ActsRunner::send(Worker* w, Job job) {
    QJsonObject o;
    o.insert("id", double(job.id));
    o.insert("zone", zoneFileOf(job.replay, job.request.zone));
    o.insert("out", QDir::toNativeSeparators(job.request.outDir));
    if (!job.request.types.isEmpty()) o.insert("types", job.request.types);
    if (!job.request.names.isEmpty()) o.insert("names", QJsonArray::fromStringList(job.request.names));
    if (job.request.geometry) o.insert("geometry", true);
    w->busy = true;
    w->log.clear();
    w->timer.restart();
    w->timeout->start(kRunTimeoutMs);
    w->job = std::move(job);
    const qint64 written = w->proc->write(QJsonDocument(o).toJson(QJsonDocument::Compact) + '\n');
    trace(QString("send %1 zone=%2 to w%3 (%4 bytes written)").arg(w->job.id).arg(w->job.request.zone).arg(w->serial).arg(written));
}

void ActsRunner::startOneShot(Job job) {
    auto worker = std::make_unique<Worker>();
    Worker* w = worker.get();
    w->serial = nextSerial_++;
    w->server = false;
    w->busy = true;
    w->key = keyOf(job);
    QStringList args = baseArgs(job, job.request.xpaks);
    if (job.request.geometry) args << "--geometry";
    if (!job.request.types.isEmpty()) args << "-a" << job.request.types;
    if (!job.request.names.isEmpty()) args << "-n" << job.request.names.join(',');
    args << "-l" << "-o" << QDir::toNativeSeparators(job.request.outDir) << zoneFileOf(job.replay, job.request.zone);
    w->proc = new QProcess(this);
    w->proc->setProgram(job.acts);
    w->proc->setArguments(args);
    w->proc->setProcessChannelMode(QProcess::MergedChannels);
    w->timeout = new QTimer(w->proc);
    w->timeout->setSingleShot(true);
    connect(w->timeout, &QTimer::timeout, w->proc, [w] {
        if (w->proc && w->proc->state() != QProcess::NotRunning) w->proc->kill();
    });
    // Output is kept in memory (for error text only): a log file held open by QProcess blocks the scratch cleanup.
    connect(w->proc, &QProcess::readyReadStandardOutput, this, [w] {
        w->log += w->proc->readAllStandardOutput();
        if (w->log.size() > kLogCap) w->log.remove(0, w->log.size() - kLogCap);
    });
    connect(w->proc, &QProcess::errorOccurred, this, [this, w](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) onFinished(w, -1, true);
    });
    connect(w->proc, &QProcess::finished, this, [this, w](int code, QProcess::ExitStatus) { onFinished(w, code, false); });
    w->job = std::move(job);
    workers_.push_back(std::move(worker));
    ++oneShots_;
    w->timer.start();
    w->timeout->start(kRunTimeoutMs);
    w->proc->start();
    trace(QString("one-shot w%1 for %2 zone=%3").arg(w->serial).arg(w->job.id).arg(w->job.request.zone));
}

// ---------------------------------------------------------------------------------------------------------------
// Process events

void ActsRunner::onOutput(Worker* w) {
    w->pending += w->proc->readAllStandardOutput();
    for (;;) {
        const qsizetype eol = w->pending.indexOf('\n');
        if (eol < 0) break;
        QByteArray line = w->pending.left(eol);
        w->pending.remove(0, eol + 1);
        if (line.endsWith('\r')) line.chop(1);
        if (line.startsWith("@acts ")) {
            onAnswer(w, line.mid(6));
            // The answer runs the request's callback and the dispatcher: either may have ended this worker.
            if (std::none_of(workers_.begin(), workers_.end(), [w](const auto& p) { return p.get() == w; })) return;
            continue;
        }
        w->log += line + '\n';
        if (w->log.size() > kLogCap) w->log.remove(0, w->log.size() - kLogCap);
    }
}

void ActsRunner::onAnswer(Worker* w, const QByteArray& json) {
    const QJsonObject o = QJsonDocument::fromJson(json).object();
    if (o.value("ready").toBool()) {
        trace(QString("ready w%1 after %2 ms").arg(w->serial).arg(w->timer.elapsed()));
        w->ready = true;
        w->timeout->stop();
        if (serve_ != Serve::Yes) {
            serve_ = Serve::Yes;
            emit progress(QString("ACTS server started in %1 ms: zones stay loaded between requests").arg(w->timer.elapsed()));
        }
        pump();
        return;
    }
    if (o.contains("zones")) {
        w->zones.clear();
        for (const QJsonValue& z : o.value("zones").toArray()) w->zones << z.toString();
    }
    if (!w->busy || quint64(o.value("id").toDouble()) != w->job.id) {  // the answer to a trim
        trace(QString("w%1 answer not for its request (busy=%2 job=%3): %4").arg(w->serial).arg(w->busy).arg(w->job.id).arg(QString::fromUtf8(json.left(160))));
        return;
    }

    ActsResult r;
    r.started = true;
    r.served = true;
    r.cached = o.value("cached").toBool();
    r.ms = w->timer.elapsed();
    r.error = o.value("error").toString();
    r.log = w->log.right(4096);
    if (!r.error.isEmpty()) r.log += "\n" + r.error.toUtf8();
    const bool complete = o.value("ok").toBool();
    const int bad = o.value("failed").toInt() + o.value("unavailable").toInt();
    r.exitCode = !complete ? 1 : bad ? 127 : 0;

    w->busy = false;
    w->timeout->stop();
    Job job = std::move(w->job);
    w->job = Job{};
    finish(job, r);
    pump();
}

void ActsRunner::onFinished(Worker* w, int exitCode, bool failedToStart) {
    // FailedToStart and finished can both arrive; the worker is gone after the first.
    const auto it = std::find_if(workers_.begin(), workers_.end(), [w](const auto& p) { return p.get() == w; });
    if (it == workers_.end()) return;
    std::unique_ptr<Worker> owned = std::move(*it);
    workers_.erase(it);
    trace(QString("w%1 ended: exit=%2 failedToStart=%3 server=%4 ready=%5 busy=%6 log tail: %7")
              .arg(owned->serial)
              .arg(exitCode)
              .arg(failedToStart)
              .arg(owned->server)
              .arg(owned->ready)
              .arg(owned->busy)
              .arg(QString::fromLocal8Bit(owned->log.right(200)).simplified()));
    if (owned->proc) {
        if (!failedToStart) owned->log += owned->proc->readAllStandardOutput();
        owned->proc->disconnect(this);
        owned->proc->deleteLater();
    }

    if (owned->server) {
        if (!owned->ready && serve_ == Serve::Unknown && !shuttingDown_) {
            // No greeting: this acts.exe has no --serve (or cannot start at all; the one-shot path reports that).
            serve_ = Serve::No;
            emit progress("ACTS has no --serve mode here: one process per request (update ACTS for the fast path)");
        }
        if (owned->busy) {
            Job job = std::move(owned->job);
            if (job.attempts++ == 0 && !shuttingDown_) {
                job.oneShot = true;  // once more, in a process of its own
                queue_.push_front(std::move(job));
            } else {
                ActsResult r;
                r.started = true;
                r.exitCode = exitCode;
                r.ms = owned->timer.elapsed();
                r.error = "the ACTS server ended during the request";
                r.log = owned->log.right(4096);
                finish(job, r);
            }
        }
    } else {
        ActsResult r;
        r.started = !failedToStart;
        r.exitCode = exitCode;
        r.ms = owned->timer.elapsed();
        r.log = owned->log.right(4096);
        Job job = std::move(owned->job);
        finish(job, r);
    }
    if (!shuttingDown_) pump();
}

// Ends a server that is not in a request. kill: at once; otherwise it is asked to quit and killed if it stays.
void ActsRunner::retire(Worker* w, bool kill) {
    const auto it = std::find_if(workers_.begin(), workers_.end(), [w](const auto& p) { return p.get() == w; });
    if (it == workers_.end()) return;
    std::unique_ptr<Worker> owned = std::move(*it);
    workers_.erase(it);
    trace(QString("retire w%1 kill=%2 stale=%3").arg(owned->serial).arg(kill).arg(owned->stale));
    QProcess* proc = owned->proc;
    if (!proc) return;
    proc->disconnect(this);
    if (owned->timeout) owned->timeout->stop();
    if (proc->state() == QProcess::NotRunning) {
        proc->deleteLater();
        return;
    }
    if (kill) {
        proc->kill();
        proc->waitForFinished(2000);
        proc->deleteLater();
        return;
    }
    connect(proc, &QProcess::finished, proc, &QObject::deleteLater);
    proc->write("{\"cmd\":\"quit\"}\n");
    proc->closeWriteChannel();
    QTimer::singleShot(5000, proc, [proc] {
        if (proc->state() != QProcess::NotRunning) proc->kill();
    });
}

void ActsRunner::idleTick() {
    if (busy()) return;
    const qint64 idle = lastActivity_.elapsed();
    if (!trimmed_ && idle >= kTrimAfterMs) {
        trimmed_ = true;
        for (const auto& w : workers_) {
            if (!w->server || !w->ready) continue;
            w->proc->write("{\"cmd\":\"trim\",\"id\":0}\n");
            w->zones.clear();
        }
    }
    if (idle >= kQuitAfterMs) {
        while (!workers_.empty()) retire(workers_.back().get(), false);
        idle_->stop();
    }
}

void ActsRunner::shutdown() {
    if (shuttingDown_) return;
    shuttingDown_ = true;
    trace("shutdown " + describe());
    queue_.clear();
    // The application is closing: finished handlers of the caches must not run any more (their widgets are gone).
    for (const auto& w : workers_) {
        if (!w->proc) continue;
        w->proc->disconnect(this);
        if (w->proc->state() == QProcess::NotRunning) continue;
        if (w->server && !w->busy) {
            w->proc->write("{\"cmd\":\"quit\"}\n");
            w->proc->closeWriteChannel();
            if (w->proc->waitForFinished(1500)) continue;
        }
        w->proc->kill();
        w->proc->waitForFinished(2000);
    }
    workers_.clear();
}

}  // namespace iw8r::qt
