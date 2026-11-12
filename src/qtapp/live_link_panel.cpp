#include "live_link_panel.h"
#ifdef _WIN32
#include <windows.h>
#endif
#include <QUrl>
#include <QFileInfo>
#include <QDir>
#include <QDateTime>
#include <QGuiApplication>
#include <QScreen>
#include <QStandardPaths>
#include <QDesktopServices>
#include <QInputDialog>
#include "compile_panel.h"
#include "devmap_launch.h"
#include "devmap_plan.h"
#include "settings.h"
#include "map_script.h"
#include <QCheckBox>

#include <QFormLayout>
#include <QComboBox>
#include <QDialog>
#include <QPlainTextEdit>
#include <QFile>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QProcess>
#include <QPushButton>
#include <QShowEvent>
#include <QTimer>
#include <QVBoxLayout>

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

namespace iw8r::qt {

namespace {

// Replay's game executable (the one that loads mw120rproxy; main_window.cpp names the same image).
constexpr const char* kReplayExe = "game_dx12_ship_replay.exe";
constexpr int kStatusIntervalMs = 2000;
constexpr int kPollIntervalMs = 250;
// While Replay is not running, look for its pid at most this often (tasklist is a process spawn).
constexpr int64_t kPidRetryMs = 10000;

int64_t nowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

QString actionLabel(live::Action a) {
    switch (a) {
        case live::Action::Status: return "status";
        case live::Action::Command: return "command";
        case live::Action::Select: return "select";
        case live::Action::Player: return "player";
        case live::Action::Read: return "read";
    }
    return "request";
}

QString textOrDash(const std::string& s) { return s.empty() ? QStringLiteral("-") : QString::fromStdString(s); }

QString vec3Text(double x, double y, double z) {
    return QString("%1, %2, %3").arg(x, 0, 'f', 2).arg(y, 0, 'f', 2).arg(z, 0, 'f', 2);
}

}  // namespace

LiveLinkPanel::LiveLinkPanel(GameDirFn gameDir, MapIdFn mapId, QWidget* parent)
    : QWidget(parent), gameDir_(std::move(gameDir)), mapId_(std::move(mapId)) {
    setObjectName("liveLinkPanel");
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(6, 6, 6, 6);

    // Status: what the proxy last answered. "-" means unknown, not zero.
    auto* status = new QGroupBox("Status", this);
    auto* form = new QFormLayout(status);
    connection_ = new QLabel(status);
    connection_->setObjectName("liveLinkConnection");
    mapLabel_ = new QLabel(status);
    mapLabel_->setObjectName("liveLinkMap");
    selectedLabel_ = new QLabel(status);
    selectedLabel_->setObjectName("liveLinkSelected");
    screenLabel_ = new QLabel(status);
    windowsLabel_ = new QLabel(status);
    form->addRow("Connection", connection_);
    form->addRow("Map (ui_mapname)", mapLabel_);
    form->addRow("Selected package", selectedLabel_);
    form->addRow("Screen", screenLabel_);
    form->addRow("Console / browser", windowsLabel_);
    auto* refresh = new QPushButton("Refresh status", status);
    refresh->setObjectName("liveLinkRefresh");
    form->addRow(refresh);
    lay->addWidget(status);

    // Select the deployed map. The proxy rescans packages first; the match still needs Start Match in the lobby.
    auto* select = new QGroupBox("Select deployed map", this);
    auto* selectLay = new QVBoxLayout(select);
    deployedLabel_ = new QLabel(select);
    deployedLabel_->setObjectName("liveLinkDeployed");
    selectButton_ = new QPushButton("Select deployed map in Replay", select);
    selectButton_->setObjectName("liveLinkSelectButton");
    auto* selectNote = new QLabel("Lobby only. Selecting rescans the packages. Start Match stays a manual step.", select);
    selectNote->setStyleSheet("color: gray");
    selectNote->setWordWrap(true);
    selectLay->addWidget(deployedLabel_);
    selectLay->addWidget(selectButton_);
    selectLay->addWidget(selectNote);
    lay->addWidget(select);

    // Console: only the allowlisted `set <dvar> <value>` form. Refusals are shown and never sent.
    auto* console = new QGroupBox("Console (set <dvar> <value> only)", this);
    auto* consoleLay = new QHBoxLayout(console);
    console_ = new QLineEdit(console);
    console_->setObjectName("liveLinkConsole");
    console_->setPlaceholderText("set r_fullbright 1");
    console_->setClearButtonEnabled(true);
    auto* consoleSend = new QPushButton("Send", console);
    consoleSend->setObjectName("liveLinkConsoleSend");
    consoleLay->addWidget(console_, 1);
    consoleLay->addWidget(consoleSend);
    lay->addWidget(console);

    // Camera: read the player position in a match. Moving the editor camera is up to the owner's click.
    auto* camera = new QGroupBox("Camera (in a match)", this);
    auto* cameraLay = new QVBoxLayout(camera);
    playerLabel_ = new QLabel("-", camera);
    playerLabel_->setObjectName("liveLinkPlayer");
    auto* cameraRow = new QHBoxLayout();
    readButton_ = new QPushButton("Read camera from Replay", camera);
    readButton_->setObjectName("liveLinkReadButton");
    moveButton_ = new QPushButton("Move editor camera there", camera);
    moveButton_->setObjectName("liveLinkMoveButton");
    moveButton_->setEnabled(false);
    cameraRow->addWidget(readButton_);
    cameraRow->addWidget(moveButton_);
    cameraLay->addWidget(playerLabel_);
    cameraLay->addLayout(cameraRow);
    lay->addWidget(camera);
    proxySections_ = {status, select, console, camera};

    // Direct link: reads and writes the game process itself (no proxy needed). Recompiled exes are resolved through
    // their .rplmap address map, so a custom Replay build works too.
    auto* directBox = directBox_ = new QGroupBox("Direct (no proxy)", this);
    auto* directLay = new QVBoxLayout(directBox);
    auto* dRow1 = new QHBoxLayout();
    auto* dRead = new QPushButton("Read camera -> editor", directBox);
    dRead->setObjectName("liveLinkDirectRead");
    auto* dMove = new QPushButton("Move player to editor camera", directBox);
    dMove->setObjectName("liveLinkDirectMove");
    followButton_ = new QPushButton("Follow game", directBox);
    followButton_->setObjectName("liveLinkFollow");
    followButton_->setCheckable(true);
    followButton_->setToolTip("The editor camera copies your in-game position and view live (about 30 times a second)");
    dRow1->addWidget(dRead);
    dRow1->addWidget(dMove);
    dRow1->addWidget(followButton_);
    auto* dRow2 = new QHBoxLayout();
    directConsole_ = new QLineEdit(directBox);
    directConsole_->setObjectName("liveLinkDirectConsole");
    directConsole_->setPlaceholderText("any console command, e.g. map_restart");
    auto* dSend = new QPushButton("Send", directBox);
    dSend->setObjectName("liveLinkDirectSend");
    dRow2->addWidget(directConsole_, 1);
    dRow2->addWidget(dSend);
    auto* dLoad = new QPushButton("Load map in game", directBox);
    dLoad->setObjectName("liveLinkDirectLoadMap");
    dLoad->setToolTip("Starts the deployed map from a lobby you host with no countdown (sets ui_mapname / ui_gametype, then "
                      "xpartygo 1 or sv_systemlinkStartMatch). Preferences live/loadMapCommand replaces it. F5 also does the menu steps.");
    dRow2->addWidget(dLoad);
    connect(dLoad, &QPushButton::clicked, this, [this] { loadMapInGame(); });
    auto* quick = new QPushButton("Quick match...", directBox);
    quick->setObjectName("liveLinkQuickMatch");
    quick->setToolTip("Starts a private match on the deployed map in a chosen mode with bots. From a lobby you host it starts at "
                      "once (no countdown); from the Multiplayer menu it hosts a private match first (research 76)");
    dRow2->addWidget(quick);
    connect(quick, &QPushButton::clicked, this, [this] {
        bool ok = false;
        const QString mode = QInputDialog::getItem(this, "Quick Match", "Mode (ui_gametype; war is the one seen working):",
                                                   {"war", "dom", "sd", "conf", "koth", "dm"}, 0, false, &ok);
        if (!ok) return;
        const int enemies = QInputDialog::getInt(this, "Quick Match", "Enemy bots:", 4, 0, 17, 1, &ok);
        if (!ok) return;
        const int allies = QInputDialog::getInt(this, "Quick Match", "Ally bots:", 0, 0, 17, 1, &ok);
        if (ok) startQuickMatch(mode, enemies, allies);
    });
    auto* shot = new QPushButton("Screenshot", directBox);
    shot->setObjectName("liveLinkScreenshot");
    shot->setToolTip("Saves the game window as a PNG in a screenshots folder beside the map");
    dRow2->addWidget(shot);
    connect(shot, &QPushButton::clicked, this, [this] {
        const QString id = mapId_ ? mapId_() : QString();
        const QString f = captureGameScreenshot(QDir(QStandardPaths::writableLocation(QStandardPaths::PicturesLocation)).filePath("IW8 Radiant"));
        if (!f.isEmpty()) QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(f).absolutePath()));
    });
    hotUpdate_ = new QCheckBox("Hot update", directBox);
    hotUpdate_->setObjectName("liveLinkHotUpdate");
    hotUpdate_->setToolTip("Moving a script_model / script_origin with a targetname moves it in the running game (no rebuild).\n"
                           "Needs a build made by this editor (its map script listens on the dvar iw8r_hot).");
    dRow2->addWidget(hotUpdate_);
    auto* dNote = new QLabel("Works with any exe running from the game folder, stock or recompiled. "
                             "Commands are appended to the game's command buffer while it is idle.", directBox);
    dNote->setStyleSheet("color: gray");
    dNote->setWordWrap(true);
    // Map debug overlay (research 78): the recompiled client draws the map's entities, triggers, the player's
    // position and what the crosshair is on. The editor sets its dvars and reads its entity dump.
    auto* dRow3 = new QHBoxLayout();
    dRow3->addWidget(new QLabel("Debug overlay", directBox));
    debugPreset_ = new QComboBox(directBox);
    debugPreset_->setObjectName("liveLinkDebugPreset");
    for (const MapDebugPreset& p : mapDebugPresets()) {
        debugPreset_->addItem(QString::fromLatin1(p.name), p.mask);
        debugPreset_->setItemData(debugPreset_->count() - 1, QString::fromLatin1(p.description), Qt::ToolTipRole);
    }
    debugPreset_->setToolTip("What the game draws over your map while you play it (dvar iw8r_mapDebug). Needs a client built with the map debug overlay; you must host the match.");
    dRow3->addWidget(debugPreset_);
    debugFilter_ = new QLineEdit(directBox);
    debugFilter_->setObjectName("liveLinkDebugFilter");
    debugFilter_->setPlaceholderText("filter: trigger door1 -spawn");
    debugFilter_->setClearButtonEnabled(true);
    debugFilter_->setToolTip("Only entities whose class, name, target or model contains one of these words are drawn; a word that starts with - hides matches");
    dRow3->addWidget(debugFilter_, 1);
    auto* debugSel = new QPushButton("Selected", directBox);
    debugSel->setObjectName("liveLinkDebugSelected");
    debugSel->setToolTip("Filter the overlay to the entity selected in the editor (by its targetname)");
    dRow3->addWidget(debugSel);
    auto* debugDump = new QPushButton("Compare with game", directBox);
    debugDump->setObjectName("liveLinkDebugDump");
    debugDump->setToolTip("Asks the game for its entity list (iw8r_mapDebugDump) and lists which named entities of the map are missing or at another place in the running match");
    dRow3->addWidget(debugDump);
    auto applyOverlay = [this] { setDebugOverlay(debugPreset_->currentData().toInt(), debugFilter_->text()); };
    connect(debugPreset_, &QComboBox::activated, this, applyOverlay);
    connect(debugFilter_, &QLineEdit::returnPressed, this, applyOverlay);
    connect(debugSel, &QPushButton::clicked, this, [this, applyOverlay] {
        const QString name = selectionName_ ? selectionName_().trimmed() : QString();
        if (name.isEmpty()) return showMessage("Debug overlay: select an entity that has a targetname first.", true);
        debugFilter_->setText(name);
        if (debugPreset_->currentData().toInt() == 0) debugPreset_->setCurrentIndex(2);
        applyOverlay();
    });
    connect(debugDump, &QPushButton::clicked, this, [this] { requestEntityDump(); });
    directLay->addLayout(dRow1);
    directLay->addLayout(dRow2);
    directLay->addLayout(dRow3);
    directLay->addWidget(dNote);
    lay->addWidget(directBox);
    connect(dRead, &QPushButton::clicked, this, &LiveLinkPanel::onDirectRead);
    followTimer_ = new QTimer(this);
    followTimer_->setInterval(33);
    connect(followTimer_, &QTimer::timeout, this, &LiveLinkPanel::onFollowTick);
    connect(followButton_, &QPushButton::toggled, this, [this](bool on) {
        if (on && !directAttach()) {
            followButton_->setChecked(false);
            return;
        }
        followFailures_ = 0;
        if (on) {
            followTimer_->start();
            showMessage("Following the game camera. Toggle Follow game to stop.", false);
        } else {
            followTimer_->stop();
            showMessage("Stopped following the game camera.", false);
        }
    });
    connect(dMove, &QPushButton::clicked, this, &LiveLinkPanel::onDirectMovePlayer);
    connect(dSend, &QPushButton::clicked, this, &LiveLinkPanel::onDirectCommand);
    connect(directConsole_, &QLineEdit::returnPressed, this, &LiveLinkPanel::onDirectCommand);

    message_ = new QLabel(this);
    message_->setObjectName("liveLinkMessage");
    message_->setWordWrap(true);
    message_->setStyleSheet("color: gray");
    lay->addWidget(message_);
    lay->addStretch(1);

    statusTimer_ = new QTimer(this);
    statusTimer_->setInterval(kStatusIntervalMs);
    connect(statusTimer_, &QTimer::timeout, this, &LiveLinkPanel::onStatusTick);
    pollTimer_ = new QTimer(this);
    pollTimer_->setInterval(kPollIntervalMs);
    connect(pollTimer_, &QTimer::timeout, this, &LiveLinkPanel::onPollTick);

    connect(refresh, &QPushButton::clicked, this, [this] { requestAction(live::Action::Status, {}, true); });
    connect(selectButton_, &QPushButton::clicked, this, &LiveLinkPanel::onSelectClicked);
    connect(consoleSend, &QPushButton::clicked, this, &LiveLinkPanel::onConsoleSubmit);
    connect(console_, &QLineEdit::returnPressed, this, &LiveLinkPanel::onConsoleSubmit);
    connect(readButton_, &QPushButton::clicked, this, &LiveLinkPanel::onReadClicked);
    connect(moveButton_, &QPushButton::clicked, this, &LiveLinkPanel::onMoveClicked);

    clearStatusFields("Not connected");
    deployedLabel_->setText("Deployed map: none");
}

LiveLinkPanel::~LiveLinkPanel() = default;

QString LiveLinkPanel::messageText() const { return message_->text(); }

bool LiveLinkPanel::proxyInstalledIn(const QString& gameDir) {
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
}

void LiveLinkPanel::hideEvent(QHideEvent* e) {
    statusTimer_->stop();
    QWidget::hideEvent(e);
}

void LiveLinkPanel::onStatusTick() {
    // The 2 s poll never queues behind a user request and never repeats one that is still waiting.
    if (link_.pending() || tasklistBusy_) return;
    requestAction(live::Action::Status, {}, false);
}

void LiveLinkPanel::requestAction(live::Action action, const QString& value, bool userInitiated) {
    if (link_.pending()) {
        if (userInitiated) showMessage("Waiting for the proxy to answer the last request.", false);
        return;
    }
    if (tasklistBusy_) {
        if (userInitiated) showMessage("Looking for the Replay process...", false);
        return;
    }
    const QString dir = gameDir_ ? gameDir_().trimmed() : QString();
    if (dir.isEmpty()) {
        clearStatusFields("No game folder");
        showMessage("Set the Replay game folder first.", true);
        return;
    }
    link_.setGameDir(std::filesystem::path(dir.toStdWString()));
    if (link_.pid() != 0) {
        sendNow(action, value, userInitiated);
        return;
    }
    if (!userInitiated && lastPidLookupMs_ >= 0 && nowMs() - lastPidLookupMs_ < kPidRetryMs) {
        clearStatusFields("Replay is not running");
        return;
    }
    findPidThenSend(action, value, userInitiated);
}

void LiveLinkPanel::findPidThenSend(live::Action action, const QString& value, bool userInitiated) {
    lastPidLookupMs_ = nowMs();
    // The process is found by its path (an exe running from the game folder), so a renamed Replay exe works.
    const QList<quint32> pids = CompilePanel::gamePidsFrom(gameDir_());
    if (pids.isEmpty()) {
        clearStatusFields("Replay is not running");
        if (userInitiated) showMessage("Replay is not running from " + gameDir_() + ". Start it, then try again.", true);
        return;
    }
    if (pids.size() > 1) {
        clearStatusFields("Several Replay processes");
        showMessage("More than one exe is running from the game folder. Close the extra one so the proxy is unambiguous.", true);
        return;
    }
    link_.setPid(pids.front());
    sendNow(action, value, userInitiated);
}

void LiveLinkPanel::sendNow(live::Action action, const QString& value, bool userInitiated) {
    std::string err;
    if (!link_.send(action, value.trimmed().toStdString(), nowMs(), &err)) {
        showMessage(QString::fromStdString(err), true);
        return;
    }
    pendingUser_ = userInitiated;
    if (action == live::Action::Select) selectSent_ = value.trimmed();
    pollTimer_->start();
    connection_->setText("Waiting for the proxy...");
    if (userInitiated) showMessage(QString("Sent %1. Waiting for the proxy to answer.").arg(actionLabel(action)), false);
}

void LiveLinkPanel::onPollTick() {
    const live::Action action = link_.pendingAction();
    live::Result r;
    switch (link_.poll(nowMs(), r)) {
        case live::Link::Event::Answered:
            pollTimer_->stop();
            handleResult(action, r);
            break;
        case live::Link::Event::TimedOut:
            pollTimer_->stop();
            handleTimeout(action);
            break;
        case live::Link::Event::None:
            break;
    }
}

void LiveLinkPanel::handleResult(live::Action action, const live::Result& r) {
    showStatus(r);
    switch (action) {
        case live::Action::Status:
            if (!awaitingMap_.isEmpty() && QString::fromStdString(r.map) != awaitingMap_) {
                showMessage(QString("Waiting for Replay to show %1 as the map.").arg(awaitingMap_), false);
            } else if (!awaitingMap_.isEmpty()) {
                showMessage(QString("Replay shows %1 as the map. Start Match stays a manual step in the lobby.").arg(awaitingMap_), false);
                awaitingMap_.clear();
            } else if (pendingUser_) {
                showMessage("Status updated.", false);
            }
            break;
        case live::Action::Command:
            if (r.ok) {
                showMessage("Queued in Replay's console. The game does not report whether it ran. Check the effect or the Replay log.", false);
            } else {
                showMessage("The proxy refused the command.", true);
            }
            break;
        case live::Action::Select:
            if (r.ok) {
                awaitingMap_ = selectSent_;
                showMessage("Select queued. Waiting for Replay to show the map.", false);
            } else {
                awaitingMap_.clear();
                showMessage("Replay did not accept the select. It works in the lobby only.", true);
            }
            break;
        case live::Action::Player: {
            live::PlayerState ps;
            if (live::parsePlayerData(r.data, ps)) {
                player_ = ps;
                playerLabel_->setText("Origin " + vec3Text(ps.origin.x, ps.origin.y, ps.origin.z) +
                                      "   angles (pitch, yaw, roll) " + vec3Text(ps.angles.x, ps.angles.y, ps.angles.z));
                moveButton_->setEnabled(true);
                showMessage("Camera read from Replay.", false);
            } else {
                player_ = {};
                playerLabel_->setText("-");
                moveButton_->setEnabled(false);
                showMessage("No player data. Start a match first, then read the camera again.", true);
            }
            break;
        }
        case live::Action::Read:
            showMessage("Read answered.", false);
            break;
    }
}

void LiveLinkPanel::handleTimeout(live::Action action) {
    // Forget the pid so the next request looks up the process again (Replay may have been restarted).
    link_.setPid(0);
    awaitingMap_.clear();
    clearStatusFields("Not answering");
    showMessage(QString("The proxy did not answer the %1 request within 5 seconds. Is Replay running with mw120rproxy? "
                        "The status fields are cleared.")
                    .arg(actionLabel(action)),
                true);
}

void LiveLinkPanel::showStatus(const live::Result& r) {
    connection_->setText(QString("Connected (pid %1)").arg(r.pid));
    mapLabel_->setText(textOrDash(r.map));
    selectedLabel_->setText(textOrDash(r.selected));
    screenLabel_->setText(r.frontend ? "Lobby or menu" : "In game");
    windowsLabel_->setText(QString("console %1, browser %2").arg(r.console ? "open" : "closed").arg(r.browser ? "open" : "closed"));
}

void LiveLinkPanel::clearStatusFields(const QString& connection) {
    connection_->setText(connection);
    mapLabel_->setText("-");
    selectedLabel_->setText("-");
    screenLabel_->setText("-");
    windowsLabel_->setText("-");
    player_ = {};
    if (playerLabel_) playerLabel_->setText("-");
    if (moveButton_) moveButton_->setEnabled(false);
}

void LiveLinkPanel::showMessage(const QString& text, bool error) {
    message_->setText(text);
    message_->setStyleSheet(error ? "color: #c62828" : "color: gray");
}

void LiveLinkPanel::onSelectClicked() {
    const QString id = mapId_ ? mapId_().trimmed() : QString();
    if (id.isEmpty()) {
        showMessage("No deployed map id to select. Deploy the map first.", true);
        return;
    }
    const live::Verdict v = live::checkMapId(id.toStdString());
    if (!v.ok) {
        showMessage(QString::fromStdString(v.reason), true);
        return;
    }
    requestAction(live::Action::Select, id, true);
}

void LiveLinkPanel::onConsoleSubmit() {
    const QString text = console_->text().trimmed();
    const live::Verdict v = live::checkConsoleCommand(text.toStdString());
    if (!v.ok) {
        showMessage(QString::fromStdString(v.reason), true);
        return;
    }
    requestAction(live::Action::Command, text, true);
}

void LiveLinkPanel::onReadClicked() { requestAction(live::Action::Player, {}, true); }

void LiveLinkPanel::onMoveClicked() {
    if (!player_.valid) return;
    emit moveEditorCameraRequested(player_.origin.x, player_.origin.y, player_.origin.z, player_.angles.x, player_.angles.y,
                                   player_.angles.z);
}

bool LiveLinkPanel::directAttach() {
    const QString dir = gameDir_ ? gameDir_().trimmed() : QString();
    const QList<quint32> pids = CompilePanel::gamePidsFrom(dir);
    if (pids.isEmpty()) {
        direct_.detach();
        showMessage("No game exe is running from " + dir + ".", true);
        return false;
    }
    if (pids.size() > 1) {
        showMessage("More than one exe is running from the game folder; close the extra one.", true);
        return false;
    }
    if (direct_.attached() && direct_.pid() == pids.front()) return true;
    std::string err;
    if (!direct_.attach(pids.front(), &err)) {
        showMessage("Direct link: " + QString::fromStdString(err), true);
        return false;
    }
    showMessage(QString("Direct link attached to pid %1 (%2 exe)").arg(pids.front()).arg(direct_.recompiled() ? "recompiled, .rplmap" : "stock"), false);
    return true;
}

void LiveLinkPanel::onDirectRead() {
    if (!directAttach()) return;
    std::string err;
    const auto v = direct_.readPlayer(&err);
    if (!v) {
        showMessage("Direct link: " + QString::fromStdString(err), true);
        return;
    }
    playerLabel_->setText(QString("origin %1 %2 %3  angles %4 %5").arg(v->origin.x, 0, 'f', 1).arg(v->origin.y, 0, 'f', 1)
                              .arg(v->origin.z, 0, 'f', 1).arg(v->angles.x, 0, 'f', 1).arg(v->angles.y, 0, 'f', 1));
    // Eye height of a standing player (60): the editor camera is an eye position.
    emit moveEditorCameraRequested(v->origin.x, v->origin.y, v->origin.z + 60, v->angles.x, v->angles.y, v->angles.z);
    showMessage("Editor camera moved to the player (direct link).", false);
}

void LiveLinkPanel::onDirectMovePlayer() {
    if (!editorCam_ || !directAttach()) return;
    const Vec3 eye = editorCam_();
    std::string err;
    if (!direct_.setPlayerOrigin(eye - Vec3{0, 0, 60}, &err)) {
        showMessage("Direct link: " + QString::fromStdString(err), true);
        return;
    }
    showMessage(QString("Player moved to %1 %2 %3 (view angles unchanged).").arg(eye.x, 0, 'f', 0).arg(eye.y, 0, 'f', 0).arg(eye.z - 60, 0, 'f', 0), false);
}

QString LiveLinkPanel::loadMapCommand(const QString& mapId) {
    QString tpl = Settings::instance().value("live/loadMapCommand").toString().trimmed();
    if (tpl.isEmpty()) {
        // The lobby start (private match form) shown as one line; loadMapInGame picks the start verb from the game state.
        QStringList lines;
        for (const std::string& c : devmap::startMatchCommands(mapId.toStdString(), DevmapRunner::gametypeSetting().toStdString(),
                                                               devmap::GameSnapshot{}))
            lines << QString::fromStdString(c);
        return lines.join("; ");
    }
    if (!tpl.contains("%1")) tpl += " %1";
    return tpl.arg(mapId);
}

bool LiveLinkPanel::hotUpdateEnabled() const { return hotUpdate_ && hotUpdate_->isChecked(); }
void LiveLinkPanel::setHotUpdateEnabled(bool on) {
    if (hotUpdate_) hotUpdate_->setChecked(on);
}

bool LiveLinkPanel::sendHotUpdate(const QString& targetname, const Vec3& origin, const Vec3& angles) {
    if (!hotUpdateEnabled()) return false;
    lastHot_ = "set iw8r_hot \"" + QString::fromStdString(hotUpdateValue(++hotSeq_, targetname.toStdString(), origin, angles)) + "\"";
    if (!direct_.attached() && !directAttach()) return false;
    std::string err;
    if (!direct_.sendCommand(lastHot_.toStdString(), &err)) {
        showMessage("Hot update: " + QString::fromStdString(err), true);
        return false;
    }
    showMessage("Hot update sent: " + targetname, false);
    return true;
}

bool LiveLinkPanel::sendHotChannel(const QString& dvar, const QString& value, const QString& label) {
    if (!hotUpdateEnabled() || value.isEmpty()) return false;
    lastHot_ = "set " + dvar + " \"" + value + "\"";
    if (!direct_.attached() && !directAttach()) return false;
    std::string err;
    if (!direct_.sendCommand(lastHot_.toStdString(), &err)) {
        showMessage("Hot update: " + QString::fromStdString(err), true);
        return false;
    }
    showMessage("Hot update sent: " + label, false);
    return true;
}

bool LiveLinkPanel::loadMapInGame() {
    const QString id = mapId_ ? mapId_() : QString();
    if (id.isEmpty()) {
        showMessage("Load map: compile and deploy the map first (no map id yet).", true);
        return false;
    }
    if (!directAttach()) return false;
    QString cmd = loadMapCommand(id);
    std::string text = cmd.toStdString(), err;
    if (Settings::instance().value("live/loadMapCommand").toString().trimmed().isEmpty()) {
        const devmap::GameSnapshot snap = DevmapRunner::snapshot(direct_);
        const devmap::State st = devmap::classify(snap);
        if (st != devmap::State::LobbyHost) {
            showMessage(QString("Load map: the game is %1. Open a lobby you host (Private Match, or Local Play > create a game), "
                                "or press F5: it builds, deploys and does the menu steps.")
                            .arg(devmap::stateName(st)),
                        true);
            return false;
        }
        const auto commands = devmap::startMatchCommands(id.toStdString(), DevmapRunner::gametypeSetting().toStdString(), snap, DevmapRunner::modeDvarsSetting());
        if (!devmap::joinCommands(commands, text, &err)) {
            showMessage("Load map: " + QString::fromStdString(err), true);
            return false;
        }
        cmd = QString::fromStdString(text).replace('\n', "; ");
    }
    if (!direct_.sendCommand(text, &err)) {
        showMessage("Direct link: " + QString::fromStdString(err), true);
        return false;
    }
    showMessage("Sent to the game: " + cmd, false);
    return true;
}

void LiveLinkPanel::onDirectCommand() {
    const QString text = directConsole_->text().trimmed();
    if (text.isEmpty() || !directAttach()) return;
    std::string err;
    if (!direct_.sendCommand(text.toStdString(), &err)) {
        showMessage("Direct link: " + QString::fromStdString(err), true);
        return;
    }
    showMessage("Queued in the game's command buffer: " + text, false);
}

void LiveLinkPanel::onFollowTick() {
    std::string err;
    const auto v = direct_.readPlayer(&err);
    if (!v) {
        // Menus and loading screens have no player: keep trying quietly; stop when the process is gone.
        if (++followFailures_ % 60 == 1) showMessage("Follow game: " + QString::fromStdString(err), true);
        if (CompilePanel::gamePidsFrom(gameDir_ ? gameDir_() : QString()).isEmpty()) {
            followButton_->setChecked(false);
            direct_.detach();
        }
        return;
    }
    followFailures_ = 0;
    if (length(v->origin - lastFollowOrigin_) < 0.01 && length(v->angles - lastFollowAngles_) < 0.01) return;
    lastFollowOrigin_ = v->origin;
    lastFollowAngles_ = v->angles;
    emit followCameraUpdate(v->origin.x, v->origin.y, v->origin.z + 60, v->angles.x, v->angles.y, v->angles.z);
}

QStringList LiveLinkPanel::quickMatchCommands(const QString& mapId, const QString& gametype, int enemyBots, int allyBots) {
    // Token dvar names (research 47 / 19): NSQLTTMRMP ui_mapname, MOLPOSLOMO ui_gametype, RNRLKSNNR bot_MaxNumEnemyBots,
    // SNMLPONRP bot_MaxNumAllyBots, NLLMRMSTLT mp_bots_sec_before_lobby_autofill.
    // replay_dw_botCount is the recompiled client's own bot count (DW_LobbyControls.cpp, 0 to 199; research 76 Q5).
    return {"set NSQLTTMRMP " + mapId, "set MOLPOSLOMO " + gametype, "set RNRLKSNNR " + QString::number(enemyBots),
            "set SNMLPONRP " + QString::number(allyBots), "set NLLMRMSTLT 3",
            "set replay_dw_botCount " + QString::number(qBound(0, enemyBots + allyBots, 199))};
}

void LiveLinkPanel::onQuickTick() {
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    auto stop = [this](const QString& text, bool error) {
        quickTimer_->stop();
        showMessage(text, error);
    };
    if (!direct_.attached()) return stop("Quick match: the game is gone.", true);
    const devmap::GameSnapshot snap = DevmapRunner::snapshot(direct_);
    const devmap::State st = devmap::classify(snap);
    if (st != devmap::State::LobbyHost) {
        quickLobbySinceMs_ = -1;
        if (now - quickSinceMs_ > devmap::kLobbyTimeoutMs)
            stop(QString("Quick match: no private match lobby after 30 s (the game is %1). Open Private Match in the game and press Quick match again.")
                     .arg(devmap::stateName(st)),
                 true);
        return;
    }
    if (quickLobbySinceMs_ < 0) quickLobbySinceMs_ = now;
    if (now - quickLobbySinceMs_ < devmap::kLobbySettleMs || !snap.commandBufferIdle) return;
    std::string text, err;
    if (!devmap::joinCommands(devmap::startMatchCommands(quickMap_.toStdString(), quickGametype_.toStdString(), snap, DevmapRunner::modeDvarsSetting()), text, &err) ||
        !direct_.sendCommand(text, &err))
        return stop("Quick match: " + QString::fromStdString(err), true);
    stop("Quick match: the lobby is up; started " + quickMap_ + " (" + quickGametype_ + ") with no countdown.", false);
}

bool LiveLinkPanel::startQuickMatch(const QString& gametype, int enemyBots, int allyBots) {
    const QString id = mapId_ ? mapId_() : QString();
    if (id.isEmpty()) {
        showMessage("Quick match: compile and deploy the map first (no map id yet).", true);
        return false;
    }
    if (!direct_.attached() && !directAttach()) return false;
    // What the game shows decides how far the editor can go: from a lobby we host the match starts at once; from the
    // Multiplayer menu a private match is hosted first and the start follows when the lobby is up.
    const devmap::GameSnapshot snap = DevmapRunner::snapshot(direct_);
    const devmap::State st = devmap::classify(snap);
    std::vector<std::string> commands;
    for (const QString& c : quickMatchCommands(id, gametype, enemyBots, allyBots)) commands.push_back(c.toStdString());
    QString tail;
    if (st == devmap::State::LobbyHost) {
        for (const std::string& c : devmap::startMatchCommands(id.toStdString(), gametype.toStdString(), snap, DevmapRunner::modeDvarsSetting())) commands.push_back(c);
        tail = "starting now, no countdown";
    } else if (st == devmap::State::FrontEndNoParty) {
        for (const std::string& c : devmap::hostLobbyCommands()) commands.push_back(c);
        tail = "hosting a private match; the match starts when the lobby is up";
    } else {
        tail = QString("the game is %1: open a lobby you host and press Quick match again, or press Start Match in the lobby").arg(devmap::stateName(st));
    }
    std::string text, err;
    if (!devmap::joinCommands(commands, text, &err) || !direct_.sendCommand(text, &err)) {
        showMessage("Quick match: " + QString::fromStdString(err), true);
        return false;
    }
    if (st == devmap::State::FrontEndNoParty) {
        if (!quickTimer_) {
            quickTimer_ = new QTimer(this);
            quickTimer_->setInterval(200);
            connect(quickTimer_, &QTimer::timeout, this, &LiveLinkPanel::onQuickTick);
        }
        quickMap_ = id;
        quickGametype_ = gametype;
        quickSinceMs_ = QDateTime::currentMSecsSinceEpoch();
        quickLobbySinceMs_ = -1;
        quickTimer_->start();
    }
    showMessage(QString("Quick match: %1 / %2 with %3 enemy and %4 ally bot(s); %5.").arg(id, gametype).arg(enemyBots).arg(allyBots).arg(tail), false);
    return true;
}

bool LiveLinkPanel::setDebugOverlay(int mask, const QString& filter) {
    bool ok = true;
    const std::string clean = cleanMapDebugFilter(filter.toStdString(), &ok);
    if (!ok) {
        showMessage("Debug overlay: the filter may hold letters, digits, _ and - with spaces between the words (at most 96 characters).", true);
        return false;
    }
    if (!direct_.attached() && !directAttach()) return false;
    std::string text, err;
    for (const std::string& c : mapDebugCommands(mask, clean)) text += (text.empty() ? "" : "\n") + c;
    if (!direct_.sendCommand(text, &err)) {
        showMessage("Debug overlay: " + QString::fromStdString(err), true);
        return false;
    }
    showMessage(mask == 0 ? QStringLiteral("Debug overlay off (iw8r_mapDebug 0).")
                          : QString("Debug overlay: iw8r_mapDebug %1%2. It draws in a match you host, with a client that has the overlay.")
                                .arg(mask)
                                .arg(clean.empty() ? QString() : ", filter '" + QString::fromStdString(clean) + "'"),
                false);
    return true;
}

QString LiveLinkPanel::dumpReport(const QString& logText, const MapFile* map, MapDebugDump* parsed) {
    const MapDebugDump dump = parseMapDebugDump(logText.toStdString());
    if (parsed) *parsed = dump;
    if (!dump.found) return {};
    QStringList lines;
    if (!dump.complete) lines << "The dump is not complete yet (no end line).";
    if (map) {
        for (const std::string& l : compareDumpWithMap(dump, *map).lines) lines << QString::fromStdString(l);
    } else {
        lines << QString("%1 entities in the game").arg(dump.entities.size());
    }
    return lines.join('\n');
}

void LiveLinkPanel::requestEntityDump() {
    if (!direct_.attached() && !directAttach()) return;
    const QString log = QDir(gameDir_ ? gameDir_() : QString()).filePath("replay-dw-client.log");
    dumpLogOffset_ = QFileInfo::exists(log) ? QFileInfo(log).size() : 0;
    std::string err;
    if (!direct_.sendCommand(kMapDebugDumpCommand, &err)) return showMessage("Compare with game: " + QString::fromStdString(err), true);
    if (!dumpTimer_) {
        dumpTimer_ = new QTimer(this);
        dumpTimer_->setInterval(200);
        connect(dumpTimer_, &QTimer::timeout, this, &LiveLinkPanel::onDumpTick);
    }
    dumpSinceMs_ = QDateTime::currentMSecsSinceEpoch();
    dumpTimer_->start();
    showMessage("Compare with game: asked the game for its entity list...", false);
}

void LiveLinkPanel::onDumpTick() {
    const QString path = QDir(gameDir_ ? gameDir_() : QString()).filePath("replay-dw-client.log");
    QFile f(path);
    QString text;
    if (f.open(QIODevice::ReadOnly)) {
        if (f.size() < dumpLogOffset_) dumpLogOffset_ = 0;  // the log was rotated
        f.seek(dumpLogOffset_);
        text = QString::fromUtf8(f.read(8 * 1024 * 1024));
    }
    MapDebugDump dump;
    const QString report = dumpReport(text, mapSource_ ? mapSource_() : nullptr, &dump);
    if (dump.found && dump.complete) {
        dumpTimer_->stop();
        return showDumpReport(report, dump);
    }
    if (QDateTime::currentMSecsSinceEpoch() - dumpSinceMs_ > 5000) {
        dumpTimer_->stop();
        showMessage(dump.found ? "Compare with game: the game started its entity list but did not finish it within 5 s."
                               : "Compare with game: no answer within 5 s. The client needs the map debug overlay (a build with DW_MapDebug.cpp), "
                                 "and you must be in a match you host.",
                    true);
    }
}

void LiveLinkPanel::showDumpReport(const QString& report, const MapDebugDump& dump) {
    dumpReport_ = report;
    showMessage("Compare with game: " + report.section('\n', 1, 1), false);
    if (QGuiApplication::platformName() == "offscreen") return;  // tests read lastDumpReport()
    auto* dlg = new QDialog(this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setWindowTitle("Game entities compared with the map");
    dlg->resize(820, 480);
    auto* lay = new QVBoxLayout(dlg);
    auto* view = new QPlainTextEdit(report, dlg);
    view->setReadOnly(true);
    view->setLineWrapMode(QPlainTextEdit::NoWrap);
    QFont mono("Consolas");
    mono.setStyleHint(QFont::Monospace);
    view->setFont(mono);
    lay->addWidget(view);
    auto* row = new QHBoxLayout();
    auto* go = new QPushButton("Move editor camera to the player", dlg);
    go->setEnabled(dump.hasView);
    row->addWidget(go);
    row->addStretch(1);
    auto* close = new QPushButton("Close", dlg);
    row->addWidget(close);
    lay->addLayout(row);
    const Vec3 at = dump.view;
    const double pitch = dump.pitch, yaw = dump.yaw;
    connect(go, &QPushButton::clicked, this, [this, at, pitch, yaw] { emit moveEditorCameraRequested(at.x, at.y, at.z, pitch, yaw, 0); });
    connect(close, &QPushButton::clicked, dlg, &QDialog::close);
    dlg->show();
}

QString LiveLinkPanel::captureGameScreenshot(const QString& folder) {
#ifdef _WIN32
    const QList<quint32> pids = CompilePanel::gamePidsFrom(gameDir_ ? gameDir_() : QString());
    if (pids.isEmpty()) {
        showMessage("Screenshot: the game is not running.", true);
        return {};
    }
    struct Find {
        DWORD pid;
        HWND best = nullptr;
        long area = 0;
    } find{DWORD(pids.front())};
    EnumWindows(
        [](HWND h, LPARAM lp) -> BOOL {
            auto* f = reinterpret_cast<Find*>(lp);
            DWORD pid = 0;
            GetWindowThreadProcessId(h, &pid);
            RECT rc{};
            if (pid == f->pid && IsWindowVisible(h) && GetClientRect(h, &rc)) {
                const long a = long(rc.right - rc.left) * long(rc.bottom - rc.top);
                if (a > f->area) f->area = a, f->best = h;
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&find));
    if (!find.best) {
        showMessage("Screenshot: no game window found.", true);
        return {};
    }
    QScreen* screen = QGuiApplication::primaryScreen();
    const QPixmap pm = screen ? screen->grabWindow(WId(find.best)) : QPixmap();
    if (pm.isNull()) {
        showMessage("Screenshot: the window could not be captured (exclusive fullscreen? use borderless).", true);
        return {};
    }
    QDir().mkpath(folder);
    const QString id = mapId_ ? mapId_() : QString("game");
    const QString file = QDir(folder).filePath(QString("%1_%2.png").arg(id.isEmpty() ? "game" : id, QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss")));
    if (!pm.save(file)) {
        showMessage("Screenshot: cannot write " + file, true);
        return {};
    }
    showMessage("Screenshot saved: " + file, false);
    return file;
#else
    Q_UNUSED(folder);
    return {};
#endif
}

}  // namespace iw8r::qt
