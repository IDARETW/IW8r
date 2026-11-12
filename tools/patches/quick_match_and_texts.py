import io
def edit(p, pairs):
    s = io.open(p, encoding='utf-8', newline='').read()
    crlf = '\r\n' in s
    s = s.replace('\r\n', '\n')
    for a, b in pairs:
        assert s.count(a) == 1, (p, s.count(a), a[:70])
        s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s.replace('\n', '\r\n') if crlf else s)

edit('src/qtapp/live_link_panel.h', [
(r'''    QTimer* followTimer_ = nullptr;''',
 r'''    QTimer* followTimer_ = nullptr;
    // Quick match from the Multiplayer menu: the lobby is hosted first, then this timer sends the start when it is up.
    QTimer* quickTimer_ = nullptr;
    QString quickMap_, quickGametype_;
    qint64 quickSinceMs_ = 0, quickLobbySinceMs_ = -1;
    void onQuickTick();'''),
])

edit('src/qtapp/live_link_panel.cpp', [
(r'''    quick->setToolTip("Selects the deployed map and a mode in the Replay lobby and sets the bot counts; then press Start Match "
                      "in the game (research 47: no start command exists)");''',
 r'''    quick->setToolTip("Starts a private match on the deployed map in a chosen mode with bots. From a lobby you host it starts at "
                      "once (no countdown); from the Multiplayer menu it hosts a private match first (research 76)");'''),
(r'''        const QString mode = QInputDialog::getItem(this, "Quick Match", "Mode (ui_gametype; war is the proxy's verified one):",''',
 r'''        const QString mode = QInputDialog::getItem(this, "Quick Match", "Mode (ui_gametype; war is the one seen working):",'''),
(r'''    dLoad->setToolTip("Starts the deployed map from a lobby you host (sets ui_mapname / ui_gametype, then xpartygo or "
                      "sv_systemlinkStartMatch). Preferences live/loadMapCommand replaces it. F5 also does the menu steps.");''',
 r'''    dLoad->setToolTip("Starts the deployed map from a lobby you host with no countdown (sets ui_mapname / ui_gametype, then "
                      "xpartygo 1 or sv_systemlinkStartMatch). Preferences live/loadMapCommand replaces it. F5 also does the menu steps.");'''),
(r'''    return {"set NSQLTTMRMP " + mapId, "set MOLPOSLOMO " + gametype, "set RNRLKSNNR " + QString::number(enemyBots),
            "set SNMLPONRP " + QString::number(allyBots), "set NLLMRMSTLT 3"};
}''',
 r'''    // replay_dw_botCount is the recompiled client's own bot count (DW_LobbyControls.cpp, 0 to 199; research 76 Q5).
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
    if (!devmap::joinCommands(devmap::startMatchCommands(quickMap_.toStdString(), quickGametype_.toStdString(), snap), text, &err) ||
        !direct_.sendCommand(text, &err))
        return stop("Quick match: " + QString::fromStdString(err), true);
    stop("Quick match: the lobby is up; started " + quickMap_ + " (" + quickGametype_ + ") with no countdown.", false);
}'''),
(r'''    if (!direct_.attached() && !directAttach()) return false;
    for (const QString& c : quickMatchCommands(id, gametype, enemyBots, allyBots)) {
        std::string err;
        if (!direct_.sendCommand(c.toStdString(), &err)) {
            showMessage("Quick match: " + QString::fromStdString(err), true);
            return false;
        }
    }
    showMessage(QString("Quick match: %1 / %2 with %3 enemy and %4 ally bot(s) selected; press Start Match in the game lobby.")
                    .arg(id, gametype).arg(enemyBots).arg(allyBots),
                false);
    return true;''',
 r'''    if (!direct_.attached() && !directAttach()) return false;
    // What the game shows decides how far the editor can go: from a lobby we host the match starts at once; from the
    // Multiplayer menu a private match is hosted first and the start follows when the lobby is up.
    const devmap::GameSnapshot snap = DevmapRunner::snapshot(direct_);
    const devmap::State st = devmap::classify(snap);
    std::vector<std::string> commands;
    for (const QString& c : quickMatchCommands(id, gametype, enemyBots, allyBots)) commands.push_back(c.toStdString());
    QString tail;
    if (st == devmap::State::LobbyHost) {
        for (const std::string& c : devmap::startMatchCommands(id.toStdString(), gametype.toStdString(), snap)) commands.push_back(c);
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
    return true;'''),
])

edit('src/qtapp/compile_panel.cpp', [
(r'''    form->addRow("Title (F6 browser)", title);''', r'''    form->addRow("Title (map.json)", title);'''),
(r'''    // Deploying while Replay runs is allowed: the proxy rescans the folder (R in the F6 browser). Only a package
    // whose zones the game has open (the map is loaded) cannot be replaced; the renames below then fail and the
    // installed package stays as it was.
    const bool running = gameRunningFrom(gameRoot);
    if (running) append("deploy: Replay is running; installing next to it (press R in the F6 browser to rescan)");''',
 r'''    // Deploying while Replay runs is allowed. Only a package whose zones the game has open (the map is loaded) cannot
    // be replaced; the renames below then fail and the installed package stays as it was. The recompiled client scans
    // the packages when it starts (research 76 Q4), so a map id it has never seen needs one game restart.
    const bool running = gameRunningFrom(gameRoot);
    if (running) append("deploy: Replay is running; installing next to it (a map id that is new to the running game needs one game restart)");'''),
(r'''            append("In Replay: Local Play, F6 opens the custom-map browser (R rescans); pick " + id + ".");''',
 r'''            append("To play: press F5 (it starts " + id + " for you), or start it from a private match lobby you host.");'''),
(r'''    append("In Replay: Local Play, F6 opens the custom-map browser; pick " + id + ". In-game loading is not verified by the editor.");''',
 r'''    append("To play: press F5 (it starts " + id + " for you), or start it from a private match lobby you host. In-game loading is not verified by the editor.");'''),
])

edit('src/qtapp/main_window.cpp', [
(r'''                                      "\nReplay may stay open: press R in the F6 browser to rescan (a map that is loaded right now"
                                      " cannot be replaced until another map is loaded). In-game loading is not verified by the editor.") != QMessageBox::Yes)''',
 r'''                                      "\nReplay may stay open (a map that is loaded right now cannot be replaced until another map is"
                                      " loaded; a map id that is new to the running game needs one game restart)."
                                      " In-game loading is not verified by the editor.") != QMessageBox::Yes)'''),
(r'''        // Starts the configured exe in the game folder (docs/research/19 §2: no arguments are needed; F6 picks the map).''',
 r'''        // Starts the game exe in the game folder with no arguments (F5 is the one that also starts the map).'''),
(r'''        if (QProcess::startDetached(exe, {}, dir)) ctx_.setStatus("Started " + QFileInfo(exe).fileName() + "; Local Play, F6 selects " + compile_->mapId());''',
 r'''        if (QProcess::startDetached(exe, {}, dir)) ctx_.setStatus("Started " + QFileInfo(exe).fileName() + "; F5 starts " + compile_->mapId() + " for you");'''),
])

edit('src/core/help_index.cpp', [
(r'''"F5 builds, deploys and starts the map in Replay (or Compile > Deploy, then F6 in game)."''',
 r'''"F5 builds, deploys and starts the map in Replay, also from a closed game (or Compile > Deploy, then a private match lobby you host)."'''),
])

edit('src/qtapp/qt_gui_test.cpp', [
(r'''            r.check(q.size() == 5 && q[0] == "set NSQLTTMRMP mp_x" && q[1] == "set MOLPOSLOMO war" && q[2] == "set RNRLKSNNR 4",
                    "quick match selects the map, the mode and the bot counts (token dvars)");''',
 r'''            r.check(q.size() == 6 && q[0] == "set NSQLTTMRMP mp_x" && q[1] == "set MOLPOSLOMO war" && q[2] == "set RNRLKSNNR 4" &&
                        q[5] == "set replay_dw_botCount 5",
                    "quick match selects the map, the mode and the bot counts (token dvars, and the client's own bot count)");
            std::string joined;
            std::vector<std::string> all;
            for (const QString& c : q) all.push_back(c.toStdString());
            for (const std::string& c : devmap::hostLobbyCommands()) all.push_back(c);
            r.check(devmap::joinCommands(all, joined) && joined.find("xstartprivatematch") != std::string::npos,
                    "quick match from the menu is one command text that also hosts the private match");'''),
])
print('ok')
