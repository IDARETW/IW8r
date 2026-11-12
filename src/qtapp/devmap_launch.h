// IW8 Radiant Suite - F5 "devmap": after a successful compile, put the owner in game on the map with no menu steps
// (docs/research/65_devmap_one_key.md). Drives the pure plan in src/core/devmap_plan.* :
//   game not running  -> deploy, make sure the local online-services host answers, start the exe (the local DW
//                        client when Tool Paths "launch" is empty), select Multiplayer once the client has signed in,
//                        host a private match (xstartprivatematch), set ui_mapname / ui_gametype, "xpartygo 1" (no
//                        countdown). A client with the start-request feature ("--devmap <id>", DW_DEVMAP log lines)
//                        does all of that itself and the editor only watches (docs/research/76, 77).
//   in a lobby we host -> deploy, set the map, start (xpartygo, or sv_systemlinkStartMatch in Local Play)
//   in a match we host -> same map on the DW client: deploy, map_reload; otherwise xpartybackout, wait for the
//                        lobby (the level zones unload), deploy, start
// The game is read and commanded through the direct link (live_direct: native command buffer + memory reads).
#pragma once

#include <vector>
#include <utility>
#include <string>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QString>

#include <functional>

#include "devmap_plan.h"
#include "live_direct.h"

class QTimer;

namespace iw8r::qt {

class DevmapCover;

class DevmapRunner : public QObject {
    Q_OBJECT
public:
    struct Config {
        QString mapId;
        QString gameRoot;     // folder of the Replay exe (deploy target, process filter, replay-dw-client.log)
        QString launchExe;    // what to start when the game is not running (.exe, or .cmd / .bat / .ps1)
        QString launchArgs;   // arguments for an .exe launch (devmap::kDefaultLaunchArgs)
        QString gametype = "war";
        bool reload = true;   // use map_reload (DW) for a rebuild of the running map
        bool hostCheck = true;       // an exe whose ini names a loopback host: the host must answer before the launch
        QString hostCommand;         // what starts the host when it does not answer (empty: stop with a message)
        QString hostHealthUrl;       // http://127.0.0.1:8085/health
        bool startRequest = true;    // pass "--devmap <id> --gametype <g>" to an exe that has the client feature
        int bots = 0;                // "--bots <n>" of the start request
        bool cover = false;          // a launch by the editor: the cover card hides the game until the map loads
    };
    using DeployFn = std::function<bool(QString* err)>;

    explicit DevmapRunner(QObject* parent = nullptr);
    ~DevmapRunner() override;

    bool busy() const { return phase_ != devmap::Phase::Done && phase_ != devmap::Phase::Failed; }
    void start(const Config& cfg, DeployFn deploy);
    void cancel(const QString& why = QString());

    // Reads the game state from an attached link (also used by tests and the Live Link panel).
    static devmap::GameSnapshot snapshot(const live::DirectLink& link);
    // Settings-backed configuration pieces (devmap/launchArgs, devmap/gametype, devmap/reload).
    static QString launchArgsSetting();
    static QString gametypeSetting();
    // The dvars of the game mode preset chosen for F5 (setting devmap/modeDvars, "name=value; ..."; game_modes.h).
    // They are sent as console lines before the map and game type when the editor starts the match itself.
    static std::vector<std::pair<std::string, std::string>> modeDvarsSetting();
    static void setModePreset(const QString& id, const QString& gametype, const QString& dvarsText);
    static QString modePresetSetting();
    static bool reloadSetting();
    static bool hostCheckSetting();        // devmap/hostCheck (true)
    static QString hostCommandSetting();   // devmap/hostCommand (Host-Local-Server.ps1 -NoLaunch when that file exists)
    static QString hostHealthSetting();    // devmap/hostHealth
    static bool startRequestSetting();     // devmap/startRequest (true)
    static int botsSetting();              // devmap/bots (0)
    static bool coverSetting();            // devmap/cover (true)
    // Fills the settings-backed fields of a Config (everything but mapId, gameRoot and launchExe).
    static void applySettings(Config& cfg);
    // The F5 options a mapper sets in Edit > Preferences (the same devmap/* settings).
    struct PlayOptions {
        QString gametype = "war";
        int bots = 0;
        bool cover = true;         // the card over the game window until the map loads
        bool hostCheck = true;     // check, and start, the local online-services host
        QString hostCommand;       // empty = the default (hostCommandSetting); "-" = never start one
        bool startRequest = true;  // ask a client that can to start the map by itself
    };
    static PlayOptions playOptions();
    static void setPlayOptions(const PlayOptions& o);  // cleans the values (game type name, bot range)
    // The exe F5 and Launch Replay start. Tool Paths "launch" when set. Otherwise the best exe in the game folder
    // (devmap::pickLaunchExe: the local DW client first); `note` says what was picked, and warns for a stock exe.
    static QString chooseLaunchExe(const QString& launchSetting, const QString& replayExe, QString* note = nullptr);
    // True when the exe file contains the client start-request marker (devmap::kClientRequestMarker). Cached.
    static bool exeHasClientRequest(const QString& exe);
    // The loopback host named by "<exe stem>.ini" beside the exe ("host=127.0.0.1"), or empty.
    static QString localHostOf(const QString& exe);
    // One blocking GET of the health address (loopback, at most about a second). True when a DW host answered.
    static bool probeHost(const QString& healthUrl);

signals:
    void status(const QString& text, bool error);
    void finished(bool ok, const QString& text);

private:
    void tick();
    void finish(bool ok, const QString& text);
    bool ensureDeployed(QString* err);
    bool send(const std::vector<std::string>& commands, QString* err);
    bool launch(QString* err);
    void readDwLog(bool force);
    void readLaunchLog();
    void enter(devmap::Phase p);
    // 1 ready (or not needed), 0 still waiting for the host, -1 stop with *err.
    int hostReady(QString* err);
    void updateCover(const devmap::GameSnapshot& snap);
    void dropCover();
    void note(const QString& text, bool error = false);  // a stamped status line, also shown on the cover card
    QString stamp() const;  // "F5 +12.3 s: " (time since F5; the measuring tool for the timeline)

    Config cfg_;
    DeployFn deploy_;
    QTimer* timer_ = nullptr;
    live::DirectLink link_;
    devmap::Phase phase_ = devmap::Phase::Done;
    qint64 phaseStartMs_ = 0, stateSinceMs_ = 0, lastDwLogReadMs_ = -1;
    devmap::State lastState_ = devmap::State::Unknown;
    bool deployed_ = false;
    bool dwBuild_ = false;
    std::vector<std::string> dwInstalled_;
    qint64 reloadLogOffset_ = -1;
    int selectMpSent_ = 0, hostLobbySent_ = 0;
    QString lastStatus_, lastClientLine_;
    qint64 runStartMs_ = 0, lastPidScanMs_ = -1, launchLogOffset_ = -1, lastLaunchLogMs_ = -1;
    qint64 hostStartedMs_ = -1, lastHostProbeMs_ = -1;
    bool hostOk_ = false, launched_ = false, saidSignedIn_ = false, sentRequest_ = false;
    QList<quint32> pids_;
    devmap::LaunchLog launchLog_;
    QPointer<DevmapCover> cover_;
    QStringList coverLines_;
    qint64 startRequestMs_ = -1, lastProgressMs_ = 0;
    bool coverDone_ = false;
};

}  // namespace iw8r::qt
