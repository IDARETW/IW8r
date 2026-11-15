// One queue for every ACTS export the editor asks for (material bodies and images, model tables and geometry, sky and
// browser images, stock world geometry).
//
// Each cache used to start its own acts.exe per request. A run maps the 300 MB Replay executable, reads the schema and
// then reads, decompresses, patches and loads the WHOLE zone before it exports a few assets: 1 to 10 seconds per run,
// repeated for the next request on the same zone. The runner keeps `acts fastfile -r mw19replay --serve` processes
// alive instead (atian-cod-tools docs/mw19/fastfile.md, "Serve mode"): a zone is loaded once and stays in memory, so
// a later request on it takes milliseconds. Requests on different zones run on different servers at the same time.
//
// An acts.exe without --serve (an older build) is detected at the first start; every request then runs as a one-shot
// process with the command line the caches used before. A request whose server dies is retried once that way.
//
// The export lands where a one-shot run puts it: <outDir>/mw19replay/<zone>/ (assets.jsonl, manifest.json, assets/).
//
// IW8R_ACTS_TRACE=<file>: every request, dispatch, answer and process event is appended to that file.
#pragma once

#include <QByteArray>
#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>

#include <deque>
#include <functional>
#include <memory>
#include <vector>

class QProcess;
class QTimer;

namespace iw8r::qt {

// The stock file of a zone: <game>/zone/<zone>.ff, or, where an installed edit (Install Edited Stock Map) took that
// file's place, the stock file kept in <game>/iw8r_stock. The editor reads stock data from stock zones: an edited
// zone's technique sets are renamed (iw8rf_<hash>) and its materials lost their cut-out masks (owner report
// 2026-11-14, Shipment's beech vista cards drawn as flat planes).
QString stockZoneFile(const QString& replay, const QString& zone);

struct ActsRequest {
    QString zone;           // zone name: the fastfile is <Replay folder>/zone/<zone>.ff
    QString types;          // -a, e.g. "material", "image", "xmodel", "xmodelsurfs", "gfx_map,gfx_map_trzone"
    QStringList names;      // -n; empty exports every asset of the types
    QString outDir;         // -o
    bool geometry = false;  // --geometry (GLB sidecars of xmodelsurfs)
    bool xpaks = true;      // one-shot runs only: pass the populated zone XPaks (a server always has them)
};

struct ActsResult {
    bool started = false;  // false: acts.exe could not be started at all (nothing is known about the assets)
    int exitCode = -1;     // as a one-shot run: 0 complete, 127 complete with failed or unavailable assets, else an error
    bool served = false;   // answered by a server (false: a one-shot process)
    bool cached = false;   // the server already had the zone in memory
    qint64 ms = 0;         // from dispatch to answer
    QString error;         // error text of a server answer
    QByteArray log;        // tail of the process output
};

class ActsRunner : public QObject {
    Q_OBJECT
public:
    using ToolPathFn = std::function<QString(const QString&)>;  // keys "acts", "replay", "oodle"
    using Done = std::function<void(const ActsResult&)>;

    // The runner every cache of this process shares (created on first use, on the GUI thread).
    static ActsRunner* shared();

    explicit ActsRunner(QObject* parent = nullptr);
    ~ActsRunner() override;

    // Queues an export. `done` runs once on this thread when the export has ended, unless `owner` is gone by then.
    void submit(const ToolPathFn& tools, const ActsRequest& request, QObject* owner, Done done);
    // Drops the queued requests of an owner; a request already running ends without calling back.
    void cancel(QObject* owner);
    bool busy() const;
    // Ends the server processes (also done when the application quits).
    void shutdown();

    // Counters for tests and the status line.
    int servedRequests() const { return served_; }
    int cachedRequests() const { return cached_; }
    int oneShotRuns() const { return oneShots_; }
    int serverStarts() const { return starts_; }
    bool serveSupported() const { return serve_ != Serve::No; }

signals:
    void progress(const QString& message);

private:
    struct Job {
        quint64 id = 0;
        ActsRequest request;
        QString acts, replay, oodle;
        QPointer<QObject> owner;
        bool hadOwner = false;
        Done done;
        int attempts = 0;
        bool oneShot = false;  // retry of a request whose server died
    };
    struct Worker {
        int serial = 0;        // for the trace
        QProcess* proc = nullptr;
        QString key;           // tool paths the process was started with
        bool server = false;   // --serve process (false: one-shot process of `job`)
        bool ready = false;    // server: greeting received
        bool busy = false;
        bool stale = false;    // server: acts.exe was replaced on disk since it started; it ends when idle
        Job job;
        QByteArray pending;    // partial output line
        QByteArray log;        // output of the current job (capped)
        QStringList zones;     // server: zones in memory, most recently used first
        QElapsedTimer timer;
        QTimer* timeout = nullptr;
    };
    enum class Serve { Unknown, Yes, No };

    static QString keyOf(const Job& job) { return job.acts + '|' + job.replay + '|' + job.oodle; }
    QStringList baseArgs(const Job& job, bool withXpaks) const;
    void pump();
    Worker* startServer(const Job& like);
    void startOneShot(Job job);
    void send(Worker* w, Job job);
    void onOutput(Worker* w);
    void onAnswer(Worker* w, const QByteArray& json);
    void onFinished(Worker* w, int exitCode, bool failedToStart);
    void finish(Job& job, const ActsResult& result);
    void retire(Worker* w, bool kill);
    void idleTick();
    int maxServers() const;
    int cacheMb() const;
    void trace(const QString& line) const;
    QString describe() const;  // queue and workers, for the trace

    std::deque<Job> queue_;
    std::vector<std::unique_ptr<Worker>> workers_;
    Serve serve_ = Serve::Unknown;
    QString serveKey_;       // acts.exe (path, size, time) the verdict in serve_ is for
    quint64 nextId_ = 1;
    int nextSerial_ = 1;
    QString tracePath_;
    QTimer* idle_ = nullptr;
    QElapsedTimer lastActivity_;
    bool trimmed_ = true;
    bool shuttingDown_ = false;
    int served_ = 0, cached_ = 0, oneShots_ = 0, starts_ = 0;
};

}  // namespace iw8r::qt
