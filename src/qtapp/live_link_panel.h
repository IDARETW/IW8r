// IW8 Radiant Suite (Qt shell) - Live Link dock, phase A: status, select the deployed map, allowlisted console line and
// a camera readback, all over the proxy control file (docs/research/25_live_link.md). The protocol and every check live
// in core/live_link; this widget only drives them. It never starts or stops Replay and never sends keys or clicks.
#pragma once

#include <QString>
#include <QWidget>
#include <cstdint>
#include <functional>

#include "live_link.h"
#include "live_direct.h"
#include "map_debug_dump.h"
#include "map_text.h"

class QLabel;
class QLineEdit;
class QCheckBox;
class QProcess;
class QPushButton;
class QGroupBox;
class QComboBox;
class QTimer;

namespace iw8r::qt {

class LiveLinkPanel : public QWidget {
    Q_OBJECT
public:
    using GameDirFn = std::function<QString()>;  // Replay game folder (the one holding .proxy)
    using MapIdFn = std::function<QString()>;    // id of the deployed map to select, may be empty
    LiveLinkPanel(GameDirFn gameDir, MapIdFn mapId, QWidget* parent = nullptr);
    ~LiveLinkPanel() override;

    QString messageText() const;  // the last message line, for tests and the log
    // Editor camera eye position, for "Move player to editor camera" (direct mode).
    void setEditorCameraSource(std::function<Vec3()> fn) { editorCam_ = std::move(fn); }
    // Direct mode (no proxy): attaches to the exe running from the game folder; false with a message otherwise.
    bool directAttach();
    // Quick test match (7.12, research 47): the commands that select the deployed map and mode and let the lobby fill
    // with bots (token dvar names, as the proxy writes them). Start Match stays manual (no start command exists).
    // True when the old proxy DLL (mw120rproxy, loaded as XInput9_1_0.dll) is in the game folder. The recompiled
    // clients do not use it (research 76). Preferences live/proxyControls = true forces the sections on.
    static bool proxyInstalledIn(const QString& gameDir);
    bool proxySectionsVisible() const;
    static QStringList quickMatchCommands(const QString& mapId, const QString& gametype, int enemyBots, int allyBots);
    bool startQuickMatch(const QString& gametype, int enemyBots, int allyBots);
    // In-game screenshot (7.11): grabs the game's window (windowed / borderless; exclusive fullscreen may come out
    // black) into <folder>/<map>_<time>.png. Returns the file, "" with a message when there is no game window.
    QString captureGameScreenshot(const QString& folder);
    live::DirectLink& direct() { return direct_; }
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
    void setSelectionNameSource(std::function<QString()> fn) { selectionName_ = std::move(fn); }
    // One-key load map (7.2): starts the compiled map from a lobby the player hosts, through the direct link:
    // set ui_mapname / ui_gametype, then xpartygo (sv_systemlinkStartMatch in Local Play). Replay MP has no `map`
    // command (docs/research/65). Preferences live/loadMapCommand ("%1" = map id) replaces it with custom text.
    // loadMapCommand is the text for tests and the status line.
    static QString loadMapCommand(const QString& mapId);
    bool loadMapInGame();
    // Hot update (7.4): while the box is ticked, moved script entities (by targetname) are sent to the running game; the
    // map script applies them (map_script.h, dvar iw8r_hot). Returns false when off or the game is not reachable.
    bool hotUpdateEnabled() const;
    void setHotUpdateEnabled(bool on);
    bool sendHotUpdate(const QString& targetname, const Vec3& origin, const Vec3& angles);
    // Hot channels (7.7 / 7.9, core hot_channels): one encoded value for the dvar iw8r_fx, iw8r_snd or iw8r_sun.
    bool sendHotChannel(const QString& dvar, const QString& value, const QString& label);
    unsigned nextHotSeq() { return ++hotSeq_; }
    QString lastHotCommand() const { return lastHot_; }

signals:
    // Emitted by "Move editor camera there" after a successful camera readback. Angles are pitch, yaw, roll in degrees.
    void moveEditorCameraRequested(double x, double y, double z, double pitch, double yaw, double roll);
    // Follow game (direct link): the in-game view every frame of the follow timer. No status message.
    void followCameraUpdate(double x, double y, double z, double pitch, double yaw, double roll);

protected:
    void showEvent(QShowEvent*) override;
    void hideEvent(QHideEvent*) override;

private:
    void onStatusTick();
    void requestAction(live::Action action, const QString& value, bool userInitiated);
    void findPidThenSend(live::Action action, const QString& value, bool userInitiated);
    void sendNow(live::Action action, const QString& value, bool userInitiated);
    void onPollTick();
    void handleResult(live::Action action, const live::Result& r);
    void handleTimeout(live::Action action);
    void showStatus(const live::Result& r);
    void clearStatusFields(const QString& connection);
    void showMessage(const QString& text, bool error);
    void onSelectClicked();
    void onConsoleSubmit();
    void onReadClicked();
    void onMoveClicked();

    GameDirFn gameDir_;
    MapIdFn mapId_;
    ::QCheckBox* hotUpdate_ = nullptr;
    unsigned hotSeq_ = 0;
    QString lastHot_;
    std::function<Vec3()> editorCam_;
    live::DirectLink direct_;
    void onDirectRead();
    void onDirectCommand();
    void onDirectMovePlayer();
    void onFollowTick();
    // The four sections that talk to the old proxy DLL through <game>\.proxy\control.ini. They are shown, and the
    // 2 s status request is written, only when that DLL is installed in the game folder (proxyInstalledIn).
    QList<QWidget*> proxySections_;
    QComboBox* debugPreset_ = nullptr;
    QLineEdit* debugFilter_ = nullptr;
    QTimer* dumpTimer_ = nullptr;
    qint64 dumpLogOffset_ = 0, dumpSinceMs_ = 0;
    QString dumpReport_;
    std::function<const MapFile*()> mapSource_;
    std::function<QString()> selectionName_;
    void onDumpTick();
    void showDumpReport(const QString& report, const MapDebugDump& dump);
    QGroupBox* directBox_ = nullptr;
    QTimer* followTimer_ = nullptr;
    // Quick match from the Multiplayer menu: the lobby is hosted first, then this timer sends the start when it is up.
    QTimer* quickTimer_ = nullptr;
    QString quickMap_, quickGametype_;
    qint64 quickSinceMs_ = 0, quickLobbySinceMs_ = -1;
    void onQuickTick();
    QPushButton* followButton_ = nullptr;
    Vec3 lastFollowOrigin_, lastFollowAngles_;
    int followFailures_ = 0;
    live::Link link_;

    QLabel* connection_ = nullptr;
    QLabel* mapLabel_ = nullptr;
    QLabel* selectedLabel_ = nullptr;
    QLabel* screenLabel_ = nullptr;
    QLabel* windowsLabel_ = nullptr;
    QLabel* deployedLabel_ = nullptr;
    QLabel* playerLabel_ = nullptr;
    QLabel* message_ = nullptr;
    QLineEdit* console_ = nullptr;
    QLineEdit* directConsole_ = nullptr;
    QPushButton* selectButton_ = nullptr;
    QPushButton* readButton_ = nullptr;
    QPushButton* moveButton_ = nullptr;

    QTimer* statusTimer_ = nullptr;  // auto status every 2 s while visible
    QTimer* pollTimer_ = nullptr;    // checks for the answer every 250 ms while a request is pending
    QProcess* tasklist_ = nullptr;
    live::Action tasklistAction_ = live::Action::Status;
    QString tasklistValue_;
    bool tasklistUser_ = false;
    bool tasklistBusy_ = false;
    int64_t lastPidLookupMs_ = -1;

    bool pendingUser_ = false;  // the pending request came from a button or the console, not the 2 s status poll
    QString selectSent_;        // map id of the select request in flight
    QString awaitingMap_;       // map id the proxy accepted, until it reports it as ui_mapname
    live::PlayerState player_;
};

}  // namespace iw8r::qt
