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
(r'''    // Fills the settings-backed fields of a Config (everything but mapId, gameRoot and launchExe).
    static void applySettings(Config& cfg);''',
 r'''    // Fills the settings-backed fields of a Config (everything but mapId, gameRoot and launchExe).
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
    static void setPlayOptions(const PlayOptions& o);  // cleans the values (game type name, bot range)'''),
])

edit('src/qtapp/devmap_launch.cpp', [
(r'''void DevmapRunner::applySettings(Config& cfg) {''',
 r'''DevmapRunner::PlayOptions DevmapRunner::playOptions() {
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

void DevmapRunner::applySettings(Config& cfg) {'''),
])

edit('src/qtapp/main_window.cpp', [
(r'''    form->addRow("Prefab folders", prefabs);
    auto* note = new QLabel("Tool paths (game, ACTS, x64-zt, Oodle): Help > First-Run Setup or Tools > Tool Paths. "''',
 r'''    form->addRow("Prefab folders", prefabs);
    // Play in game (F5): what the one-key start does (docs: User Guide, "F5 from a closed game").
    const DevmapRunner::PlayOptions play = DevmapRunner::playOptions();
    auto* playMode = new QComboBox(&dlg);
    playMode->setObjectName("prefsPlayGametype");
    playMode->setEditable(true);
    playMode->addItems({"war", "dm", "dom", "sd", "ctf", "conf", "koth"});
    playMode->setCurrentText(play.gametype);
    playMode->setToolTip("The game mode F5 starts (ui_gametype name: war = Team Deathmatch, dm = Free-for-all, dom = Domination, "
                         "sd = Search and Destroy, ctf = Capture the Flag). The map needs that mode's spawns and objects.");
    auto* playBots = new QSpinBox(&dlg);
    playBots->setObjectName("prefsPlayBots");
    playBots->setRange(0, 199);
    playBots->setValue(play.bots);
    playBots->setToolTip("Bots for a client that starts the map by itself (0 = none)");
    auto* playCover = new QCheckBox("Cover the game window until the map loads", &dlg);
    playCover->setObjectName("prefsPlayCover");
    playCover->setChecked(play.cover);
    auto* playHost = new QCheckBox("Check the local online-services host and start it when it does not answer", &dlg);
    playHost->setObjectName("prefsPlayHost");
    playHost->setChecked(play.hostCheck);
    auto* playHostCmd = new QLineEdit(play.hostCommand, &dlg);
    playHostCmd->setObjectName("prefsPlayHostCommand");
    playHostCmd->setPlaceholderText(DevmapRunner::hostCommandSetting().isEmpty() ? QStringLiteral("no host script found: type the command that starts your host")
                                                                               : DevmapRunner::hostCommandSetting());
    playHostCmd->setToolTip("The command that starts the local host. Blank uses the default shown here; a single - never starts one.");
    auto* playRequest = new QCheckBox("Let a client that can start the map by itself (faster, no menus)", &dlg);
    playRequest->setObjectName("prefsPlayRequest");
    playRequest->setChecked(play.startRequest);
    form->addRow("F5: game mode", playMode);
    form->addRow("F5: bots", playBots);
    form->addRow("F5", playCover);
    form->addRow("", playHost);
    form->addRow("F5: host command", playHostCmd);
    form->addRow("", playRequest);
    auto* note = new QLabel("Tool paths (game, ACTS, x64-zt, Oodle): Help > First-Run Setup or Tools > Tool Paths. "'''),
(r'''    p.prefabRoots = split(prefabs);
    applyPreferences(p);
}''',
 r'''    p.prefabRoots = split(prefabs);
    applyPreferences(p);
    DevmapRunner::PlayOptions po;
    po.gametype = playMode->currentText();
    po.bots = playBots->value();
    po.cover = playCover->isChecked();
    po.hostCheck = playHost->isChecked();
    po.hostCommand = playHostCmd->text();
    po.startRequest = playRequest->isChecked();
    DevmapRunner::setPlayOptions(po);
}'''),
])

edit('src/qtapp/qt_gui_test.cpp', [
(r'''        r.check(statuses.join("\n").contains(QRegularExpression("F5 \\+\\d+\\.\\d s: ")), "F5 status lines carry the time since F5");''',
 r'''        r.check(statuses.join("\n").contains(QRegularExpression("F5 \\+\\d+\\.\\d s: ")), "F5 status lines carry the time since F5");
        {
            // The F5 options of Edit > Preferences are the devmap/* settings; values are cleaned on the way in.
            const DevmapRunner::PlayOptions before = DevmapRunner::playOptions();
            DevmapRunner::PlayOptions o;
            o.gametype = " DOM ";
            o.bots = 500;
            o.cover = false;
            o.hostCheck = false;
            o.hostCommand = " - ";
            o.startRequest = false;
            DevmapRunner::setPlayOptions(o);
            const DevmapRunner::PlayOptions got = DevmapRunner::playOptions();
            DevmapRunner::Config c2;
            DevmapRunner::applySettings(c2);
            r.check(got.gametype == "dom" && got.bots == 199 && !got.cover && !got.hostCheck && got.hostCommand == "-" && !got.startRequest &&
                        c2.gametype == "dom" && c2.bots == 199 && !c2.cover && !c2.hostCheck && c2.hostCommand.isEmpty() && !c2.startRequest,
                    "the F5 options are stored cleaned and reach the next F5 run (- means never start a host)");
            o.gametype = "war; quit";
            DevmapRunner::setPlayOptions(o);
            r.check(DevmapRunner::playOptions().gametype == "war", "a game mode that is not a plain name falls back to war");
            DevmapRunner::setPlayOptions(before);
            r.check(DevmapRunner::playOptions().gametype == before.gametype && DevmapRunner::playOptions().cover == before.cover,
                    "the F5 options are put back");
        }'''),
])
print('ok')
